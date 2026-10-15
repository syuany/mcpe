/**
 * @file InstanceManager.cc
 * @brief 多实例并发 Actor 调度器与大厅动态弹性分线中心实现
 */

#include "game/InstanceManager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <format>
#include <utility>

namespace mcpe::game {

InstanceManager::InstanceManager(
    std::shared_ptr<world::World> lobby_world,
    size_t lobby_max_players,
    size_t max_lobby_instances,
    size_t parkour_max_players,
    bool auto_start_threads
)
    : lobby_world_(std::move(lobby_world)),
      lobby_max_players_(std::max<size_t>(1, lobby_max_players)),
      max_lobby_instances_(std::max<size_t>(1, max_lobby_instances)),
      parkour_max_players_(std::max<size_t>(1, parkour_max_players)),
      auto_start_threads_(auto_start_threads) {
  if (!lobby_world_) {
    lobby_world_ = std::make_shared<world::World>();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  (void)createLobbyShardUnlocked();  // 默认初始化常驻主分线 lobby-1
}

InstanceManager::~InstanceManager() {
  stopAll();
}

void
InstanceManager::setUnicastCallback(PacketUnicastFn fn) {
  std::lock_guard<std::mutex> lock(mutex_);
  unicast_fn_ = std::move(fn);
}

void
InstanceManager::setTeleportCallback(TeleportPlayerFn fn) {
  std::lock_guard<std::mutex> lock(mutex_);
  teleport_fn_ = std::move(fn);
  for (auto& [_, inst] : instances_by_id_) {
    inst->setTeleportCallback(teleport_fn_);
  }
}

void
InstanceManager::setChatDispatchCallback(ChatDispatchFn fn) {
  std::lock_guard<std::mutex> lock(mutex_);
  chat_dispatch_fn_ = std::move(fn);
  for (auto& [_, inst] : instances_by_id_) {
    inst->setChatDispatchCallback(chat_dispatch_fn_);
  }
}

void
InstanceManager::setWorldSwitchCallback(WorldSwitchFn fn) {
  std::lock_guard<std::mutex> lock(mutex_);
  world_switch_fn_ = std::move(fn);
}

void
InstanceManager::registerParkourMap(
    const parkour::ParkourMapConfig& cfg,
    std::shared_ptr<world::World> map_world
) {
  std::lock_guard<std::mutex> lock(mutex_);
  parkour_templates_[cfg.map_name] = ParkourTemplate{cfg, std::move(map_world)};
  if (parkour_shards_[cfg.map_name].empty()) {
    (void)createParkourShardUnlocked(cfg.map_name);
  }
}

void
InstanceManager::startAll() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& [_, inst] : instances_by_id_) {
    inst->start();
  }
}

void
InstanceManager::stopAll() {
  std::vector<std::shared_ptr<IGameInstance>> all;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    all.reserve(instances_by_id_.size());
    for (auto& [_, inst] : instances_by_id_) {
      all.push_back(inst);
    }
  }
  for (auto& inst : all) {
    inst->stop();
  }
}

std::shared_ptr<lobby::LobbyInstance>
InstanceManager::createLobbyShardUnlocked() {
  const size_t next_idx = lobbies_.size() + 1;
  const std::string id = std::format("lobby-{}", next_idx);

  auto inst = std::make_shared<lobby::LobbyInstance>(
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
  bindInstanceCallbacksUnlocked(inst);
  if (auto_start_threads_) {
    inst->start();
  }
  lobbies_.push_back(inst);
  instances_by_id_[id] = inst;

  spdlog::info(
      "[InstanceManager] Created lobby shard '{}' (max_players={})",
      id,
      lobby_max_players_
  );
  return inst;
}

std::shared_ptr<parkour::ParkourInstance>
InstanceManager::createParkourShardUnlocked(std::string_view map_name) {
  auto tpl_it = parkour_templates_.find(std::string(map_name));
  if (tpl_it == parkour_templates_.end()) {
    return nullptr;
  }

  auto& vec = parkour_shards_[std::string(map_name)];
  const size_t next_idx = vec.size() + 1;
  const std::string id = std::format("parkour-{}-{}", map_name, next_idx);

  auto inst = std::make_shared<parkour::ParkourInstance>(
      id,
      tpl_it->second.world,
      tpl_it->second.config,
      parkour_max_players_,
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
  bindInstanceCallbacksUnlocked(inst);
  if (auto_start_threads_) {
    inst->start();
  }
  vec.push_back(inst);
  instances_by_id_[id] = inst;

  spdlog::info(
      "[InstanceManager] Created parkour shard '{}' for map '{}' (max_players={})",
      id,
      map_name,
      parkour_max_players_
  );
  return inst;
}

void
InstanceManager::bindInstanceCallbacksUnlocked(const std::shared_ptr<IGameInstance>& inst) {
  if (teleport_fn_) {
    inst->setTeleportCallback(teleport_fn_);
  }
  if (chat_dispatch_fn_) {
    inst->setChatDispatchCallback(chat_dispatch_fn_);
  }
  inst->setReturnToLobbyCallback([this](uint64_t sid) {
    PlayerIdentity id_info;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = player_identities_.find(sid);
      if (it == player_identities_.end()) {
        return;
      }
      id_info = it->second;
    }
    auto lobby = acquireLobbyForPlayer(sid);
    if (lobby) {
      (void)transferPlayer(sid, id_info.entity_id, id_info.username, id_info.uuid, lobby);
    }
  });
}

std::shared_ptr<lobby::LobbyInstance>
InstanceManager::acquireLobbyForPlayer(uint64_t session_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  // 1. 按序号扫描现有大厅分线，寻找首个能成功预留席位的分线
  for (const auto& lobby : lobbies_) {
    if (lobby->tryReserveSlot(session_id)) {
      if (!player_instance_map_.contains(session_id)) {
        player_instance_map_[session_id] = lobby->instanceId();
      }
      return lobby;
    }
  }

  // 2. 所有现有大厅分线已满：若未达最大分线数上限，自动新建大厅分线
  if (lobbies_.size() < max_lobby_instances_) {
    auto new_lobby = createLobbyShardUnlocked();
    (void)new_lobby->tryReserveSlot(session_id);
    if (!player_instance_map_.contains(session_id)) {
      player_instance_map_[session_id] = new_lobby->instanceId();
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

std::shared_ptr<parkour::ParkourInstance>
InstanceManager::acquireParkourForPlayer(uint64_t session_id, std::string_view map_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = parkour_shards_.find(std::string(map_name));
  if (it == parkour_shards_.end()) {
    return nullptr;
  }

  for (const auto& inst : it->second) {
    if (inst->tryReserveSlot(session_id)) {
      if (!player_instance_map_.contains(session_id)) {
        player_instance_map_[session_id] = inst->instanceId();
      }
      return inst;
    }
  }

  auto new_inst = createParkourShardUnlocked(map_name);
  if (new_inst) {
    (void)new_inst->tryReserveSlot(session_id);
    if (!player_instance_map_.contains(session_id)) {
      player_instance_map_[session_id] = new_inst->instanceId();
    }
  }
  return new_inst;
}

bool
InstanceManager::transferPlayer(
    uint64_t session_id,
    int32_t entity_id,
    std::string_view username,
    const std::array<uint8_t, 16>& uuid,
    const std::shared_ptr<IGameInstance>& target_inst
) {
  if (!target_inst) {
    return false;
  }

  std::shared_ptr<IGameInstance> old_inst;
  WorldSwitchFn switch_cb;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    player_identities_[session_id] = PlayerIdentity{entity_id, std::string(username), uuid};

    auto old_it = player_instance_map_.find(session_id);
    if (old_it != player_instance_map_.end()) {
      auto inst_it = instances_by_id_.find(old_it->second);
      if (inst_it != instances_by_id_.end() && inst_it->second != target_inst) {
        old_inst = inst_it->second;
      } else if (inst_it != instances_by_id_.end() && inst_it->second == target_inst &&
                 target_inst->entityManager().hasPlayer(session_id)) {
        return false;
      }
    }

    if (!target_inst->hasReservedSlot(session_id)) {
      if (!target_inst->tryReserveSlot(session_id)) {
        return false;
      }
    }

    player_instance_map_[session_id] = target_inst->instanceId();
    switch_cb = world_switch_fn_;
  }

  // 1. 从旧实例释放席位并投递 PlayerLeaveEvent（由旧实例线程广播 0x32 DestroyEntities）
  if (old_inst) {
    old_inst->releaseSlot(session_id);
    world::WorldEvent leave_ev;
    leave_ev.session_id = session_id;
    leave_ev.entity_id = entity_id;
    leave_ev.payload = world::PlayerLeaveEvent{};
    old_inst->pushEvent(std::move(leave_ev));
  }

  // 2. 触发协议层 0x35 Respawn 跨世界切换与新区块流下发
  if (switch_cb) {
    switch_cb(session_id, target_inst);
  }

  // 3. 向目标实例投递 PlayerJoinEvent
  world::WorldEvent join_ev;
  join_ev.session_id = session_id;
  join_ev.entity_id = entity_id;
  world::PlayerJoinEvent join_payload;
  join_payload.setName(username);
  join_payload.uuid = uuid;
  join_ev.payload = std::move(join_payload);
  target_inst->pushEvent(std::move(join_ev));

  spdlog::info(
      "[InstanceManager] Transferred player '{}' (SID {}) from '{}' -> '{}'",
      username,
      session_id,
      old_inst ? old_inst->instanceId() : "login",
      target_inst->instanceId()
  );
  return true;
}

void
InstanceManager::onPlayerDisconnected(uint64_t session_id, int32_t entity_id) {
  std::vector<std::shared_ptr<IGameInstance>> affected;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    player_identities_.erase(session_id);
    auto it = player_instance_map_.find(session_id);
    if (it != player_instance_map_.end()) {
      auto inst_it = instances_by_id_.find(it->second);
      if (inst_it != instances_by_id_.end()) {
        affected.push_back(inst_it->second);
      }
      player_instance_map_.erase(it);
    }
    for (auto& [_, inst] : instances_by_id_) {
      if (inst->hasReservedSlot(session_id) &&
          std::find(affected.begin(), affected.end(), inst) == affected.end()) {
        affected.push_back(inst);
      }
    }
  }

  for (const auto& inst : affected) {
    inst->releaseSlot(session_id);
    world::WorldEvent leave_ev;
    leave_ev.session_id = session_id;
    leave_ev.entity_id = entity_id;
    leave_ev.payload = world::PlayerLeaveEvent{};
    inst->pushEvent(std::move(leave_ev));
  }
}

std::shared_ptr<IGameInstance>
InstanceManager::findInstance(std::string_view instance_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = instances_by_id_.find(std::string(instance_id));
  if (it != instances_by_id_.end()) {
    return it->second;
  }
  return nullptr;
}

std::shared_ptr<IGameInstance>
InstanceManager::getPlayerInstance(uint64_t session_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = player_instance_map_.find(session_id);
  if (it == player_instance_map_.end()) {
    return nullptr;
  }
  auto inst_it = instances_by_id_.find(it->second);
  if (inst_it != instances_by_id_.end()) {
    return inst_it->second;
  }
  return nullptr;
}

std::vector<std::shared_ptr<lobby::LobbyInstance>>
InstanceManager::getLobbies() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lobbies_;
}

std::vector<std::string>
InstanceManager::getRegisteredParkourMaps() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::string> names;
  names.reserve(parkour_templates_.size());
  for (const auto& [name, _] : parkour_templates_) {
    names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  return names;
}

void
InstanceManager::setLobbyMaxPlayers(size_t max_p) {
  std::lock_guard<std::mutex> lock(mutex_);
  lobby_max_players_ = std::max<size_t>(1, max_p);
  for (auto& lobby : lobbies_) {
    lobby->setMaxPlayers(lobby_max_players_);
  }
}

size_t
InstanceManager::lobbyInstanceCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return lobbies_.size();
}

}  // namespace mcpe::game
