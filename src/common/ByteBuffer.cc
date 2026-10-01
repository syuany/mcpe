#include "common/ByteBuffer.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "common/ShardedBufferPool.h"

using namespace mcpe;

ByteBuffer::ByteBuffer()
    : pooled_block_(ShardedBufferPool::instance().allocate()),
      data_ptr_(pooled_block_.get()),
      capacity_(BufferPool::kBlockSize),
      readerIndex_(kCheapPrepend),
      writerIndex_(kCheapPrepend) {}

ByteBuffer::ByteBuffer(size_t initialSize) {
  if (initialSize + kCheapPrepend <= BufferPool::kBlockSize) {
    pooled_block_ = ShardedBufferPool::instance().allocate();
    data_ptr_ = pooled_block_.get();
    capacity_ = BufferPool::kBlockSize;
  } else {
    heap_fallback_.resize(kCheapPrepend + initialSize);
    data_ptr_ = heap_fallback_.data();
    capacity_ = heap_fallback_.size();
  }
  readerIndex_ = kCheapPrepend;
  writerIndex_ = kCheapPrepend;
}

ByteBuffer::ByteBuffer(PooledBlock block, size_t capacity) noexcept
    : pooled_block_(std::move(block)),
      data_ptr_(pooled_block_.get()),
      capacity_(capacity),
      readerIndex_(kCheapPrepend),
      writerIndex_(kCheapPrepend) {}

ByteBuffer::ByteBuffer(ByteBuffer&& other) noexcept
    : pooled_block_(std::move(other.pooled_block_)),
      heap_fallback_(std::move(other.heap_fallback_)),
      capacity_(other.capacity_),
      readerIndex_(other.readerIndex_),
      writerIndex_(other.writerIndex_) {
  if (pooled_block_) {
    data_ptr_ = pooled_block_.get();
  } else if (!heap_fallback_.empty()) {
    data_ptr_ = heap_fallback_.data();
  } else {
    data_ptr_ = nullptr;
  }
  other.data_ptr_ = nullptr;
  other.capacity_ = 0;
  other.readerIndex_ = kCheapPrepend;
  other.writerIndex_ = kCheapPrepend;
}

ByteBuffer&
ByteBuffer::operator=(ByteBuffer&& other) noexcept {
  if (this != &other) {
    pooled_block_ = std::move(other.pooled_block_);
    heap_fallback_ = std::move(other.heap_fallback_);
    capacity_ = other.capacity_;
    readerIndex_ = other.readerIndex_;
    writerIndex_ = other.writerIndex_;
    if (pooled_block_) {
      data_ptr_ = pooled_block_.get();
    } else if (!heap_fallback_.empty()) {
      data_ptr_ = heap_fallback_.data();
    } else {
      data_ptr_ = nullptr;
    }
    other.data_ptr_ = nullptr;
    other.capacity_ = 0;
    other.readerIndex_ = kCheapPrepend;
    other.writerIndex_ = kCheapPrepend;
  }
  return *this;
}

ByteBuffer
ByteBuffer::clone() const {
  ByteBuffer copy(readableBytes());
  if (readableBytes() > 0) {
    copy.append(peek(), readableBytes());
  }
  return copy;
}

void
ByteBuffer::append(const char* data, size_t len) {
  // 确保可写空间充足（不足则自动触发扩容或内部内存整理）
  ensureWritableBytes(len);
  std::copy(data, data + len, beginWrite());
  hasWritten(len);
}

void
ByteBuffer::append(const void* data, size_t len) {
  append(static_cast<const char*>(data), len);
}

void
ByteBuffer::append(std::string_view str) {
  append(str.data(), str.size());
}

void
ByteBuffer::appendVarInt(int32_t value) {
  // VarInt 最大占用 5 字节
  ensureWritableBytes(varint::kMaxVarIntBytes);
  const size_t bytes = varint::encodeVarInt(value, beginWrite());
  hasWritten(bytes);
}

void
ByteBuffer::appendVarLong(int64_t value) {
  // VarLong 最大占用 10 字节
  ensureWritableBytes(varint::kMaxVarLongBytes);
  const size_t bytes = varint::encodeVarLong(value, beginWrite());
  hasWritten(bytes);
}

void
ByteBuffer::prependVarInt(int32_t value) {
  // 在栈上先编码 VarInt，然后前插到头部预留区 (kCheapPrepend)
  char buf[varint::kMaxVarIntBytes];
  const size_t bytes = varint::encodeVarInt(value, buf);
  prepend(buf, bytes);
}

void
ByteBuffer::appendString(std::string_view str) {
  // Minecraft 协议规范字符串：VarInt 字节长度前缀 + UTF-8 字节流
  appendVarInt(static_cast<int32_t>(str.size()));
  append(str.data(), str.size());
}

std::expected<VarIntResult, DecodeError>
ByteBuffer::peekVarIntWithBytes() const noexcept {
  // 零副作用探测：仅查看不移动 readerIndex_
  return varint::decodeVarInt(peek(), readableBytes());
}

std::expected<VarLongResult, DecodeError>
ByteBuffer::peekVarLongWithBytes() const noexcept {
  return varint::decodeVarLong(peek(), readableBytes());
}

std::expected<int32_t, DecodeError>
ByteBuffer::peekVarInt() const noexcept {
  const auto res = peekVarIntWithBytes();
  if (!res) {
    return std::unexpected(res.error());
  }
  return res->value;
}

std::expected<int64_t, DecodeError>
ByteBuffer::peekVarLong() const noexcept {
  const auto res = peekVarLongWithBytes();
  if (!res) {
    return std::unexpected(res.error());
  }
  return res->value;
}

std::expected<VarIntResult, DecodeError>
ByteBuffer::readVarIntWithBytes() noexcept {
  // 先预览，若成功则消费对应的字节数并推进读游标
  const auto res = peekVarIntWithBytes();
  if (!res) {
    return std::unexpected(res.error());
  }
  retrieve(res->bytes);
  return res;
}

std::expected<VarLongResult, DecodeError>
ByteBuffer::readVarLongWithBytes() noexcept {
  const auto res = peekVarLongWithBytes();
  if (!res) {
    return std::unexpected(res.error());
  }
  retrieve(res->bytes);
  return res;
}

std::expected<int32_t, DecodeError>
ByteBuffer::readVarInt() noexcept {
  const auto res = readVarIntWithBytes();
  if (!res) {
    return std::unexpected(res.error());
  }
  return res->value;
}

std::expected<int64_t, DecodeError>
ByteBuffer::readVarLong() noexcept {
  const auto res = readVarLongWithBytes();
  if (!res) {
    return std::unexpected(res.error());
  }
  return res->value;
}

std::expected<std::string, DecodeError>
ByteBuffer::readString(size_t maxBytes) noexcept {
  // RAII 事务读标记：遇到半包或非法数据提早 return 时，析构自动回滚读游标
  Mark mark(*this);

  // 1. 读取字符串 VarInt 字节长度
  const auto len = readVarInt();
  if (!len) {
    return std::unexpected(len.error());
  }

  // 2. 校验长度范围（负数或超过最大协议上限 32767 则判为畸形）
  if (*len < 0 || static_cast<size_t>(*len) > maxBytes) {
    return std::unexpected(DecodeError::kMalformed);
  }

  const size_t byteLen = static_cast<size_t>(*len);
  // 3. 检查剩余可读字节是否足够支撑整个字符串体
  if (!hasBytes(byteLen)) {
    return std::unexpected(DecodeError::kIncomplete);
  }

  // 4. 提取字符串并确认事务提交
  std::string result = retrieveAsString(byteLen);
  mark.commit();
  return result;
}

void
ByteBuffer::retrieve(size_t len) noexcept {
  assert(len <= readableBytes());
  if (len < readableBytes()) {
    readerIndex_ += len;
  } else {
    // 全部读完，将读写游标复位到头部预留区起点，提高内存利用率
    retrieveAll();
  }
}

void
ByteBuffer::retrieveUntil(const char* end) noexcept {
  assert(peek() <= end);
  assert(end <= beginWrite());
  retrieve(static_cast<size_t>(end - peek()));
}

std::string
ByteBuffer::retrieveAsString(size_t len) {
  assert(len <= readableBytes());
  std::string res(peek(), len);
  retrieve(len);
  return res;
}

std::string
ByteBuffer::retrieveAllAsString() {
  return retrieveAsString(readableBytes());
}

void
ByteBuffer::retrieveAll() noexcept {
  readerIndex_ = kCheapPrepend;
  writerIndex_ = kCheapPrepend;
}

std::string_view
ByteBuffer::toStringView() const noexcept {
  return std::string_view(peek(), readableBytes());
}

std::span<const char>
ByteBuffer::toSpan() const noexcept {
  return std::span<const char>(peek(), readableBytes());
}

void
ByteBuffer::prepend(const void* data, size_t len) noexcept {
  assert(len <= prependableBytes());
  const char* des = static_cast<const char*>(data);
  readerIndex_ -= len;
  std::copy(des, des + len, begin() + readerIndex_);
}

void
ByteBuffer::shrink(size_t reserve) {
  // 压缩内部缓冲区，释放空闲未使用的内存空间
  ByteBuffer other;
  other.ensureWritableBytes(readableBytes() + reserve);
  other.append(toStringView());
  swap(other);
}

void
ByteBuffer::swap(ByteBuffer& rhs) noexcept {
  pooled_block_.swap(rhs.pooled_block_);
  heap_fallback_.swap(rhs.heap_fallback_);
  std::swap(data_ptr_, rhs.data_ptr_);
  std::swap(capacity_, rhs.capacity_);
  std::swap(readerIndex_, rhs.readerIndex_);
  std::swap(writerIndex_, rhs.writerIndex_);
}

void
ByteBuffer::ensureWritableBytes(size_t len) {
  if (writableBytes() < len) {
    makeSpace(len);
  }
  assert(writableBytes() >= len);
}

void
ByteBuffer::makeSpace(size_t len) {
  if (writableBytes() + prependableBytes() < len + kCheapPrepend) {
    size_t new_cap = std::max(capacity_ * 2, writerIndex_ + len);
    if (pooled_block_) {
      heap_fallback_.resize(new_cap);
      if (data_ptr_ && writerIndex_ > 0) {
        std::memcpy(heap_fallback_.data(), data_ptr_, writerIndex_);
      }
      pooled_block_.reset();
      data_ptr_ = heap_fallback_.data();
      capacity_ = heap_fallback_.size();
    } else {
      heap_fallback_.resize(new_cap);
      data_ptr_ = heap_fallback_.data();
      capacity_ = heap_fallback_.size();
    }
  } else {
    assert(kCheapPrepend < readerIndex_);
    size_t readable = readableBytes();
    std::copy(begin() + readerIndex_, begin() + writerIndex_, begin() + kCheapPrepend);
    readerIndex_ = kCheapPrepend;
    writerIndex_ = readerIndex_ + readable;
    assert(readable == readableBytes());
  }
}
