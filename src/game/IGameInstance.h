/**
 * @file IGameInstance.h
 * @brief 游戏空间实例抽象基类（Game 玩法与实例调度层）
 *
 * 核心架构设计（对齐 Cuberite cWorld 与商业群组服 Actor 实例模型）：
 * 1. 静态地形共享：持有 std::shared_ptr<world::World>，多条大厅分线或同图跑酷房间共享同一份只读区块内存；
 * 2. 动态状态隔离：每个实例独占一套 EventQueue + EntityManager (含 SpatialGrid) + WorldTicker (20 TPS 线程)；
 * 3. 原子席位预留：提供 tryReserveSlot / releaseSlot 保证高并发瞬时连入时的分线满员判定 100% 精确无竞态；
 * 4. 插件式扩展钩子：定义 Waiting -> Countdown -> Playing -> Ended 状态机与虚函数钩子，
 *    供 LobbyInstance, ParkourInstance 及未来新小游戏零侵入继承扩展。
 */

#ifndef MCPE_GAME_I_GAME_INSTANCE_H
#define MCPE_GAME_I_GAME_INSTANCE_H

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

namespace mcpe::game {

/**
 * @brief 通用房间/实例生命周期状态枚举
 */
enum class InstanceState : uint8_t {
  Waiting = 0,    // 等待玩家加入
  Countdown = 1,  // 开局倒计时
  Playing = 2,    // 正在进行中 (大厅与随进随跑跑酷常驻此状态)
  Ended = 3       // 结算与回收中
};

/**
 * @brief 实例玩法类型枚举
 */
enum class InstanceType : uint8_t {
  Lobby = 0,
  Parkour = 1,
  CustomMinigame = 2
};

class IGameInstance {
 public:
  using PacketUnicastFn = std::function<void(uint64_t session_id, ByteBuffer packet)>;
  using TeleportPlayerFn =
      std::function<void(uint64_t session_id, const Vector3d& pos, const Rotation& rot)>;
  using ReturnToLobbyFn = std::function<void(uint64_t session_id)>;
  using ChatDispatchFn = std::function<
      void(uint64_t session_id, std::string_view sender_name, const std::string& msg)>;

  IGameInstance(
      std::string instance_id,
      InstanceType type,
      std::shared_ptr<world::World> world,
      size_t max_players,
      PacketUnicastFn unicast_fn
  );

  virtual ~IGameInstance();

  IGameInstance(const IGameInstance&) = delete;
  IGameInstance& operator=(const IGameInstance&) = delete;

  /// 启动当前实例专属的 20 TPS 物理线程
  void start();

  /// 停止当前实例专属的 20 TPS 物理线程
  void stop();

  /// 单步执行一次物理 Tick（供确定性单元测试驱动）
  void stepTick();

  /// 线程安全预留一个玩家席位（满员返回 false）
  [[nodiscard]] bool tryReserveSlot(uint64_t session_id);

  /// 释放玩家席位
  void releaseSlot(uint64_t session_id);

  /// 检查指定会话是否已在本实例中预留席位
  [[nodiscard]] bool hasReservedSlot(uint64_t session_id) const;

  /// 供网络线程或调度器推入事件至本实例的 MPSC 队列
  void pushEvent(world::WorldEvent event);

  /// 向当前实例内的所有在线玩家零拷贝广播数据包（基于 ByteBuffer::retainedSlice）
  void broadcastInInstance(const ByteBuffer& packet, uint64_t except_sid = 0);

  /// 向指定玩家单播系统提示消息 (Position = SystemMessage)
  void sendSystemMessage(uint64_t session_id, std::string_view text);

  /// 向指定玩家单播快捷栏上方动态提示 (Position = ActionBar)
  void sendActionBar(uint64_t session_id, std::string_view text);

  [[nodiscard]] const std::string& instanceId() const noexcept { return instance_id_; }
  [[nodiscard]] InstanceType type() const noexcept { return type_; }
  [[nodiscard]] InstanceState state() const noexcept {
    return state_.load(std::memory_order_acquire);
  }
  void setState(InstanceState s) noexcept { state_.store(s, std::memory_order_release); }

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

  /// 当前已分配进本实例的玩家人数
  [[nodiscard]] size_t playerCount() const noexcept;

  /// 本实例允许的最大玩家人数
  [[nodiscard]] size_t maxPlayers() const noexcept {
    return max_players_.load(std::memory_order_acquire);
  }
  void setMaxPlayers(size_t max_p) noexcept {
    max_players_.store(max_p, std::memory_order_release);
  }

  [[nodiscard]] bool isFull() const noexcept { return playerCount() >= maxPlayers(); }

  void setTeleportCallback(TeleportPlayerFn fn) { teleport_fn_ = std::move(fn); }
  void setReturnToLobbyCallback(ReturnToLobbyFn fn) { return_lobby_fn_ = std::move(fn); }
  void setChatDispatchCallback(ChatDispatchFn fn);

  static std::string escapeJsonText(std::string_view text);
  static ByteBuffer buildChatPacket(std::string_view json_text, protocol::ChatPosition pos);

 protected:
  // 供子类 (LobbyInstance / ParkourInstance / 未来小游戏) 重写的 20 TPS 单线程安全钩子
  virtual void onPlayerJoined(uint64_t /*session_id*/, std::string_view /*username*/) {}
  virtual void onPlayerLeft(uint64_t /*session_id*/, std::string_view /*username*/) {}
  virtual void onPlayerMoved(
      uint64_t session_id,
      world::PlayerMoveEvent& move_event
  ) = 0;
  virtual void onTick() {}

  std::string instance_id_;
  InstanceType type_;
  std::shared_ptr<world::World> world_;
  std::atomic<size_t> max_players_{50};
  std::atomic<InstanceState> state_{InstanceState::Playing};

  mutable std::mutex slot_mutex_;
  std::unordered_set<uint64_t> reserved_sessions_;

  std::shared_ptr<world::EventQueue> event_queue_;
  std::shared_ptr<world::EntityManager> entity_manager_;
  std::unique_ptr<world::WorldTicker> ticker_;

  PacketUnicastFn unicast_fn_;
  TeleportPlayerFn teleport_fn_;
  ReturnToLobbyFn return_lobby_fn_;
  ChatDispatchFn chat_dispatch_fn_;
};

}  // namespace mcpe::game

#endif  // MCPE_GAME_I_GAME_INSTANCE_H
