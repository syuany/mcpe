#include "protocol/HandshakeHandler.h"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <utility>

#include "protocol/BungeeCord.h"
#include "protocol/ClientSession.h"
#include "protocol/ConnectionState.h"
#include "protocol/GameTypes.h"

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

  const auto& cfg = ServerConfig::defaultInstance();
  const bool has_null_terminator = (host.find('\0') != std::string::npos);

  // 处理 BungeeCord / Velocity IP Forwarding 协议穿透与 Bungee-Guard 防伪鉴权
  if (has_null_terminator) {
    if (!cfg.bungee_forwarding) {
      spdlog::warn(
          "[Handshake] Forwarded handshake received from {} but bungee_forwarding is disabled, dropping!",
          session->remoteAddress()
      );
      session->stop();
      return;
    }

    auto bungee_opt = BungeeCord::parseHandshakeAddress(host);
    if (!bungee_opt) {
      spdlog::warn(
          "[Handshake] Malformed BungeeCord forwarding payload from {}, dropping!",
          session->remoteAddress()
      );
      session->stop();
      return;
    }

    // 校验 Bungee-Guard 密钥 (若配置中启用了网关密钥)
    if (!cfg.bungee_guard_token.empty() && bungee_opt->guard_token != cfg.bungee_guard_token) {
      spdlog::warn(
          "[Handshake] Bungee-Guard token mismatch from {}, dropping!",
          session->remoteAddress()
      );
      session->stop();
      return;
    }

    spdlog::info(
        "[Handshake] BungeeCord Forwarding active: RealIP={}, UUID={}, Host={}",
        bungee_opt->real_ip,
        bungee_opt->uuid_str,
        bungee_opt->virtual_host
    );

    session->setRemoteAddress(bungee_opt->real_ip);
    session->setUuid(bungee_opt->uuid_str);
    session->setUuidBytes(bungee_opt->uuid_bytes);
    session->setProperties(std::move(bungee_opt->properties));
    session->setIsBungeeForwarded(true);
  } else {
    // 未带 \0 的直连连接：若启用了 Bungee-Guard 密钥，则禁止直连绕过网关
    if (!cfg.bungee_guard_token.empty()) {
      spdlog::warn(
          "[Handshake] Direct connection blocked by Bungee-Guard from {}",
          session->remoteAddress()
      );
      session->stop();
      return;
    }
  }

  spdlog::info(
      "[Handshake] Client {} -> ProtoVer: {}, Port: {}, NextState: {}",
      session->remoteAddress(),
      protocol_version,
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
