/**
 * @file WorldTicker.h
 * @brief 20 TPS 物理主循环与单调时钟漂移补偿节拍器（Domain 领域层）
 *
 * 核心架构定位：
 * 1. 作为单写入者 Actor 模型的核心主驱动引擎，独占游戏世界状态写权限；
 * 2. 严格对标 Minecraft 20 TPS (50ms) 恒定步长物理循环；
 * 3. 采用 std::chrono::steady_clock 单调时钟并具备防死亡螺旋累加器保护；
 * 4. 批量消费 EventQueue 事件，调度 EntityManager 广播差分位移。
 */

#ifndef MCPE_WORLD_WORLD_TICKER_H
#define MCPE_WORLD_WORLD_TICKER_H

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace mcpe::world {

class EventQueue;
class EntityManager;

class WorldTicker {
 public:
  using ChatCallback = std::function<
      void(uint64_t session_id, std::string_view sender_name, const std::string& msg)>;

  WorldTicker(
      EventQueue& event_queue,
      EntityManager& entity_manager,
      ChatCallback chat_cb = nullptr
  );
  ~WorldTicker();

  WorldTicker(const WorldTicker&) = delete;
  WorldTicker& operator=(const WorldTicker&) = delete;

  void setChatCallback(ChatCallback cb) { chat_callback_ = std::move(cb); }

  void start();
  void stop();
  bool isRunning() const noexcept { return running_.load(std::memory_order_acquire); }

  /**
   * @brief 单 Tick 逻辑执行流水线（供内部线程循环调用，亦可用于单元测试单步确定性驱动）
   */
  void tick();

 private:
  void run();

 private:
  EventQueue& event_queue_;
  EntityManager& entity_manager_;
  ChatCallback chat_callback_;

  std::atomic<bool> running_{false};
  std::thread worker_thread_;
};

}  // namespace mcpe::world

#endif  // MCPE_WORLD_WORLD_TICKER_H
