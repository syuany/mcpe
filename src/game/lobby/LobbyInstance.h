/**
 * @file LobbyInstance.h
 * @brief 商业大厅分线实例（Game 玩法层 - lobby 子模块）
 *
 * 核心职责：
 * 1. 承载单个大厅分线（如 lobby-1, lobby-2）的在线玩家与局部九宫格 AOI 广播；
 * 2. 在本分线 20 TPS 物理线程内执行虚空跌落熔断保护（Fall Reset）并同步更新 SpatialGrid。
 */

#ifndef MCPE_GAME_LOBBY_LOBBY_INSTANCE_H
#define MCPE_GAME_LOBBY_LOBBY_INSTANCE_H

#include "game/IGameInstance.h"

namespace mcpe::game::lobby {

class LobbyInstance final : public IGameInstance {
 public:
  LobbyInstance(
      std::string instance_id,
      std::shared_ptr<world::World> world,
      size_t max_players,
      double fall_reset_y,
      PacketUnicastFn unicast_fn
  );

  [[nodiscard]] double fallResetY() const noexcept { return fall_reset_y_; }
  void setFallResetY(double y) noexcept { fall_reset_y_ = y; }

 protected:
  void onPlayerJoined(uint64_t session_id, std::string_view username) override;
  void onPlayerMoved(uint64_t session_id, world::PlayerMoveEvent& move_event) override;

 private:
  double fall_reset_y_{20.0};
};

}  // namespace mcpe::game::lobby

#endif  // MCPE_GAME_LOBBY_LOBBY_INSTANCE_H
