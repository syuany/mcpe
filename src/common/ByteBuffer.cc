#include "include/mcpe/common/ByteBuffer.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <utility>

using namespace mcpe;

namespace {
constexpr uint8_t kValueMask = 0x7F;
constexpr uint8_t kContinuationBit = 0x80;

/** VarInt / VarLong 解析核心：不消费、不越界、三态返回。
 *
 *  UInt      : uint32_t（VarInt）或 uint64_t（VarLong）
 *  kMaxBytes : 5 或 10 —— 协议规定的字节数上限
 */
template <typename UInt, size_t kMaxBytes>
std::expected<UInt, DecodeError>
decodeVar(const char* data, size_t available, size_t& consumed) noexcept {
  UInt value = 0;
  for (size_t i = 0; i < kMaxBytes; ++i) {
    if (i >= available) {
      return std::unexpected(DecodeError::kIncomplete);  // 半包：还差字节，等下次 read
    }
    const uint8_t byte = static_cast<uint8_t>(data[i]);
    value |= static_cast<UInt>(byte & kValueMask) << (7 * i);
    if ((byte & kContinuationBit) == 0) {
      consumed = i + 1;
      return value;
    }
  }
  // 用满上限仍带继续位：只有畸形包才会走到这里
  return std::unexpected(DecodeError::kMalformed);
}
}  // namespace

void
ByteBuffer::append(const char* data, size_t len) {
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
  uint32_t val = static_cast<uint32_t>(value);
  do {
    uint8_t temp = static_cast<uint8_t>(val & kValueMask);
    val >>= 7;
    if (val != 0) {
      temp |= kContinuationBit;
    }
    writeBe(temp);
  } while (val != 0);
}

void
ByteBuffer::appendVarLong(int64_t value) {
  uint64_t val = static_cast<uint64_t>(value);
  do {
    uint8_t temp = static_cast<uint8_t>(val & kValueMask);
    val >>= 7;
    if (val != 0) {
      temp |= kContinuationBit;
    }
    writeBe(temp);
  } while (val != 0);
}

void
ByteBuffer::appendString(std::string_view str) {
  appendVarInt(static_cast<int32_t>(str.size()));
  append(str.data(), str.size());
}

std::expected<int32_t, DecodeError>
ByteBuffer::peekVarInt() const noexcept {
  size_t consumed = 0;
  const auto parsed = decodeVar<uint32_t, kMaxVarIntBytes>(peek(), readableBytes(), consumed);
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  return static_cast<int32_t>(*parsed);
}

std::expected<int64_t, DecodeError>
ByteBuffer::peekVarLong() const noexcept {
  size_t consumed = 0;
  const auto parsed = decodeVar<uint64_t, kMaxVarLongBytes>(peek(), readableBytes(), consumed);
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  return static_cast<int64_t>(*parsed);
}

std::expected<int32_t, DecodeError>
ByteBuffer::readVarInt() noexcept {
  size_t consumed = 0;
  const auto parsed = decodeVar<uint32_t, kMaxVarIntBytes>(peek(), readableBytes(), consumed);
  if (!parsed) {
    return std::unexpected(parsed.error());  // 失败零副作用：一个字节都不消费
  }
  retrieve(consumed);  // 只有成功才推进读游标
  return static_cast<int32_t>(*parsed);
}

std::expected<int64_t, DecodeError>
ByteBuffer::readVarLong() noexcept {
  size_t consumed = 0;
  const auto parsed = decodeVar<uint64_t, kMaxVarLongBytes>(peek(), readableBytes(), consumed);
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  retrieve(consumed);
  return static_cast<int64_t>(*parsed);
}

std::expected<std::string, DecodeError>
ByteBuffer::readString(size_t maxBytes) noexcept {
  Mark mark(*this);
  const auto len = readVarInt();
  if (!len) {
    return std::unexpected(len.error());
  }
  if (*len < 0 || static_cast<size_t>(*len) > maxBytes) {
    return std::unexpected(DecodeError::kMalformed);
  }
  const size_t byteLen = static_cast<size_t>(*len);
  if (!hasBytes(byteLen)) {
    return std::unexpected(DecodeError::kIncomplete);
  }
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
  ByteBuffer other;
  other.ensureWritableBytes(readableBytes() + reserve);
  other.append(toStringView());
  swap(other);
}

void
ByteBuffer::swap(ByteBuffer& rhs) noexcept {
  buffer_.swap(rhs.buffer_);
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
    buffer_.resize(writerIndex_ + len);
  } else {
    assert(kCheapPrepend < readerIndex_);
    size_t readable = readableBytes();
    std::copy(begin() + readerIndex_, begin() + writerIndex_, begin() + kCheapPrepend);
    readerIndex_ = kCheapPrepend;
    writerIndex_ = readerIndex_ + readable;
    assert(readable == readableBytes());
  }
}

const char*
mcpe::toString(DecodeError error) noexcept {
  switch (error) {
    case DecodeError::kIncomplete:
      return "kIncomplete(need more data)";
    case DecodeError::kMalformed:
      return "kMalformed(protocol limit exceeded)";
  }
  return "kUnknown";
}
