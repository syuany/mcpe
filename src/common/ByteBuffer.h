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
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/VarInt.h"

namespace mcpe {

/**
 * @brief 现代 C++ 高性能非阻塞网络字节缓冲区（支持 Netty 风格零拷贝切片与 Copy-On-Write 防护）
 *
 * 核心架构特性：
 * 1. Cheap Prepend（头部预留 8 字节）：允许在不重新分配和挪动内存的情况下，O(1) 原地前插协议包长 VarInt 前缀。
 * 2. Netty 风格零拷贝切片（slice / readSlice）：支持从大缓冲区中切分出共享底层内存的独立 ByteBuffer，
 *    通过 std::shared_ptr 引用计数保证生命周期，彻底消灭二次堆内存分配与 memcpy 拷贝开销。
 * 3. Copy-On-Write（写时复制安全隔离）：当父缓冲区需要扩容或移动内存时，若检测到底层 Storage 正被活跃切片共享
 *    （use_count > 1），自动断开共享并重新分配独立存储，彻底消灭野指针与悬挂引用（Use-After-Free）。
 * 4. 泛型大端数值读写：原生支持 int8_t~int64_t, uint8_t~uint64_t, float, double 大端序列化与反序列化。
 * 5. 原生支持 Minecraft 协议类型：VarInt, VarLong, String。
 * 6. RAII 事务快照（Mark）：支撑多字段原子试探解析与失败回滚。
 */
class ByteBuffer {
 public:
  static constexpr size_t kCheapPrepend = 8;    ///< 头部预留用于前插协议头的字节数
  static constexpr size_t kInitialSize = 1024;  ///< 默认初始容量
  static constexpr size_t kMaxVarIntBytes = varint::kMaxVarIntBytes;    ///< VarInt 最大 5 字节
  static constexpr size_t kMaxVarLongBytes = varint::kMaxVarLongBytes;  ///< VarLong 最大 10 字节

  struct Storage {
    std::vector<char> data;
    explicit Storage(size_t capacity) : data(capacity) {}
  };

  /**
   * @brief 构造函数：初始化具有预留头部的独立缓冲区
   * @param initialSize 初始可写空间大小（默认为 1024 字节）
   */
  explicit ByteBuffer(size_t initialSize = kInitialSize)
      : storage_(std::make_shared<Storage>(kCheapPrepend + initialSize)),
        offset_(0),
        readerIndex_(kCheapPrepend),
        writerIndex_(kCheapPrepend),
        capacity_(kCheapPrepend + initialSize),
        is_slice_(false) {
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
    storage_.swap(rhs.storage_);
    std::swap(offset_, rhs.offset_);
    std::swap(readerIndex_, rhs.readerIndex_);
    std::swap(writerIndex_, rhs.writerIndex_);
    std::swap(capacity_, rhs.capacity_);
    std::swap(is_slice_, rhs.is_slice_);
  }

  // ---------------------------------------------------------------------------
  // Netty 风格零拷贝切片接口（Zero-Copy Slicing）
  // ---------------------------------------------------------------------------

  /**
   * @brief 从当前读游标位置切出一个长度为 len 的共享切片（只读共享，不移动父级读游标）
   * @param len 切片长度
   */
  [[nodiscard]] ByteBuffer slice(size_t len) const;

  /**
   * @brief 从当前读游标位置切出一个长度为 len 的共享切片，并同步推进父级读游标（对标 Netty readRetainedSlice）
   * @param len 切片长度
   */
  ByteBuffer readSlice(size_t len);

  /// 当前缓冲区是否为切片视图
  bool isSlice() const noexcept { return is_slice_; }

  /// 底层 Storage 当前的引用计数（等于 1 代表独占，大于 1 代表正被切片共享）
  long useCount() const noexcept { return storage_ ? storage_.use_count() : 0; }

  // ---------------------------------------------------------------------------
  // 基础游标与容量查询接口
  // ---------------------------------------------------------------------------

  size_t readableBytes() const noexcept { return writerIndex_ - readerIndex_; }
  size_t writableBytes() const noexcept { return capacity_ - writerIndex_; }
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

  void prepend(const void* data, size_t len);
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
  /// 私有切片构造函数：供 slice() 与 readSlice() 内部复用底层连续内存块
  ByteBuffer(std::shared_ptr<Storage> storage, size_t offset, size_t len)
      : storage_(std::move(storage)),
        offset_(offset),
        readerIndex_(0),
        writerIndex_(len),
        capacity_(len),
        is_slice_(true) {}

  char* begin() noexcept { return storage_->data.data() + offset_; }
  const char* begin() const noexcept { return storage_->data.data() + offset_; }

  void makeSpace(size_t len);

  std::shared_ptr<Storage> storage_;  ///< 底层连续内存管理块
  size_t offset_{0};                  ///< 切片在 storage_ 内部的起始偏移
  size_t readerIndex_{kCheapPrepend}; ///< 读游标（相对于 offset_）
  size_t writerIndex_{kCheapPrepend}; ///< 写游标（相对于 offset_）
  size_t capacity_{0};                ///< 当前 Buffer 允许访问的最大容量（相对于 offset_）
  bool is_slice_{false};              ///< 是否为共享切片标识
};

}  // namespace mcpe

#endif  // BYTEBUFFER_H