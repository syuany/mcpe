#include "network/PacketFraming.h"

#include <cstddef>
#include <expected>

#include "common/ByteBuffer.h"

using namespace mcpe;
using namespace mcpe::network;

std::expected<PacketFraming::FrameResult, FramingError>
PacketFraming::slicePacketFromView(std::string_view view) {
  // 1. 无副作用从视图解析包长 VarInt 字段
  const auto length_res = varint::decodeVarInt(view.data(), view.size());
  if (!length_res) {
    // 若 VarInt 不完整则说明是半包；若超过 5 字节上限则为畸形长度
    return (length_res.error() == DecodeError::kIncomplete)
               ? std::unexpected(FramingError::kIncomplete)
               : std::unexpected(FramingError::kMalformedLength);
  }

  const auto [packet_len, varint_len] = *length_res;
  // 2. 校验包长合法性：长度必须 > 0 且不得超过单包 2MB 上限
  if (packet_len <= 0 || static_cast<size_t>(packet_len) > kMaxPacketSize) {
    return std::unexpected(FramingError::kInvalidPacketSize);
  }

  // 3. 校验整包（长度前缀 + 包体数据）是否已全量接收就绪
  const size_t total_len = static_cast<size_t>(packet_len) + varint_len;
  if (view.size() < total_len) {
    // 载荷数据尚未到齐，返回半包状态
    return std::unexpected(FramingError::kIncomplete);
  }

  // 4. 提取包体 [Packet ID + Payload] 封装为独立的 ByteBuffer
  ByteBuffer packet;
  packet.append(view.data() + varint_len, static_cast<size_t>(packet_len));

  return FrameResult{
      .packet = std::move(packet),
      .consumed_bytes = total_len,
  };
}

std::expected<ByteBuffer, FramingError>
PacketFraming::slicePacket(ByteBuffer& recv_buf) {
  auto res = slicePacketFromView(recv_buf.toStringView());
  if (!res) {
    return std::unexpected(res.error());
  }

  recv_buf.retrieve(res->consumed_bytes);
  return std::move(res->packet);
}
