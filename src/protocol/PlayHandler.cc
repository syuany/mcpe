#include "protocol/PlayHandler.h"

#include <spdlog/spdlog.h>

#include "network/PacketWriter.h"
#include "protocol/ClientSession.h"

namespace mcpe::protocol {

void
PlayHandler::onPlayerJoin(const std::shared_ptr<ClientSession>& session) {
  sendJoinGame(session, 1);
  const int32_t teleport_id = session->nextTeleportId();
  sendPlayerPositionAndLook(session, 0.0, 64.0, 0.0, 0.0f, 0.0f, 0x00, teleport_id);
  session->startKeepAliveTimer();
}

void
PlayHandler::sendJoinGame(const std::shared_ptr<ClientSession>& session, int32_t entity_id) {
  network::PacketWriter writer(session, 0x23);
  writer.writeBe<int32_t>(entity_id);
  writer.writeBe<uint8_t>(1);
  writer.writeBe<int32_t>(0);
  writer.writeBe<uint8_t>(1);
  writer.writeBe<uint8_t>(100);
  writer.appendString("flat");
  writer.writeBe<uint8_t>(0);

  spdlog::info("[PlayHandler] Sent Join Game (0x23) to {}", session->remoteAddress());
}

void
PlayHandler::sendPlayerPositionAndLook(
    const std::shared_ptr<ClientSession>& session,
    double x,
    double y,
    double z,
    float yaw,
    float pitch,
    uint8_t flags,
    int32_t teleport_id
) {
  network::PacketWriter writer(session, 0x2F);
  writer.writeBe<double>(x);
  writer.writeBe<double>(y);
  writer.writeBe<double>(z);
  writer.writeBe<float>(yaw);
  writer.writeBe<float>(pitch);
  writer.writeBe<uint8_t>(flags);
  writer.appendVarInt(teleport_id);

  spdlog::info(
      "[PlayHandler] Sent Player Position And Look (0x2F) to {}, TeleportID: {}",
      session->remoteAddress(),
      teleport_id
  );
}

void
PlayHandler::handleTeleportConfirm(
    const std::shared_ptr<ClientSession>& session,
    ByteBuffer& payload
) {
  auto id_res = payload.readVarInt();
  if (!id_res) {
    spdlog::warn("[PlayHandler] Malformed Teleport Confirm from {}", session->remoteAddress());
    session->stop();
    return;
  }

  const int32_t confirmed_id = *id_res;
  if (confirmed_id == session->pendingTeleportId()) {
    session->clearPendingTeleportId();
    spdlog::info(
        "[PlayHandler] Teleport confirmed by {} (ID: {})",
        session->remoteAddress(),
        confirmed_id
    );
  } else {
    spdlog::warn(
        "[PlayHandler] Teleport ID mismatch from {}: expected {}, got {}",
        session->remoteAddress(),
        session->pendingTeleportId(),
        confirmed_id
    );
  }
}

void
PlayHandler::handleKeepAlive(
    const std::shared_ptr<ClientSession>& session,
    ByteBuffer& payload
) {
  // ⚠️ 1.12.2 铁律：Keep Alive 载荷必须是 8 字节大端整数 readBe<int64_t>()
  auto id_res = payload.readBe<int64_t>();
  if (!id_res) {
    spdlog::warn("[PlayHandler] Malformed Keep Alive echo from {}", session->remoteAddress());
    session->stop();
    return;
  }

  const int64_t keep_alive_id = *id_res;
  session->onKeepAliveResponse(keep_alive_id);
}

}  // namespace mcpe::protocol
