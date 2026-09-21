#ifndef STATUSHANDLER_H
#define STATUSHANDLER_H

#include <memory>

#include "mcpe/common/ByteBuffer.h"

namespace mcpe::network {
class TcpSession;
}

namespace mcpe::protocol {

/**
 * @brief 处理握手（Handshake）与状态查询（Server List Ping）的业务处理器
 *
 * 覆盖 Minecraft 协议生命周期前期的关键业务：
 * 1. [TASK-1.6] 处理 Handshake (0x00)：解析客户端协议版本、域名地址、端口及目标状态
 * 2. [TASK-1.7] 处理 Status Request (0x00)：组装包含 MOTD、版本号、最大/在线人数的 JSON 描述包下发
 * 3. [TASK-1.8] 处理 Ping (0x01)：读取 8 字节大端时间戳并回送 Pong (0x01)
 */
class StatusHandler {
 public:
  /**
   * @brief 处理客户端握手数据包 (Handshake 0x00)
   *
   * 解析协议版本号、主机名、端口号与目标意图 Next State：
   * - Next State == 1：流转至 ConnectionState::STATUS（状态查询）
   * - Next State == 2：流转至 ConnectionState::LOGIN（玩家进服登录）
   * - 其它值：非法意图，记录告警并断开连接
   *
   * @param session 当前客户端会话
   * @param payload 握手包数据载荷（已剔除 Packet ID）
   */
  static void
  handleHandshake(const std::shared_ptr<network::TcpSession>& session, ByteBuffer& payload);

  /**
   * @brief 处理客户端服务器列表状态查询请求 (Status Request 0x00)
   *
   * 构造符合 Minecraft 1.12.2 (Protocol 340) 规范的 Status Response (0x00) JSON 报文并下发
   *
   * @param session 当前客户端会话
   */
  static void handleStatusRequest(const std::shared_ptr<network::TcpSession>& session);

  /**
   * @brief 处理客户端延迟探测 Ping 数据包 (Ping 0x01)
   *
   * 读取载荷中原生的 8 字节大端长整型（Long）时间戳，构造 Pong (0x01) 原样回显，
   * 供客户端多人游戏列表计算往返网络延迟（RTT）并显示绿色信号条。
   *
   * @param session 当前客户端会话
   * @param payload Ping 数据载荷（必须包含 8 字节大端时间戳）
   */
  static void handlePing(const std::shared_ptr<network::TcpSession>& session, ByteBuffer& payload);
};

}  // namespace mcpe::protocol

#endif  // STATUSHANDLER_H