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
      "[Router] State: {}, PacketID: 0x{:02X}, PayloadBytes: {}", toString(state), packet_id,
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
      // 登录阶段（Sprint 2 将支持 0x00 Login Start）
      spdlog::warn("[Router] Unhandled packet in LOGIN: 0x{:02X}", packet_id);
      session->stop();
      break;

    case ConnectionState::PLAY:
      // 游戏内阶段：处理原版客户端高频发送的例行报文白名单
      switch (packet_id) {
        case 0x03:  // Client Status（客户端状态）
        case 0x04:  // Client Settings（客户端设置：语言、视距、皮肤偏好等）
        case 0x05:  // Confirm Transaction（窗口事务确认）
        case 0x08:  // Close Window（关闭窗口）
        case 0x09:  // Plugin Message（插件通道消息，如 MC|Brand）
        case 0x13:  // Player Abilities（玩家飞行/疾跑能力同步）
        case 0x15:  // Entity Action（玩家蹲下、跳跃等实体动作）
        case 0x18:  // Resource Pack Status（材质包加载状态回执）
        case 0x1A:  // Held Item Change（快捷栏选定物品切换）
          // 容忍原版客户端未实现例行包，避免非必要断连导致客户端闪退
          spdlog::debug("[Router] Ignored routine PLAY packet 0x{:02X}", packet_id);
          break;
        default:
          spdlog::warn("[Router] Unhandled PLAY packet 0x{:02X}", packet_id);
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
}
