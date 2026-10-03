#ifndef BUFFERPOOL_H
#define BUFFERPOOL_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace mcpe {

class BufferPool;
struct BufferBlockDeleter {
  BufferPool* pool_{nullptr};

  explicit BufferBlockDeleter(BufferPool* pool = nullptr) noexcept : pool_(pool) {}

  void operator()(char* ptr) const noexcept;
};
using PooledBlock = std::unique_ptr<char[], BufferBlockDeleter>;

/**
 * @brief 定长 4KB 页面无锁并发内存池 (Lock-Free Page Buffer Pool)
 * 
 * 架构特性：
 * 1. Facebook Folly TaggedHead 防 ABA：32-bit Index + 32-bit Tag 组合成 64-bit 原子变量，单字 CAS 消除 ABA。
 * 2. 二级连续 Arena 链动态扩容：当预分配容量耗尽时，按 4MB 批量动态分配新 Chunk（二级连续 Arena），
 *    无锁（Lock-Free）穿针挂载至全局空闲链表 head_，彻底消除退化为散碎堆分配的性能悬崖。
 * 3. 双轨血统检查与极限保底：精准判断内存块归属（Chunk 链表或系统堆），支持业务极限情况下的 Fallback。
 */
class BufferPool {
 public:
  static constexpr size_t kBlockSize = 4096;
  static constexpr size_t kDefaultCapacity = 1024;
  static constexpr size_t kChunkSize = 4 * 1024 * 1024; // 4MB 动态扩容 Chunk 尺寸
  static constexpr size_t kBlocksPerChunk = kChunkSize / kBlockSize; // 1024 个 4KB 页面
  static constexpr size_t kMaxChunks = 1024; // 支持最高 4GB 动态 Arena 扩容
  static constexpr uint32_t kInvalidIndex = 0xFFFFFFFF;

  static BufferPool& instance() noexcept;

  explicit BufferPool(
      size_t capacity = kDefaultCapacity,
      bool allow_dynamic_expansion = true
  );
  ~BufferPool();

  BufferPool(const BufferPool&) = delete;
  BufferPool& operator=(const BufferPool&) = delete;

  PooledBlock allocate();
  PooledBlock tryAllocate() noexcept;

  void deallocate(char* ptr) noexcept;

  size_t capacity() const noexcept {
    return capacity_.load(std::memory_order_relaxed);
  }

  size_t allocatedCount() const noexcept {
    return allocated_count_.load(std::memory_order_relaxed);
  }

  size_t chunkCount() const noexcept {
    return chunk_count_.load(std::memory_order_relaxed);
  }

  bool isDynamicExpansionEnabled() const noexcept {
    return allow_dynamic_expansion_;
  }

  bool isInArena(const char* ptr) const noexcept;

 private:
  struct Node {
    uint32_t next;
  };

  struct TaggedHead {
    uint32_t index{kInvalidIndex};
    uint32_t tag{0};

    bool operator==(const TaggedHead& other) const noexcept = default;
  };

  // 动态 Chunk 二级连续 Arena 链表节点
  struct Chunk {
    char* arena{nullptr};
    size_t capacity{0};
    uint32_t base_index{0};
    std::atomic<Chunk*> next{nullptr};
  };

  char* indexToPtr(uint32_t index) const noexcept;
  uint32_t ptrToIndex(const char* ptr) const noexcept;

  PooledBlock expandAndAllocate();
  PooledBlock mountNewChunk();

  const size_t initial_capacity_;
  const bool allow_dynamic_expansion_{true};

  char* arena_{nullptr};
  const char* arena_start_{nullptr};
  const char* arena_end_{nullptr};

  alignas(64) std::atomic<TaggedHead> head_{};
  alignas(64) std::atomic<size_t> allocated_count_{0};
  alignas(64) std::atomic<size_t> capacity_{0};

  // 二级连续 Arena 动态扩容相关
  alignas(64) std::atomic<Chunk*> chunk_chain_head_{nullptr};
  alignas(64) std::atomic<uint32_t> chunk_count_{1}; // 初始 Chunk 为 0，动态 Chunk 从 1 开始
  alignas(64) std::atomic<bool> expanding_flag_{false};

  // 动态分配指针数组用于 O(1) 索引寻址
  std::atomic<Chunk*>* dynamic_chunks_{nullptr};
};

}  // namespace mcpe

#endif  // BUFFERPOOL_H
