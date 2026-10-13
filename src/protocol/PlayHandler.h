#ifndef PLAYHANDLER_H
#define PLAYHANDLER_H

#include <cstdint>
#include <memory>

#include "common/ByteBuffer.h"
#include "protocol/GameTypes.h"

namespace mcpe::world {
class World;
}

namespace mcpe::protocol {

class ClientSession;

class PlayHandler {
 public:
  /** @brief 绑定全局世界实例（大厅/跑酷） */
  static void setWorld(std::shared_ptr<world::World> world);
  static std::shared_ptr<world::World> getWorld();

  /** @brief 玩家鉴权通过后调用：下发 Join Game、Player Position And Look 并启动保活心跳 */
  static void onPlayerJoin(const std::shared_ptr<ClientSession>& session);

  /** @brief 下发 Join Game (0x23) 世界参数包（使用领域配置消除硬编码魔数） */
  static void sendJoinGame(
      const std::shared_ptr<ClientSession>& session,
      int32_t entity_id,
      const ServerConfig& config = ServerConfig::defaultInstance()
  );

  /** @brief 下发 Player Position And Look (0x2F) 玩家位置与视线包 */
  static void sendPlayerPositionAndLook(
      const std::shared_ptr<ClientSession>& session,
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
  handleTeleportConfirm(const std::shared_ptr<ClientSession>& session, ByteBuffer& payload);

  /** @brief 处理客户端应答的 Keep Alive (0x0B) */
  static void handleKeepAlive(const std::shared_ptr<ClientSession>& session, ByteBuffer& payload);

  /** @brief 处理客户端发来的移动包族 (0x0C, 0x0D, 0x0E, 0x0F) 并投递至 EventQueue */
  static void handlePlayerMovement(
      const std::shared_ptr<ClientSession>& session,
      int32_t packet_id,
      ByteBuffer& payload
  );

  /** @brief 处理客户端发来的聊天信息 (0x02) 并投递至 EventQueue */
  static void handleChatMessage(
      const std::shared_ptr<ClientSession>& session,
      ByteBuffer& payload
  );
};

}  // namespace mcpe::protocol

#endif  // PLAYHANDLER_H