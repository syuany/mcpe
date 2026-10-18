#ifndef MCPE_WORLD_EVENT_QUEUE_H
#define MCPE_WORLD_EVENT_QUEUE_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "common/SpatialTypes.h"

namespace mcpe::world {

/**
 * @brief 玩家移动与视角同步事件 (对应 0x0C, 0x0D, 0x0E, 0x0F)
 */
struct PlayerMoveEvent {
  Vector3d position{};
  Rotation rotation{};
  bool on_ground{true};
  bool has_position{false};
  bool has_rotation{false};
};

/**
 * @brief 玩家聊天发言事件 (对应 0x02 Chat Message)
 */
struct PlayerChatEvent {
  std::string message{};
};

/**
 * @brief 玩家方块挖掘事件 (对应 0x14 Player Digging)
 */
struct PlayerDiggingEvent {
  DigStatus status{DigStatus::Started};
  BlockPos block_pos{};
  BlockFace face{BlockFace::Top};
};

/**
 * @brief 玩家方块放置事件 (对应 0x1F Player Block Placement)
 */
struct BlockPlacementEvent {
  BlockPos clicked_pos{};
  BlockFace face{BlockFace::Top};
  uint16_t placed_state{0};
};

/**
 * @brief 玩家进服通知事件 (紧凑结构，零额外堆分配，保障 WorldEvent <= 64 字节)
 */
struct PlayerJoinEvent {
  std::array<char, 16> player_name{};
  uint8_t name_len{0};
  std::array<uint8_t, 16> uuid{};

  [[nodiscard]] std::string_view
  nameView() const noexcept {
    return std::string_view(player_name.data(), name_len);
  }

  void
  setName(std::string_view name) noexcept {
    name_len = static_cast<uint8_t>(std::min(name.size(), size_t{16}));
    for (size_t i = 0; i < name_len; ++i) {
      player_name[i] = name[i];
    }
  }
};

/**
 * @brief 玩家退服断开事件
 */
struct PlayerLeaveEvent {};

/**
 * @brief 强类型多态事件载荷变体
 */
using EventPayload = std::variant<
    PlayerMoveEvent,
    PlayerChatEvent,
    PlayerDiggingEvent,
    BlockPlacementEvent,
    PlayerJoinEvent,
    PlayerLeaveEvent>;

/**
 * @brief 世界统一事件聚合根（零堆分配，极致紧凑，Cache Line 友好）
 */
struct WorldEvent {
  uint64_t session_id{0};
  int32_t entity_id{0};
  EventPayload payload;
};

/**
 * @brief 现代 C++ 模式匹配辅助结构体 (Overloaded pattern)
 */
template <class... Ts>
struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

/**
 * @brief 跨线程多生产者单消费者 (MPSC) 双缓冲任务队列
 */
class EventQueue {
 public:
  EventQueue() = default;

  /** @brief 供网络线程池调用：线程安全推入事件（持锁极短，纳秒级） */
  void
  push(WorldEvent event) {
    std::lock_guard<std::mutex> lock(mutex_);
    write_queue_.push_back(std::move(event));
  }

  /** @brief 供世界主线程调用：交换读写队列并一次性清空排出（消费过程完全无锁） */
  void
  drain(std::vector<WorldEvent>& out) {
    out.clear();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (write_queue_.empty()) {
        return;
      }
      out.swap(write_queue_);
    }
  }

 private:
  std::mutex mutex_;
  std::vector<WorldEvent> write_queue_;
};

}  // namespace mcpe::world

#endif  // MCPE_WORLD_EVENT_QUEUE_H
