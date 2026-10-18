/**
 * @file LobbyManager.h
 * @brief 大厅动态弹性分线并发调度中心（Lobby 微服务引擎调度层）
 *
 * 核心职责：
 * 1. 管理常驻与动态裂变的大厅分线实例（lobby-1, lobby-2, ...，共享只读 lobby_world_）；
 * 2. 提供线程安全的席位分配、满员自动裂变分线与跨分线迁移路由。
 */

#ifndef MCPE_LOBBY_LOBBY_MANAGER_H
#define MCPE_LOBBY_LOBBY_MANAGER_H

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "lobby/LobbyInstance.h"

namespace mcpe::lobby {

class LobbyManager {
 public:
  using PacketUnicastFn = LobbyInstance::PacketUnicastFn;
  using TeleportPlayerFn = LobbyInstance::TeleportPlayerFn;
  using ChatDispatchFn = LobbyInstance::ChatDispatchFn;
  using ServerConnectFn = LobbyInstance::ServerConnectFn;
  using PropertyProvider = world::EntityManager::PropertyProvider;
  using WorldSwitchFn =
      std::function<void(uint64_t session_id, const std::shared_ptr<LobbyInstance>& target_lobby)>;

  explicit LobbyManager(
      std::shared_ptr<world::World> lobby_world,
      size_t lobby_max_players = 50,
      size_t max_lobby_instances = 10,
      bool auto_start_threads = true
  );

  ~LobbyManager();

  LobbyManager(const LobbyManager&) = delete;
  LobbyManager& operator=(const LobbyManager&) = delete;

  void setUnicastCallback(PacketUnicastFn fn);
  void setTeleportCallback(TeleportPlayerFn fn);
  void setChatDispatchCallback(ChatDispatchFn fn);
  void setWorldSwitchCallback(WorldSwitchFn fn);
  void setServerConnectCallback(ServerConnectFn fn);

  bool loadPortals(const std::filesystem::path& ini_path);
  void addPortal(Portal portal);
  [[nodiscard]] std::vector<Portal> portals() const;

  /// 启动所有已创建分线的 20 TPS 物理线程
  void startAll();

  /// 停止所有分线的 20 TPS 物理线程
  void stopAll();

  /**
   * @brief 为指定会话分配并预留一个未满员的大厅分线实例；
   *        若所有现有分线均满员且未超 max_lobby_instances_，自动新建分线 lobby-(N+1)
   */
  [[nodiscard]] std::shared_ptr<LobbyInstance> acquireLobbyForPlayer(uint64_t session_id);

  /**
   * @brief 将玩家从当前大厅分线原子迁移至目标分线（自动投递旧分线 Leave、触发 0x35 切换回调并投递新分线 Join）
   */
  bool transferPlayer(
      uint64_t session_id,
      int32_t entity_id,
      std::string_view username,
      const std::array<uint8_t, 16>& uuid,
      const std::shared_ptr<LobbyInstance>& target_lobby,
      std::vector<protocol::PlayerProperty> properties = {}
  );

  /**
   * @brief 玩家断开连接时清理其在所属分线中的席位与实体
   */
  void onPlayerDisconnected(uint64_t session_id, int32_t entity_id);

  [[nodiscard]] std::shared_ptr<LobbyInstance> findLobby(std::string_view lobby_id) const;
  [[nodiscard]] std::shared_ptr<LobbyInstance> getPlayerLobby(uint64_t session_id) const;

  [[nodiscard]] std::vector<std::shared_ptr<LobbyInstance>> getLobbies() const;

  void setLobbyMaxPlayers(size_t max_p);
  [[nodiscard]] size_t lobbyMaxPlayers() const noexcept { return lobby_max_players_; }
  [[nodiscard]] size_t lobbyInstanceCount() const;

  void setPropertyProvider(PropertyProvider provider);
  [[nodiscard]] std::vector<protocol::PlayerProperty> getPlayerProperties(
      uint64_t session_id
  ) const;

 private:
  std::shared_ptr<LobbyInstance> createLobbyShardUnlocked();
  void bindLobbyCallbacksUnlocked(const std::shared_ptr<LobbyInstance>& lobby);

  mutable std::mutex mutex_;
  std::shared_ptr<world::World> lobby_world_;
  size_t lobby_max_players_{50};
  size_t max_lobby_instances_{10};
  bool auto_start_threads_{true};

  PacketUnicastFn unicast_fn_;
  TeleportPlayerFn teleport_fn_;
  ChatDispatchFn chat_dispatch_fn_;
  WorldSwitchFn world_switch_fn_;
  ServerConnectFn server_connect_fn_;
  PropertyProvider property_provider_;
  std::vector<Portal> configured_portals_;

  std::vector<std::shared_ptr<LobbyInstance>> lobbies_;
  std::unordered_map<std::string, std::shared_ptr<LobbyInstance>> lobbies_by_id_;
  std::unordered_map<uint64_t, std::string> player_lobby_map_;

  struct PlayerIdentity {
    int32_t entity_id{0};
    std::string username;
    std::array<uint8_t, 16> uuid{};
    std::vector<protocol::PlayerProperty> properties;
  };
  std::unordered_map<uint64_t, PlayerIdentity> player_identities_;
};

}  // namespace mcpe::lobby

#endif  // MCPE_LOBBY_LOBBY_MANAGER_H
