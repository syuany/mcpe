#include "protocol/LoginHandler.h"

#include <cstdint>

#include "common/ByteBuffer.h"
#include "network/TcpSession.h"
#include "protocol/AuthUtils.h"
#include "protocol/ConnectionState.h"
#include "protocol/PlayHandler.h"
#include "spdlog/spdlog.h"

namespace mcpe::protocol {

void
LoginHandler::handleLoginStart(
    const std::shared_ptr<network::TcpSession> &session,
    ByteBuffer &payload
) {
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
  session->setUsername(username);
  session->setUuid(uuid_str);

  spdlog::info(
      "[LoginHandler] Player logged in: {} (UUID: {}) from {}",
      username,
      uuid_str,
      session->remoteAddress()
  );

  ByteBuffer packet;
  packet.appendVarInt(0x02);
  packet.appendString(uuid_str);
  packet.appendString(username);
  packet.prependVarInt(static_cast<int32_t>(packet.readableBytes()));

  session->sendPacket(std::move(packet));
  session->setState(ConnectionState::PLAY);

  PlayHandler::onPlayerJoin(session);
}

}  // namespace mcpe::protocol