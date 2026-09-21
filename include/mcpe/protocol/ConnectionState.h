#ifndef CONNECTIONSTATE_H
#define CONNECTIONSTATE_H

#include <cstdint>
#include <string_view>

namespace mcpe::protocol {

/**
 * @brief Minecraft 协议连接状态机枚举
 *
 * 对应 Minecraft 官方网络协议规范中的 4 个核心状态阶段：
 * - HANDSHAKING (0): 初始握手阶段，客户端连入后发出的首个包必为 Handshake (0x00)
 * - STATUS (1): 状态查询与服务器列表探测（Server List Ping / MOTD / 延迟探测）
 * - LOGIN (2): 玩家登录认证阶段（进服身份核验、UUID 交换与加密协商）
 * - PLAY (3): 游戏内正常交互阶段（世界加载、玩家移动、聊天等）
 */
enum class ConnectionState : uint8_t {
  HANDSHAKING = 0,  ///< 初始握手阶段
  STATUS = 1,       ///< 状态探测与 Ping 阶段
  LOGIN = 2,        ///< 身份认证与登录阶段
  PLAY = 3          ///< 游戏内玩法交互阶段
};

/**
 * @brief 将连接状态枚举转换为人类可读的字符串
 * @param state 协议连接状态
 * @return 对应的状态文本名称（如 "STATUS"），若非法则返回 "UNKNOWN"
 */
constexpr std::string_view
toString(ConnectionState state) noexcept {
  switch (state) {
    case ConnectionState::HANDSHAKING:
      return "HANDSHAKING";
    case ConnectionState::STATUS:
      return "STATUS";
    case ConnectionState::LOGIN:
      return "LOGIN";
    case ConnectionState::PLAY:
      return "PLAY";
  }
  return "UNKNOWN";
}

}  // namespace mcpe::protocol

#endif  // CONNECTIONSTATE_H