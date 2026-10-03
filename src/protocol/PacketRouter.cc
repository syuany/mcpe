#include "protocol/PacketRouter.h"

#include "network/TcpSession.h"
#include "protocol/ConnectionState.h"
#include "protocol/LoginHandler.h"
#include "protocol/PlayHandler.h"
#include "protocol/StatusHandler.h"
#include "spdlog/spdlog.h"

using namespace mcpe::protocol;

void
PacketRouter::dispatch(
    const std::shared_ptr<network::TcpSession>& session,
    ConnectionState state,
    ByteBuffer packet
) {
  // 1. 读取包头部的 Packet ID（VarInt）
  auto id_res = packet.readVarInt();
  if (!id_res) {
    // 无法解析出合法的 Packet ID，判定为畸形数据包，切断连接以防恶意攻击
    spdlog::warn("[Router] Failed to read packet ID from {}", session->remoteAddress());
    session->stop();
    return;
  }

  const int32_t packet_id = *id_res;
  spdlog::debug(
      "[Router] State: {}, PacketID: 0x{:02X}, PayloadBytes: {}",
      toString(state),
      packet_id,
      packet.readableBytes()
  );

  // 2. 根据会话当前所处的协议连接状态机进行分支路由
  switch (state) {
    case ConnectionState::HANDSHAKING:
      // 握手状态：仅允许 0x00 Handshake 包
      if (packet_id == 0x00) {
        StatusHandler::handleHandshake(session, packet);
      } else {
        spdlog::warn("[Router] Unknown packet in HANDSHAKING: 0x{:02X}", packet_id);
        session->stop();
      }
      break;

    case ConnectionState::STATUS:
      // 状态探测阶段：支持 0x00 (Status Request) 和 0x01 (Ping)
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
      if (packet_id == 0x00) {
        LoginHandler::handleLoginStart(session, packet);
      } else {
        spdlog::warn("[Router] Unknown packet in LOGIN: 0x{:02X}", packet_id);
        session->stop();
      }
      break;

    case ConnectionState::PLAY:
      if (packet_id == 0x00) {
        PlayHandler::handleTeleportConfirm(session, packet);
      } else if (packet_id == 0x0B) {
        PlayHandler::handleKeepAlive(session, packet);
      } else {
        // 白名单容忍原版客户端高频例行报文（语言、视距、皮肤偏好、快捷栏切换等）
        switch (packet_id) {
          case 0x03:  // Client Status
          case 0x04:  // Client Settings
          case 0x05:  // Confirm Transaction
          case 0x08:  // Close Window
          case 0x09:  // Plugin Message (MC|Brand)
          case 0x13:  // Player Abilities
          case 0x15:  // Entity Action
          case 0x18:  // Resource Pack Status
          case 0x1A:  // Held Item Change
            spdlog::debug("[Router] Ignored routine PLAY packet 0x{:02X}", packet_id);
            break;
          default:
            spdlog::warn(
                "[Router] Unhandled PLAY packet 0x{:02X} from {}",
                packet_id,
                session->remoteAddress()
            );
            session->stop();
            break;
        }
      }
      break;

    default:
      // 非法状态枚举，断连防御
      spdlog::warn("[Router] Invalid connection state: {}", static_cast<int>(state));
      session->stop();
      break;
  }
}
