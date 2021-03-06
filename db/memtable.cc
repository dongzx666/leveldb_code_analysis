// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/memtable.h"
#include "db/dbformat.h"
#include "leveldb/comparator.h"
#include "leveldb/env.h"
#include "leveldb/iterator.h"
#include "util/coding.h"

namespace leveldb {
// TODO: GetLengthPrefixedSlice是干嘛的？
static Slice GetLengthPrefixedSlice(const char* data) {
  uint32_t len;
  const char* p = data;
  p = GetVarint32Ptr(p, p + 5, &len);  // +5: we assume "p" is not corrupted
  return Slice(p, len);
}
// Memtable的四个核心，comparator做比较，arena做内存管理, refs做引用计数，table做底层实现（跳表）
// TODO: 这里的疑惑点是为什么table已经使用了comparator_，外部还要再使用一次？
MemTable::MemTable(const InternalKeyComparator& comparator)
    : comparator_(comparator), refs_(0), table_(comparator_, &arena_) {}

MemTable::~MemTable() { assert(refs_ == 0); }

size_t MemTable::ApproximateMemoryUsage() { return arena_.MemoryUsage(); }

int MemTable::KeyComparator::operator()(const char* aptr,
                                        const char* bptr) const {
  // Internal keys are encoded as length-prefixed strings.
  Slice a = GetLengthPrefixedSlice(aptr);
  Slice b = GetLengthPrefixedSlice(bptr);
  return comparator.Compare(a, b);
}

// Encode a suitable internal key target for "target" and return it.
// Uses *scratch as scratch space, and the returned pointer will point
// into this scratch space.
// 编码key
// TODO: 为什么要使用一个std::string* scratch？
static const char* EncodeKey(std::string* scratch, const Slice& target) {
  scratch->clear();
  PutVarint32(scratch, target.size());
  scratch->append(target.data(), target.size());
  return scratch->data();
}
// MemTable中存储的key是InternalKey，value是用户指定的值
class MemTableIterator : public Iterator {
 public:
  explicit MemTableIterator(MemTable::Table* table) : iter_(table) {}

  MemTableIterator(const MemTableIterator&) = delete;
  MemTableIterator& operator=(const MemTableIterator&) = delete;

  ~MemTableIterator() override = default;

  bool Valid() const override { return iter_.Valid(); }
  void Seek(const Slice& k) override { iter_.Seek(EncodeKey(&tmp_, k)); }
  void SeekToFirst() override { iter_.SeekToFirst(); }
  void SeekToLast() override { iter_.SeekToLast(); }
  void Next() override { iter_.Next(); }
  void Prev() override { iter_.Prev(); }
  // TODO: 原来GetLengthPrefixedSlice用在这里，用于获取memtable的key
  Slice key() const override { return GetLengthPrefixedSlice(iter_.key()); }
  Slice value() const override {
    // 在内存中跳过键的部分后面就是值
    Slice key_slice = GetLengthPrefixedSlice(iter_.key());
    return GetLengthPrefixedSlice(key_slice.data() + key_slice.size());
  }
  // TODO: 永远返回正确???
  Status status() const override { return Status::OK(); }

 private:
  // 用到的迭代器就是SkipList::Iterator
  MemTable::Table::Iterator iter_;
  // 由于SkipList的键是const char*，所以需要一个临时缓存做类型转换
  std::string tmp_;  // For passing to EncodeKey
};

Iterator* MemTable::NewIterator() { return new MemTableIterator(&table_); }
// add方法的前置知识：SequenceNumber，ValueType
void MemTable::Add(SequenceNumber s, ValueType type, const Slice& key,
                   const Slice& value) {
  // Format of an entry is concatenation of:
  //  key_size     : varint32 of internal_key.size()
  //  key bytes    : char[internal_key.size()]
  //  value_size   : varint32 of value.size()
  //  value bytes  : char[value.size()]
  size_t key_size = key.size();
  size_t val_size = value.size();
  // 多出来的8个字节用来存储序列号和值类型
  size_t internal_key_size = key_size + 8;
  // 存储空间大小，主要有key的长度, key的内容, value的长度, value的内容
  const size_t encoded_len = VarintLength(internal_key_size) +
                             internal_key_size + VarintLength(val_size) +
                             val_size;
  // 分配存储的buffer
  char* buf = arena_.Allocate(encoded_len);
  // 将key的长度编码
  char* p = EncodeVarint32(buf, internal_key_size);
  // 拷贝key的内容
  std::memcpy(p, key.data(), key_size);
  p += key_size;
  // 将序列号和值类型写入
  EncodeFixed64(p, (s << 8) | type);
  // 指针后移
  p += 8;
  // 将value的长度编码
  p = EncodeVarint32(p, val_size);
  // 拷贝value的内容
  std::memcpy(p, value.data(), val_size);
  assert(p + val_size == buf + encoded_len);
  // 将组装的内容插入到跳表中
  table_.Insert(buf);
}
// get方法的前置知识：LookupKey
bool MemTable::Get(const LookupKey& key, std::string* value, Status* s) {
  // 根据传入的LookupmKey得到在emtable中存储的key, 然后调用Skip list::Iterator的Seek函数查找
  Slice memkey = key.memtable_key();
  Table::Iterator iter(&table_);
  iter.Seek(memkey.data());
  if (iter.Valid()) {
    // entry format is:
    //    klength  varint32
    //    userkey  char[klength]
    //    tag      uint64
    //    vlength  varint32
    //    value    char[vlength]
    // Check that it belongs to same user key.  We do not check the
    // sequence number since the Seek() call above should have skipped
    // all entries with overly large sequence numbers.
    const char* entry = iter.key();
    uint32_t key_length;
    const char* key_ptr = GetVarint32Ptr(entry, entry + 5, &key_length);
    // 比较user_key是否相同
    // TODO: comparator_.comparator.user_comparator()->Compare()
    if (comparator_.comparator.user_comparator()->Compare(
            Slice(key_ptr, key_length - 8), key.user_key()) == 0) {
      // Correct user key
      const uint64_t tag = DecodeFixed64(key_ptr + key_length - 8);
      // TODO: static_cast<ValueType>(tag & 0xff)
      switch (static_cast<ValueType>(tag & 0xff)) {
        // 找到并且是真实数据
        case kTypeValue: {
          Slice v = GetLengthPrefixedSlice(key_ptr + key_length);
          value->assign(v.data(), v.size());
          return true;
        }
        // 找到但是为删除标记
        case kTypeDeletion:
          *s = Status::NotFound(Slice());
          return true;
      }
    }
  }
  return false;
}

}  // namespace leveldb
