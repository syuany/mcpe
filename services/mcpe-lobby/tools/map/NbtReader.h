/**
 * @file NbtReader.h
 * @brief 轻量级只读 NBT 二进制流解析工具（Foundations 基础层）
 */

#ifndef MCPE_COMMON_NBT_READER_H
#define MCPE_COMMON_NBT_READER_H

#include <bit>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace mcpe::common {

enum class NbtTag : uint8_t {
  End = 0,
  Byte = 1,
  Short = 2,
  Int = 3,
  Long = 4,
  Float = 5,
  Double = 6,
  ByteArray = 7,
  String = 8,
  List = 9,
  Compound = 10,
  IntArray = 11,
  LongArray = 12
};

class NbtReader {
 public:
  explicit NbtReader(std::span<const uint8_t> data) noexcept : data_(data) {}

  [[nodiscard]] size_t
  offset() const noexcept {
    return offset_;
  }
  void
  setOffset(size_t off) noexcept {
    offset_ = off;
  }
  [[nodiscard]] size_t
  remaining() const noexcept {
    return data_.size() > offset_ ? data_.size() - offset_ : 0;
  }
  [[nodiscard]] bool
  hasRemaining(size_t bytes = 1) const noexcept {
    return offset_ + bytes <= data_.size();
  }

  std::optional<uint8_t>
  readU8() noexcept {
    if (!hasRemaining(1)) return std::nullopt;
    return data_[offset_++];
  }

  std::optional<int8_t>
  readI8() noexcept {
    if (!hasRemaining(1)) return std::nullopt;
    return static_cast<int8_t>(data_[offset_++]);
  }

  std::optional<int16_t>
  readI16() noexcept {
    if (!hasRemaining(2)) return std::nullopt;
    uint16_t val = (static_cast<uint16_t>(data_[offset_]) << 8) |
                   static_cast<uint16_t>(data_[offset_ + 1]);
    offset_ += 2;
    return static_cast<int16_t>(val);
  }

  std::optional<int32_t>
  readI32() noexcept {
    if (!hasRemaining(4)) return std::nullopt;
    uint32_t val = (static_cast<uint32_t>(data_[offset_]) << 24) |
                   (static_cast<uint32_t>(data_[offset_ + 1]) << 16) |
                   (static_cast<uint32_t>(data_[offset_ + 2]) << 8) |
                   static_cast<uint32_t>(data_[offset_ + 3]);
    offset_ += 4;
    return static_cast<int32_t>(val);
  }

  std::optional<int64_t>
  readI64() noexcept {
    if (!hasRemaining(8)) return std::nullopt;
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
      val = (val << 8) | static_cast<uint64_t>(data_[offset_ + i]);
    }
    offset_ += 8;
    return static_cast<int64_t>(val);
  }

  std::optional<double>
  readDouble() noexcept {
    auto i64 = readI64();
    if (!i64) return std::nullopt;
    return std::bit_cast<double>(static_cast<uint64_t>(*i64));
  }

  std::optional<std::string_view>
  readString() noexcept {
    auto len_opt = readI16();
    if (!len_opt || *len_opt < 0) return std::nullopt;
    const size_t len = static_cast<size_t>(*len_opt);
    if (!hasRemaining(len)) return std::nullopt;
    std::string_view sv(reinterpret_cast<const char*>(data_.data() + offset_), len);
    offset_ += len;
    return sv;
  }

  std::optional<std::span<const uint8_t>>
  readByteArray() noexcept {
    auto len_opt = readI32();
    if (!len_opt || *len_opt < 0) return std::nullopt;
    const size_t len = static_cast<size_t>(*len_opt);
    if (!hasRemaining(len)) return std::nullopt;
    std::span<const uint8_t> span(data_.data() + offset_, len);
    offset_ += len;
    return span;
  }

  bool
  skip(size_t bytes) noexcept {
    if (!hasRemaining(bytes)) return false;
    offset_ += bytes;
    return true;
  }

  bool
  skipTag(NbtTag type) noexcept {
    switch (type) {
      case NbtTag::End:
        return true;
      case NbtTag::Byte:
        return skip(1);
      case NbtTag::Short:
        return skip(2);
      case NbtTag::Int:
      case NbtTag::Float:
        return skip(4);
      case NbtTag::Long:
      case NbtTag::Double:
        return skip(8);
      case NbtTag::ByteArray: {
        auto len = readI32();
        return len && *len >= 0 && skip(static_cast<size_t>(*len));
      }
      case NbtTag::String: {
        auto len = readI16();
        return len && *len >= 0 && skip(static_cast<size_t>(*len));
      }
      case NbtTag::List: {
        auto elem_type_opt = readU8();
        auto len_opt = readI32();
        if (!elem_type_opt || !len_opt || *len_opt < 0) return false;
        const auto elem_type = static_cast<NbtTag>(*elem_type_opt);
        const int32_t count = *len_opt;
        for (int32_t i = 0; i < count; ++i) {
          if (!skipTag(elem_type)) return false;
        }
        return true;
      }
      case NbtTag::Compound: {
        while (true) {
          auto tag_type_opt = readU8();
          if (!tag_type_opt) return false;
          auto tag_type = static_cast<NbtTag>(*tag_type_opt);
          if (tag_type == NbtTag::End) break;
          auto name_opt = readString();
          if (!name_opt || !skipTag(tag_type)) return false;
        }
        return true;
      }
      case NbtTag::IntArray: {
        auto len = readI32();
        return len && *len >= 0 && skip(static_cast<size_t>(*len) * 4);
      }
      case NbtTag::LongArray: {
        auto len = readI32();
        return len && *len >= 0 && skip(static_cast<size_t>(*len) * 8);
      }
      default:
        return false;
    }
  }

 private:
  std::span<const uint8_t> data_;
  size_t offset_{0};
};

}  // namespace mcpe::common

#endif  // MCPE_COMMON_NBT_READER_H
