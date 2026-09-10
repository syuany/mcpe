#ifndef BYTEBUFFER_H
#define BYTEBUFFER_H

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace mcpe {

class ByteBuffer {
 public:
  static constexpr size_t kCheapPrepend = 8;
  static constexpr size_t kInitialSize = 1024;
  static constexpr size_t kMaxVarIntBytes = 5;

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

  // 以网络字节序（大端 Big-Endian）向末尾追加整型数据
  void appendInt64(int64_t x);
  void appendInt32(int32_t x);
  void appendInt16(int16_t x);
  void appendInt8(int8_t x);

  void appendFloat(float x);
  void appendDouble(double x);
  //   void appendBytes(const uint8_t* data, size_t len);
  //   void appendString(const std::string& str);

  // 从缓冲区读取大端 64/32/16/8 位整数，转换为主机字节序返回，并从 Buffer 中移出
  [[nodiscard]] int64_t readInt64();
  [[nodiscard]] int32_t readInt32();
  [[nodiscard]] int16_t readInt16();
  [[nodiscard]] int8_t readInt8();

  [[nodiscard]] float readFloat();
  [[nodiscard]] double readDouble();

  // 预览（查看）大端 64/32/16/8 位整数，转换为主机字节序返回，但不移动读指针
  [[nodiscard]] int64_t peekInt64() const noexcept;
  [[nodiscard]] int32_t peekInt32() const noexcept;
  [[nodiscard]] int16_t peekInt16() const noexcept;
  [[nodiscard]] int8_t peekInt8() const noexcept;
  [[nodiscard]] float peekFloat() const noexcept;
  [[nodiscard]] double peekDouble() const noexcept;

  void writeVarInt(int32_t x);

  int32_t readVarInt();

  // 消费/移出 len 字节已读数据（向前移动 readerIndex_）
  void retrieve(size_t len) noexcept;

  // 消费/移出数据直到指定指针位置 end
  void retrieveUntil(const char* end) noexcept;

  // 提取 len 字节数据并作为 std::string 返回，同时将该部分从 Buffer 中移出
  [[nodiscard]] std::string retrieveAsString(size_t len);

  // 提取所有可读数据并作为 std::string 返回，同时清空 Buffer
  [[nodiscard]] std::string retrieveAllAsString();

  // 移出固定长度为 8/4/2/1 字节的数据
  void retrieveInt64() noexcept;
  void retrieveInt32() noexcept;
  void retrieveInt16() noexcept;
  void retrieveInt8() noexcept;

  void retrieveFloat() noexcept;
  void retrieveDouble() noexcept;

  void retrieveAll() noexcept;

  // 获取当前可读区域的 std::string_view（零拷贝）
  [[nodiscard]] std::string_view toStringView() const noexcept;

  // 获取当前可读区域的 std::span<const char>（零拷贝视图）
  [[nodiscard]] std::span<const char> toSpan() const noexcept;

  // 向 prependable 头部预留区前插数据（通常用于封装协议头）
  void prepend(const void* data, size_t len) noexcept;

  // 释放多余未使用的内存空间，缩减 Buffer 容量至合适大小
  void shrink(size_t reserve = 0);

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
}  // namespace mcpe

#endif  // BYTEBUFFER_H