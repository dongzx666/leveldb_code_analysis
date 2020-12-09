// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "leveldb/comparator.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <type_traits>

#include "leveldb/slice.h"
#include "util/logging.h"
#include "util/no_destructor.h"

namespace leveldb {

Comparator::~Comparator() = default;
/*
在创建leveldb数据库对象的时候通过Option指定, 而它的构造函数默认是把比较器设定为BytewiseComparatorImpl的, 也就是说BytewiseComparatorImpl是leveldb默认的比较器
 */
namespace {
class BytewiseComparatorImpl : public Comparator {
 public:
  BytewiseComparatorImpl() = default;

  const char* Name() const override { return "leveldb.BytewiseComparator"; }

  // 直接使用slice.compare(用memcmp实现)
  int Compare(const Slice& a, const Slice& b) const override {
    return a.compare(b);
  }

  void FindShortestSeparator(std::string* start,
                             const Slice& limit) const override {
    // Find length of common prefix
    // 取最小长度，避免越界
    size_t min_length = std::min(start->size(), limit.size());
    size_t diff_index = 0;
    // 找到第一个不相同的字节值的位置
    while ((diff_index < min_length) &&
           ((*start)[diff_index] == limit[diff_index])) {
      diff_index++;
    }
    // 如果index大于等于min_length，说明找不到一个字符串比start大比limit小
    if (diff_index >= min_length) {
      // Do not shorten if one string is a prefix of the other
    } else {
      // 取出这个start中对应的字节
      uint8_t diff_byte = static_cast<uint8_t>((*start)[diff_index]);
      // TODO: 该字节不能大于0xff，+1不能大于limit[diff_index]?
      if (diff_byte < static_cast<uint8_t>(0xff) &&
          diff_byte + 1 < static_cast<uint8_t>(limit[diff_index])) {
        // 取出的字节+1
        (*start)[diff_index]++;
        // 调整start大小为diff_index+1
        start->resize(diff_index + 1);
        // TODO: 这里为什么还要加个断言？
        assert(Compare(*start, limit) < 0);
      }
    }
  }

  void FindShortSuccessor(std::string* key) const override {
    // Find first character that can be incremented
    // 获取键长
    size_t n = key->size();
    // 遍历键
    for (size_t i = 0; i < n; i++) {
      const uint8_t byte = (*key)[i];
      // 找到第一个不是0xff的字节
      // TODO: 0xff有什么特殊意义?
      if (byte != static_cast<uint8_t>(0xff)) {
        // 把这个位置的字节+1，然后截断
        (*key)[i] = byte + 1;
        key->resize(i + 1);
        return;
      }
    }
    // *key is a run of 0xffs.  Leave it alone.
  }
};
}  // namespace

const Comparator* BytewiseComparator() {
  static NoDestructor<BytewiseComparatorImpl> singleton;
  return singleton.get();
}

}  // namespace leveldb
