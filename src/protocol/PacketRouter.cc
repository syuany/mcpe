#include "mcpe/protocol/PacketRouter.h"

#include "mcpe/network/TcpSession.h"
#include "mcpe/protocol/ConnectionState.h"
#include "mcpe/protocol/StatusHandler.h"
#include "spdlog/spdlog.h"

using namespace mcpe::protocol;

void
PacketRouter::dispatch(
    const std::shared_ptr<network::TcpSession>& session,
    ConnectionState state,
    ByteBuffer packet
) {
  auto id_res = packet.readVarInt();
  if (!id_res) {
    spdlog::warn("[Router] Failed to read packet ID from {}", session->remoteAddress());
    session->stop();
    return;
  }

  const int32_t packet_id = *id_res;
  spdlog::debug(
      "[Router] State: {}, PacketID: 0x{:02X}, PayloadBytes: {}", toString(state), packet_id,
      packet.readableBytes()
  );

  switch (state) {
    case ConnectionState::HANDSHAKING:
      if (packet_id == 0x00) {
        StatusHandler::handleHandshake(session, packet);
      } else {
        spdlog::warn("[Router] Unknown packet in HANDSHAKING: 0x{:02X}", packet_id);
        session->stop();
      }
      break;

    case ConnectionState::STATUS:
      if (packet_id == 0x00) {
        StatusHandler::handleStatusRequest(session);
      } else if (packet_id == 0x01) {
        StatusHandler::handlePing(session, packet);
      } else {
        spdlog::warn("[Router] Unknown packet in STATUS: 0x{:02X}", packet_id);
        session->stop();
      }
      break;

    case ConnectionState::LOGIN:
      spdlog::warn("[Router] Unhandled packet in LOGIN: 0x{:02X}", packet_id);
      session->stop();
      break;

    case ConnectionState::PLAY:
      switch (packet_id) {
        case 0x03:  // Client Status
        case 0x04:  // Client Settings
        case 0x05:  // Confirm Transaction
        case 0x08:  // Close Window
        case 0x09:  // Plugin Message
        case 0x13:  // Player Abilities
        case 0x15:  // Entity Action
        case 0x18:  // Resource Pack Status
        case 0x1A:  // Held Item Change
          spdlog::debug("[Router] Ignored routine PLAY packet 0x{:02X}", packet_id);
          break;
        default:
          spdlog::warn("[Router] Unhandled PLAY packet 0x{:02X}", packet_id);
          session->stop();
          break;
      }
      break;

    default:
      spdlog::warn("[Router] Invalid connection state: {}", static_cast<int>(state));
      session->stop();
      break;
  }
}
