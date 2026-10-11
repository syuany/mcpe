/**
 * @file ChatHandler.cc
 * @brief 聊天多播与快捷指令路由系统实现
 */

#include "protocol/ChatHandler.h"

#include <spdlog/spdlog.h>

#include <format>
#include <utility>

namespace mcpe::protocol {

ChatHandler::ChatHandler(SystemMessageSender sys_sender, MulticastSender multicast_sender)
    : sys_sender_(std::move(sys_sender)), multicast_sender_(std::move(multicast_sender)) {}

void
ChatHandler::setSenders(SystemMessageSender sys_sender, MulticastSender multicast_sender) {
  sys_sender_ = std::move(sys_sender);
  multicast_sender_ = std::move(multicast_sender);
}

void
ChatHandler::handleMessage(
    uint64_t sender_sid,
    std::string_view sender_name,
    const std::string& raw_msg
) {
  if (raw_msg.empty()) {
    return;
  }

  // 1. 快捷指令分流拦截
  if (raw_msg[0] == '/') {
    handleCommand(sender_sid, raw_msg);
    return;
  }

  // 2. 普通玩家公屏聊天富文本 JSON 格式化
  const std::string escaped_text = escapeJson(raw_msg);
  const std::string json_chat = std::format(
      R"({{"text":"<{}> {}","color":"white"}})",
      escapeJson(sender_name),
      escaped_text
  );

  spdlog::info("[Chat] <{}> {}", sender_name, raw_msg);

  auto chat_packet = buildChatPacket(json_chat, ChatPosition::ChatBox);

  // 3. 全局广播（包括发送者自己）
  if (multicast_sender_) {
    multicast_sender_(std::move(chat_packet));
  }
}

void
ChatHandler::handleCommand(uint64_t sender_sid, const std::string& cmd_line) {
  spdlog::info("[Command] SID {} executed: {}", sender_sid, cmd_line);

  if (cmd_line == "/lobby") {
    sendSystemMessage(sender_sid, "§a[系统] 您已经在主大厅中！");
  } else if (cmd_line == "/maps") {
    sendSystemMessage(sender_sid, "§e[地图列表] 1. 新手跑酷 (输入 /join 1)  2. 螺旋跑酷 (输入 /join 2)");
  } else {
    sendSystemMessage(sender_sid, "§c[错误] 未知指令，请输入 /maps 查看支持的指令。");
  }
}

void
ChatHandler::sendSystemMessage(uint64_t session_id, std::string_view message) {
  const std::string json_msg = std::format(R"({{"text":"{}"}})", escapeJson(message));
  auto packet = buildChatPacket(json_msg, ChatPosition::SystemMessage);
  if (sys_sender_) {
    sys_sender_(session_id, std::move(packet));
  }
}

std::string
ChatHandler::escapeJson(std::string_view text) {
  std::string result;
  result.reserve(text.size() + 8);
  for (char c : text) {
    switch (c) {
      case '"':
        result += "\\\"";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '\b':
        result += "\\b";
        break;
      case '\f':
        result += "\\f";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          // 过滤掉不可见控制字符
        } else {
          result.push_back(c);
        }
        break;
    }
  }
  return result;
}

ByteBuffer
ChatHandler::buildChatPacket(std::string_view json_text, ChatPosition pos) {
  ByteBuffer buf;
  buf.appendVarInt(ClientboundPacket::ChatMessage);
  buf.appendString(json_text);
  buf.appendByte(static_cast<uint8_t>(pos));

  buf.prependVarInt(static_cast<int32_t>(buf.readableBytes()));
  return buf;
}

}  // namespace mcpe::protocol
