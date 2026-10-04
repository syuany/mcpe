#ifndef PACKETROUTER_H
#define PACKETROUTER_H

#include <cstdint>
#include <memory>

#include "common/ByteBuffer.h"
#include "protocol/ConnectionState.h"
#include "protocol/ProtocolHandler.h"

namespace mcpe::protocol {

class ClientSession;

/**
 * @brief Minecraft 1.12.2 (Protocol 340) 数据包路由分发器（对标 Cuberite cProtocol_1_12_2 架构设计）
 *
 * 继承 ProtocolHandler 多态接口，负责 Protocol 340 的数据包边界解析与路由：
 * 1. 自动从 session->state() 获取当前状态并派发到对应业务处理器
 * 2. 校验非法数据包、畸形包与未授权包，并执行断网与安全防御
 * 3. 维护 PLAY 阶段的原版客户端例行包白名单（静默忽略不中断连接）
 * 4. 借鉴 Cuberite 检查数据包完整消费（Under-read 告警）
 */
class PacketRouter : public ProtocolHandler {
 public:
  int32_t protocolVersion() const noexcept override { return 340; }

  void handlePacket(
      const std::shared_ptr<ClientSession>& session,
      ByteBuffer packet
  ) override {
    dispatch(session, std::move(packet));
  }

  void handlePacket(
      const std::shared_ptr<ClientSession>& session,
      ConnectionState state,
      ByteBuffer packet
  ) override {
    dispatch(session, state, std::move(packet));
  }

  /**
   * @brief 数据包路由分发主入口（直接内聚从 session->state() 获取当前状态）
   *
   * @param session 当前触发数据包的客户端会话
   * @param packet 已剔除外层总长度前缀的完整包体（格式为：[Packet ID (VarInt)] + [Payload]）
   */
  static void dispatch(
      const std::shared_ptr<ClientSession>& session,
      ByteBuffer packet
  );

  /**
   * @brief 重载：显式指定状态的分发入口（兼容单元测试特定状态隔离注入）
   *
   * @param session 当前触发数据包的客户端会话
   * @param state 会话当前所处的协议连接状态
   * @param packet 已剔除外层总长度前缀的完整包体
   */
  static void dispatch(
      const std::shared_ptr<ClientSession>& session,
      ConnectionState state,
      ByteBuffer packet
  );
};

}  // namespace mcpe::protocol

#endif  // PACKETROUTER_H