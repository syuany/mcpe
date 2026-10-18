#ifndef PROTOCOLHANDLER_H
#define PROTOCOLHANDLER_H

#include <cstdint>
#include <memory>

#include "common/ByteBuffer.h"
#include "protocol/ConnectionState.h"

namespace mcpe::protocol {

class ClientSession;

/**
 * @brief Minecraft 协议版本多态解析基类（严格对齐 Cuberite cProtocol 架构设计）
 *
 * 核心设计优势：
 * 1. 消除静态死代码 switch-case，为后续扩展支持其他版本（如 1.8.9、1.16.5
 * 或基岩版）提供插件式插拔接口
 * 2. ClientSession 仅持有抽象 ProtocolHandler 指针，运行时根据握手协议号动态多态解析
 */
class ProtocolHandler {
 public:
  virtual ~ProtocolHandler() = default;

  /// 该解析器对应的官方协议版本号（如 1.12.2 为 340）
  virtual int32_t protocolVersion() const noexcept = 0;

  /// 协议数据包分发主入口（从会话提取状态机）
  virtual void handlePacket(const std::shared_ptr<ClientSession>& session, ByteBuffer packet) = 0;

  /// 指定特定状态机的数据包分发入口
  virtual void handlePacket(
      const std::shared_ptr<ClientSession>& session,
      ConnectionState state,
      ByteBuffer packet
  ) = 0;
};

}  // namespace mcpe::protocol

#endif  // PROTOCOLHANDLER_H
