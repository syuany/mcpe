#include "include/mcpe/common/ByteBuffer.h"

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

using namespace mcpe;

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
  ensureWritableBytes(varint::kMaxVarIntBytes);
  const size_t bytes = varint::encodeVarInt(value, beginWrite());
  hasWritten(bytes);
}

void
ByteBuffer::appendVarLong(int64_t value) {
  ensureWritableBytes(varint::kMaxVarLongBytes);
  const size_t bytes = varint::encodeVarLong(value, beginWrite());
  hasWritten(bytes);
}

void
ByteBuffer::prependVarInt(int32_t value) {
  char buf[varint::kMaxVarIntBytes];
  const size_t bytes = varint::encodeVarInt(value, buf);
  prepend(buf, bytes);
}

void
ByteBuffer::appendString(std::string_view str) {
  appendVarInt(static_cast<int32_t>(str.size()));
  append(str.data(), str.size());
}

std::expected<VarIntResult, DecodeError>
ByteBuffer::peekVarIntWithBytes() const noexcept {
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
