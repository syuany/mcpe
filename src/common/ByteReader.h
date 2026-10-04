#ifndef BYTEREADER_H
#define BYTEREADER_H

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <string>
#include <string_view>

#include "common/VarInt.h"

namespace mcpe {

/**
 * @brief 针对只读连续内存视图（std::string_view）的高性能零拷贝流式解析器
 *
 * 对应 Minecraft 协议的反序列化需求，与 ByteBuffer 拥有完全对齐的只读解析接口，
 * 但本身不拥有堆内存、不触发任何堆分配，可直接结合 PacketFraming::slicePacketView 使用。
 */
class ByteReader {
 public:
  constexpr explicit ByteReader(std::string_view view) noexcept : view_(view) {}

  constexpr size_t readableBytes() const noexcept { return view_.size(); }
  constexpr bool empty() const noexcept { return view_.empty(); }
  constexpr const char* peek() const noexcept { return view_.data(); }
  constexpr std::string_view remaining() const noexcept { return view_; }

  constexpr bool hasBytes(size_t count) const noexcept { return view_.size() >= count; }

  void retrieve(size_t len) noexcept {
    if (len >= view_.size()) {
      view_ = {};
    } else {
      view_.remove_prefix(len);
    }
  }

  void retrieveAll() noexcept { view_ = {}; }

  template <typename T>
    requires(std::integral<T> || std::floating_point<T>)
  [[nodiscard]] std::expected<T, DecodeError> readBe() noexcept {
    if (!hasBytes(sizeof(T))) {
      return std::unexpected(DecodeError::kIncomplete);
    }
    if constexpr (sizeof(T) == 1) {
      T val = static_cast<T>(view_.front());
      retrieve(1);
      return val;
    } else if constexpr (std::integral<T>) {
      T val;
      std::memcpy(&val, view_.data(), sizeof(T));
      retrieve(sizeof(T));
      if constexpr (std::endian::native == std::endian::little) {
        val = std::byteswap(val);
      }
      return val;
    } else if constexpr (std::floating_point<T>) {
      using UInt = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
      UInt bits;
      std::memcpy(&bits, view_.data(), sizeof(UInt));
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
      return static_cast<T>(view_.front());
    } else if constexpr (std::integral<T>) {
      T val;
      std::memcpy(&val, view_.data(), sizeof(T));
      if constexpr (std::endian::native == std::endian::little) {
        val = std::byteswap(val);
      }
      return val;
    } else if constexpr (std::floating_point<T>) {
      using UInt = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
      UInt bits;
      std::memcpy(&bits, view_.data(), sizeof(UInt));
      if constexpr (std::endian::native == std::endian::little) {
        bits = std::byteswap(bits);
      }
      return std::bit_cast<T>(bits);
    }
  }

  [[nodiscard]] std::expected<int32_t, DecodeError> readVarInt() noexcept {
    auto res = varint::decodeVarInt(view_.data(), view_.size());
    if (!res) return std::unexpected(res.error());
    retrieve(res->bytes);
    return res->value;
  }

  [[nodiscard]] std::expected<int64_t, DecodeError> readVarLong() noexcept {
    auto res = varint::decodeVarLong(view_.data(), view_.size());
    if (!res) return std::unexpected(res.error());
    retrieve(res->bytes);
    return res->value;
  }

  [[nodiscard]] std::expected<int32_t, DecodeError> peekVarInt() const noexcept {
    auto res = varint::decodeVarInt(view_.data(), view_.size());
    if (!res) return std::unexpected(res.error());
    return res->value;
  }

  [[nodiscard]] std::expected<std::string, DecodeError> readString(
      size_t max_bytes = 32767
  ) noexcept {
    auto len_res = readVarInt();
    if (!len_res) return std::unexpected(len_res.error());
    if (*len_res < 0 || static_cast<size_t>(*len_res) > max_bytes) {
      return std::unexpected(DecodeError::kMalformed);
    }
    const size_t len = static_cast<size_t>(*len_res);
    if (!hasBytes(len)) return std::unexpected(DecodeError::kIncomplete);
    std::string str(view_.data(), len);
    retrieve(len);
    return str;
  }

  [[nodiscard]] std::expected<std::string_view, DecodeError> readStringView(
      size_t max_bytes = 32767
  ) noexcept {
    auto len_res = readVarInt();
    if (!len_res) return std::unexpected(len_res.error());
    if (*len_res < 0 || static_cast<size_t>(*len_res) > max_bytes) {
      return std::unexpected(DecodeError::kMalformed);
    }
    const size_t len = static_cast<size_t>(*len_res);
    if (!hasBytes(len)) return std::unexpected(DecodeError::kIncomplete);
    std::string_view str(view_.data(), len);
    retrieve(len);
    return str;
  }

 private:
  std::string_view view_;
};

}  // namespace mcpe

#endif  // BYTEREADER_H
