#ifndef STATUSHANDLER_H
#define STATUSHANDLER_H

#include <cstddef>
#include <memory>

#include "common/ByteBuffer.h"
#include "protocol/GameTypes.h"

namespace mcpe::protocol {

class ClientSession;

/**
 * @brief 处理 STATUS 状态下状态查询（Server List Ping）的业务处理器
 *
 * 纯粹负责 STATUS 阶段的两大报文：
 * 1. [TASK-1.7] 处理 Status Request (0x00)：组装包含 MOTD、版本号、最大/在线人数的 JSON 描述包下发
 * 2. [TASK-1.8] 处理 Ping (0x01)：读取 8 字节大端时间戳并回送 Pong (0x01)
 */
class StatusHandler {
 public:
  /**
   * @brief 处理客户端服务器列表状态查询请求 (Status Request 0x00)
   *
   * @param session 当前客户端业务会话
   * @param config 服务端运行配置
   * @param online_players 当前真实在线玩家数量（由 ClientManager 动态提供）
   */
  static void handleStatusRequest(
      const std::shared_ptr<ClientSession>& session,
      const ServerConfig& config = ServerConfig::defaultInstance(),
      size_t online_players = 0
  );

  /**
   * @brief 处理客户端延迟探测 Ping 数据包 (Ping 0x01)
   *
   * @param session 当前客户端业务会话
   * @param payload Ping 数据载荷（包含 8 字节大端时间戳）
   */
  static void handlePing(const std::shared_ptr<ClientSession>& session, ByteBuffer& payload);
};

}  // namespace mcpe::protocol

#endif  // STATUSHANDLER_H