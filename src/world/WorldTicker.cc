/**
 * @file WorldTicker.cc
 * @brief 20 TPS 物理主循环与单调时钟漂移补偿节拍器实现
 */

#include "world/WorldTicker.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <optional>
#include <utility>
#include <vector>

#include "world/EntityManager.h"
#include "world/EventQueue.h"

namespace mcpe::world {

WorldTicker::WorldTicker(
    EventQueue& event_queue,
    EntityManager& entity_manager,
    ChatCallback chat_cb
)
    : event_queue_(event_queue),
      entity_manager_(entity_manager),
      chat_callback_(std::move(chat_cb)) {}

WorldTicker::~WorldTicker() {
  stop();
}

void
WorldTicker::start() {
  if (running_.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  worker_thread_ = std::thread(&WorldTicker::run, this);
  spdlog::info("[WorldTicker] 20 TPS physics ticker thread started");
}

void
WorldTicker::stop() {
  if (running_.exchange(false, std::memory_order_acq_rel)) {
    if (worker_thread_.joinable()) {
      worker_thread_.join();
    }
    spdlog::info("[WorldTicker] 20 TPS physics ticker thread stopped");
  }
}

void
WorldTicker::run() {
  using namespace std::chrono;
  constexpr nanoseconds kTickInterval(50'000'000);     // 50ms = 20 TPS
  constexpr nanoseconds kMaxAccumulator(250'000'000);  // 防死亡螺旋上限: 250ms (最多追 5 帧)

  auto last_time = steady_clock::now();
  nanoseconds accumulator(0);

  while (running_.load(std::memory_order_relaxed)) {
    const auto current_time = steady_clock::now();
    accumulator += (current_time - last_time);
    last_time = current_time;

    if (accumulator > kMaxAccumulator) {
      spdlog::warn("[WorldTicker] Lag spike detected, capping accumulator to prevent death spiral");
      accumulator = kMaxAccumulator;
    }

    // 恒定步长物理推进
    while (accumulator >= kTickInterval && running_.load(std::memory_order_relaxed)) {
      tick();
      accumulator -= kTickInterval;
    }

    // 动态休眠余量
    const auto elapsed = steady_clock::now() - current_time;
    if (elapsed < kTickInterval) {
      std::this_thread::sleep_for(kTickInterval - elapsed);
    }
  }
}

void
WorldTicker::tick() {
  // 1. 微秒级交换 MPSC 任务队列，批量收取
  std::vector<WorldEvent> events;
  event_queue_.drain(events);

  // 2. 消费事件流水线
  for (auto& ev : events) {
    std::visit(
        Overloaded{
            [this, &ev](PlayerMoveEvent& move) {
              if (move_filter_callback_) {
                move_filter_callback_(ev.session_id, move);
              }
              std::optional<Vector3d> pos;
              if (move.has_position) {
                pos = move.position;
              }
              std::optional<Rotation> rot;
              if (move.has_rotation) {
                rot = move.rotation;
              }
              entity_manager_.updatePlayerPosition(ev.session_id, pos, rot, move.on_ground);
            },
            [this, &ev](const PlayerJoinEvent& join) {
              entity_manager_.addPlayer(
                  ev.session_id,
                  ev.entity_id,
                  join.nameView(),
                  join.uuid
              );
              if (join_leave_callback_) {
                join_leave_callback_(ev.session_id, join.nameView(), true);
              }
            },
            [this, &ev](const PlayerLeaveEvent&) {
              const std::string name = entity_manager_.getPlayerName(ev.session_id);
              entity_manager_.removePlayer(ev.session_id);
              if (join_leave_callback_) {
                join_leave_callback_(ev.session_id, name, false);
              }
            },
            [this, &ev](const PlayerChatEvent& chat) {
              if (chat_callback_) {
                const std::string name = entity_manager_.getPlayerName(ev.session_id);
                chat_callback_(ev.session_id, name, chat.message);
              }
            },
            [](const auto&) {
              // 方块挖掘、方块放置等事件留作后续扩展
            }},
        ev.payload
    );
  }

  // 3. 执行上层游戏实例自定义 Tick 钩子（如大厅定时任务、倒计时推进等）
  if (tick_hook_callback_) {
    tick_hook_callback_();
  }

  // 4. 调度实体管理器广播差分位移
  entity_manager_.updateAndBroadcast();
}

}  // namespace mcpe::world
