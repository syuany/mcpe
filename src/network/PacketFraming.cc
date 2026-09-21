#include "include/mcpe/network/PacketFraming.h"

#include <cstddef>
#include <expected>

#include "mcpe/common/ByteBuffer.h"

using namespace mcpe;
using namespace mcpe::network;

std::expected<ByteBuffer, FramingError>
PacketFraming::slicePacket(ByteBuffer& recv_buf) {
  // 1. 无副作用预览包长 VarInt 字段（不移动 recv_buf 的读游标）
  const auto length_res = recv_buf.peekVarIntWithBytes();
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
  const size_t total_len = packet_len + varint_len;
  if (recv_buf.readableBytes() < total_len) {
    // 载荷数据尚未到齐，返回半包状态，读游标保持在原地零移动
    return std::unexpected(FramingError::kIncomplete);
  }

  // 4. 数据已完整就绪：消费掉外层的 VarInt 包长前缀
  recv_buf.retrieve(varint_len);

  // 5. 提取包体 [Packet ID + Payload] 封装为独立的 ByteBuffer 并划过对应字节
  ByteBuffer packet;
  packet.append(recv_buf.peek(), packet_len);
  recv_buf.retrieve(packet_len);

  return packet;
}
