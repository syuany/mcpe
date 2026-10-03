#ifndef MCPE_NETWORK_SESSION_READ_BUFFER_H
#define MCPE_NETWORK_SESSION_READ_BUFFER_H

#include <asio/buffer.hpp>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string_view>
#include <utility>

#include "common/BufferPool.h"
#include "common/ByteBuffer.h"
#include "common/ShardedBufferPool.h"

namespace mcpe::network {

/**
 * @brief 专为 TcpSession 打造的零常驻、惰性池化接收缓冲区 (SessionReadBuffer)
 *
 * 核心架构特性：
 * 1. 惰性按需借池：连接空闲发呆时，不持有任何内存物理块（连接常驻内存严格为 0 字节）。
 * 2. 线程分片与工作窃取：默认接入 ShardedBufferPool，多线程网络 IO 零总线 CAS 争用。
 * 3. 零拷贝数据视图：提供 unconsumedView() 直接暴露 std::string_view 供 PacketFraming 切包，无额外内存拷贝。
 * 4. 自动全量回收：一旦当前物理块中的数据被完全切包消费，立即触发 recycle() 归还内存池。
 * 5. 滑动窗口就地对齐：发生半包时通过 compactIfIncomplete() 在 4KB 物理块内就地整理碎片。
 * 6. 超限包弹性伸缩：单包大于 4KB 的极端边界下平滑晋升为动态 ByteBuffer，消费完毕自动回退并回收。
 */
class SessionReadBuffer {
 public:
  /** @brief 默认构造：绑定全局分片内存池（具备核亲和与跨核窃取） */
  SessionReadBuffer()
      : sharded_pool_(&ShardedBufferPool::instance()), pool_(nullptr) {}

  /** @brief 显式绑定单池构造（主要用于隔离单元测试） */
  explicit SessionReadBuffer(BufferPool& pool)
      : sharded_pool_(nullptr), pool_(&pool) {}

  /** @brief 显式绑定分片池构造 */
  explicit SessionReadBuffer(ShardedBufferPool& sharded_pool)
      : sharded_pool_(&sharded_pool), pool_(nullptr) {}

  ~SessionReadBuffer() {
    recycle();
  }

  // 禁止拷贝（连接独占缓冲区生命周期）
  SessionReadBuffer(const SessionReadBuffer&) = delete;
  SessionReadBuffer& operator=(const SessionReadBuffer&) = delete;

  // 允许移动
  SessionReadBuffer(SessionReadBuffer&& other) noexcept
      : sharded_pool_(other.sharded_pool_),
        pool_(other.pool_),
        block_(std::move(other.block_)),
        oversized_buf_(std::move(other.oversized_buf_)),
        read_pos_(other.read_pos_),
        write_pos_(other.write_pos_) {
    other.read_pos_ = 0;
    other.write_pos_ = 0;
  }

  SessionReadBuffer& operator=(SessionReadBuffer&& other) noexcept {
    if (this != &other) {
      recycle();
      sharded_pool_ = other.sharded_pool_;
      pool_ = other.pool_;
      block_ = std::move(other.block_);
      oversized_buf_ = std::move(other.oversized_buf_);
      read_pos_ = other.read_pos_;
      write_pos_ = other.write_pos_;
      other.read_pos_ = 0;
      other.write_pos_ = 0;
    }
    return *this;
  }

  /**
   * @brief 准备供 asio::async_read_some 直接写入的连续缓冲区间
   * 若当前未持有物理块，则向内存池按需惰性借出 4KB 页面
   */
  asio::mutable_buffer prepareWriteBuffer(size_t max_read_size = BufferPool::kBlockSize) {
    if (oversized_buf_) {
      oversized_buf_->ensureWritableBytes(max_read_size);
      return asio::buffer(oversized_buf_->beginWrite(), max_read_size);
    }

    if (!block_) {
      block_ = allocateBlock();
      read_pos_ = 0;
      write_pos_ = 0;
    }

    size_t writable = BufferPool::kBlockSize - write_pos_;
    if (writable == 0) {
      // 遭遇超大包（>4KB）：晋升至动态 ByteBuffer
      oversized_buf_ = std::make_unique<ByteBuffer>();
      if (read_pos_ < write_pos_) {
        oversized_buf_->append(block_.get() + read_pos_, write_pos_ - read_pos_);
      }
      block_.reset();
      read_pos_ = 0;
      write_pos_ = 0;

      oversized_buf_->ensureWritableBytes(max_read_size);
      return asio::buffer(oversized_buf_->beginWrite(), max_read_size);
    }

    if (writable > max_read_size) {
      writable = max_read_size;
    }
    return asio::buffer(block_.get() + write_pos_, writable);
  }

  /**
   * @brief 提交底层 socket 读取完成的实际字节数，推进写游标
   */
  void commit(size_t bytes_transferred) noexcept {
    if (oversized_buf_) {
      oversized_buf_->hasWritten(bytes_transferred);
    } else {
      assert(write_pos_ + bytes_transferred <= BufferPool::kBlockSize);
      write_pos_ += bytes_transferred;
    }
  }

  /**
   * @brief 获取当前未消费数据的连续零拷贝视图
   */
  [[nodiscard]] std::string_view unconsumedView() const noexcept {
    if (oversized_buf_) {
      return oversized_buf_->toStringView();
    }
    if (!block_ || read_pos_ >= write_pos_) {
      return {};
    }
    return std::string_view(block_.get() + read_pos_, write_pos_ - read_pos_);
  }

  /**
   * @brief 消费指定字节数
   * 核心逻辑：若物理块中累积数据已被切包完全消费，立即触发 recycle() 归还内存池！
   */
  void consume(size_t bytes) noexcept {
    if (oversized_buf_) {
      oversized_buf_->retrieve(bytes);
      if (oversized_buf_->readableBytes() == 0) {
        oversized_buf_.reset();
      }
      return;
    }

    read_pos_ += bytes;
    if (read_pos_ >= write_pos_) {
      recycle();
    }
  }

  /**
   * @brief 发生半包时，将未读残留字节就地向前移动对齐至块首，释放后续写入空间
   */
  void compactIfIncomplete() noexcept {
    if (oversized_buf_) {
      return;
    }
    if (read_pos_ > 0 && read_pos_ < write_pos_) {
      std::memmove(block_.get(), block_.get() + read_pos_, write_pos_ - read_pos_);
      write_pos_ -= read_pos_;
      read_pos_ = 0;
    }
  }

  /**
   * @brief 归还持有的物理块并重置状态，恢复 0 字节内存常驻状态
   */
  void recycle() noexcept {
    block_.reset();
    oversized_buf_.reset();
    read_pos_ = 0;
    write_pos_ = 0;
  }

  /** @brief 查询当前是否持有物理内存块 */
  [[nodiscard]] bool isHoldingBlock() const noexcept {
    return block_ != nullptr || oversized_buf_ != nullptr;
  }

  /** @brief 查询当前未消费的有效字节数 */
  [[nodiscard]] size_t readableBytes() const noexcept {
    if (oversized_buf_) {
      return oversized_buf_->readableBytes();
    }
    return (block_ && write_pos_ > read_pos_) ? (write_pos_ - read_pos_) : 0;
  }

 private:
  PooledBlock allocateBlock() {
    if (pool_) {
      return pool_->allocate();
    }
    if (sharded_pool_) {
      return sharded_pool_->allocate();
    }
    return BufferPool::instance().allocate();
  }

  ShardedBufferPool* sharded_pool_{nullptr};
  BufferPool* pool_{nullptr};
  PooledBlock block_{nullptr};
  std::unique_ptr<ByteBuffer> oversized_buf_{nullptr};
  size_t read_pos_{0};
  size_t write_pos_{0};
};

}  // namespace mcpe::network

#endif  // MCPE_NETWORK_SESSION_READ_BUFFER_H
