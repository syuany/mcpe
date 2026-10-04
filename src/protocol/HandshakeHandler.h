#ifndef HANDSHAKEHANDLER_H
#define HANDSHAKEHANDLER_H

#include <memory>

#include "common/ByteBuffer.h"

namespace mcpe::protocol {

class ClientSession;

/**
 * @brief 处理 HANDSHAKING 状态下数据包的独立处理器（彻底解除 StatusHandler 的越界侵入）
 *
 * 专门负责 Minecraft 协议生命周期最开端的握手流转：
 * - 解析客户端协议版本号、连接主机名、端口与目标意图 Next State
 * - 意图为 1 时流转至 ConnectionState::STATUS（状态查询）
 * - 意图为 2 时流转至 ConnectionState::LOGIN（玩家登录）
 */
class HandshakeHandler {
 public:
  static void handleHandshake(const std::shared_ptr<ClientSession>& session, ByteBuffer& payload);
};

}  // namespace mcpe::protocol

#endif  // HANDSHAKEHANDLER_H
