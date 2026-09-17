#ifndef BYTEBUFFER_H
#define BYTEBUFFER_H

#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mcpe {

/** 解析错误分类（两种语义完全不同，禁止压成一个 bool）：
 *  - kIncomplete 数据不足（TCP 半包）。这是正常控制流：保留读游标、等下一次 read，
 *                不要打 warn 日志、不要断连、更不要当异常抛。
 *  - kMalformed  畸形或突破协议上限（VarInt > 5 字节 / VarLong > 10 字节 / 字符串越界）。
 *                属于不可信输入，应当断开连接。
 *
 *  统一契约（所有解析函数都遵守）：失败零副作用 —— 失败时既不消费任何字节，
 *  也不产生任何可用值。因此"半包 → 原样返回 → 下次数据到了重试"永远安全。 */
enum class DecodeError : std::uint8_t { kIncomplete, kMalformed };

class ByteBuffer {
 public:
  static constexpr size_t kCheapPrepend = 8;
  static constexpr size_t kInitialSize = 1024;
  static constexpr size_t kMaxVarIntBytes = 5;
  static constexpr size_t kMaxVarLongBytes = 10;

  ByteBuffer(size_t initialSize = kInitialSize)
      : buffer_(kCheapPrepend + initialSize),
        readerIndex_(kCheapPrepend),
        writerIndex_(kCheapPrepend) {
    assert(readableBytes() == 0);
    assert(writableBytes() == initialSize);
    assert(prependableBytes() == kCheapPrepend);
  }

  ByteBuffer(const ByteBuffer&) = default;
  ByteBuffer& operator=(const ByteBuffer&) = default;
  ByteBuffer(ByteBuffer&&) noexcept = default;
  ByteBuffer& operator=(ByteBuffer&&) noexcept = default;
  ~ByteBuffer() = default;

  // 交换两个 Buffer 的内部状态
  void swap(ByteBuffer& rhs) noexcept;

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

  // 向 Buffer 追加无类型指针的数据
  void append(const void* data, size_t len);

  // 向 Buffer 追加字符串视图
  void append(std::string_view str);

  // 向末尾追加单个字节（常用原始字节写入）
  void appendByte(uint8_t byte) { append(&byte, 1); }

  /** ① 谓词层：可读字节是否 ≥ count（不消费、不改状态、不涉及错误分类）。
   *  拆包器用它判断整包是否到齐：if (!buf.hasBytes(packet_len)) return; */
  [[nodiscard]] bool hasBytes(size_t count) const noexcept { return readableBytes() >= count; }

  // ---------------------------------------------------------------------------
  // 现代 C++ 泛型定宽基础数值读写 (Big-Endian)
  // 支持: int8_t ~ int64_t, uint8_t ~ uint64_t, float, double
  // 统一通过 std::expected 返回结果，失败零副作用
  // ---------------------------------------------------------------------------

  /** 以网络字节序（大端 Big-Endian）向末尾追加定长数值 */
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

  /** 安全读取大端定长数值：
   *  - 数据不足时返回 kIncomplete，读游标绝对不移动（失败零副作用）；
   *  - 数据足够时转换为主机字节序，消费对应字节并返回结果。 */
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

  /** 非消费安全预览定长数值：成败均不移动读游标 */
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
  void appendString(std::string_view str);

  [[nodiscard]] std::expected<int32_t, DecodeError> readVarInt() noexcept;
  [[nodiscard]] std::expected<int64_t, DecodeError> readVarLong() noexcept;
  [[nodiscard]] std::expected<std::string, DecodeError> readString(
      size_t maxBytes = 32767) noexcept;

  [[nodiscard]] std::expected<int32_t, DecodeError> peekVarInt() const noexcept;
  [[nodiscard]] std::expected<int64_t, DecodeError> peekVarLong() const noexcept;

  // 消费/移出 len 字节已读数据（向前移动 readerIndex_）
  void retrieve(size_t len) noexcept;

  // 消费/移出数据直到指定指针位置 end
  void retrieveUntil(const char* end) noexcept;

  // 提取 len 字节数据并作为 std::string 返回，同时将该部分从 Buffer 中移出
  [[nodiscard]] std::string retrieveAsString(size_t len);

  // 提取所有可读数据并作为 std::string 返回，同时清空 Buffer
  [[nodiscard]] std::string retrieveAllAsString();

  void retrieveAll() noexcept;

  // 获取当前可读区域的 std::string_view（零拷贝）
  [[nodiscard]] std::string_view toStringView() const noexcept;

  // 获取当前可读区域的 std::span<const char>（零拷贝视图）
  [[nodiscard]] std::span<const char> toSpan() const noexcept;

  // 向 prependable 头部预留区前插数据（通常用于封装协议头）
  void prepend(const void* data, size_t len) noexcept;

  // 释放多余未使用的内存空间，缩减 Buffer 容量至合适大小
  void shrink(size_t reserve = 0);

  /** 读游标快照（RAII 事务）。
   *
   *  用途：解析由多个字段组成的包体时"要么全成功、要么全回滚"。
   *
   *    ByteBuffer::Mark mark(buf);          // 记录当前读游标
   *    if (!buf.readVarInt()) return;       // 提前 return：析构自动回滚到快照点
   *    if (!buf.readString()) return;
   *    mark.commit();                       // 全部字段成功，确认消费 */
  class Mark {
   public:
    explicit Mark(ByteBuffer& buf) noexcept : buf_(&buf), pos_(buf.readerIndex_) {}
    Mark(const Mark&) = delete;
    Mark& operator=(const Mark&) = delete;
    Mark(Mark&&) = delete;
    Mark& operator=(Mark&&) = delete;
    ~Mark() { rollback(); }

    /** 确认消费：析构时不再回滚 */
    void commit() noexcept { buf_ = nullptr; }

    /** 立即回滚到快照点（幂等，可重复调用） */
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
  // 获取底层 vector 内存起始地址指针
  char* begin() { return buffer_.data(); }
  const char* begin() const { return buffer_.data(); }

  // 扩容/碎片整理内部实现：当 writableBytes() < len 时被调用
  void makeSpace(size_t len);

  std::vector<char> buffer_;
  size_t readerIndex_;
  size_t writerIndex_;
};

/** 日志友好的错误名（禁止用于业务判断，业务判断请直接比较枚举值） */
[[nodiscard]] const char* toString(DecodeError error) noexcept;

}  // namespace mcpe

#endif  // BYTEBUFFER_H