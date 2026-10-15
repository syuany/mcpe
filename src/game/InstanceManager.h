/**
 * @file InstanceManager.h
 * @brief 多实例并发 Actor 调度器与大厅动态弹性分线中心（Game 玩法与实例调度层）
 *
 * 核心职责：
 * 1. 管理常驻与动态裂变的大厅分线实例（lobby-1, lobby-2, ...，共享只读 lobby_world_）；
 * 2. 管理多地图跑酷房间实例（parkour-spiral-1, ...，共享只读跑酷地图 World）；
 * 3. 提供线程安全的席位分配、满员自动裂变分线与跨实例迁移路由。
 */

#ifndef MCPE_GAME_INSTANCE_MANAGER_H
#define MCPE_GAME_INSTANCE_MANAGER_H

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "game/IGameInstance.h"
#include "game/lobby/LobbyInstance.h"
#include "game/parkour/ParkourInstance.h"
#include "game/parkour/ParkourMapConfig.h"

namespace mcpe::game {

class InstanceManager {
 public:
  using PacketUnicastFn = IGameInstance::PacketUnicastFn;
  using TeleportPlayerFn = IGameInstance::TeleportPlayerFn;
  using ChatDispatchFn = IGameInstance::ChatDispatchFn;
  using WorldSwitchFn =
      std::function<void(uint64_t session_id, const std::shared_ptr<IGameInstance>& target_inst)>;

  InstanceManager(
      std::shared_ptr<world::World> lobby_world,
      size_t lobby_max_players = 50,
      size_t max_lobby_instances = 10,
      size_t parkour_max_players = 25,
      bool auto_start_threads = true
  );

  ~InstanceManager();

  InstanceManager(const InstanceManager&) = delete;
  InstanceManager& operator=(const InstanceManager&) = delete;

  void setUnicastCallback(PacketUnicastFn fn);
  void setTeleportCallback(TeleportPlayerFn fn);
  void setChatDispatchCallback(ChatDispatchFn fn);
  void setWorldSwitchCallback(WorldSwitchFn fn);

  /// 注册一张可玩的跑酷地图模板（供按需创建与满员裂变跑酷实例）
  void registerParkourMap(
      const parkour::ParkourMapConfig& cfg,
      std::shared_ptr<world::World> map_world
  );

  /// 启动所有已创建实例的 20 TPS 物理线程
  void startAll();

  /// 停止所有实例的 20 TPS 物理线程
  void stopAll();

  /**
   * @brief 为指定会话分配并预留一个未满员的大厅分线实例；
   *        若所有现有分线均满员且未超 max_lobby_instances_，自动新建分线 lobby-(N+1)
   */
  [[nodiscard]] std::shared_ptr<lobby::LobbyInstance> acquireLobbyForPlayer(uint64_t session_id);

  /**
   * @brief 为指定会话分配并预留指定地图的跑酷实例；
   *        若现有跑酷实例满员，自动新建分线 parkour-<map>-(N+1)
   */
  [[nodiscard]] std::shared_ptr<parkour::ParkourInstance> acquireParkourForPlayer(
      uint64_t session_id,
      std::string_view map_name
  );

  /**
   * @brief 将玩家从当前实例原子迁移至目标实例（自动投递旧实例 Leave、触发 0x35 切换回调并投递新实例 Join）
   */
  bool transferPlayer(
      uint64_t session_id,
      int32_t entity_id,
      std::string_view username,
      const std::array<uint8_t, 16>& uuid,
      const std::shared_ptr<IGameInstance>& target_inst
  );

  /**
   * @brief 玩家断开连接时清理其在所属实例中的席位与实体
   */
  void onPlayerDisconnected(uint64_t session_id, int32_t entity_id);

  [[nodiscard]] std::shared_ptr<IGameInstance> findInstance(std::string_view instance_id) const;
  [[nodiscard]] std::shared_ptr<IGameInstance> getPlayerInstance(uint64_t session_id) const;

  [[nodiscard]] std::vector<std::shared_ptr<lobby::LobbyInstance>> getLobbies() const;
  [[nodiscard]] std::vector<std::string> getRegisteredParkourMaps() const;

  void setLobbyMaxPlayers(size_t max_p);
  [[nodiscard]] size_t lobbyMaxPlayers() const noexcept { return lobby_max_players_; }
  [[nodiscard]] size_t lobbyInstanceCount() const;

 private:
  std::shared_ptr<lobby::LobbyInstance> createLobbyShardUnlocked();
  std::shared_ptr<parkour::ParkourInstance> createParkourShardUnlocked(std::string_view map_name);
  void bindInstanceCallbacksUnlocked(const std::shared_ptr<IGameInstance>& inst);

  struct ParkourTemplate {
    parkour::ParkourMapConfig config;
    std::shared_ptr<world::World> world;
  };

  mutable std::mutex mutex_;
  std::shared_ptr<world::World> lobby_world_;
  size_t lobby_max_players_{50};
  size_t max_lobby_instances_{10};
  size_t parkour_max_players_{25};
  bool auto_start_threads_{true};

  PacketUnicastFn unicast_fn_;
  TeleportPlayerFn teleport_fn_;
  ChatDispatchFn chat_dispatch_fn_;
  WorldSwitchFn world_switch_fn_;

  std::vector<std::shared_ptr<lobby::LobbyInstance>> lobbies_;
  std::unordered_map<std::string, ParkourTemplate> parkour_templates_;
  std::unordered_map<std::string, std::vector<std::shared_ptr<parkour::ParkourInstance>>>
      parkour_shards_;
  std::unordered_map<std::string, std::shared_ptr<IGameInstance>> instances_by_id_;
  std::unordered_map<uint64_t, std::string> player_instance_map_;

  struct PlayerIdentity {
    int32_t entity_id{0};
    std::string username;
    std::array<uint8_t, 16> uuid{};
  };
  std::unordered_map<uint64_t, PlayerIdentity> player_identities_;
};

}  // namespace mcpe::game

#endif  // MCPE_GAME_INSTANCE_MANAGER_H
