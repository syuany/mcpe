#include "protocol/PacketRouter.h"

#include <spdlog/spdlog.h>

#include "protocol/ClientManager.h"
#include "protocol/ClientSession.h"
#include "protocol/ConnectionState.h"
#include "protocol/HandshakeHandler.h"
#include "protocol/LoginHandler.h"
#include "protocol/PacketTypes.h"
#include "protocol/PlayHandler.h"
#include "protocol/StatusHandler.h"

using namespace mcpe::protocol;

void
PacketRouter::dispatch(const std::shared_ptr<ClientSession>& session, ByteBuffer packet) {
  dispatch(session, session->state(), std::move(packet));
}

void
PacketRouter::dispatch(
    const std::shared_ptr<ClientSession>& session,
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
        HandshakeHandler::handleHandshake(session, packet);
      } else {
        spdlog::warn("[Router] Unknown packet in HANDSHAKING: 0x{:02X}", packet_id);
        session->stop();
      }
      break;

    case ConnectionState::STATUS:
      // 状态探测阶段：支持 0x00 (Status Request) 和 0x01 (Ping)
      switch (packet_id) {
        case 0x00: {
          size_t online_players = 0;
          if (auto mgr = session->clientManager()) {
            online_players = mgr->playerCount();
          }
          StatusHandler::handleStatusRequest(
              session,
              ServerConfig::defaultInstance(),
              online_players
          );
          break;
        }
        case 0x01:
          StatusHandler::handlePing(session, packet);
          break;
        default:
          spdlog::warn("[Router] Unknown packet in STATUS: 0x{:02X}", packet_id);
          session->stop();
          break;
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
      switch (packet_id) {
        case ServerboundPacket::TeleportConfirm:
          PlayHandler::handleTeleportConfirm(session, packet);
          break;

        case ServerboundPacket::ChatMessage:
          PlayHandler::handleChatMessage(session, packet);
          break;

        case ServerboundPacket::KeepAlive:
          PlayHandler::handleKeepAlive(session, packet);
          break;

        case ServerboundPacket::Player:
        case ServerboundPacket::PlayerPosition:
        case ServerboundPacket::PlayerPositionAndLook:
        case ServerboundPacket::PlayerLook:
          PlayHandler::handlePlayerMovement(session, packet_id, packet);
          break;

        case ServerboundPacket::PlayerDigging:
          PlayHandler::handlePlayerDigging(session, packet);
          break;

        case ServerboundPacket::PlayerBlockPlacement:
          PlayHandler::handleBlockPlacement(session, packet);
          break;

        // 白名单容忍原版客户端高频例行与交互报文（语言、视距、皮肤偏好、快捷栏切换、摆臂挥手、物品使用等）
        case ServerboundPacket::ClientStatus:
        case ServerboundPacket::ClientSettings:
        case ServerboundPacket::ConfirmTransaction:
        case ServerboundPacket::ClickWindow:
        case ServerboundPacket::CloseWindow:
        case ServerboundPacket::PluginMessage:
        case ServerboundPacket::UseEntity:
        case ServerboundPacket::PlayerAbilities:
        case ServerboundPacket::EntityAction:
        case ServerboundPacket::ResourcePackStatus:
        case ServerboundPacket::HeldItemChange:
        case ServerboundPacket::Animation:
        case ServerboundPacket::UseItem:
          spdlog::debug("[Router] Ignored routine PLAY packet 0x{:02X}", packet_id);
          packet.retrieveAll();
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
      break;

    default:
      // 非法状态枚举，断连防御
      spdlog::warn("[Router] Invalid connection state: {}", static_cast<int>(state));
      session->stop();
      break;
  }

  // 3. 包消费完整性检查（借鉴 Cuberite 协议消费完整性断言机制）
  //    若会话未被断开且该包依然残留未读字节，说明业务层与协议规范出现字段漂移或漏读
  if (!session->isStopped() && packet.readableBytes() > 0) {
    spdlog::warn(
        "[Router] Packet 0x{:02X} in state {} had {} unconsumed bytes remaining from {}! Potential "
        "protocol mismatch.",
        packet_id,
        toString(state),
        packet.readableBytes(),
        session->remoteAddress()
    );
  }
}
