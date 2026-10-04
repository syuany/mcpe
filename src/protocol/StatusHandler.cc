#include "protocol/StatusHandler.h"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <format>
#include <utility>

#include "common/ByteBuffer.h"
#include "network/PacketWriter.h"
#include "protocol/ClientSession.h"
#include "protocol/ConnectionState.h"

using namespace mcpe::protocol;

void
StatusHandler::handleHandshake(
    const std::shared_ptr<ClientSession>& session,
    ByteBuffer& payload
) {
  // 1. 按 Minecraft 协议规范顺序解包 Handshake 字段：
  //    - Protocol Version: VarInt (如 1.12.2 为 340)
  //    - Server Address: String (客户端连接的主机名或 IP)
  //    - Server Port: uint16_t (大端端口号，通常 25565)
  //    - Next State: VarInt (1: STATUS, 2: LOGIN)
  auto version_res = payload.readVarInt();
  auto host_res = payload.readString();
  auto port_res = payload.readBe<uint16_t>();
  auto next_state_res = payload.readVarInt();

  // 若任一字段读取失败，视为畸形握手包，记录警告并切断连接
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

  // 2. 根据 Next State 流转状态机
  if (next_state == 1) {
    // 客户端意图进行状态查询（Server List Ping / MOTD）
    session->setState(ConnectionState::STATUS);
  } else if (next_state == 2) {
    // 客户端意图登录进入游戏
    session->setState(ConnectionState::LOGIN);
    spdlog::info("[Hanshake] Client wants to LOGIN, ProtocolVersion: {}", protocol_version);
  } else {
    // 非法意图（非 1 非 2），切断连接防御未知攻击
    spdlog::warn("[Hanshake] Invalid next state {} from {}", next_state, session->remoteAddress());
    session->stop();
  }
}

void
StatusHandler::handleStatusRequest(const std::shared_ptr<ClientSession>& session) {
  spdlog::debug("[StatusHandler] Responding to Status Request from {}", session->remoteAddress());

  // 1. 组装符合 Minecraft 1.12.2 官方规范的 JSON 文本
  //    包含版本协议号 340、版本名称 1.12.2、最大/当前在线人数以及带颜色代码的 MOTD
  const std::string json_payload = std::format(
      R"({{"version":{{"name":"1.12.2","protocol":340}},"players":{{"max":100,"online":0}},"description":{{"text":"§aA High Performance C++ Server §7[Asio/C++23]"}}}})"
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
  //    注意：此处必须是 8 字节大端整数，绝不是 VarInt！
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
