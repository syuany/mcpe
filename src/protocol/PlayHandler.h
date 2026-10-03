#ifndef PLAYHANDLER_H
#define PLAYHANDLER_H

#include <cstdint>
#include <memory>

#include "common/ByteBuffer.h"

namespace mcpe::network {
class TcpSession;
}

namespace mcpe::protocol {

class PlayHandler {
 public:
  /** @brief 玩家鉴权通过后调用：下发 Join Game、出生点并启动保活心跳 */
  static void onPlayerJoin(const std::shared_ptr<network::TcpSession>& session);

  /** @brief 下发 Join Game (0x23) 世界参数包 */
  static void sendJoinGame(const std::shared_ptr<network::TcpSession>& session, int32_t entity_id);

  /** @brief 下发 Player Position And Look (0x2F) 出生点包 */
  static void sendPlayerPositionAndLook(
      const std::shared_ptr<network::TcpSession>& session,
      double x,
      double y,
      double z,
      float yaw,
      float pitch,
      uint8_t flags,
      int32_t teleport_id
  );

  /** @brief 处理客户端回传的 Teleport Confirm (0x00) */
  static void
  handleTeleportConfirm(const std::shared_ptr<network::TcpSession>& session, ByteBuffer& payload);

  /** @brief 处理客户端应答的 Keep Alive (0x0B) */
  static void
  handleKeepAlive(const std::shared_ptr<network::TcpSession>& session, ByteBuffer& payload);
};

}  // namespace mcpe::protocol

#endif  // PLAYHANDLER_H