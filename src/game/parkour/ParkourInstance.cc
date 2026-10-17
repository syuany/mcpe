/**
 * @file ParkourInstance.cc
 * @brief 多人同赛道随进随跑跑酷实例实现
 */

#include "game/parkour/ParkourInstance.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <format>
#include <sstream>
#include <utility>

#include "network/PacketWriter.h"

namespace mcpe::game::parkour {

ParkourInstance::ParkourInstance(
    std::string instance_id,
    std::shared_ptr<world::World> world,
    ParkourMapConfig map_cfg,
    size_t max_players,
    PacketUnicastFn unicast_fn
)
    : IGameInstance(
          std::move(instance_id),
          InstanceType::Parkour,
          std::move(world),
          max_players,
          std::move(unicast_fn)
      ),
      cfg_(std::move(map_cfg)) {
  world_->setSpawnPoint(cfg_.spawn_point);
  world_->setSpawnRotation(cfg_.spawn_rotation);
  world_->setFallResetY(cfg_.fall_reset_y);
  entity_manager_->setDefaultSpawn(cfg_.spawn_point, cfg_.spawn_rotation);
}

const PlayerParkourState*
ParkourInstance::getRunnerState(uint64_t session_id) const noexcept {
  auto it = runners_.find(session_id);
  if (it != runners_.end()) {
    return &it->second;
  }
  return nullptr;
}

void
ParkourInstance::onPlayerJoined(uint64_t session_id, std::string_view username) {
  PlayerParkourState st;
  st.session_id = session_id;
  st.username = std::string(username);
  st.respawn_pos = cfg_.spawn_point;
  st.respawn_rot = cfg_.spawn_rotation;
  st.current_checkpoint_idx = -1;
  runners_[session_id] = std::move(st);

  sendSystemMessage(
      session_id,
      std::format(
          "§a[跑酷] 欢迎进入赛道 §e{} §7({})§a！踏上黄金起点块开始计时，输入 §b/lobby §a随时返回大厅。",
          cfg_.map_name,
          instanceId()
      )
  );
  spdlog::info(
      "[ParkourInstance:{}] Player '{}' (SID {}) joined parkour course",
      instanceId(),
      username,
      session_id
  );
}

void
ParkourInstance::onPlayerLeft(uint64_t session_id, std::string_view username) {
  runners_.erase(session_id);
  spdlog::info(
      "[ParkourInstance:{}] Player '{}' (SID {}) left parkour course",
      instanceId(),
      username,
      session_id
  );
}

void
ParkourInstance::onPlayerMoved(uint64_t session_id, world::PlayerMoveEvent& move_event) {
  if (!move_event.has_position) {
    return;
  }

  auto it = runners_.find(session_id);
  if (it == runners_.end()) {
    return;
  }
  auto& runner = it->second;
  if (runner.dev_mode) {
    return;
  }
  const Vector3d& pos = move_event.position;

  // 1. 掉落熔断保护 (Fall Reset)：当 Y < fall_reset_y 时立即拉回最近激活的检查点或起点（计时不中断！）
  if (pos.y < cfg_.fall_reset_y) {
    ++runner.fall_count;
    if (teleport_fn_) {
      teleport_fn_(session_id, runner.respawn_pos, runner.respawn_rot);
    }
    move_event.position = runner.respawn_pos;
    move_event.rotation = runner.respawn_rot;
    move_event.has_rotation = true;
    move_event.on_ground = true;

    sendActionBar(
        session_id,
        runner.current_checkpoint_idx >= 0
            ? std::format("§c[掉落复位] 已拉回第 {} 号检查点，计时继续！", runner.current_checkpoint_idx + 1)
            : "§c[掉落复位] 已拉回赛道起点！"
    );
    return;
  }

  // 2. 起点 AABB 触发：踏入起点包围盒开启或重新开始计时
  if (!runner.running && !runner.finished && cfg_.start_box.contains(pos)) {
    runner.running = true;
    runner.finished = false;
    runner.post_finish_ticks = 0;
    runner.current_checkpoint_idx = -1;
    runner.respawn_pos = cfg_.spawn_point;
    runner.respawn_rot = cfg_.spawn_rotation;
    runner.start_time = std::chrono::steady_clock::now();

    sendActionBar(session_id, "§e§l⏱ 计时开始！冲向终点！");
    spdlog::info(
        "[ParkourInstance:{}] Player '{}' triggered start timer",
        instanceId(),
        runner.username
    );
    return;
  }

  if (!runner.running || runner.finished) {
    return;
  }

  // 3. 多级检查点 AABB 触发
  for (size_t i = 0; i < cfg_.checkpoints.size(); ++i) {
    if (static_cast<int32_t>(i) > runner.current_checkpoint_idx &&
        cfg_.checkpoints[i].box.contains(pos)) {
      runner.current_checkpoint_idx = static_cast<int32_t>(i);
      runner.respawn_pos = cfg_.checkpoints[i].respawn_pos;
      runner.respawn_rot = cfg_.checkpoints[i].respawn_rot;
      sendSystemMessage(
          session_id,
          std::format("§b[跑酷] 已激活第 {} 号检查点！", i + 1)
      );
      spdlog::info(
          "[ParkourInstance:{}] Player '{}' activated checkpoint #{}",
          instanceId(),
          runner.username,
          i + 1
      );
      break;
    }
  }

  // 4. 终点 AABB 触发与毫秒级成绩结算
  if (cfg_.finish_box.contains(pos)) {
    const auto now = std::chrono::steady_clock::now();
    const int64_t elapsed_ms = std::max<int64_t>(
        1,
        std::chrono::duration_cast<std::chrono::milliseconds>(now - runner.start_time).count()
    );

    runner.running = false;
    runner.finished = true;
    runner.finish_time = now;
    runner.post_finish_ticks = 0;
    runner.last_elapsed_ms = elapsed_ms;
    if (runner.best_elapsed_ms == 0 || elapsed_ms < runner.best_elapsed_ms) {
      runner.best_elapsed_ms = elapsed_ms;
    }

    recordLeaderboard(runner.username, elapsed_ms);

    const std::string announce_text = std::format(
        "§a[跑酷榜单] 恭喜玩家 §e{} §a通关赛道 §b[{}]§a！用时: §6{:.3f} 秒 §7(失误 {} 次)§a！3 秒后自动返回大厅...",
        runner.username,
        cfg_.map_name,
        static_cast<double>(elapsed_ms) / 1000.0,
        runner.fall_count
    );
    const std::string json_announce =
        std::format(R"({{"text":"{}"}})", escapeJsonText(announce_text));
    broadcastInInstance(buildChatPacket(json_announce, protocol::ChatPosition::SystemMessage));

    spdlog::info(
        "[ParkourInstance:{}] Player '{}' finished course '{}' in {} ms",
        instanceId(),
        runner.username,
        cfg_.map_name,
        elapsed_ms
    );
  }
}

void
ParkourInstance::onTick() {
  ++tick_counter_;
  const auto now = std::chrono::steady_clock::now();

  std::vector<uint64_t> to_return_lobby;

  for (auto& [sid, runner] : runners_) {
    if (runner.running && !runner.finished && (tick_counter_ % 5 == 0)) {
      const int64_t ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(now - runner.start_time).count();
      sendActionBar(
          sid,
          std::format("§e⏱ 当前用时: §f{:.2f}s §7| 检查点: {}/{}",
                      static_cast<double>(ms) / 1000.0,
                      runner.current_checkpoint_idx + 1,
                      cfg_.checkpoints.size())
      );
    }

    if (runner.finished) {
      ++runner.post_finish_ticks;
      const auto post_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(now - runner.finish_time).count();
      if (runner.post_finish_ticks >= kReturnLobbyDelayTicks || post_ms >= 3000) {
        runner.finished = false;
        runner.post_finish_ticks = 0;
        to_return_lobby.push_back(sid);
      }
    }
  }

  for (uint64_t sid : to_return_lobby) {
    if (return_lobby_fn_) {
      return_lobby_fn_(sid);
    }
  }
}

void
ParkourInstance::recordLeaderboard(std::string_view username, int64_t elapsed_ms) {
  auto it = std::find_if(
      leaderboard_.begin(),
      leaderboard_.end(),
      [username](const LeaderboardEntry& e) { return e.username == username; }
  );
  if (it != leaderboard_.end()) {
    if (elapsed_ms < it->elapsed_ms) {
      it->elapsed_ms = elapsed_ms;
    }
  } else {
    leaderboard_.push_back(LeaderboardEntry{std::string(username), elapsed_ms});
  }

  std::sort(
      leaderboard_.begin(),
      leaderboard_.end(),
      [](const LeaderboardEntry& a, const LeaderboardEntry& b) {
        return a.elapsed_ms < b.elapsed_ms;
      }
  );
  if (leaderboard_.size() > 10) {
    leaderboard_.resize(10);
  }
}

bool
ParkourInstance::isDeveloperMode(uint64_t session_id) const noexcept {
  auto it = runners_.find(session_id);
  return (it != runners_.end()) ? it->second.dev_mode : false;
}

void
ParkourInstance::setDeveloperMode(uint64_t session_id, bool enable) {
  auto it = runners_.find(session_id);
  if (it != runners_.end()) {
    it->second.dev_mode = enable;
    if (enable) {
      it->second.running = false;
      it->second.finished = false;
    }
  }
}

std::filesystem::path
ParkourInstance::companionIniPath() const {
  if (!cfg_.companion_ini_path.empty()) {
    return cfg_.companion_ini_path;
  }
  const auto parkour_path = std::filesystem::path("assets/maps/parkour") / (cfg_.map_name + ".ini");
  if (std::filesystem::exists(parkour_path)) {
    return parkour_path;
  }
  const auto fallback_path = std::filesystem::path("assets/maps") / (cfg_.map_name + ".ini");
  if (std::filesystem::exists(fallback_path)) {
    return fallback_path;
  }
  return parkour_path;
}

bool
ParkourInstance::setSpawnPoint(uint64_t /*session_id*/, const Vector3d& pos, const Rotation& rot) {
  cfg_.spawn_point = pos;
  cfg_.spawn_rotation = rot;
  world_->setSpawnPoint(pos);
  world_->setSpawnRotation(rot);
  entity_manager_->setDefaultSpawn(pos, rot);
  return cfg_.saveToIni(companionIniPath());
}

bool
ParkourInstance::setStartBox(uint64_t /*session_id*/, const Vector3d& pos) {
  cfg_.start_box = AABB{
      Vector3d{pos.x - 1.5, pos.y - 0.5, pos.z - 1.5},
      Vector3d{pos.x + 1.5, pos.y + 2.5, pos.z + 1.5}
  };
  return cfg_.saveToIni(companionIniPath());
}

bool
ParkourInstance::setCheckpoint(
    uint64_t /*session_id*/,
    const Vector3d& pos,
    const Rotation& rot,
    int32_t index
) {
  ParkourCheckpoint cp;
  cp.box = AABB{
      Vector3d{pos.x - 1.5, pos.y - 0.5, pos.z - 1.5},
      Vector3d{pos.x + 1.5, pos.y + 2.5, pos.z + 1.5}
  };
  cp.respawn_pos = pos;
  cp.respawn_rot = rot;

  if (index <= 0 || static_cast<size_t>(index) > cfg_.checkpoints.size() + 1) {
    cfg_.checkpoints.push_back(cp);
  } else {
    const size_t zero_idx = static_cast<size_t>(index - 1);
    if (zero_idx < cfg_.checkpoints.size()) {
      cfg_.checkpoints[zero_idx] = cp;
    } else {
      cfg_.checkpoints.push_back(cp);
    }
  }

  return cfg_.saveToIni(companionIniPath());
}

bool
ParkourInstance::setFinishBox(uint64_t /*session_id*/, const Vector3d& pos) {
  cfg_.finish_box = AABB{
      Vector3d{pos.x - 1.5, pos.y - 0.5, pos.z - 1.5},
      Vector3d{pos.x + 1.5, pos.y + 2.5, pos.z + 1.5}
  };
  return cfg_.saveToIni(companionIniPath());
}

std::string
ParkourInstance::getCourseInfo() const {
  std::ostringstream oss;
  oss << std::format("§e[赛道详情 - {}] 配置文件: {}\n", cfg_.map_name, companionIniPath().generic_string());
  oss << std::format("§7- 出生点: ({:.2f}, {:.2f}, {:.2f})\n", cfg_.spawn_point.x, cfg_.spawn_point.y, cfg_.spawn_point.z);
  oss << std::format("§7- 起点 AABB: ({:.1f}, {:.1f}, {:.1f}) ~ ({:.1f}, {:.1f}, {:.1f})\n",
                     cfg_.start_box.min_pt.x, cfg_.start_box.min_pt.y, cfg_.start_box.min_pt.z,
                     cfg_.start_box.max_pt.x, cfg_.start_box.max_pt.y, cfg_.start_box.max_pt.z);
  oss << std::format("§7- 检查点总数: {} 个\n", cfg_.checkpoints.size());
  for (size_t i = 0; i < cfg_.checkpoints.size(); ++i) {
    const auto& sp = cfg_.checkpoints[i].respawn_pos;
    oss << std::format("  §b#{}§7 复位坐标: ({:.1f}, {:.1f}, {:.1f})\n", i + 1, sp.x, sp.y, sp.z);
  }
  oss << std::format("§7- 终点 AABB: ({:.1f}, {:.1f}, {:.1f}) ~ ({:.1f}, {:.1f}, {:.1f})",
                     cfg_.finish_box.min_pt.x, cfg_.finish_box.min_pt.y, cfg_.finish_box.min_pt.z,
                     cfg_.finish_box.max_pt.x, cfg_.finish_box.max_pt.y, cfg_.finish_box.max_pt.z);
  return oss.str();
}

}  // namespace mcpe::game::parkour
