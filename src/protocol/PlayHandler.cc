#include "protocol/PlayHandler.h"

#include <spdlog/spdlog.h>
#include "network/TcpSession.h"

namespace mcpe::protocol {

void PlayHandler::onPlayerJoin(const std::shared_ptr<network::TcpSession>& session) {
  sendJoinGame(session, 1);
  sendPlayerPositionAndLook(session, 0.0, 64.0, 0.0, 0.0f, 0.0f, 0x00, 1);
  session->startKeepAliveTimer();
}

void PlayHandler::sendJoinGame(const std::shared_ptr<network::TcpSession>& session, int32_t entity_id) {
  ByteBuffer body;
  body.appendVarInt(0x23);
  body.writeBe<int32_t>(entity_id);
  body.writeBe<uint8_t>(1);
  body.writeBe<int32_t>(0);
  body.writeBe<uint8_t>(1);
  body.writeBe<uint8_t>(100);
  body.appendString("flat");
  body.writeBe<uint8_t>(0);

  ByteBuffer packet;
  packet.appendVarInt(static_cast<int32_t>(body.readableBytes()));
  packet.append(body.peek(), body.readableBytes());
  session->sendPacket(std::move(packet));

  spdlog::info("[PlayHandler] Sent Join Game (0x23) to {}", session->remoteAddress());
}

void PlayHandler::sendPlayerPositionAndLook(
    const std::shared_ptr<network::TcpSession>& session,
    double x, double y, double z,
    float yaw, float pitch,
    uint8_t flags, int32_t teleport_id
) {
  ByteBuffer body;
  body.appendVarInt(0x2F);
  body.writeBe<double>(x);
  body.writeBe<double>(y);
  body.writeBe<double>(z);
  body.writeBe<float>(yaw);
  body.writeBe<float>(pitch);
  body.writeBe<uint8_t>(flags);
  body.appendVarInt(teleport_id);

  ByteBuffer packet;
  packet.appendVarInt(static_cast<int32_t>(body.readableBytes()));
  packet.append(body.peek(), body.readableBytes());
  session->sendPacket(std::move(packet));

  spdlog::info("[PlayHandler] Sent Spawn Position (0x2F) to {}, TeleportID: {}", session->remoteAddress(), teleport_id);
}

void PlayHandler::handleTeleportConfirm(const std::shared_ptr<network::TcpSession>& /*session*/, ByteBuffer& /*payload*/) {}

void PlayHandler::handleKeepAlive(const std::shared_ptr<network::TcpSession>& /*session*/, ByteBuffer& /*payload*/) {}

}  // namespace mcpe::protocol
