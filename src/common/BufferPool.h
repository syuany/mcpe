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
 * 借鉴 Facebook Folly IndexedMemPool 设计思想：
 * 采用 32-bit Index + 32-bit Tag (Version Counter) 组合成 64-bit TaggedHead，
 * 通过单一原子 CAS 操作，彻底消除经典 Treiber Stack 并发弹出时的 ABA 漏洞。
 */
class BufferPool {
 public:
  static constexpr size_t kBlockSize = 4096;
  static constexpr size_t kDefaultCapacity = 1024;
  static constexpr uint32_t kInvalidIndex = 0xFFFFFFFF;

  static BufferPool& instance() noexcept;

  explicit BufferPool(size_t capacity = kDefaultCapacity);
  ~BufferPool();

  BufferPool(const BufferPool&) = delete;
  BufferPool& operator=(const BufferPool&) = delete;

  PooledBlock allocate();
  PooledBlock tryAllocate() noexcept;

  void deallocate(char* ptr) noexcept;

  size_t capacity() const noexcept {
    return capacity_;
  }

  size_t allocatedCount() const noexcept {
    return allocated_count_.load(std::memory_order_relaxed);
  }

  bool isInArena(const char* ptr) const noexcept {
    return ptr >= arena_start_ && ptr < arena_end_ &&
           (static_cast<size_t>(ptr - arena_start_) % kBlockSize == 0);
  }

 private:
  struct Node {
    uint32_t next;
  };

  struct TaggedHead {
    uint32_t index{kInvalidIndex};
    uint32_t tag{0};

    bool operator==(const TaggedHead& other) const noexcept = default;
  };

  const size_t capacity_;
  char* arena_{nullptr};
  const char* arena_start_{nullptr};
  const char* arena_end_{nullptr};

  alignas(64) std::atomic<TaggedHead> head_{};
  alignas(64) std::atomic<size_t> allocated_count_{0};
};

}  // namespace mcpe

#endif  // BUFFERPOOL_H
