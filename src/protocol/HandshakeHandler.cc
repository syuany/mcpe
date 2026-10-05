#include "protocol/HandshakeHandler.h"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <utility>

#include "protocol/ClientSession.h"
#include "protocol/ConnectionState.h"

namespace mcpe::protocol {

void
HandshakeHandler::handleHandshake(
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
    spdlog::warn("[HandshakeHandler] Malformed handshake packet from {}", session->remoteAddress());
    session->stop();
    return;
  }

  const auto protocol_version = *version_res;
  const auto& host = *host_res;
  const auto port = *port_res;
  const auto next_state = *next_state_res;

  spdlog::info(
      "[Handshake] Client {} -> ProtoVer: {}, Host: {}:{}, NextState: {}",
      session->remoteAddress(),
      protocol_version,
      host,
      port,
      next_state
  );

  // 2. 根据 Next State 流转状态机
  if (next_state == 1) {
    // 客户端意图进行状态查询（Server List Ping / MOTD）
    session->setState(ConnectionState::STATUS);
  } else if (next_state == 2) {
    // 客户端意图登录进入游戏
    session->setState(ConnectionState::LOGIN);
    spdlog::info("[Handshake] Client wants to LOGIN, ProtocolVersion: {}", protocol_version);
  } else {
    // 非法意图（非 1 非 2），切断连接防御未知攻击
    spdlog::warn("[Handshake] Invalid next state {} from {}", next_state, session->remoteAddress());
    session->stop();
  }
}

}  // namespace mcpe::protocol
