/**
 * @file LobbyManager.cc
 * @brief 大厅动态弹性分线并发调度中心实现
 */

#include "lobby/LobbyManager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <format>
#include <utility>

namespace mcpe::lobby {

LobbyManager::LobbyManager(
    std::shared_ptr<world::World> lobby_world,
    size_t lobby_max_players,
    size_t max_lobby_instances,
    bool auto_start_threads
)
    : lobby_world_(std::move(lobby_world)),
      lobby_max_players_(std::max<size_t>(1, lobby_max_players)),
      max_lobby_instances_(std::max<size_t>(1, max_lobby_instances)),
      auto_start_threads_(auto_start_threads) {
  if (!lobby_world_) {
    lobby_world_ = std::make_shared<world::World>();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  (void)createLobbyShardUnlocked();  // 默认初始化常驻主分线 lobby-1
}

LobbyManager::~LobbyManager() {
  stopAll();
}

void
LobbyManager::setUnicastCallback(PacketUnicastFn fn) {
  std::lock_guard<std::mutex> lock(mutex_);
  unicast_fn_ = std::move(fn);
}

void
LobbyManager::setTeleportCallback(TeleportPlayerFn fn) {
  std::lock_guard<std::mutex> lock(mutex_);
  teleport_fn_ = std::move(fn);
  for (auto& [_, lobby] : lobbies_by_id_) {
    lobby->setTeleportCallback(teleport_fn_);
  }
}

void
LobbyManager::setChatDispatchCallback(ChatDispatchFn fn) {
  std::lock_guard<std::mutex> lock(mutex_);
  chat_dispatch_fn_ = std::move(fn);
  for (auto& [_, lobby] : lobbies_by_id_) {
    lobby->setChatDispatchCallback(chat_dispatch_fn_);
  }
}

void
LobbyManager::setWorldSwitchCallback(WorldSwitchFn fn) {
  std::lock_guard<std::mutex> lock(mutex_);
  world_switch_fn_ = std::move(fn);
}

void
LobbyManager::setServerConnectCallback(ServerConnectFn fn) {
  std::lock_guard<std::mutex> lock(mutex_);
  server_connect_fn_ = std::move(fn);
  for (auto& [_, lobby] : lobbies_by_id_) {
    lobby->setServerConnectCallback(server_connect_fn_);
  }
}

void
LobbyManager::setPropertyProvider(PropertyProvider provider) {
  std::lock_guard<std::mutex> lock(mutex_);
  property_provider_ = std::move(provider);
}

std::vector<protocol::PlayerProperty>
LobbyManager::getPlayerProperties(uint64_t session_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = player_identities_.find(session_id);
  if (it != player_identities_.end() && !it->second.properties.empty()) {
    return it->second.properties;
  }
  if (property_provider_) {
    return property_provider_(session_id);
  }
  return {};
}

bool
LobbyManager::loadPortals(const std::filesystem::path& ini_path) {
  PortalManager pm;
  if (!pm.loadFromIni(ini_path)) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  configured_portals_ = pm.portals();
  for (auto& [_, lobby] : lobbies_by_id_) {
    for (const auto& p : configured_portals_) {
      lobby->portalManager().addPortal(p);
    }
  }
  return true;
}

void
LobbyManager::addPortal(Portal portal) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& [_, lobby] : lobbies_by_id_) {
    lobby->portalManager().addPortal(portal);
  }
  configured_portals_.push_back(std::move(portal));
}

std::vector<Portal>
LobbyManager::portals() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return configured_portals_;
}

void
LobbyManager::startAll() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& [_, lobby] : lobbies_by_id_) {
    lobby->start();
  }
}

void
LobbyManager::stopAll() {
  std::vector<std::shared_ptr<LobbyInstance>> all;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    all.reserve(lobbies_by_id_.size());
    for (auto& [_, lobby] : lobbies_by_id_) {
      all.push_back(lobby);
    }
  }
  for (auto& lobby : all) {
    lobby->stop();
  }
}

std::shared_ptr<LobbyInstance>
LobbyManager::createLobbyShardUnlocked() {
  const size_t next_idx = lobbies_.size() + 1;
  const std::string id = std::format("lobby-{}", next_idx);

  auto lobby = std::make_shared<LobbyInstance>(
      id,
      lobby_world_,
      lobby_max_players_,
      lobby_world_->fallResetY(),
      [this](uint64_t sid, ByteBuffer pkt) {
        PacketUnicastFn fn;
        {
          std::lock_guard<std::mutex> lk(mutex_);
          fn = unicast_fn_;
        }
        if (fn) {
          fn(sid, std::move(pkt));
        }
      }
  );
  bindLobbyCallbacksUnlocked(lobby);
  if (auto_start_threads_) {
    lobby->start();
  }
  lobbies_.push_back(lobby);
  lobbies_by_id_[id] = lobby;

  spdlog::info(
      "[LobbyManager] Created lobby shard '{}' (max_players={})",
      id,
      lobby_max_players_
  );
  return lobby;
}

void
LobbyManager::bindLobbyCallbacksUnlocked(const std::shared_ptr<LobbyInstance>& lobby) {
  if (teleport_fn_) {
    lobby->setTeleportCallback(teleport_fn_);
  }
  if (chat_dispatch_fn_) {
    lobby->setChatDispatchCallback(chat_dispatch_fn_);
  }
  if (server_connect_fn_) {
    lobby->setServerConnectCallback(server_connect_fn_);
  }
  lobby->setPropertyProvider([this](uint64_t sid) {
    return getPlayerProperties(sid);
  });
  for (const auto& p : configured_portals_) {
    lobby->portalManager().addPortal(p);
  }
}

std::shared_ptr<LobbyInstance>
LobbyManager::acquireLobbyForPlayer(uint64_t session_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  // 1. 按序号扫描现有大厅分线，寻找首个能成功预留席位的分线
  for (const auto& lobby : lobbies_) {
    if (lobby->tryReserveSlot(session_id)) {
      if (!player_lobby_map_.contains(session_id)) {
        player_lobby_map_[session_id] = lobby->instanceId();
      }
      return lobby;
    }
  }

  // 2. 所有现有大厅分线已满：若未达最大分线数上限，自动新建大厅分线
  if (lobbies_.size() < max_lobby_instances_) {
    auto new_lobby = createLobbyShardUnlocked();
    (void)new_lobby->tryReserveSlot(session_id);
    if (!player_lobby_map_.contains(session_id)) {
      player_lobby_map_[session_id] = new_lobby->instanceId();
    }
    return new_lobby;
  }

  // 3. 若已达最大分线数上限，选择当前人数最少的大厅分线
  auto min_it = std::min_element(
      lobbies_.begin(),
      lobbies_.end(),
      [](const auto& a, const auto& b) { return a->playerCount() < b->playerCount(); }
  );
  return (min_it != lobbies_.end()) ? *min_it : nullptr;
}

bool
LobbyManager::transferPlayer(
    uint64_t session_id,
    int32_t entity_id,
    std::string_view username,
    const std::array<uint8_t, 16>& uuid,
    const std::shared_ptr<LobbyInstance>& target_lobby,
    std::vector<protocol::PlayerProperty> properties
) {
  if (!target_lobby) {
    return false;
  }

  std::shared_ptr<LobbyInstance> old_lobby;
  WorldSwitchFn switch_cb;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& identity = player_identities_[session_id];
    identity.entity_id = entity_id;
    identity.username = std::string(username);
    identity.uuid = uuid;
    if (!properties.empty()) {
      identity.properties = std::move(properties);
    }

    auto old_it = player_lobby_map_.find(session_id);
    if (old_it != player_lobby_map_.end()) {
      auto lobby_it = lobbies_by_id_.find(old_it->second);
      if (lobby_it != lobbies_by_id_.end() && lobby_it->second != target_lobby) {
        old_lobby = lobby_it->second;
      } else if (lobby_it != lobbies_by_id_.end() && lobby_it->second == target_lobby &&
                 target_lobby->entityManager().hasPlayer(session_id)) {
        return false;
      }
    }

    if (!target_lobby->hasReservedSlot(session_id)) {
      if (!target_lobby->tryReserveSlot(session_id)) {
        return false;
      }
    }

    player_lobby_map_[session_id] = target_lobby->instanceId();
    switch_cb = world_switch_fn_;
  }

  // 1. 从旧分线释放席位并投递 PlayerLeaveEvent（由旧分线线程广播 0x32 DestroyEntities）
  if (old_lobby) {
    old_lobby->releaseSlot(session_id);
    world::WorldEvent leave_ev;
    leave_ev.session_id = session_id;
    leave_ev.entity_id = entity_id;
    leave_ev.payload = world::PlayerLeaveEvent{};
    old_lobby->pushEvent(std::move(leave_ev));
  }

  // 2. 触发协议层 0x35 Respawn 跨分线切换与新区块流下发
  if (switch_cb) {
    switch_cb(session_id, target_lobby);
  }

  // 3. 向目标分线投递 PlayerJoinEvent
  world::WorldEvent join_ev;
  join_ev.session_id = session_id;
  join_ev.entity_id = entity_id;
  world::PlayerJoinEvent join_payload;
  join_payload.setName(username);
  join_payload.uuid = uuid;
  join_ev.payload = std::move(join_payload);
  target_lobby->pushEvent(std::move(join_ev));

  spdlog::info(
      "[LobbyManager] Transferred player '{}' (SID {}) from '{}' -> '{}'",
      username,
      session_id,
      old_lobby ? old_lobby->instanceId() : "login",
      target_lobby->instanceId()
  );
  return true;
}

void
LobbyManager::onPlayerDisconnected(uint64_t session_id, int32_t entity_id) {
  std::vector<std::shared_ptr<LobbyInstance>> affected;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    player_identities_.erase(session_id);
    auto it = player_lobby_map_.find(session_id);
    if (it != player_lobby_map_.end()) {
      auto lobby_it = lobbies_by_id_.find(it->second);
      if (lobby_it != lobbies_by_id_.end()) {
        affected.push_back(lobby_it->second);
      }
      player_lobby_map_.erase(it);
    }
    for (auto& [_, lobby] : lobbies_by_id_) {
      if (lobby->hasReservedSlot(session_id) &&
          std::find(affected.begin(), affected.end(), lobby) == affected.end()) {
        affected.push_back(lobby);
      }
    }
  }

  for (const auto& lobby : affected) {
    lobby->releaseSlot(session_id);
    world::WorldEvent leave_ev;
    leave_ev.session_id = session_id;
    leave_ev.entity_id = entity_id;
    leave_ev.payload = world::PlayerLeaveEvent{};
    lobby->pushEvent(std::move(leave_ev));
  }
}

std::shared_ptr<LobbyInstance>
LobbyManager::findLobby(std::string_view lobby_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = lobbies_by_id_.find(std::string(lobby_id));
  if (it != lobbies_by_id_.end()) {
    return it->second;
  }
  return nullptr;
}

std::shared_ptr<LobbyInstance>
LobbyManager::getPlayerLobby(uint64_t session_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = player_lobby_map_.find(session_id);
  if (it == player_lobby_map_.end()) {
    return nullptr;
  }
  auto lobby_it = lobbies_by_id_.find(it->second);
  if (lobby_it != lobbies_by_id_.end()) {
    return lobby_it->second;
  }
  return nullptr;
}

std::vector<std::shared_ptr<LobbyInstance>>
LobbyManager::getLobbies() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lobbies_;
}

void
LobbyManager::setLobbyMaxPlayers(size_t max_p) {
  std::lock_guard<std::mutex> lock(mutex_);
  lobby_max_players_ = std::max<size_t>(1, max_p);
  for (auto& lobby : lobbies_) {
    lobby->setMaxPlayers(lobby_max_players_);
  }
}

size_t
LobbyManager::lobbyInstanceCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lobbies_.size();
}

}  // namespace mcpe::lobby
