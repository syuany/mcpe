#ifndef MCPE_LOBBY_PORTAL_MANAGER_H
#define MCPE_LOBBY_PORTAL_MANAGER_H

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "common/SpatialTypes.h"

namespace mcpe::lobby {

/**
 * @brief 空间传送门定义（由包围盒、目标子服与显示名称构成）
 */
struct Portal {
  std::string name;          // 传送门唯一标识
  std::string target_server; // 目标子服名称 (如 "survival")
  std::string display_name;  // 悬浮/提示名称 (如 "§a§l[生存服]")
  AABB bounding_box;         // 空间检测轴对齐包围盒
};

class PortalManager {
 public:
  PortalManager() = default;

  void addPortal(Portal portal);
  void clear();

  [[nodiscard]] const std::vector<Portal>& portals() const noexcept {
    return portals_;
  }

  /**
   * @brief 从 INI 配置文件加载传送门定义（形如 [Portal.<name>]）
   */
  bool loadFromIni(const std::filesystem::path& ini_path);

  /**
   * @brief 检测玩家坐标是否落入某个传送门中，并进行防抖冷却校验
   * @param session_id 玩家会话 ID
   * @param pos 玩家当前权威坐标
   * @param cooldown 防抖冷却时间 (默认 3 秒)
   * @return 若命中且冷却通过，返回目标 Portal 指针；否则返回 nullptr
   */
  [[nodiscard]] const Portal* checkAndTrigger(
      uint64_t session_id,
      const Vector3d& pos,
      std::chrono::milliseconds cooldown = std::chrono::seconds(3)
  );

  void onPlayerLeave(uint64_t session_id);

 private:
  std::vector<Portal> portals_;
  std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> last_trigger_time_;
};

}  // namespace mcpe::lobby

#endif  // MCPE_LOBBY_PORTAL_MANAGER_H
