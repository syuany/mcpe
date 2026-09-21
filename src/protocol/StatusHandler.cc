#include "mcpe/protocol/StatusHandler.h"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <format>
#include <utility>

#include "mcpe/common/ByteBuffer.h"
#include "mcpe/network/TcpSession.h"
#include "mcpe/protocol/ConnectionState.h"

using namespace mcpe::protocol;

void
StatusHandler::handleHandshake(
    const std::shared_ptr<network::TcpSession>& session,
    ByteBuffer& payload
) {
  auto version_res = payload.readVarInt();
  auto host_res = payload.readString();
  auto port_res = payload.readBe<uint16_t>();
  auto next_state_res = payload.readVarInt();

  if (!version_res || !host_res || !port_res || !next_state_res) {
    spdlog::warn("[StatusHandler] Malformed handshake packet from {}", session->remoteAddress());
    session->stop();
    return;
  }

  const auto protocol_version = *version_res;
  const auto& host = *host_res;
  const auto port = *port_res;
  const auto next_state = *next_state_res;

  spdlog::info(
      "[Hanshake] Client {} -> ProtoVer: {}, Host: {}:{}, NextState: {}", session->remoteAddress(),
      protocol_version, host, port, next_state
  );

  if (next_state == 1) {
    session->setState(ConnectionState::STATUS);
  } else if (next_state == 2) {
    session->setState(ConnectionState::LOGIN);
    spdlog::info("[Hanshake] Client wants to LOGIN, ProtocolVersion: {}", protocol_version);
  } else {
    spdlog::warn("[Hanshake] Invalid next state {} from {}", next_state, session->remoteAddress());
    session->stop();
  }
}

void
StatusHandler::handleStatusRequest(const std::shared_ptr<network::TcpSession>& session) {
  spdlog::debug("[StatusHandler] Responding to Status Request from {}", session->remoteAddress());

  const std::string json_payload = std::format(
      R"({{"version":{{"name":"1.12.2","protocol":340}},"players":{{"max":100,"online":0}},"description":{{"text":"§aA High Performance C++ Server §7[Asio/C++23]"}}}})"
  );

  ByteBuffer packet;
  packet.appendVarInt(0x00);
  packet.appendString(std::move(json_payload));
  packet.prependVarInt(packet.readableBytes());

  session->sendPacket(std::move(packet));
}

void
StatusHandler::handlePing(
    const std::shared_ptr<network::TcpSession>& session,
    ByteBuffer& payload
) {
  auto time_res = payload.readBe<int64_t>();
  if (!time_res) {
    spdlog::warn("[StatusHandler] Malformed Ping packet from {}", session->remoteAddress());
    session->stop();
    return;
  }

  const auto client_time = *time_res;
  ByteBuffer packet;
  packet.appendVarInt(0x01);
  packet.writeBe(client_time);
  packet.prependVarInt(packet.readableBytes());

  session->sendPacket(packet);
  spdlog::info("[StatusHandler] Server List Ping flow completed for {}", session->remoteAddress());
}
