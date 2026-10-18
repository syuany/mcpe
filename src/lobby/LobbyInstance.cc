/**
 * @file LobbyInstance.cc
 * @brief 大厅分线实例 Actor 实现
 */

#include "lobby/LobbyInstance.h"

#include <spdlog/spdlog.h>

#include <format>
#include <utility>

#include "network/PacketWriter.h"

namespace mcpe::lobby {

LobbyInstance::LobbyInstance(
    std::string instance_id,
    std::shared_ptr<world::World> world,
    size_t max_players,
    double fall_reset_y,
    PacketUnicastFn unicast_fn
)
    : instance_id_(std::move(instance_id)),
      world_(std::move(world)),
      max_players_(max_players),
      fall_reset_y_(fall_reset_y),
      event_queue_(std::make_shared<world::EventQueue>()),
      entity_manager_(std::make_shared<world::EntityManager>()),
      unicast_fn_(std::move(unicast_fn)) {
  if (!world_) {
    world_ = std::make_shared<world::World>();
  }
  entity_manager_->setDefaultSpawn(world_->spawnPoint(), world_->spawnRotation());

  entity_manager_->setSenders(
      [this](uint64_t sid, ByteBuffer pkt) {
        if (unicast_fn_) {
          unicast_fn_(sid, std::move(pkt));
        }
      },
      [this](uint64_t except_sid, ByteBuffer pkt) {
        broadcastInInstance(pkt, except_sid);
      },
      [this](ByteBuffer pkt) {
        broadcastInInstance(pkt, 0);
      }
  );

  ticker_ = std::make_unique<world::WorldTicker>(
      *event_queue_,
      *entity_manager_,
      [this](uint64_t sid, std::string_view name, const std::string& msg) {
        if (chat_dispatch_fn_) {
          chat_dispatch_fn_(sid, name, msg);
        }
      }
  );

  ticker_->setMoveFilterCallback([this](uint64_t sid, world::PlayerMoveEvent& move_ev) {
    if (!move_ev.has_position) {
      return;
    }
    // 虚空跌落保护 (Fall Reset)
    if (move_ev.position.y < fall_reset_y_) {
      const Vector3d reset_pos = world_->spawnPoint();
      const Rotation reset_rot = world_->spawnRotation();
      if (teleport_fn_) {
        teleport_fn_(sid, reset_pos, reset_rot);
      }
      move_ev.position = reset_pos;
      move_ev.rotation = reset_rot;
      move_ev.has_rotation = true;
      move_ev.on_ground = true;
    }
  });

  ticker_->setJoinLeaveCallback([this](uint64_t sid, std::string_view name, bool joined) {
    if (joined) {
      {
        std::lock_guard<std::mutex> lock(slot_mutex_);
        reserved_sessions_.insert(sid);
      }
      spdlog::info(
          "[LobbyInstance:{}] Player '{}' (SID {}) entered lobby shard ({}/{})",
          instance_id_,
          name,
          sid,
          playerCount(),
          maxPlayers()
      );
    } else {
      releaseSlot(sid);
      spdlog::info(
          "[LobbyInstance:{}] Player '{}' (SID {}) left lobby shard ({}/{})",
          instance_id_,
          name,
          sid,
          playerCount(),
          maxPlayers()
      );
    }
  });
}

LobbyInstance::~LobbyInstance() {
  stop();
}

void
LobbyInstance::start() {
  if (ticker_) {
    ticker_->start();
    spdlog::info(
        "[LobbyInstance] Started 20 TPS actor instance '{}' (max_players={})",
        instance_id_,
        maxPlayers()
    );
  }
}

void
LobbyInstance::stop() {
  if (ticker_) {
    ticker_->stop();
  }
}

void
LobbyInstance::stepTick() {
  if (ticker_) {
    ticker_->tick();
  }
}

bool
LobbyInstance::tryReserveSlot(uint64_t session_id) {
  std::lock_guard<std::mutex> lock(slot_mutex_);
  if (reserved_sessions_.contains(session_id)) {
    return true;
  }
  if (reserved_sessions_.size() >= max_players_.load(std::memory_order_acquire)) {
    return false;
  }
  reserved_sessions_.insert(session_id);
  return true;
}

void
LobbyInstance::releaseSlot(uint64_t session_id) {
  std::lock_guard<std::mutex> lock(slot_mutex_);
  reserved_sessions_.erase(session_id);
}

bool
LobbyInstance::hasReservedSlot(uint64_t session_id) const {
  std::lock_guard<std::mutex> lock(slot_mutex_);
  return reserved_sessions_.contains(session_id);
}

size_t
LobbyInstance::playerCount() const noexcept {
  std::lock_guard<std::mutex> lock(slot_mutex_);
  return reserved_sessions_.size();
}

void
LobbyInstance::pushEvent(world::WorldEvent event) {
  if (event_queue_) {
    event_queue_->push(std::move(event));
  }
}

void
LobbyInstance::broadcastInInstance(const ByteBuffer& packet, uint64_t except_sid) {
  if (!unicast_fn_) {
    return;
  }
  std::vector<uint64_t> targets = entity_manager_->getAllSessionIds();
  if (targets.empty()) {
    std::lock_guard<std::mutex> lock(slot_mutex_);
    targets.assign(reserved_sessions_.begin(), reserved_sessions_.end());
  }
  for (uint64_t sid : targets) {
    if (sid == except_sid) {
      continue;
    }
    unicast_fn_(sid, packet.retainedSlice());
  }
}

void
LobbyInstance::sendSystemMessage(uint64_t session_id, std::string_view text) {
  if (!unicast_fn_) {
    return;
  }
  const std::string json = std::format(R"({{"text":"{}"}})", escapeJsonText(text));
  unicast_fn_(session_id, buildChatPacket(json, protocol::ChatPosition::SystemMessage));
}

void
LobbyInstance::sendActionBar(uint64_t session_id, std::string_view text) {
  if (!unicast_fn_) {
    return;
  }
  const std::string json = std::format(R"({{"text":"{}"}})", escapeJsonText(text));
  unicast_fn_(session_id, buildChatPacket(json, protocol::ChatPosition::ActionBar));
}

void
LobbyInstance::setChatDispatchCallback(ChatDispatchFn fn) {
  chat_dispatch_fn_ = std::move(fn);
}

std::string
LobbyInstance::escapeJsonText(std::string_view text) {
  std::string result;
  result.reserve(text.size() + 8);
  for (char c : text) {
    switch (c) {
      case '"':
        result += "\\\"";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '\b':
        result += "\\b";
        break;
      case '\f':
        result += "\\f";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) >= 0x20) {
          result.push_back(c);
        }
        break;
    }
  }
  return result;
}

ByteBuffer
LobbyInstance::buildChatPacket(std::string_view json_text, protocol::ChatPosition pos) {
  network::PacketBuilder builder(protocol::ClientboundPacket::ChatMessage);
  builder.appendString(json_text);
  builder.appendByte(static_cast<uint8_t>(pos));
  return builder.build();
}

}  // namespace mcpe::lobby
