#include "common/BufferPool.h"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>

namespace mcpe {

BufferPool&
BufferPool::instance() noexcept {
  static BufferPool s_instance(kDefaultCapacity);
  return s_instance;
}

BufferPool::BufferPool(size_t capacity) : capacity_(capacity) {
  assert(capacity_ > 0 && "Capacity must be greater than zero");

  arena_ = static_cast<char*>(::operator new(capacity_ * kBlockSize, std::align_val_t{64}));
  arena_start_ = arena_;
  arena_end_ = arena_ + (capacity_ * kBlockSize);

  // 初始化侵入式单向链表索引：0 -> 1 -> 2 -> ... -> (capacity_ - 1) -> kInvalidIndex
  for (size_t i = 0; i < capacity_; ++i) {
    char* block_ptr = arena_ + (i * kBlockSize);
    auto* node = reinterpret_cast<Node*>(block_ptr);
    node->next = (i + 1 < capacity_) ? static_cast<uint32_t>(i + 1) : kInvalidIndex;
  }

  // 初始栈顶指向第 0 块，版本号 tag = 0
  head_.store(TaggedHead{0, 0}, std::memory_order_release);
}

BufferPool::~BufferPool() {
  if (arena_) {
    ::operator delete(arena_, std::align_val_t{64});
    arena_ = nullptr;
  }
}

PooledBlock
BufferPool::tryAllocate() noexcept {
  TaggedHead old_head = head_.load(std::memory_order_acquire);

  // 核心 CAS 循环：借鉴 Folly 的 Tagged Version 机制，彻底杜绝 ABA 问题
  while (old_head.index != kInvalidIndex) {
    char* block_ptr = arena_ + (old_head.index * kBlockSize);
    auto* node = reinterpret_cast<Node*>(block_ptr);
    uint32_t next_index = node->next;

    // 每次弹出栈顶，tag 严格自增，确保即使同一 index 被再次压回，CAS 也会因 tag 不匹配而失败重试
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
    // CAS 失败时，old_head 已由 compare_exchange_weak 自动更新为当前最新 head_，直接进入下一轮循环
  }

  return PooledBlock(nullptr, BufferBlockDeleter{this});
}

PooledBlock
BufferPool::allocate() {
  if (auto block = tryAllocate()) {
    return block;
  }

  // ⚡ 弹性降级 (Fallback)：池内预分配块已被借光！调用系统 new 保证业务不中断
  allocated_count_.fetch_add(1, std::memory_order_relaxed);
  char* fallback_mem = static_cast<char*>(::operator new(kBlockSize));
  return PooledBlock(fallback_mem, BufferBlockDeleter{this});
}

void
BufferPool::deallocate(char* ptr) noexcept {
  if (ptr == nullptr) return;

  // 1. 双轨血统检查：判断是否属于本池物理 Arena 预分配范围
  if (isInArena(ptr)) {
    uint32_t index = static_cast<uint32_t>((ptr - arena_start_) / kBlockSize);
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
    // 2. 属于突发洪峰期间临时通过 Fallback 申请的内存：直接归还给操作系统
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
