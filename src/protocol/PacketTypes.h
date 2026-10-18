/**
 * @file PacketTypes.h
 * @brief Minecraft 1.12.2 (Protocol 340) 数据包 ID 与强类型常量定义（Protocol 协议层）
 *
 * 严格杜绝散装魔数（Zero Magic Numbers），所有包 ID、字段枚举与配置一律在此集中定义。
 */

#ifndef MCPE_PROTOCOL_PACKET_TYPES_H
#define MCPE_PROTOCOL_PACKET_TYPES_H

#include <cstdint>
#include <string>

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
inline constexpr int32_t ClickWindow = 0x07;
inline constexpr int32_t CloseWindow = 0x08;
inline constexpr int32_t PluginMessage = 0x09;
inline constexpr int32_t UseEntity = 0x0A;
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
inline constexpr int32_t Animation = 0x1D;
inline constexpr int32_t PlayerBlockPlacement = 0x1F;
inline constexpr int32_t UseItem = 0x20;
}  // namespace ServerboundPacket

/**
 * @brief 服务端发往客户端的数据包 ID (Clientbound / Outbound)
 */
namespace ClientboundPacket {
inline constexpr int32_t LoginDisconnect = 0x00;
inline constexpr int32_t SpawnGlobalEntity = 0x02;
inline constexpr int32_t SpawnPlayer = 0x05;
inline constexpr int32_t ChatMessage = 0x0F;
inline constexpr int32_t PluginMessage = 0x18;
inline constexpr int32_t PlayDisconnect = 0x1A;
inline constexpr int32_t UnloadChunk = 0x1D;
inline constexpr int32_t KeepAlive = 0x1F;
inline constexpr int32_t ChunkData = 0x20;
inline constexpr int32_t JoinGame = 0x23;
inline constexpr int32_t EntityRelativeMove = 0x26;
inline constexpr int32_t EntityLookAndRelativeMove = 0x27;
inline constexpr int32_t EntityLook = 0x28;
inline constexpr int32_t PlayerAbilities = 0x2C;
inline constexpr int32_t PlayerListItem = 0x2E;
inline constexpr int32_t PlayerPositionAndLook = 0x2F;
inline constexpr int32_t DestroyEntities = 0x32;
inline constexpr int32_t RemoveEntityEffect = 0x33;
inline constexpr int32_t Respawn = 0x35;
inline constexpr int32_t EntityHeadLook = 0x36;
inline constexpr int32_t EntityVelocity = 0x3E;
inline constexpr int32_t SpawnPosition = 0x46;
inline constexpr int32_t TimeUpdate = 0x47;
inline constexpr int32_t Title = 0x48;
inline constexpr int32_t EntityTeleport = 0x4C;
inline constexpr int32_t EntityProperties = 0x4E;
inline constexpr int32_t EntityEffect = 0x4F;
}  // namespace ClientboundPacket

/**
 * @brief Minecraft 1.12.2 (Protocol 340) 原版状态效果 ID (Potion Effect IDs)
 */
namespace EffectId {
inline constexpr uint8_t Speed = 1;            // 迅捷 (增加移动速度与拉大 FOV)
inline constexpr uint8_t Slowness = 2;         // 缓慢
inline constexpr uint8_t Haste = 3;            // 急迫
inline constexpr uint8_t MiningFatigue = 4;    // 挖掘疲劳
inline constexpr uint8_t Strength = 5;         // 力量
inline constexpr uint8_t InstantHealth = 6;    // 瞬间治疗
inline constexpr uint8_t InstantDamage = 7;    // 瞬间伤害
inline constexpr uint8_t JumpBoost = 8;        // 跳跃提升
inline constexpr uint8_t Nausea = 9;           // 反胃
inline constexpr uint8_t Regeneration = 10;    // 生命恢复
inline constexpr uint8_t Resistance = 11;      // 抗性提升
inline constexpr uint8_t FireResistance = 12;  // 抗火
inline constexpr uint8_t WaterBreathing = 13; // 水下呼吸
inline constexpr uint8_t Invisibility = 14;    // 隐身
inline constexpr uint8_t Blindness = 15;       // 失明
inline constexpr uint8_t NightVision = 16;     // 夜视
inline constexpr uint8_t Hunger = 17;          // 饥饿
inline constexpr uint8_t Weakness = 18;        // 虚弱
inline constexpr uint8_t Poison = 19;          // 中毒
inline constexpr uint8_t Wither = 20;          // 凋零
inline constexpr uint8_t HealthBoost = 21;     // 生命提升
inline constexpr uint8_t Absorption = 22;      // 伤害吸收
inline constexpr uint8_t Saturation = 23;      // 饱和
inline constexpr uint8_t Glowing = 24;         // 发光
inline constexpr uint8_t Levitation = 25;      // 飘浮
}  // namespace EffectId

/**
 * @brief 0x2C / 0x13 Player Abilities 标志位掩码 (Bit field)
 */
namespace PlayerAbilitiesFlags {
inline constexpr uint8_t Invulnerable = 0x01;  // 无敌 (不承受伤害)
inline constexpr uint8_t Flying       = 0x02;  // 正在飞行
inline constexpr uint8_t AllowFlying  = 0x04;  // 允许飞行 (双击空格起飞)
inline constexpr uint8_t CreativeMode = 0x08;  // 创造模式
}  // namespace PlayerAbilitiesFlags

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
 * @brief 0x0F Chat Message 显示位置枚举
 */
enum class ChatPosition : uint8_t {
  ChatBox = 0,        // 聊天框
  SystemMessage = 1,  // 系统提示
  ActionBar = 2       // 快捷栏上方
};

/**
 * @brief 玩家属性（如皮肤与材质纹理，包含可选数字签名）
 */
struct PlayerProperty {
  std::string name;
  std::string value;
  std::string signature;
};

}  // namespace mcpe::protocol

#endif  // MCPE_PROTOCOL_PACKET_TYPES_H
