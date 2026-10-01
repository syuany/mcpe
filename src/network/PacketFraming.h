#ifndef PACKETFRAMING_H
#define PACKETFRAMING_H

#include <cstddef>
#include <expected>

#include "common/ByteBuffer.h"

namespace mcpe::network {

/**
 * @brief 拆包（Packet Framing）错误分类
 */
enum class FramingError {
  kIncomplete,  ///< 半包：数据未到齐（等待对端后续网络数据，游标零移动）
  kMalformedLength,  ///< 畸形长度：包头 VarInt 长度格式错误（例如超过 5 字节上限）
  kInvalidPacketSize  ///< 非法包长：包长数值非法（<= 0 或超过单包 2MB 上限）
};

/**
 * @brief Minecraft 协议基于变长长度前缀的流式拆包器
 *
 * Minecraft 传输层使用 [Packet Length: VarInt] + [Packet ID: VarInt] + [Payload] 的二进制格式。
 * 本拆包器负责从 TCP 字节流缓冲区中切分出独立的数据包，解决粘包与半包问题，并防范恶意包 Dos 攻击。
 */
class PacketFraming {
 public:
  /// 单包最大合法上限（2MB = 2097151 字节，超出则直接拒绝，防止恶意连接发动 OOM 攻击）
  static constexpr size_t kMaxPacketSize = 2097151;

  struct FrameResult {
    ByteBuffer packet;
    size_t consumed_bytes{0};
  };

  /**
   * @brief 从连续内存视图中尝试切出一个完整数据包（零拷贝输入，支持基于 SessionReadBuffer 切包）
   *
   * @param view 当前未消费的可读数据零拷贝视图
   * @return 成功返回拆分出的数据包 ByteBuffer 以及消耗的总字节数（包长前缀 + 包体），失败返回 FramingError
   */
  static std::expected<FrameResult, FramingError> slicePacketFromView(std::string_view view);

  /**
   * @brief 从累积接收缓冲区中尝试切出一个完整的数据包
   *
   * 严格遵循“失败零副作用”契约：
   * - 若返回 kIncomplete，recv_buf 的读游标绝不移动（字节保留等待后续读取）；
   * - 若返回错误（kMalformedLength 或 kInvalidPacketSize），由调用方切断连接；
   * - 若成功切出，返回包含 [Packet ID + Payload] 的新 ByteBuffer，且 recv_buf 游标精准划过该整包。
   *
   * @param recv_buf 累积接收数据的缓冲区引用
   * @return 成功返回拆分出的数据包 ByteBuffer，失败返回 FramingError
   */
  static std::expected<ByteBuffer, FramingError> slicePacket(ByteBuffer& recv_buf);
};

}  // namespace mcpe::network

#endif  // PACKETFRAMING_H