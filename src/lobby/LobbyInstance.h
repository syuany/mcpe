/**
 * @file LobbyInstance.h
 * @brief 大厅分线实例 Actor（Lobby 微服务引擎核心空间层）
 *
 * 核心架构定位：
 * 1. 静态地形共享：持有 std::shared_ptr<world::World>，多条大厅分线共享同一份只读区块内存；
 * 2. 动态状态隔离：每个分线独占独立的 EventQueue + EntityManager (含 SpatialGrid) + WorldTicker (20 TPS 线程)；
 * 3. 原子席位管理：提供 tryReserveSlot / releaseSlot 保证高并发连入时分线满员判定精确无竞态；
 * 4. 空间与安全保障：在 20 TPS 物理节拍中执行虚空跌落拉回（Fall Reset）与九宫格 AOI 视界广播。
 */

#ifndef MCPE_LOBBY_LOBBY_INSTANCE_H
#define MCPE_LOBBY_LOBBY_INSTANCE_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

#include "common/ByteBuffer.h"
#include "common/SpatialTypes.h"
#include "protocol/PacketTypes.h"
#include "world/EntityManager.h"
#include "world/EventQueue.h"
#include "world/World.h"
#include "world/WorldTicker.h"

namespace mcpe::lobby {

class LobbyInstance final {
 public:
  using PacketUnicastFn = std::function<void(uint64_t session_id, ByteBuffer packet)>;
  using TeleportPlayerFn =
      std::function<void(uint64_t session_id, const Vector3d& pos, const Rotation& rot)>;
  using ChatDispatchFn = std::function<
      void(uint64_t session_id, std::string_view sender_name, const std::string& msg)>;

  LobbyInstance(
      std::string instance_id,
      std::shared_ptr<world::World> world,
      size_t max_players,
      double fall_reset_y = 20.0,
      PacketUnicastFn unicast_fn = nullptr
  );

  ~LobbyInstance();

  LobbyInstance(const LobbyInstance&) = delete;
  LobbyInstance& operator=(const LobbyInstance&) = delete;

  /// 启动当前大厅分线专属的 20 TPS 物理线程
  void start();

  /// 停止当前大厅分线专属的 20 TPS 物理线程
  void stop();

  /// 单步执行一次物理 Tick（供确定性单元测试驱动）
  void stepTick();

  /// 线程安全预留一个玩家席位（满员返回 false）
  [[nodiscard]] bool tryReserveSlot(uint64_t session_id);

  /// 释放玩家席位
  void releaseSlot(uint64_t session_id);

  /// 检查指定会话是否已在本分线中预留席位
  [[nodiscard]] bool hasReservedSlot(uint64_t session_id) const;

  /// 供网络线程或调度器推入事件至本分线的 MPSC 队列
  void pushEvent(world::WorldEvent event);

  /// 向当前大厅分线内的所有在线玩家零拷贝广播数据包（基于 ByteBuffer::retainedSlice）
  void broadcastInInstance(const ByteBuffer& packet, uint64_t except_sid = 0);

  /// 向指定玩家单播系统提示消息 (Position = SystemMessage)
  void sendSystemMessage(uint64_t session_id, std::string_view text);

  /// 向指定玩家单播快捷栏上方动态提示 (Position = ActionBar)
  void sendActionBar(uint64_t session_id, std::string_view text);

  [[nodiscard]] const std::string& instanceId() const noexcept { return instance_id_; }

  [[nodiscard]] const world::World& world() const noexcept { return *world_; }
  [[nodiscard]] std::shared_ptr<world::World> worldPtr() const noexcept { return world_; }

  [[nodiscard]] std::shared_ptr<world::EventQueue> eventQueuePtr() const noexcept {
    return event_queue_;
  }
  [[nodiscard]] std::shared_ptr<world::EntityManager> entityManagerPtr() const noexcept {
    return entity_manager_;
  }
  [[nodiscard]] world::EntityManager& entityManager() noexcept { return *entity_manager_; }
  [[nodiscard]] const world::EntityManager& entityManager() const noexcept {
    return *entity_manager_;
  }

  /// 当前已分配进本分线的玩家人数
  [[nodiscard]] size_t playerCount() const noexcept;

  /// 本分线允许的最大玩家人数
  [[nodiscard]] size_t maxPlayers() const noexcept {
    return max_players_.load(std::memory_order_acquire);
  }
  void setMaxPlayers(size_t max_p) noexcept {
    max_players_.store(max_p, std::memory_order_release);
  }

  [[nodiscard]] bool isFull() const noexcept { return playerCount() >= maxPlayers(); }

  [[nodiscard]] double fallResetY() const noexcept { return fall_reset_y_; }
  void setFallResetY(double y) noexcept { fall_reset_y_ = y; }

  void setTeleportCallback(TeleportPlayerFn fn) { teleport_fn_ = std::move(fn); }
  void setChatDispatchCallback(ChatDispatchFn fn);

  static std::string escapeJsonText(std::string_view text);
  static ByteBuffer buildChatPacket(std::string_view json_text, protocol::ChatPosition pos);

 private:
  std::string instance_id_;
  std::shared_ptr<world::World> world_;
  std::atomic<size_t> max_players_{50};
  double fall_reset_y_{20.0};

  mutable std::mutex slot_mutex_;
  std::unordered_set<uint64_t> reserved_sessions_;

  std::shared_ptr<world::EventQueue> event_queue_;
  std::shared_ptr<world::EntityManager> entity_manager_;
  std::unique_ptr<world::WorldTicker> ticker_;

  PacketUnicastFn unicast_fn_;
  TeleportPlayerFn teleport_fn_;
  ChatDispatchFn chat_dispatch_fn_;
};

}  // namespace mcpe::lobby

#endif  // MCPE_LOBBY_LOBBY_INSTANCE_H
