#include "common/BufferPool.h"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <thread>

namespace mcpe {

BufferPool&
BufferPool::instance() noexcept {
  static BufferPool s_instance(kDefaultCapacity, true);
  return s_instance;
}

BufferPool::BufferPool(size_t capacity, bool allow_dynamic_expansion)
    : initial_capacity_(capacity),
      allow_dynamic_expansion_(allow_dynamic_expansion),
      capacity_(capacity) {
  assert(initial_capacity_ > 0 && "Capacity must be greater than zero");

  // 1. 初始化预留指针数组，用于 O(1) 块索引寻址
  dynamic_chunks_ = new std::atomic<Chunk*>[kMaxChunks] {};

  // 2. 预分配初始连续 Arena 物理内存
  arena_ = static_cast<char*>(::operator new(initial_capacity_ * kBlockSize, std::align_val_t{64}));
  arena_start_ = arena_;
  arena_end_ = arena_ + (initial_capacity_ * kBlockSize);

  // 3. 初始化侵入式单向链表索引：0 -> 1 -> 2 -> ... -> (initial_capacity_ - 1) -> kInvalidIndex
  for (size_t i = 0; i < initial_capacity_; ++i) {
    char* block_ptr = arena_ + (i * kBlockSize);
    auto* node = reinterpret_cast<Node*>(block_ptr);
    node->next = (i + 1 < initial_capacity_) ? static_cast<uint32_t>(i + 1) : kInvalidIndex;
  }

  // 4. 初始栈顶指向第 0 块，版本号 tag = 0
  head_.store(TaggedHead{0, 0}, std::memory_order_release);
}

BufferPool::~BufferPool() {
  // 1. 释放所有动态扩容挂载的 Chunk 二级 Arena 物理内存及元数据
  Chunk* curr = chunk_chain_head_.load(std::memory_order_relaxed);
  while (curr != nullptr) {
    Chunk* next = curr->next.load(std::memory_order_relaxed);
    if (curr->arena) {
      ::operator delete(curr->arena, std::align_val_t{64});
      curr->arena = nullptr;
    }
    delete curr;
    curr = next;
  }
  chunk_chain_head_.store(nullptr, std::memory_order_relaxed);

  // 2. 释放动态指针映射表
  if (dynamic_chunks_) {
    delete[] dynamic_chunks_;
    dynamic_chunks_ = nullptr;
  }

  // 3. 释放初始 Arena 物理内存
  if (arena_) {
    ::operator delete(arena_, std::align_val_t{64});
    arena_ = nullptr;
  }
}

char*
BufferPool::indexToPtr(uint32_t index) const noexcept {
  if (index < initial_capacity_) {
    return const_cast<char*>(arena_start_) + (index * kBlockSize);
  }
  uint32_t rel = index - initial_capacity_;
  uint32_t chunk_id = 1 + (rel / kBlocksPerChunk);
  uint32_t offset = rel % kBlocksPerChunk;
  assert(chunk_id < kMaxChunks && "Chunk id exceeds kMaxChunks");
  Chunk* chunk = dynamic_chunks_[chunk_id].load(std::memory_order_acquire);
  assert(chunk != nullptr && chunk->arena != nullptr);
  return chunk->arena + (offset * kBlockSize);
}

uint32_t
BufferPool::ptrToIndex(const char* ptr) const noexcept {
  if (ptr == nullptr) return kInvalidIndex;

  // 1. 初始连续 Arena 范围检查 (O(1))
  if (ptr >= arena_start_ && ptr < arena_end_) {
    size_t diff = static_cast<size_t>(ptr - arena_start_);
    if (diff % kBlockSize == 0) {
      return static_cast<uint32_t>(diff / kBlockSize);
    }
    return kInvalidIndex;
  }

  // 2. 二级连续 Arena 动态 Chunk 链表检查（逆序遍历：最新扩容挂载的 Chunk 在链头，高频命中）
  Chunk* curr = chunk_chain_head_.load(std::memory_order_acquire);
  while (curr != nullptr) {
    const char* start = curr->arena;
    const char* end = start + (curr->capacity * kBlockSize);
    if (ptr >= start && ptr < end) {
      size_t diff = static_cast<size_t>(ptr - start);
      if (diff % kBlockSize == 0) {
        return curr->base_index + static_cast<uint32_t>(diff / kBlockSize);
      }
      return kInvalidIndex;
    }
    curr = curr->next.load(std::memory_order_relaxed);
  }

  return kInvalidIndex;
}

bool
BufferPool::isInArena(const char* ptr) const noexcept {
  return ptrToIndex(ptr) != kInvalidIndex;
}

PooledBlock
BufferPool::tryAllocate() noexcept {
  TaggedHead old_head = head_.load(std::memory_order_acquire);

  // 核心 CAS 循环：Tagged Version 彻底杜绝 ABA 问题
  while (old_head.index != kInvalidIndex) {
    char* block_ptr = indexToPtr(old_head.index);
    auto* node = reinterpret_cast<Node*>(block_ptr);
    uint32_t next_index = node->next;

    // 每次弹出栈顶，tag 严格自增
    TaggedHead new_head{next_index, old_head.tag + 1};

    if (head_.compare_exchange_weak(
            old_head,
            new_head,
            std::memory_order_acquire,
            std::memory_order_relaxed
        )) {
      allocated_count_.fetch_add(1, std::memory_order_relaxed);
      return PooledBlock(block_ptr, BufferBlockDeleter{this});
    }
    // CAS 失败时，old_head 已由 compare_exchange_weak 自动更新为当前最新 head_
  }

  return PooledBlock(nullptr, BufferBlockDeleter{this});
}

PooledBlock
BufferPool::mountNewChunk() {
  uint32_t chunk_id = chunk_count_.fetch_add(1, std::memory_order_relaxed);
  if (chunk_id >= kMaxChunks) {
    return PooledBlock(nullptr, BufferBlockDeleter{this});
  }

  // 批量申请 4MB 物理连续且 64 字节对齐的 Arena
  char* chunk_arena =
      static_cast<char*>(::operator new(kChunkSize, std::align_val_t{64}, std::nothrow));
  if (!chunk_arena) {
    return PooledBlock(nullptr, BufferBlockDeleter{this});
  }

  uint32_t base_index = static_cast<uint32_t>(initial_capacity_ + (chunk_id - 1) * kBlocksPerChunk);

  auto* chunk = new Chunk{chunk_arena, kBlocksPerChunk, base_index, nullptr};

  // 1. 发布到 dynamic_chunks_ 数组供 indexToPtr 快速寻址
  dynamic_chunks_[chunk_id].store(chunk, std::memory_order_release);

  // 2. 无锁 CAS 推入 chunk_chain_head_ 供 ptrToIndex 遍历
  Chunk* old_chain_head = chunk_chain_head_.load(std::memory_order_relaxed);
  do {
    chunk->next.store(old_chain_head, std::memory_order_relaxed);
  } while (!chunk_chain_head_.compare_exchange_weak(
      old_chain_head,
      chunk,
      std::memory_order_release,
      std::memory_order_relaxed
  ));

  // 3. 统计容量递增
  capacity_.fetch_add(kBlocksPerChunk, std::memory_order_relaxed);

  // 4. 将 Chunk 内部的 1024 个 4KB 页面穿针成侵入式单向链表
  // 零拷贝优化：第 0 块直接分配给当前扩容发起线程，避免一次无意义的进栈与出栈
  char* return_block = chunk_arena;

  if (kBlocksPerChunk > 1) {
    // 穿针第 1 块至第 (kBlocksPerChunk - 1) 块
    for (size_t i = 1; i < kBlocksPerChunk - 1; ++i) {
      char* block_ptr = chunk_arena + (i * kBlockSize);
      auto* node = reinterpret_cast<Node*>(block_ptr);
      node->next = static_cast<uint32_t>(base_index + i + 1);
    }

    // 最后一块的 next 动态链接到当前全局栈顶
    char* last_block = chunk_arena + ((kBlocksPerChunk - 1) * kBlockSize);
    auto* last_node = reinterpret_cast<Node*>(last_block);

    // 单次原子 CAS 操作，将这批连续 1023 个新块整体无锁挂载到全局栈顶 head_
    TaggedHead old_pool_head = head_.load(std::memory_order_acquire);
    TaggedHead new_pool_head;
    do {
      last_node->next = old_pool_head.index;
      new_pool_head = TaggedHead{base_index + 1, old_pool_head.tag + 1};
    } while (!head_.compare_exchange_weak(
        old_pool_head,
        new_pool_head,
        std::memory_order_release,
        std::memory_order_relaxed
    ));
  }

  allocated_count_.fetch_add(1, std::memory_order_relaxed);
  return PooledBlock(return_block, BufferBlockDeleter{this});
}

PooledBlock
BufferPool::expandAndAllocate() {
  while (true) {
    // 1. 先尝试获取空闲块（可能刚刚被其他并发线程扩容挂载或归还）
    if (auto block = tryAllocate()) {
      return block;
    }

    // 2. 检查是否已达到最大 Chunk 数量限制
    if (chunk_count_.load(std::memory_order_relaxed) >= kMaxChunks) {
      return PooledBlock(nullptr, BufferBlockDeleter{this});
    }

    // 3. 尝试抢占 Chunk 扩容令牌（CAS 保证并发洪峰下同一时刻仅单一线程执行 4MB 批量申请）
    bool expected = false;
    if (expanding_flag_.compare_exchange_strong(expected, true, std::memory_order_acquire)) {
      // 成功抢到扩容权
      PooledBlock block = mountNewChunk();
      expanding_flag_.store(false, std::memory_order_release);
      return block;
    }

    // 4. 未抢到扩容权：说明有其他并发线程正在执行 4MB 物理分配与无锁穿针
    // 自旋/让步等待当前正在进行的扩容完成
    int spins = 0;
    while (expanding_flag_.load(std::memory_order_relaxed)) {
      if (++spins < 1000) {
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
        __builtin_ia32_pause();
#elif (defined(__x86_64__) || defined(_M_X64)) && defined(_MSC_VER)
        _mm_pause();
#else
        std::this_thread::yield();
#endif
      } else {
        std::this_thread::yield();
      }
    }

    // 扩容已完成，下一轮循环将在步骤 1 的 tryAllocate() 中直接拿到新页面！
  }
}

PooledBlock
BufferPool::allocate() {
  // 1. 尝试从本地空闲单向栈快速弹栈 (Fast Path)
  if (auto block = tryAllocate()) {
    return block;
  }

  // 2. 预分配容量已借光：尝试触发 4MB 动态 Chunk 无锁扩容 (Medium Path)
  if (allow_dynamic_expansion_) {
    if (auto block = expandAndAllocate()) {
      return block;
    }
  }

  // 3. ⚡ 弹性降级 (Fallback Slow Path)：极限保底调用系统 new
  allocated_count_.fetch_add(1, std::memory_order_relaxed);
  char* fallback_mem = static_cast<char*>(::operator new(kBlockSize));
  return PooledBlock(fallback_mem, BufferBlockDeleter{this});
}

void
BufferPool::deallocate(char* ptr) noexcept {
  if (ptr == nullptr) return;

  // 1. 双轨血统检查：计算 index 并判断归属（初始 Arena 或动态 Chunk 链）
  uint32_t index = ptrToIndex(ptr);
  if (index != kInvalidIndex) {
    auto* node = reinterpret_cast<Node*>(ptr);
    TaggedHead old_head = head_.load(std::memory_order_relaxed);
    TaggedHead new_head;

    // 核心 CAS 循环：把 index 节点作为新的栈顶压入链表，tag 保持自增
    do {
      node->next = old_head.index;
      new_head = TaggedHead{index, old_head.tag + 1};
    } while (!head_.compare_exchange_weak(
        old_head,
        new_head,
        std::memory_order_release,
        std::memory_order_relaxed
    ));
  } else {
    // 2. 属于极限异常情况下通过 Fallback 申请的裸堆内存：归还操作系统
    ::operator delete(ptr);
  }

  allocated_count_.fetch_sub(1, std::memory_order_relaxed);
}

void
BufferBlockDeleter::operator()(char* ptr) const noexcept {
  if (ptr) {
    if (pool_) {
      pool_->deallocate(ptr);
    } else {
      BufferPool::instance().deallocate(ptr);
    }
  }
}

}  // namespace mcpe
