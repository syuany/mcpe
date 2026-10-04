#include "protocol/StatusHandler.h"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <format>
#include <utility>

#include "common/ByteBuffer.h"
#include "network/PacketWriter.h"
#include "protocol/ClientSession.h"

namespace mcpe::protocol {

void
StatusHandler::handleStatusRequest(
    const std::shared_ptr<ClientSession>& session,
    const ServerConfig& config,
    size_t online_players
) {
  spdlog::debug("[StatusHandler] Responding to Status Request from {}", session->remoteAddress());

  // 1. 根据 ServerConfig 动态组装符合 Minecraft 1.12.2 规范的 JSON 描述文本（消灭硬编码）
  const std::string json_payload = std::format(
      R"({{"version":{{"name":"{}","protocol":{}}},"players":{{"max":{},"online":{}}},"description":{{"text":"{}"}}}})",
      config.version_name,
      config.protocol_version,
      config.max_players,
      online_players,
      config.motd
  );

  // 2. 利用 RAII PacketWriter 自动打入 Packet ID 0x00，析构时自动前插包长并异步发送
  network::PacketWriter writer(session, 0x00);
  writer.appendString(json_payload);
}

void
StatusHandler::handlePing(
    const std::shared_ptr<ClientSession>& session,
    ByteBuffer& payload
) {
  // 1. 读取原生的 8 字节大端有符号长整型（Long）时间戳
  auto time_res = payload.readBe<int64_t>();
  if (!time_res) {
    spdlog::warn("[StatusHandler] Malformed Ping packet from {}", session->remoteAddress());
    session->stop();
    return;
  }

  const auto client_time = *time_res;

  // 2. 利用 RAII PacketWriter 封装 Pong (0x01) 响应报文，离开作用域自动前插包长下发
  network::PacketWriter writer(session, 0x01);
  writer.writeBe(client_time);

  spdlog::info("[StatusHandler] Server List Ping flow completed for {}", session->remoteAddress());
}

}  // namespace mcpe::protocol
