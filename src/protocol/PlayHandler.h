#ifndef PLAYHANDLER_H
#define PLAYHANDLER_H

#include <cstdint>
#include <memory>
#include <string_view>

#include "common/ByteBuffer.h"
#include "protocol/GameTypes.h"

namespace mcpe::world {
class World;
}

namespace mcpe::game {
class InstanceManager;
}

namespace mcpe::protocol {

class ClientSession;

class PlayHandler {
 public:
  /** @brief 绑定全局默认世界实例（兼容单实例模式） */
  static void setWorld(std::shared_ptr<world::World> world);
  static std::shared_ptr<world::World> getWorld();

  /** @brief 绑定多实例并发调度中心（Sprint 4 多大厅分线与小游戏引擎） */
  static void setInstanceManager(std::shared_ptr<game::InstanceManager> mgr);
  static std::shared_ptr<game::InstanceManager> getInstanceManager();

  /** @brief 玩家鉴权通过后调用：分配大厅分线、下发 Join Game、Chunk 流与出生点并启动心跳 */
  static void onPlayerJoin(const std::shared_ptr<ClientSession>& session);

  /** @brief 下发 Join Game (0x23) 世界参数包 */
  static void sendJoinGame(
      const std::shared_ptr<ClientSession>& session,
      int32_t entity_id,
      const ServerConfig& config = ServerConfig::defaultInstance()
  );

  /** @brief 下发 Respawn (0x35) 重生/切世界包（Dimension 严格为 4 字节大端 int32_t） */
  static void sendRespawn(
      const std::shared_ptr<ClientSession>& session,
      Dimension dimension,
      Difficulty difficulty = Difficulty::Peaceful,
      GameMode gamemode = GameMode::Adventure,
      std::string_view level_type = "flat"
  );

  /** @brief 单连接无缝切换玩家所处世界（双发 0x35 清空客户端区块缓存 -> 11x11 区块流 -> 0x2F 起点定位） */
  static void switchPlayerWorld(
      const std::shared_ptr<ClientSession>& session,
      const world::World& target_world,
      GameMode target_gamemode = GameMode::Adventure
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

  /** @brief 下发 Player Abilities (0x2C) 玩家能力/飞行状态包 */
  static void sendPlayerAbilities(
      const std::shared_ptr<ClientSession>& session,
      uint8_t flags,
      float flying_speed = 0.05f,
      float fov_modifier = 0.1f
  );

  /** @brief 下发 Time Update (0x47) 世界时间包（time_of_day 为负数则冻结昼夜，保持正午永昼） */
  static void sendTimeUpdate(
      const std::shared_ptr<ClientSession>& session,
      int64_t world_age = 6000LL,
      int64_t time_of_day = -6000LL
  );

  /** @brief 构建 Entity Effect (0x4E) 实体药水状态包 */
  static ByteBuffer buildEntityEffect(
      int32_t entity_id,
      uint8_t effect_id,
      uint8_t amplifier,
      int32_t duration_ticks,
      uint8_t flags = 0x00
  );

  /** @brief 构建 Remove Entity Effect (0x4F) 移除实体药水状态包 */
  static ByteBuffer buildRemoveEntityEffect(
      int32_t entity_id,
      uint8_t effect_id
  );

  /** @brief 构建 Entity Velocity (0x12) 实体运动冲量/弹跳包 */
  static ByteBuffer buildEntityVelocity(
      int32_t entity_id,
      int16_t velocity_x,
      int16_t velocity_y,
      int16_t velocity_z
  );

  /** @brief 下发 Entity Effect (0x4E) 实体药水状态包 */
  static void sendEntityEffect(
      const std::shared_ptr<ClientSession>& session,
      int32_t entity_id,
      uint8_t effect_id,
      uint8_t amplifier,
      int32_t duration_ticks,
      uint8_t flags = 0x00
  );

  /** @brief 下发 Remove Entity Effect (0x4F) 移除实体药水状态包 */
  static void sendRemoveEntityEffect(
      const std::shared_ptr<ClientSession>& session,
      int32_t entity_id,
      uint8_t effect_id
  );

  /** @brief 下发 Entity Velocity (0x12) 实体运动冲量/弹跳包 */
  static void sendEntityVelocity(
      const std::shared_ptr<ClientSession>& session,
      int32_t entity_id,
      int16_t velocity_x,
      int16_t velocity_y,
      int16_t velocity_z
  );

  /** @brief 处理客户端发来的 Player Abilities (0x13) 飞行状态同步包 */
  static void handlePlayerAbilities(
      const std::shared_ptr<ClientSession>& session,
      ByteBuffer& payload
  );

  /** @brief 处理客户端回传的 Teleport Confirm (0x00) */
  static void
  handleTeleportConfirm(const std::shared_ptr<ClientSession>& session, ByteBuffer& payload);

  /** @brief 处理客户端应答的 Keep Alive (0x0B) */
  static void handleKeepAlive(const std::shared_ptr<ClientSession>& session, ByteBuffer& payload);

  /** @brief 处理客户端发来的移动包族 (0x0C, 0x0D, 0x0E, 0x0F) 并投递至当前实例 EventQueue */
  static void handlePlayerMovement(
      const std::shared_ptr<ClientSession>& session,
      int32_t packet_id,
      ByteBuffer& payload
  );

  /** @brief 处理客户端发来的聊天信息 (0x02) 并投递至当前实例 EventQueue */
  static void handleChatMessage(
      const std::shared_ptr<ClientSession>& session,
      ByteBuffer& payload
  );

  /** @brief 处理客户端发来的方块挖掘包 (0x14)：大厅保护环境下拒绝并静默消费 */
  static void handlePlayerDigging(
      const std::shared_ptr<ClientSession>& session,
      ByteBuffer& payload
  );

  /** @brief 处理客户端发来的方块放置包 (0x1F)：大厅保护环境下拒绝并静默消费 */
  static void handleBlockPlacement(
      const std::shared_ptr<ClientSession>& session,
      ByteBuffer& payload
  );
};

}  // namespace mcpe::protocol

#endif  // PLAYHANDLER_H