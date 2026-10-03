#ifndef BYTEBUFFER_H
#define BYTEBUFFER_H

#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/VarInt.h"

namespace mcpe {

/**
 * @brief 现代 C++ 高性能非阻塞网络字节缓冲区
 *
 * 采用类似 Muduo 的三段式连续内存布局设计：
 * +-------------------+------------------+------------------+
 * | prependable bytes |  readable bytes  |  writable bytes  |
 * |                   |     (CONTENT)    |                  |
 * +-------------------+------------------+------------------+
 * |                   |                  |                  |
 * 0      <=      readerIndex_   <=   writerIndex_    <=    size
 *
 * 核心特性：
 * 1. Cheap Prepend（头部预留 8 字节）：允许在不重新分配和挪动内存的情况下，O(1) 前插协议包长 VarInt 前缀。
 * 2. 自动扩容与内存碎片整理：当可写空间不足但头部有已读空闲时，自动向前挪动数据复用空间（makeSpace）。
 * 3. 泛型大端数值读写：支持 int8_t~int64_t, uint8_t~uint64_t, float, double 大端序列化与反序列化。
 * 4. 原生支持 Minecraft 协议类型：VarInt, VarLong, String。
 * 5. 零副作用预览（peek*）与 RAII 事务快照（Mark），支撑协议拆包与多字段原子解析。
 */
class ByteBuffer {
 public:
  static constexpr size_t kCheapPrepend = 8;    ///< 头部预留用于前插协议头的字节数
  static constexpr size_t kInitialSize = 1024;  ///< 默认初始容量
  static constexpr size_t kMaxVarIntBytes = varint::kMaxVarIntBytes;    ///< VarInt 最大 5 字节
  static constexpr size_t kMaxVarLongBytes = varint::kMaxVarLongBytes;  ///< VarLong 最大 10 字节

  /**
   * @brief 构造函数：初始化具有预留头部的缓冲区
   * @param initialSize 初始可写空间大小（默认为 1024 字节）
   */
  explicit ByteBuffer(size_t initialSize = kInitialSize)
      : buffer_(kCheapPrepend + initialSize),
        readerIndex_(kCheapPrepend),
        writerIndex_(kCheapPrepend) {
    assert(readableBytes() == 0);
    assert(writableBytes() == initialSize);
    assert(prependableBytes() == kCheapPrepend);
  }

  // Move-Only 契约：禁止隐式深拷贝（如需复制请显式调用 clone()）
  ByteBuffer(const ByteBuffer&) = delete;
  ByteBuffer& operator=(const ByteBuffer&) = delete;

  // 支持高效的移动语义
  ByteBuffer(ByteBuffer&& other) noexcept = default;
  ByteBuffer& operator=(ByteBuffer&& other) noexcept = default;
  ~ByteBuffer() = default;

  /**
   * @brief 显式深拷贝：克隆出一个内容完全一致、拥有独立内存空间的 ByteBuffer
   */
  [[nodiscard]] ByteBuffer clone() const {
    ByteBuffer copy(readableBytes());
    copy.append(peek(), readableBytes());
    return copy;
  }

  /**
   * @brief 与另一个 ByteBuffer 高效交换底层内容
   */
  void swap(ByteBuffer& rhs) noexcept {
    buffer_.swap(rhs.buffer_);
    std::swap(readerIndex_, rhs.readerIndex_);
    std::swap(writerIndex_, rhs.writerIndex_);
  }

  // ---------------------------------------------------------------------------
  // 基础游标与容量查询接口
  // ---------------------------------------------------------------------------

  size_t readableBytes() const noexcept { return writerIndex_ - readerIndex_; }
  size_t writableBytes() const noexcept { return buffer_.size() - writerIndex_; }
  size_t prependableBytes() const noexcept { return readerIndex_; }

  const char* peek() const noexcept { return begin() + readerIndex_; }
  char* beginWrite() noexcept { return begin() + writerIndex_; }

  void ensureWritableBytes(size_t len);

  void hasWritten(size_t len) noexcept {
    assert(len <= writableBytes());
    writerIndex_ += len;
  }

  // 向 Buffer 追加连续内存块区域的数据
  void append(const char* data, size_t len);
  void append(const void* data, size_t len);
  void append(std::string_view str);

  void appendByte(uint8_t byte) { append(&byte, 1); }

  [[nodiscard]] bool hasBytes(size_t count) const noexcept {
    return readableBytes() >= count;
  }

  // ---------------------------------------------------------------------------
  // 现代 C++ 泛型定宽基础数值读写 (Big-Endian)
  // 支持: int8_t ~ int64_t, uint8_t ~ uint64_t, float, double
  // 统一通过 std::expected 返回结果，失败零副作用
  // ---------------------------------------------------------------------------

  template <typename T>
    requires(std::integral<T> || std::floating_point<T>)
  void writeBe(T value) {
    if constexpr (sizeof(T) == 1) {
      append(&value, 1);
    } else if constexpr (std::integral<T>) {
      if constexpr (std::endian::native == std::endian::little) {
        value = std::byteswap(value);
      }
      append(&value, sizeof(T));
    } else if constexpr (std::floating_point<T>) {
      using UInt = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
      UInt bits = std::bit_cast<UInt>(value);
      if constexpr (std::endian::native == std::endian::little) {
        bits = std::byteswap(bits);
      }
      append(&bits, sizeof(UInt));
    }
  }

  template <typename T>
    requires(std::integral<T> || std::floating_point<T>)
  [[nodiscard]] std::expected<T, DecodeError> readBe() noexcept {
    if (!hasBytes(sizeof(T))) {
      return std::unexpected(DecodeError::kIncomplete);
    }
    if constexpr (sizeof(T) == 1) {
      T val = static_cast<T>(*peek());
      retrieve(1);
      return val;
    } else if constexpr (std::integral<T>) {
      T val;
      std::memcpy(&val, peek(), sizeof(T));
      retrieve(sizeof(T));
      if constexpr (std::endian::native == std::endian::little) {
        val = std::byteswap(val);
      }
      return val;
    } else if constexpr (std::floating_point<T>) {
      using UInt = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
      UInt bits;
      std::memcpy(&bits, peek(), sizeof(UInt));
      retrieve(sizeof(T));
      if constexpr (std::endian::native == std::endian::little) {
        bits = std::byteswap(bits);
      }
      return std::bit_cast<T>(bits);
    }
  }

  template <typename T>
    requires(std::integral<T> || std::floating_point<T>)
  [[nodiscard]] std::expected<T, DecodeError> peekBe() const noexcept {
    if (!hasBytes(sizeof(T))) {
      return std::unexpected(DecodeError::kIncomplete);
    }
    if constexpr (sizeof(T) == 1) {
      return static_cast<T>(*peek());
    } else if constexpr (std::integral<T>) {
      T val;
      std::memcpy(&val, peek(), sizeof(T));
      if constexpr (std::endian::native == std::endian::little) {
        val = std::byteswap(val);
      }
      return val;
    } else if constexpr (std::floating_point<T>) {
      using UInt = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
      UInt bits;
      std::memcpy(&bits, peek(), sizeof(UInt));
      if constexpr (std::endian::native == std::endian::little) {
        bits = std::byteswap(bits);
      }
      return std::bit_cast<T>(bits);
    }
  }

  // ---------------------------------------------------------------------------
  // Minecraft 协议特有类型：VarInt / VarLong / String (带 VarInt 长度前缀的 UTF-8)
  // ---------------------------------------------------------------------------
  void appendVarInt(int32_t value);
  void appendVarLong(int64_t value);
  void prependVarInt(int32_t value);
  void prependVarInt(size_t value) {
    assert(value <= static_cast<size_t>(std::numeric_limits<int32_t>::max()) && "Packet size exceeds int32_t");
    prependVarInt(static_cast<int32_t>(value));
  }

  void appendString(std::string_view str);

  [[nodiscard]] std::expected<int32_t, DecodeError> readVarInt() noexcept;
  [[nodiscard]] std::expected<int64_t, DecodeError> readVarLong() noexcept;

  [[nodiscard]] std::expected<VarIntResult, DecodeError> readVarIntWithBytes() noexcept;
  [[nodiscard]] std::expected<VarLongResult, DecodeError> readVarLongWithBytes() noexcept;

  [[nodiscard]] std::expected<std::string, DecodeError> readString(size_t maxBytes = 32767) noexcept;

  [[nodiscard]] std::expected<int32_t, DecodeError> peekVarInt() const noexcept;
  [[nodiscard]] std::expected<int64_t, DecodeError> peekVarLong() const noexcept;

  [[nodiscard]] std::expected<VarIntResult, DecodeError> peekVarIntWithBytes() const noexcept;
  [[nodiscard]] std::expected<VarLongResult, DecodeError> peekVarLongWithBytes() const noexcept;

  void retrieve(size_t len) noexcept;
  void retrieveUntil(const char* end) noexcept;
  [[nodiscard]] std::string retrieveAsString(size_t len);
  [[nodiscard]] std::string retrieveAllAsString();
  void retrieveAll() noexcept;

  [[nodiscard]] std::string_view toStringView() const noexcept;
  [[nodiscard]] std::span<const char> toSpan() const noexcept;

  void prepend(const void* data, size_t len) noexcept;
  void shrink(size_t reserve = 0);

  class Mark {
   public:
    explicit Mark(ByteBuffer& buf) noexcept : buf_(&buf), pos_(buf.readerIndex_) {}
    Mark(const Mark&) = delete;
    Mark& operator=(const Mark&) = delete;
    Mark(Mark&&) = delete;
    Mark& operator=(Mark&&) = delete;
    ~Mark() { rollback(); }

    void commit() noexcept { buf_ = nullptr; }
    void rollback() noexcept {
      if (buf_ != nullptr) {
        buf_->readerIndex_ = pos_;
        buf_ = nullptr;
      }
    }

   private:
    ByteBuffer* buf_;
    size_t pos_;
  };

 private:
  char* begin() noexcept { return buffer_.data(); }
  const char* begin() const noexcept { return buffer_.data(); }

  void makeSpace(size_t len);

  std::vector<char> buffer_;
  size_t readerIndex_;
  size_t writerIndex_;
};

}  // namespace mcpe

#endif  // BYTEBUFFER_H