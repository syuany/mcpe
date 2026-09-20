#include "include/mcpe/network/PacketFraming.h"

#include <cstddef>
#include <expected>

#include "mcpe/common/ByteBuffer.h"

using namespace mcpe;
using namespace mcpe::network;

std::expected<ByteBuffer, FramingError>
PacketFraming::slicePacket(ByteBuffer& recv_buf) {
  const auto length_res = recv_buf.peekVarIntWithBytes();
  if (!length_res) {
    return (length_res.error() == DecodeError::kIncomplete)
               ? std::unexpected(FramingError::kIncomplete)
               : std::unexpected(FramingError::kMalformedLength);
  }

  const auto [packet_len, varint_len] = *length_res;
  if (packet_len <= 0 || packet_len > kMaxPacketSize) {
    return std::unexpected(FramingError::kInvalidPacketSize);
  }

  const size_t total_len = packet_len + varint_len;
  if (recv_buf.readableBytes() < total_len) {
    return std::unexpected(FramingError::kIncomplete);
  }

  recv_buf.retrieve(varint_len);
  ByteBuffer packet;
  packet.append(recv_buf.peek(), packet_len);
  recv_buf.retrieve(packet_len);

  return packet;
}
