#include "lobby/PortalManager.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <utility>

#include "common/IniConfig.h"

namespace mcpe::lobby {

void
PortalManager::addPortal(Portal portal) {
  auto it = std::find_if(portals_.begin(), portals_.end(), [&](const Portal& p) {
    return p.name == portal.name;
  });
  if (it != portals_.end()) {
    *it = std::move(portal);
  } else {
    portals_.push_back(std::move(portal));
  }
}

void
PortalManager::clear() {
  portals_.clear();
  last_trigger_time_.clear();
}

bool
PortalManager::loadFromIni(const std::filesystem::path& ini_path) {
  common::IniConfig ini;
  if (!ini.loadFromFile(ini_path)) {
    return false;
  }

  bool found_any = false;
  for (const auto& sec : ini.sections()) {
    if (sec.starts_with("Portal.")) {
      const std::string name = sec.substr(7);
      auto aabb_opt = ini.getAABB(sec, "min", "max");
      if (!aabb_opt) {
        spdlog::warn("[PortalManager] Portal section '{}' missing valid min/max AABB in {}", sec, ini_path.string());
        continue;
      }

      std::string target = ini.getString(sec, "target", name);
      std::string display = ini.getString(sec, "display_name", target);

      addPortal(Portal{
          .name = name,
          .target_server = std::move(target),
          .display_name = std::move(display),
          .bounding_box = *aabb_opt
      });
      found_any = true;
      spdlog::info(
          "[PortalManager] Registered portal '{}' -> target: '{}' (display: {}) from {}",
          name,
          portals_.back().target_server,
          portals_.back().display_name,
          ini_path.string()
      );
    }
  }

  return found_any;
}

const Portal*
PortalManager::checkAndTrigger(
    uint64_t session_id,
    const Vector3d& pos,
    std::chrono::milliseconds cooldown
) {
  for (const auto& portal : portals_) {
    if (portal.bounding_box.contains(pos)) {
      const auto now = std::chrono::steady_clock::now();
      auto it = last_trigger_time_.find(session_id);
      if (it != last_trigger_time_.end() && (now - it->second) < cooldown) {
        return nullptr;  // 处于防抖冷却期内
      }
      last_trigger_time_[session_id] = now;
      return &portal;
    }
  }
  return nullptr;
}

void
PortalManager::onPlayerLeave(uint64_t session_id) {
  last_trigger_time_.erase(session_id);
}

}  // namespace mcpe::lobby
