#ifndef MCPE_COMMON_SHARDED_BUFFER_POOL_H
#define MCPE_COMMON_SHARDED_BUFFER_POOL_H

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

#include "common/BufferPool.h"

namespace mcpe {

/**
 * @brief 分片并发定长内存池 (Sharded Lock-Free Buffer Pool)
 *
 * 核心设计目标：
 * 1. 消除全局单 Head 争用热点：将大池切分为多个基于 64 字节缓存行对齐 (alignas(64)) 的独立
 * BufferPool 分片。
 * 2. 线程分片亲和映射 (Thread Affinity)：根据当前线程 TLS ID
 * 映射至默认分片，高并发借还命中本地分片，消除 CPU 总线仲裁。
 * 3. 跨分片工作窃取 (Work
 * Stealing)：当本线程分片被借空时，无锁尝试从其他分片“窃取”空闲块，最大限度避免过早退化到系统堆锁。
 * 4. 自动化透明跨分片归还：由于 PooledBlock 的 BufferBlockDeleter 自带出借分片的 pool_
 * 指针，借出块无论在哪个线程或分片被释放， 均能精准归还到它原本的所属分片，全过程完全无锁。
 */
class ShardedBufferPool {
 public:
  static constexpr size_t kDefaultCapacityPerShard = 256;

  static ShardedBufferPool& instance() noexcept;

  /**
   * @brief 构造分片内存池
   * @param shard_count 分片数。若为 0，则自动探测系统的硬件并发核心数
   * (hardware_concurrency)，保底至少为 4
   * @param capacity_per_shard 每个分片初始容量（默认 256 块，即 1MB/分片）
   */
  explicit ShardedBufferPool(
      size_t shard_count = 0,
      size_t capacity_per_shard = kDefaultCapacityPerShard
  );

  ~ShardedBufferPool() = default;

  ShardedBufferPool(const ShardedBufferPool&) = delete;
  ShardedBufferPool& operator=(const ShardedBufferPool&) = delete;

  /**
   * @brief 从内存池中借出一个 4KB 物理块
   *
   * 借取策略：
   * 1. 尝试从当前线程绑定的分片快速借出（无锁、无跨核争用）
   * 2. 若本地分片已耗尽，遍历其他分片进行 Work Stealing 借取
   * 3. 若全部分片均已借光，降级由本地分片调用 ::operator new
   */
  PooledBlock allocate();

  /**
   * @brief 尝试非阻塞借出一个 4KB 内存块（不触发 ::operator new 降级）
   * @return 成功返回有效 PooledBlock，全部分片均耗尽时返回 nullptr
   */
  PooledBlock tryAllocate() noexcept;

  /**
   * @brief 获取当前线程绑定的 BufferPool 分片引用
   */
  BufferPool& getShardForCurrentThread() noexcept;

  /**
   * @brief 获取指定索引的分片
   */
  BufferPool&
  getShard(size_t index) noexcept {
    return shards_[index % shards_.size()]->pool;
  }

  size_t
  shardCount() const noexcept {
    return shards_.size();
  }

  size_t
  totalCapacity() const noexcept {
    size_t total = 0;
    for (const auto& s : shards_) {
      total += s->pool.capacity();
    }
    return total;
  }

  size_t
  totalAllocatedCount() const noexcept {
    size_t total = 0;
    for (const auto& s : shards_) {
      total += s->pool.allocatedCount();
    }
    return total;
  }

 private:
  size_t getShardIndexForCurrentThread() const noexcept;

  struct alignas(64) AlignedShard {
    BufferPool pool;
    explicit AlignedShard(size_t capacity) : pool(capacity) {}
  };

  std::vector<std::unique_ptr<AlignedShard>> shards_;
};

}  // namespace mcpe

#endif  // MCPE_COMMON_SHARDED_BUFFER_POOL_H
