#ifndef SESSIONCALLBACKS_H
#define SESSIONCALLBACKS_H

#include <memory>

#include "common/ByteBuffer.h"

namespace mcpe::network {

class TcpSession;

/**
 * @brief TCP 会话抽象事件监听与回调接口（严格对齐 Cuberite cTCPLink::cCallbacks 架构设计）
 *
 * 核心设计优势：
 * 1. 采用经典面向对象纯虚函数接口代替 std::function 回调，消灭类型擦除开销与隐式小对象堆分配
 * 2. 彻底解除 main() / server.cc 对数据包中转的胶水代码，会话与协议层在运行时直接虚分发直达
 * 3. 严格遵循依赖倒置原则（DIP），网络层（mcpe_network）与协议层（mcpe_protocol）完全无静态循环依赖
 */
class SessionCallbacks {
 public:
  virtual ~SessionCallbacks() = default;

  /**
   * @brief 当底层网络拆包器（PacketFraming）成功切出一个完整数据包时触发
   * @param session 触发该事件的客户端 TCP 会话实例
   * @param packet 已切分完毕的独立数据包（格式：[Packet ID (VarInt)] + [Payload]）
   */
  virtual void onPacket(const std::shared_ptr<TcpSession>& session, ByteBuffer packet) = 0;

  /**
   * @brief 当客户端连接断开时触发
   * @param session 断开连接的客户端 TCP 会话实例
   */
  virtual void
  onDisconnected(const std::shared_ptr<TcpSession>& session) {
    (void)session;
  }
};

}  // namespace mcpe::network

#endif  // SESSIONCALLBACKS_H
