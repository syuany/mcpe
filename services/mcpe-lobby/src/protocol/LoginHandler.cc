#include "protocol/LoginHandler.h"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <format>

#include "common/ByteBuffer.h"
#include "network/PacketWriter.h"
#include "protocol/AuthUtils.h"
#include "protocol/ClientManager.h"
#include "protocol/ClientSession.h"
#include "protocol/ConnectionState.h"
#include "protocol/GameTypes.h"
#include "protocol/PacketTypes.h"

namespace mcpe::protocol {

void
LoginHandler::handleLoginStart(const std::shared_ptr<ClientSession>& session, ByteBuffer& payload) {
  auto username_res = payload.readString(16);
  if (!username_res) {
    spdlog::warn("[LoginHandler] Failed to read username from {}", session->remoteAddress());
    session->stop();
    return;
  }

  const std::string username = *username_res;

  if (!AuthUtils::isValidUsername(username)) {
    spdlog::warn(
        "[LoginHandler] Illegal username '{}' from {}, disconnecting!",
        username,
        session->remoteAddress()
    );
    session->stop();
    return;
  }

  // 全服总人数硬上限拦截（对标 src/settings.ini [Server] max_players）
  if (auto mgr = session->clientManager()) {
    const int32_t max_total = ServerConfig::defaultInstance().max_players;
    const size_t current_online = mgr->playerCount();
    if (max_total > 0 && static_cast<int32_t>(current_online) >= max_total) {
      spdlog::warn(
          "[LoginHandler] Server full ({}/{}), rejecting player '{}' from {}",
          current_online,
          max_total,
          username,
          session->remoteAddress()
      );
      network::PacketWriter disconnect_writer(session, ClientboundPacket::LoginDisconnect);
      disconnect_writer.appendString(std::format(
          R"({{"text":"§c服务器已满员 ({}/{})，请稍后再试！"}})",
          current_online,
          max_total
      ));
      disconnect_writer.flush();
      session->stop();
      return;
    }
  }

  session->setUsername(username);
  if (!session->isBungeeForwarded()) {
    const std::string uuid_str = AuthUtils::generateOfflineUuid(username);
    const auto uuid_bytes = AuthUtils::generateOfflineUuidBytes(username);
    session->setUuid(uuid_str);
    session->setUuidBytes(uuid_bytes);
  }

  spdlog::info(
      "[LoginHandler] Player logged in: {} (UUID: {}) from {}",
      username,
      session->uuid(),
      session->remoteAddress()
  );

  // 利用 PacketWriter 下发 Login Success (0x02)
  network::PacketWriter writer(session, 0x02);
  writer.appendString(session->uuid());
  writer.appendString(username);
  writer.flush();

  // 移交 ClientSession 自行驱动 PLAY 阶段世界准入（彻底解耦 LoginHandler 与 PlayHandler）
  session->startPlay();
}

}  // namespace mcpe::protocol