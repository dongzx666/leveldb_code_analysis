// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
/*
Arena负责管理内存使用，把内存分配出去，但是不负责重复利用，这因为memtable本身就是追加。
从AllocateFallback可以看出Arena一个缺点，就是当申请的内存大小频繁的比最新块的剩余空间大时，就会出现一些内存浪费。但是相比从系统申请的开销还是来的值得。
 */

#ifndef STORAGE_LEVELDB_UTIL_ARENA_H_
#define STORAGE_LEVELDB_UTIL_ARENA_H_

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace leveldb {

class Arena {
 public:
  Arena();

  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;

  ~Arena();

  // Return a pointer to a newly allocated memory block of "bytes" bytes.
  char* Allocate(size_t bytes);

  // Allocate memory with the normal alignment guarantees provided by malloc.
  char* AllocateAligned(size_t bytes);

  // Returns an estimate of the total memory usage of data allocated
  // by the arena.
  // 返回所分配数据的总内存使用量的估计值
  size_t MemoryUsage() const {
    return memory_usage_.load(std::memory_order_relaxed);
  }

 private:
  char* AllocateFallback(size_t bytes);
  char* AllocateNewBlock(size_t block_bytes);

  // Allocation state
  // 指向最新内存块剩余空间的首地址
  char* alloc_ptr_;
  // 记录最新内存块还有多少剩余空间可用
  size_t alloc_bytes_remaining_;

  // Array of new[] allocated memory blocks
  // 用vector管理所有分配的内存块
  std::vector<char*> blocks_;

  // Total memory usage of the arena.
  //
  // TODO(costan): This member is accessed via atomics, but the others are
  //               accessed without any locking. Is this OK?
  // TODO: 内存使用量, 为什么要用原子操作?
  std::atomic<size_t> memory_usage_;
};

inline char* Arena::Allocate(size_t bytes) {
  // The semantics of what to return are a bit messy if we allow
  // 0-byte allocations, so we disallow them here (we don't need
  // them for our internal use).
  // 不允许分配0字节和负数
  assert(bytes > 0);
  // 最新申请的内存块中剩余空间够用, 指针偏移, 剩余空间减去申请量
  if (bytes <= alloc_bytes_remaining_) {
    char* result = alloc_ptr_;
    alloc_ptr_ += bytes;
    alloc_bytes_remaining_ -= bytes;
    return result;
  }
  // 剩余空间不够用，申请新空间
  return AllocateFallback(bytes);
}

}  // namespace leveldb

#endif  // STORAGE_LEVELDB_UTIL_ARENA_H_
