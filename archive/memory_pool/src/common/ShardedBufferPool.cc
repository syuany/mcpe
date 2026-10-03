#include "common/ShardedBufferPool.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <thread>

namespace mcpe {

ShardedBufferPool&
ShardedBufferPool::instance() noexcept {
  static ShardedBufferPool s_instance;
  return s_instance;
}

ShardedBufferPool::ShardedBufferPool(size_t shard_count, size_t capacity_per_shard) {
  if (shard_count == 0) {
    unsigned int hw = std::thread::hardware_concurrency();
    shard_count = (hw > 0) ? std::max<size_t>(hw, 4) : 4;
  }

  shards_.reserve(shard_count);
  for (size_t i = 0; i < shard_count; ++i) {
    shards_.push_back(std::make_unique<AlignedShard>(capacity_per_shard));
  }
}

size_t
ShardedBufferPool::getShardIndexForCurrentThread() const noexcept {
  static std::atomic<size_t> s_thread_counter{0};
  static thread_local size_t t_index = s_thread_counter.fetch_add(1, std::memory_order_relaxed);
  return t_index % shards_.size();
}

BufferPool&
ShardedBufferPool::getShardForCurrentThread() noexcept {
  return shards_[getShardIndexForCurrentThread()]->pool;
}

PooledBlock
ShardedBufferPool::tryAllocate() noexcept {
  const size_t n = shards_.size();
  size_t shard_idx = getShardIndexForCurrentThread();

  // 1. 首选本地分片快速借取（无争用路径）
  if (auto block = shards_[shard_idx]->pool.tryAllocate()) {
    return block;
  }

  // 2. 本地分片已耗尽：进行工作窃取 (Work Stealing)，遍历其他分片借出空闲块
  for (size_t i = 1; i < n; ++i) {
    size_t target = (shard_idx + i);
    if (target >= n) target -= n;
    if (auto block = shards_[target]->pool.tryAllocate()) {
      return block;
    }
  }

  return PooledBlock(nullptr, BufferBlockDeleter{nullptr});
}

PooledBlock
ShardedBufferPool::allocate() {
  // 1. 先尝试池内借出（包括本地快路径和工作窃取）
  if (auto block = tryAllocate()) {
    return block;
  }

  // 2. 全部分片均已借满，由当前线程绑定的分片退化回退至系统堆分配 (::operator new)
  size_t shard_idx = getShardIndexForCurrentThread();
  return shards_[shard_idx]->pool.allocate();
}

}  // namespace mcpe
