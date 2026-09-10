#include "include/mcpe/common/ByteBuffer.h"

#include <endian.h>
#include <netinet/in.h>

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>

void mcpe::ByteBuffer::append(const char* data, size_t len) {
  ensureWritableBytes(len);
  std::copy(data, data + len, beginWrite());
  hasWritten(len);
}

void mcpe::ByteBuffer::append(const void* data, size_t len) {
  append(static_cast<const char*>(data), len);
}

void mcpe::ByteBuffer::append(std::string_view str) { append(str.data(), str.size()); }

void mcpe::ByteBuffer::appendInt64(int64_t x) {
  uint64_t be64 = __builtin_bswap64(static_cast<uint64_t>(x));
  append(&be64, sizeof(be64));
}

void mcpe::ByteBuffer::appendInt32(int32_t x) {
  uint32_t be32 = htobe32(static_cast<uint32_t>(x));
  append(&be32, sizeof(be32));
}

void mcpe::ByteBuffer::appendInt16(int16_t x) {
  uint16_t be16 = htobe16(static_cast<uint16_t>(x));
  append(&be16, sizeof(be16));
}

void mcpe::ByteBuffer::appendInt8(int8_t x) { append(&x, sizeof(x)); }

// IEEE-754 单精度位模式按网络字节序（大端）追加
void mcpe::ByteBuffer::appendFloat(float x) {
  uint32_t be = htobe32(std::bit_cast<uint32_t>(x));
  append(&be, sizeof(be));
}

void mcpe::ByteBuffer::appendDouble(double x) {
  uint64_t be = htobe64(std::bit_cast<uint64_t>(x));
  append(&be, sizeof(be));
}

int64_t mcpe::ByteBuffer::readInt64() {
  int64_t res = peekInt64();
  retrieveInt64();
  return res;
}

int32_t mcpe::ByteBuffer::readInt32() {
  int32_t res = peekInt32();
  retrieveInt32();
  return res;
}

int16_t mcpe::ByteBuffer::readInt16() {
  int16_t res = peekInt16();
  retrieveInt16();
  return res;
}

int8_t mcpe::ByteBuffer::readInt8() {
  int8_t res = peekInt8();
  retrieveInt8();
  return res;
}

float mcpe::ByteBuffer::readFloat() {
  float res = peekFloat();
  retrieveFloat();
  return res;
}

double mcpe::ByteBuffer::readDouble() {
  double res = peekDouble();
  retrieveDouble();
  return res;
}

int64_t mcpe::ByteBuffer::peekInt64() const noexcept {
  assert(readableBytes() >= sizeof(int64_t));
  uint64_t be64 = 0;
  ::memcpy(&be64, peek(), sizeof(int64_t));
  return static_cast<int64_t>(__builtin_bswap64(be64));
}

int32_t mcpe::ByteBuffer::peekInt32() const noexcept {
  assert(readableBytes() >= sizeof(int32_t));
  uint32_t be32 = 0;
  ::memcpy(&be32, peek(), sizeof(int32_t));
  return static_cast<int32_t>(be32toh(be32));
}

int16_t mcpe::ByteBuffer::peekInt16() const noexcept {
  assert(readableBytes() >= sizeof(int16_t));
  uint16_t be16 = 0;
  ::memcpy(&be16, peek(), sizeof(int16_t));
  return static_cast<int16_t>(be16toh(be16));
}

int8_t mcpe::ByteBuffer::peekInt8() const noexcept {
  assert(readableBytes() >= sizeof(int8_t));
  return static_cast<int8_t>(*peek());
}

float mcpe::ByteBuffer::peekFloat() const noexcept {
  assert(readableBytes() >= sizeof(float));
  uint32_t be32 = 0;
  ::memcpy(&be32, peek(), sizeof(uint32_t));
  return std::bit_cast<float>(be32toh(be32));
}

double mcpe::ByteBuffer::peekDouble() const noexcept {
  assert(readableBytes() >= sizeof(double));
  uint64_t be64 = 0;
  ::memcpy(&be64, peek(), sizeof(double));
  return std::bit_cast<double>(__builtin_bswap64(be64));
}

void mcpe::ByteBuffer::writeVarInt(int32_t x) {}

int32_t mcpe::ByteBuffer::readVarInt() {}

void mcpe::ByteBuffer::retrieve(size_t len) noexcept {
  assert(len <= readableBytes());
  if (len < readableBytes()) {
    readerIndex_ += len;
  } else {
    retrieveAll();
  }
}

void mcpe::ByteBuffer::retrieveUntil(const char* end) noexcept {
  assert(peek() <= end);
  assert(end <= beginWrite());
  retrieve(static_cast<size_t>(end - peek()));
}

std::string mcpe::ByteBuffer::retrieveAsString(size_t len) {
  assert(len <= readableBytes());
  std::string res(peek(), len);
  retrieve(len);
  return res;
}

std::string mcpe::ByteBuffer::retrieveAllAsString() { return retrieveAsString(readableBytes()); }

void mcpe::ByteBuffer::retrieveInt64() noexcept { return retrieve(sizeof(int64_t)); }

void mcpe::ByteBuffer::retrieveInt32() noexcept { return retrieve(sizeof(int32_t)); }

void mcpe::ByteBuffer::retrieveInt16() noexcept { return retrieve(sizeof(int16_t)); }

void mcpe::ByteBuffer::retrieveInt8() noexcept { return retrieve(sizeof(int8_t)); }

void mcpe::ByteBuffer::retrieveFloat() noexcept { return retrieve(sizeof(float)); }

void mcpe::ByteBuffer::retrieveDouble() noexcept { return retrieve(sizeof(double)); }

void mcpe::ByteBuffer::retrieveAll() noexcept {
  readerIndex_ = kCheapPrepend;
  writerIndex_ = kCheapPrepend;
}

std::string_view mcpe::ByteBuffer::toStringView() const noexcept {
  return std::string_view(peek(), readableBytes());
}

std::span<const char> mcpe::ByteBuffer::toSpan() const noexcept {
  return std::span<const char>(peek(), readableBytes());
}

void mcpe::ByteBuffer::prepend(const void* data, size_t len) noexcept {
  assert(len <= prependableBytes());
  const char* des = static_cast<const char*>(data);
  readerIndex_ -= len;
  std::copy(des, des + len, begin() + readerIndex_);
}

void mcpe::ByteBuffer::shrink(size_t reserve) {
  ByteBuffer other;
  other.ensureWritableBytes(readableBytes() + reserve);
  other.append(toStringView());
  swap(other);
}

void mcpe::ByteBuffer::swap(ByteBuffer& rhs) noexcept {
  buffer_.swap(rhs.buffer_);
  std::swap(readerIndex_, rhs.readerIndex_);
  std::swap(writerIndex_, rhs.writerIndex_);
}

void mcpe::ByteBuffer::ensureWritableBytes(size_t len) {
  if (writableBytes() < len) {
    makeSpace(len);
  }
  assert(writableBytes() >= len);
}

void mcpe::ByteBuffer::makeSpace(size_t len) {
  if (readableBytes() + prependableBytes() < len + kCheapPrepend) {
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
