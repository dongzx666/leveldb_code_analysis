// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "util/arena.h"

namespace leveldb {
// 定义的内存块大小
static const int kBlockSize = 4096;
// 构造函数
Arena::Arena()
    : alloc_ptr_(nullptr), alloc_bytes_remaining_(0), memory_usage_(0) {}
// 析构函数, 释放所有内存
Arena::~Arena() {
  for (size_t i = 0; i < blocks_.size(); i++) {
    delete[] blocks_[i];
  }
}
// 内存块剩余空间不够用, 申请新空间
char* Arena::AllocateFallback(size_t bytes) {
  // 对象的大小是块大小的四分之一以上, 则单独分配，以避免在剩余字节中浪费太多空间。
  if (bytes > kBlockSize / 4) {
    // Object is more than a quarter of our block size.  Allocate it separately
    // to avoid wasting too much space in leftover bytes.
    char* result = AllocateNewBlock(bytes);
    return result;
  }

  // We waste the remaining space in the current block.
  alloc_ptr_ = AllocateNewBlock(kBlockSize);
  alloc_bytes_remaining_ = kBlockSize;

  char* result = alloc_ptr_;
  alloc_ptr_ += bytes;
  alloc_bytes_remaining_ -= bytes;
  return result;
}

// 与Allocate不同的是内存要对齐
// 小知识: new()/malloc()申请的内存一般都是8字节对齐的
char* Arena::AllocateAligned(size_t bytes) {
  // 确认对齐方式，默认8位
  // sizeof(void*): 获取一个指针的大小, 与编译器的目标平台相关
  // https://blog.csdn.net/liutianshx2012/article/details/50974854
  const int align = (sizeof(void*) > 8) ? sizeof(void*) : 8;
  // (align & (align - 1)) == 0判断是否为2的次幂，可见下两链接
  // https://www.cnblogs.com/RioTian/p/13676080.html
  // https://www.cnblogs.com/zy230530/p/6645431.html
  static_assert((align & (align - 1)) == 0,
                "Pointer size should be a power of 2");
  // 获取当前最新内存块可用空间的首地址现对于对齐地址偏移了多少
  size_t current_mod = reinterpret_cast<uintptr_t>(alloc_ptr_) & (align - 1);
  // 计算将当前可用地址再偏移多少才能对齐
  size_t slop = (current_mod == 0 ? 0 : align - current_mod);
  // 实际申请的内存大小要加上对齐偏移量
  size_t needed = bytes + slop;
  // 下面和Allocate等同
  char* result;
  if (needed <= alloc_bytes_remaining_) {
    result = alloc_ptr_ + slop;
    alloc_ptr_ += needed;
    alloc_bytes_remaining_ -= needed;
  } else {
    // AllocateFallback always returned aligned memory
    result = AllocateFallback(bytes);
  }
  assert((reinterpret_cast<uintptr_t>(result) & (align - 1)) == 0);
  return result;
}
// 申请新内存
char* Arena::AllocateNewBlock(size_t block_bytes) {
  // 申请内存块
  char* result = new char[block_bytes];
  // 为析构清理内存做准备
  blocks_.push_back(result);
  // 累计内存使用量, sizeof(char*): vector使用内存也统计
  // TODO: fetch_add? memory_order_relaxed?
  memory_usage_.fetch_add(block_bytes + sizeof(char*),
                          std::memory_order_relaxed);
  return result;
}

}  // namespace leveldb
