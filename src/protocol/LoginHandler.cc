#include "protocol/LoginHandler.h"

#include <cstdint>

#include "common/ByteBuffer.h"
#include "network/PacketWriter.h"
#include "protocol/AuthUtils.h"
#include "protocol/ClientSession.h"
#include "protocol/ConnectionState.h"
#include "spdlog/spdlog.h"

namespace mcpe::protocol {

void
LoginHandler::handleLoginStart(const std::shared_ptr<ClientSession> &session, ByteBuffer &payload) {
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

  const std::string uuid_str = AuthUtils::generateOfflineUuid(username);
  const auto uuid_bytes = AuthUtils::generateOfflineUuidBytes(username);
  session->setUsername(username);
  session->setUuid(uuid_str);
  session->setUuidBytes(uuid_bytes);

  spdlog::info(
      "[LoginHandler] Player logged in: {} (UUID: {}) from {}",
      username,
      uuid_str,
      session->remoteAddress()
  );

  // 利用 PacketWriter 下发 Login Success (0x02)
  network::PacketWriter writer(session, 0x02);
  writer.appendString(uuid_str);
  writer.appendString(username);
  writer.flush();

  // 移交 ClientSession 自行驱动 PLAY 阶段世界准入（彻底解耦 LoginHandler 与 PlayHandler）
  session->startPlay();
}

}  // namespace mcpe::protocol