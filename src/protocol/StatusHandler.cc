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
StatusHandler::handleStatusRequest(const std::shared_ptr<network::TcpSession>& session) {
  spdlog::debug("[StatusHandler] Responding to Status Request from {}", session->remoteAddress());

  // 1. 组装符合 Minecraft 1.12.2 官方规范的 JSON 文本
  //    包含版本协议号 340、版本名称 1.12.2、最大/当前在线人数以及带颜色代码的 MOTD
  const std::string json_payload = std::format(
      R"({{"version":{{"name":"1.12.2","protocol":340}},"players":{{"max":100,"online":0}},"description":{{"text":"§aA High Performance C++ Server §7[Asio/C++23]"}}}})"
  );

  // 2. 构造包体：[Packet ID: 0x00 (VarInt)] + [JSON String: (VarInt len + UTF-8 文本)]
  ByteBuffer packet;
  packet.appendVarInt(0x00);
  packet.appendString(std::move(json_payload));

  // 3. 在最外层头部打入总包长 VarInt 前缀（利用 prependVarInt 实现零内存拷贝原位打包）
  packet.prependVarInt(packet.readableBytes());

  // 4. 将打包好的数据包交由底层 Session 的写队列并发安全下发
  session->sendPacket(std::move(packet));
}

void
StatusHandler::handlePing(
    const std::shared_ptr<network::TcpSession>& session,
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

  // 2. 封装 Pong 响应报文：[Packet ID: 0x01 (VarInt)] + [8 字节大端时间戳]
  ByteBuffer packet;
  packet.appendVarInt(0x01);
  packet.writeBe(client_time);

  // 3. 外层包裹包长 VarInt 前缀（通常长度为 9：1 字节 ID + 8 字节时间戳）
  packet.prependVarInt(packet.readableBytes());

  // 4. 下发 Pong 给客户端
  session->sendPacket(std::move(packet));
  spdlog::info("[StatusHandler] Server List Ping flow completed for {}", session->remoteAddress());
}
