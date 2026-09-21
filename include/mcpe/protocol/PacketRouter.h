#ifndef PACKETROUTER_H
#define PACKETROUTER_H

#include <memory>

#include "mcpe/common/ByteBuffer.h"
#include "mcpe/protocol/ConnectionState.h"

namespace mcpe::network {
class TcpSession;
}

namespace mcpe::protocol {

/**
 * @brief 协议数据包全局路由器与分发中心
 *
 * 充当网络层与业务层之间的解耦枢纽：
 * 1. 负责从解出的数据包头部读取 VarInt 类型的 Packet ID
 * 2. 结合当前 Session 的 ConnectionState 状态机，将数据包精准路由到对应的业务 Handler
 * 3. 校验非法数据包、畸形包与未授权包，并执行断网与安全防御
 * 4. 维护 PLAY 阶段的原版客户端例行包白名单（静默忽略不中断连接）
 */
class PacketRouter {
 public:
  /**
   * @brief 数据包路由分发主入口
   *
   * @param session 当前触发数据包的客户端会话
   * @param state 会话当前所处的协议连接状态
   * @param packet 已剔除外层总长度前缀的完整包体（格式为：[Packet ID (VarInt)] + [Payload]）
   */
  static void dispatch(
      const std::shared_ptr<network::TcpSession>& session,
      ConnectionState state,
      ByteBuffer packet
  );
};

}  // namespace mcpe::protocol

#endif  // PACKETROUTER_H