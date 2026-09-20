#ifndef MCPE_COMMON_VARINT_H
#define MCPE_COMMON_VARINT_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

namespace mcpe {

/** 解析错误分类（统一错误契约：失败零副作用）：
 *  - kIncomplete 数据不足（TCP 半包）。保留读游标、等下一次 read。
 *  - kMalformed  畸形或突破协议上限（VarInt > 5 字节 / VarLong > 10 字节）。不可信输入，断开连接。
 */
enum class DecodeError : std::uint8_t { kIncomplete, kMalformed };

[[nodiscard]] const char* toString(DecodeError error) noexcept;

namespace varint {

inline constexpr uint8_t kValueMask = 0x7F;        // 0b01111111: 低 7 位有效载荷掩码
inline constexpr uint8_t kContinuationBit = 0x80;  // 0b10000000: 最高位续标志位

inline constexpr size_t kMaxVarIntBytes = 5;
inline constexpr size_t kMaxVarLongBytes = 10;
inline constexpr size_t kMaxBytes32 = 5;
inline constexpr size_t kMaxBytes64 = 10;

/** 解码结果类型：包含反序列化数值与实际消耗的字节数 */
template <typename T>
struct DecodedVar {
  T value{};
  size_t bytes{0};

  constexpr bool operator==(const DecodedVar&) const = default;
};

using VarIntResult = DecodedVar<int32_t>;
using VarLongResult = DecodedVar<int64_t>;

/** 栈上轻量预分配的 VarInt 编码结果（避免堆分配） */
struct EncodedVarInt {
  std::array<char, kMaxVarIntBytes> data{};
  size_t size{0};

  [[nodiscard]] std::string_view view() const noexcept {
    return std::string_view(data.data(), size);
  }
  [[nodiscard]] std::span<const char> span() const noexcept {
    return std::span<const char>(data.data(), size);
  }
};

/** 栈上轻量预分配的 VarLong 编码结果（避免堆分配） */
struct EncodedVarLong {
  std::array<char, kMaxVarLongBytes> data{};
  size_t size{0};

  [[nodiscard]] std::string_view view() const noexcept {
    return std::string_view(data.data(), size);
  }
  [[nodiscard]] std::span<const char> span() const noexcept {
    return std::span<const char>(data.data(), size);
  }
};

/** VarInt / VarLong 核心解析模板（零拷贝、不越界、三态返回）
 *  UInt      : uint32_t 或 uint64_t
 *  kMaxBytes : 5 或 10
 */
template <typename UInt, size_t kMaxBytes>
[[nodiscard]] constexpr std::expected<DecodedVar<UInt>, DecodeError>
decode(const char* data, size_t available) noexcept {
  UInt value = 0;
  for (size_t i = 0; i < kMaxBytes; ++i) {
    if (i >= available) {
      return std::unexpected(DecodeError::kIncomplete);
    }
    const uint8_t byte = static_cast<uint8_t>(data[i]);
    value |= static_cast<UInt>(byte & kValueMask) << (7 * i);
    if ((byte & kContinuationBit) == 0) {
      return DecodedVar<UInt>{value, i + 1};
    }
  }
  return std::unexpected(DecodeError::kMalformed);
}

/** 从内存中安全探测 VarInt（不移动游标，同时返回数值与字节数） */
[[nodiscard]] inline std::expected<VarIntResult, DecodeError>
decodeVarInt(const char* data, size_t available) noexcept {
  const auto res = decode<uint32_t, kMaxVarIntBytes>(data, available);
  if (!res) {
    return std::unexpected(res.error());
  }
  return VarIntResult{static_cast<int32_t>(res->value), res->bytes};
}

[[nodiscard]] inline std::expected<VarIntResult, DecodeError>
decodeVarInt(std::span<const char> data) noexcept {
  return decodeVarInt(data.data(), data.size());
}

/** 从内存中安全探测 VarLong（不移动游标，同时返回数值与字节数） */
[[nodiscard]] inline std::expected<VarLongResult, DecodeError>
decodeVarLong(const char* data, size_t available) noexcept {
  const auto res = decode<uint64_t, kMaxVarLongBytes>(data, available);
  if (!res) {
    return std::unexpected(res.error());
  }
  return VarLongResult{static_cast<int64_t>(res->value), res->bytes};
}

[[nodiscard]] inline std::expected<VarLongResult, DecodeError>
decodeVarLong(std::span<const char> data) noexcept {
  return decodeVarLong(data.data(), data.size());
}

/** 将 int32_t 编码为 VarInt 写入 out 缓冲区（out 必须至少有 kMaxVarIntBytes = 5 字节容量）
 *  @return 实际写入的字节数（1~5）
 */
inline size_t encodeVarInt(int32_t value, char* out) noexcept {
  uint32_t val = static_cast<uint32_t>(value);
  size_t bytes = 0;
  do {
    uint8_t temp = static_cast<uint8_t>(val & kValueMask);
    val >>= 7;
    if (val != 0) {
      temp |= kContinuationBit;
    }
    out[bytes++] = static_cast<char>(temp);
  } while (val != 0);
  return bytes;
}

/** 将 int64_t 编码为 VarLong 写入 out 缓冲区（out 必须至少有 kMaxVarLongBytes = 10 字节容量）
 *  @return 实际写入的字节数（1~10）
 */
inline size_t encodeVarLong(int64_t value, char* out) noexcept {
  uint64_t val = static_cast<uint64_t>(value);
  size_t bytes = 0;
  do {
    uint8_t temp = static_cast<uint8_t>(val & kValueMask);
    val >>= 7;
    if (val != 0) {
      temp |= kContinuationBit;
    }
    out[bytes++] = static_cast<char>(temp);
  } while (val != 0);
  return bytes;
}

/** 返回栈上编码对象，方便直接使用 .view() 或 .span() */
[[nodiscard]] inline EncodedVarInt encodeVarInt(int32_t value) noexcept {
  EncodedVarInt res;
  res.size = encodeVarInt(value, res.data.data());
  return res;
}

[[nodiscard]] inline EncodedVarLong encodeVarLong(int64_t value) noexcept {
  EncodedVarLong res;
  res.size = encodeVarLong(value, res.data.data());
  return res;
}

}  // namespace varint

// 提升常用类型至 mcpe 命名空间，便于直接使用
using VarIntResult = varint::VarIntResult;
using VarLongResult = varint::VarLongResult;

}  // namespace mcpe

#endif  // MCPE_COMMON_VARINT_H
