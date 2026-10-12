/**
 * @file PacketTypes.h
 * @brief Minecraft 1.12.2 (Protocol 340) 数据包 ID 与强类型常量定义（Protocol 协议层）
 *
 * 严格杜绝散装魔数（Zero Magic Numbers），所有包 ID、字段枚举与配置一律在此集中定义。
 */

#ifndef MCPE_PROTOCOL_PACKET_TYPES_H
#define MCPE_PROTOCOL_PACKET_TYPES_H

#include <cstdint>

namespace mcpe::protocol {

/**
 * @brief 客户端发往服务端的数据包 ID (Serverbound / Inbound)
 */
namespace ServerboundPacket {
inline constexpr int32_t TeleportConfirm = 0x00;
inline constexpr int32_t ChatMessage = 0x02;
inline constexpr int32_t ClientStatus = 0x03;
inline constexpr int32_t ClientSettings = 0x04;
inline constexpr int32_t ConfirmTransaction = 0x05;
inline constexpr int32_t CloseWindow = 0x08;
inline constexpr int32_t PluginMessage = 0x09;
inline constexpr int32_t KeepAlive = 0x0B;
inline constexpr int32_t Player = 0x0C;
inline constexpr int32_t PlayerPosition = 0x0D;
inline constexpr int32_t PlayerPositionAndLook = 0x0E;
inline constexpr int32_t PlayerLook = 0x0F;
inline constexpr int32_t PlayerAbilities = 0x13;
inline constexpr int32_t PlayerDigging = 0x14;
inline constexpr int32_t EntityAction = 0x15;
inline constexpr int32_t ResourcePackStatus = 0x18;
inline constexpr int32_t HeldItemChange = 0x1A;
inline constexpr int32_t PlayerBlockPlacement = 0x1F;
}  // namespace ServerboundPacket

/**
 * @brief 服务端发往客户端的数据包 ID (Clientbound / Outbound)
 */
namespace ClientboundPacket {
inline constexpr int32_t SpawnGlobalEntity = 0x02;
inline constexpr int32_t SpawnPlayer = 0x05;
inline constexpr int32_t ChatMessage = 0x0F;
inline constexpr int32_t KeepAlive = 0x1F;
inline constexpr int32_t ChunkData = 0x20;
inline constexpr int32_t JoinGame = 0x23;
inline constexpr int32_t EntityRelativeMove = 0x26;
inline constexpr int32_t EntityLookAndRelativeMove = 0x27;
inline constexpr int32_t EntityLook = 0x28;
inline constexpr int32_t PlayerListItem = 0x2E;
inline constexpr int32_t PlayerPositionAndLook = 0x2F;
inline constexpr int32_t DestroyEntities = 0x32;
inline constexpr int32_t EntityTeleport = 0x4C;
}  // namespace ClientboundPacket

/**
 * @brief 0x2E Player List Item 动作枚举
 */
enum class PlayerListItemAction : int32_t {
  AddPlayer = 0,
  UpdateGamemode = 1,
  UpdateLatency = 2,
  UpdateDisplayName = 3,
  RemovePlayer = 4
};

/**
 * @brief 0x02 Chat Message 显示位置枚举
 */
enum class ChatPosition : uint8_t {
  ChatBox = 0,        // 聊天框
  SystemMessage = 1,  // 系统提示
  ActionBar = 2       // 快捷栏上方
};

}  // namespace mcpe::protocol

#endif  // MCPE_PROTOCOL_PACKET_TYPES_H
