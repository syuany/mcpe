/**
 * @file EntityManager.h
 * @brief 实体管理器与同屏可见性/差分位移同步（Domain 领域层）
 *
 * 核心架构定位：
 * 1. 负责管理世界中的玩家实体生命周期、坐标、朝向与广播脏标记；
 * 2. 严格对标 Cuberite 架构，与网络传输层（TcpSession / Socket）完全解耦；
 * 3. 通过 PacketSender 函数回调向网络层投递格式化封包；
 * 4. 实现 1/4096 方块短整型相对位移与 8.0 格临界溢出降级绝对瞬移算法。
 */

#ifndef MCPE_WORLD_ENTITY_MANAGER_H
#define MCPE_WORLD_ENTITY_MANAGER_H

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "common/ByteBuffer.h"
#include "common/SpatialTypes.h"
#include "protocol/PacketTypes.h"
#include "world/SpatialGrid.h"

namespace mcpe::world {

/**
 * @brief 玩家世界实体状态
 */
struct PlayerEntity {
  uint64_t session_id{0};
  int32_t entity_id{0};
  std::string username;
  std::array<uint8_t, 16> uuid{};

  Vector3d position{0.5, 4.0, 0.5};
  Rotation rotation{0.0f, 0.0f};
  bool on_ground{true};

  Vector3d last_synced_pos{0.5, 4.0, 0.5};
  Rotation last_synced_rot{0.0f, 0.0f};
  bool dirty{false};
  std::vector<protocol::PlayerProperty> properties;
};

class EntityManager {
 public:
  using PacketSender = std::function<void(uint64_t session_id, ByteBuffer packet)>;
  using BroadcastExceptSender =
      std::function<void(uint64_t except_session_id, ByteBuffer packet)>;
  using BroadcastSender = std::function<void(ByteBuffer packet)>;

  explicit EntityManager(
      PacketSender sender = nullptr,
      BroadcastExceptSender broadcast_except = nullptr,
      BroadcastSender broadcast = nullptr
  );

  void setSenders(
      PacketSender sender,
      BroadcastExceptSender broadcast_except,
      BroadcastSender broadcast
  );

  void setDefaultSpawn(const Vector3d& pos, const Rotation& rot = {0.0f, 0.0f}) noexcept {
    default_spawn_pos_ = pos;
    default_spawn_rot_ = rot;
  }

  using PropertyProvider =
      std::function<std::vector<protocol::PlayerProperty>(uint64_t session_id)>;

  void setPropertyProvider(PropertyProvider provider) {
    property_provider_ = std::move(provider);
  }

  /**
   * @brief 注册新玩家加入世界：触发双向同屏广播与 Player List 呈现
   */
  void addPlayer(
      uint64_t session_id,
      int32_t entity_id,
      std::string_view username,
      const std::array<uint8_t, 16>& uuid,
      std::vector<protocol::PlayerProperty> properties = {}
  );

  void addPlayer(
      uint64_t session_id,
      int32_t entity_id,
      std::string_view username,
      const std::array<uint8_t, 16>& uuid,
      const Vector3d& spawn_pos,
      const Rotation& spawn_rot = {0.0f, 0.0f},
      std::vector<protocol::PlayerProperty> properties = {}
  );

  /**
   * @brief 玩家退服：向其他在线玩家广播 Destroy Entities (0x32) 与 Tab 移除
   */
  void removePlayer(uint64_t session_id);

  /**
   * @brief 更新玩家坐标与朝向
   */
  void updatePlayerPosition(
      uint64_t session_id,
      const std::optional<Vector3d>& pos,
      const std::optional<Rotation>& rot,
      bool on_ground
  );

  /**
   * @brief 每 Tick 执行：全量扫描脏标记，计算相对位移，防溢出多播广播
   */
  void updateAndBroadcast();

  [[nodiscard]] std::string getPlayerName(uint64_t session_id) const;
  [[nodiscard]] bool hasPlayer(uint64_t session_id) const;
  [[nodiscard]] size_t playerCount() const noexcept { return players_.size(); }
  [[nodiscard]] const PlayerEntity* getPlayer(uint64_t session_id) const;
  [[nodiscard]] std::vector<uint64_t> getAllSessionIds() const;

  [[nodiscard]] const SpatialGrid& grid() const noexcept { return grid_; }
  [[nodiscard]] SpatialGrid& grid() noexcept { return grid_; }

  // --- 静态协议数据包构建器 (Minecraft 1.12.2 Protocol 340 物理二进制规范) ---
  static ByteBuffer buildPlayerListItemPacket(
      const PlayerEntity& player,
      protocol::PlayerListItemAction action
  );
  static ByteBuffer buildSpawnPlayerPacket(const PlayerEntity& player);
  static ByteBuffer buildEntityHeadLookPacket(int32_t entity_id, const Rotation& rot);
  static ByteBuffer buildRelMovePacket(int32_t entity_id, const Vector3d& delta, bool on_ground);
  static ByteBuffer buildLookAndRelMovePacket(
      int32_t entity_id,
      const Vector3d& delta,
      const Rotation& rot,
      bool on_ground
  );
  static ByteBuffer buildEntityLookPacket(int32_t entity_id, const Rotation& rot, bool on_ground);
  static ByteBuffer buildEntityTeleportPacket(const PlayerEntity& player);
  static ByteBuffer buildDestroyEntitiesPacket(int32_t entity_id);

 private:
  PacketSender sender_;
  BroadcastExceptSender broadcast_except_;
  BroadcastSender broadcast_;
  SpatialGrid grid_;
  std::unordered_map<uint64_t, PlayerEntity> players_;
  Vector3d default_spawn_pos_{0.5, 4.0, 0.5};
  Rotation default_spawn_rot_{0.0f, 0.0f};
  PropertyProvider property_provider_;
};

}  // namespace mcpe::world

#endif  // MCPE_WORLD_ENTITY_MANAGER_H
