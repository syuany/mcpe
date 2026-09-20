#ifndef PACKETFRAMING_H
#define PACKETFRAMING_H

#include <cstddef>
#include <expected>

#include "mcpe/common/ByteBuffer.h"

namespace mcpe::network {

enum class FramingError {
  kIncomplete,        // 半包：数据未到齐，保留游标，等待更多数据
  kMalformedLength,   // 畸形：VarInt 超过 5 字节
  kInvalidPacketSize  // 负数、0、超大数
};

class PacketFraming {
 public:
  // 单包最大合法上限（2MB，防止恶意连接发动 OOM 攻击）
  static constexpr size_t kMaxPacketSize = 2097151;

  /**
   * 从累积接收缓冲区中尝试切出一个完整的数据包。
   *
   * 契约保证：
   * - 若返回 kIncomplete，recv_buf 的读游标绝不移动（零副作用）！
   * - 若成功，返回包含 [Packet ID + Payload] 的新 ByteBuffer，recv_buf 游标自动划过该包。
   */
  static std::expected<ByteBuffer, FramingError> slicePacket(ByteBuffer& recv_buf);
};

}  // namespace mcpe::network

#endif  // PACKETFRAMING_H