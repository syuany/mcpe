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

using namespace mcpe;

ByteBuffer
ByteBuffer::slice(size_t len) const {
  assert(len <= readableBytes() && "Slice length exceeds readable bytes");
  return ByteBuffer(storage_, offset_ + readerIndex_, len);
}

ByteBuffer
ByteBuffer::readSlice(size_t len) {
  assert(len <= readableBytes() && "Slice length exceeds readable bytes");
  ByteBuffer sl(storage_, offset_ + readerIndex_, len);
  retrieve(len);
  return sl;
}

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
  readerIndex_ = is_slice_ ? 0 : kCheapPrepend;
  writerIndex_ = readerIndex_;
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
ByteBuffer::prepend(const void* data, size_t len) {
  if (is_slice_ || (storage_ && storage_.use_count() > 1)) {
    // Copy-On-Write (写时复制隔离防护)
    size_t readable = readableBytes();
    size_t new_prepend = std::max(kCheapPrepend, len);
    size_t new_capacity = std::max(new_prepend + readable + writableBytes(), kCheapPrepend + kInitialSize);
    auto new_storage = std::make_shared<Storage>(new_capacity);
    if (readable > 0) {
      std::copy(peek(), peek() + readable, new_storage->data.data() + new_prepend);
    }
    storage_ = std::move(new_storage);
    offset_ = 0;
    readerIndex_ = new_prepend;
    writerIndex_ = new_prepend + readable;
    capacity_ = storage_->data.size();
    is_slice_ = false;
  }
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
ByteBuffer::ensureWritableBytes(size_t len) {
  if (writableBytes() < len || (storage_ && storage_.use_count() > 1)) {
    makeSpace(len);
  }
  assert(writableBytes() >= len);
}

void
ByteBuffer::makeSpace(size_t len) {
  if (is_slice_ || (storage_ && storage_.use_count() > 1)) {
    // Copy-On-Write (写时复制隔离防护)：
    // 当检测到当前缓冲区为切片，或底层 Storage 正被外部切片共享时，
    // 分配独立全新的 Storage 并迁移未读字节，彻底切断内存共享关系，保障在途切片内存绝对安全
    size_t readable = readableBytes();
    size_t new_capacity = std::max(kCheapPrepend + readable + len, kCheapPrepend + kInitialSize);
    auto new_storage = std::make_shared<Storage>(new_capacity);
    if (readable > 0) {
      std::copy(peek(), peek() + readable, new_storage->data.data() + kCheapPrepend);
    }
    storage_ = std::move(new_storage);
    offset_ = 0;
    readerIndex_ = kCheapPrepend;
    writerIndex_ = kCheapPrepend + readable;
    capacity_ = storage_->data.size();
    is_slice_ = false;
    return;
  }

  // 独占状态：若总空闲空间不足，则直接扩容底层的 vector
  if (writableBytes() + prependableBytes() < len + kCheapPrepend) {
    storage_->data.resize(writerIndex_ + len);
    capacity_ = storage_->data.size();
  } else {
    // 空间足够（之前读过的 prependableBytes 可复用），进行内存整理（碎片紧凑）
    assert(kCheapPrepend < readerIndex_);
    size_t readable = readableBytes();
    std::copy(begin() + readerIndex_, begin() + writerIndex_, begin() + kCheapPrepend);
    readerIndex_ = kCheapPrepend;
    writerIndex_ = readerIndex_ + readable;
    assert(readable == readableBytes());
  }
}
