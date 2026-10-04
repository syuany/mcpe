#include "network/PacketFraming.h"

#include <cstddef>
#include <expected>

#include "common/ByteBuffer.h"

using namespace mcpe;
using namespace mcpe::network;

std::expected<ByteBuffer, FramingError>
PacketFraming::slicePacket(ByteBuffer& recv_buf) {
  // 1. 无副作用试探解析包长 VarInt 字段与所占字节数
  const auto length_res = recv_buf.peekVarIntWithBytes();
  if (!length_res) {
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
  if (recv_buf.readableBytes() < total_len) {
    // 载荷数据尚未到齐，返回半包状态，读游标零移动
    return std::unexpected(FramingError::kIncomplete);
  }

  // 4. 消费跳过包长 VarInt 前缀
  recv_buf.retrieve(varint_len);

  // 5. Netty 风格零拷贝切出包体数据 [Packet ID + Payload]（底层共享 Storage，零堆内存分配）
  return recv_buf.readSlice(static_cast<size_t>(packet_len));
}
