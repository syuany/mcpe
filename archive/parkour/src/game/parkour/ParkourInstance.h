/**
 * @file ParkourInstance.h
 * @brief 多人同赛道随进随跑跑酷实例（Game 玩法层 - parkour 子模块）
 *
 * 核心规则：
 * 1. 支持多人随时加入同一跑酷赛道同屏竞跳（通过 EntityManager + SpatialGrid 局部九宫格互见）；
 * 2. 每名玩家拥有完全独立的 steady_clock 毫秒级高精度计时器、激活检查点与完赛状态；
 * 3. 踏入起点 AABB 开启/重置计时；踏入检查点 AABB 更新复位点；
 * 4. 跌落 Y < fall_reset_y 时立即下发 0x2F 拉回最近检查点或起点（计时不中断，并同步更新 SpatialGrid）；
 * 5. 踏入终点 AABB 通报毫秒级通关成绩、更新内存排行榜，并在 3 秒（60 Ticks）后自动触发切回大厅。
 */

#ifndef MCPE_GAME_PARKOUR_PARKOUR_INSTANCE_H
#define MCPE_GAME_PARKOUR_PARKOUR_INSTANCE_H

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "game/IGameInstance.h"
#include "game/parkour/ParkourMapConfig.h"

namespace mcpe::game::parkour {

/**
 * @brief 单名玩家在跑酷赛道中的实时竞速状态
 */
struct PlayerParkourState {
  uint64_t session_id{0};
  int32_t entity_id{0};
  std::string username;
  Vector3d respawn_pos{};
  Rotation respawn_rot{};
  int32_t current_checkpoint_idx{-1};     // -1 表示起点，0..K-1 表示已激活的检查点下标
  int16_t current_speed_amplifier{-1};    // -1 表示无速度药水，>=0 表示当前生效的速度等级
  int32_t current_speed_zone_idx{-1};     // 当前触发的 speed_zone 下标，-1 表示不在任何区域
  int16_t checkpoint_speed_amplifier{-1}; // 最近激活检查点或起点时的速度等级，供跌落复位时还原
  bool running{false};
  bool finished{false};
  std::chrono::steady_clock::time_point start_time{};
  std::chrono::steady_clock::time_point finish_time{};
  uint32_t post_finish_ticks{0};
  int64_t last_elapsed_ms{0};
  int64_t best_elapsed_ms{0};
  uint32_t fall_count{0};
  bool dev_mode{false};
};

/**
 * @brief 跑酷赛道排行榜记录项
 */
struct LeaderboardEntry {
  std::string username;
  int64_t elapsed_ms{0};
};

class ParkourInstance final : public IGameInstance {
 public:
  static constexpr uint32_t kReturnLobbyDelayTicks = 60;  // 完赛后 60 Ticks (3.0s) 自动切回大厅

  ParkourInstance(
      std::string instance_id,
      std::shared_ptr<world::World> world,
      ParkourMapConfig map_cfg,
      size_t max_players,
      PacketUnicastFn unicast_fn
  );

  [[nodiscard]] const ParkourMapConfig& mapConfig() const noexcept { return cfg_; }
  [[nodiscard]] const std::string& mapName() const noexcept { return cfg_.map_name; }

  /**
   * @brief 查询指定玩家的当前跑酷状态（供单元测试与指令查询使用）
   */
  [[nodiscard]] const PlayerParkourState* getRunnerState(uint64_t session_id) const noexcept;

  /**
   * @brief 获取当前赛道 Top 排行榜（按耗时升序排列）
   */
  [[nodiscard]] const std::vector<LeaderboardEntry>& leaderboard() const noexcept {
    return leaderboard_;
  }

  /**
   * @brief 查询玩家是否处于开发者建造与调试模式
   */
  [[nodiscard]] bool isDeveloperMode(uint64_t session_id) const noexcept;

  /**
   * @brief 开启或关闭指定玩家的开发者模式
   */
  void setDeveloperMode(uint64_t session_id, bool enable);

  /**
   * @brief 开发者指令：设置赛道默认出生点并持久化写入 .ini
   */
  bool setSpawnPoint(uint64_t session_id, const Vector3d& pos, const Rotation& rot);

  /**
   * @brief 开发者指令：将指定坐标设置为赛道起点并持久化写入 .ini
   */
  bool setStartBox(uint64_t session_id, const Vector3d& pos);

  /**
   * @brief 开发者指令：设置或追加赛道检查点并持久化写入 .ini
   * @param index 1-based 序号，若 <= 0 则自动追加下一个检查点
   */
  bool setCheckpoint(uint64_t session_id, const Vector3d& pos, const Rotation& rot, int32_t index = -1);

  /**
   * @brief 开发者指令：将指定坐标设置为赛道终点并持久化写入 .ini
   */
  bool setFinishBox(uint64_t session_id, const Vector3d& pos);

  /**
   * @brief 开发者指令：格式化获取当前赛道参数与配置详情
   */
  [[nodiscard]] std::string getCourseInfo() const;

  /**
   * @brief 获取当前赛道伴生配置 INI 文件磁盘路径
   */
  [[nodiscard]] std::filesystem::path companionIniPath() const;

 protected:
  void onPlayerJoined(uint64_t session_id, std::string_view username) override;
  void onPlayerLeft(uint64_t session_id, std::string_view username) override;
  void onPlayerMoved(uint64_t session_id, world::PlayerMoveEvent& move_event) override;
  void onTick() override;

 private:
  void recordLeaderboard(std::string_view username, int64_t elapsed_ms);

  ParkourMapConfig cfg_;
  uint64_t tick_counter_{0};
  std::unordered_map<uint64_t, PlayerParkourState> runners_;
  std::vector<LeaderboardEntry> leaderboard_;
};

}  // namespace mcpe::game::parkour

#endif  // MCPE_GAME_PARKOUR_PARKOUR_INSTANCE_H
