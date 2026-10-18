#ifndef CLIENTMANAGER_H
#define CLIENTMANAGER_H

#include <cstddef>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <vector>

#include "common/ByteBuffer.h"

namespace mcpe::protocol {

class ClientSession;

/**
 * @brief 全局客户端连接与玩家会话管理中心（严格对齐 Cuberite cServer::m_Clients 架构设计）
 *
 * 核心职责：
 * 1. 线程安全集中管理所有活跃的 ClientSession 生命周期集合
 * 2. 动态统计全服在线总连接数与正式在玩人数（State == PLAY）
 * 3. 提供数据包全服多播广播（Broadcast）与除自身外广播（BroadcastExcept）能力
 * 4. 提供安全停机时的全服踢人与资源清理（kickAll）
 */
class ClientManager {
 public:
  ClientManager() = default;
  ~ClientManager() = default;

  ClientManager(const ClientManager&) = delete;
  ClientManager& operator=(const ClientManager&) = delete;

  /// 注册新连入客户端会话
  void addSession(std::shared_ptr<ClientSession> session);

  /// 注销已断开客户端会话
  void removeSession(const std::shared_ptr<ClientSession>& session);

  /// 当前网络连接总数（包含握手、探测与登录中）
  size_t sessionCount() const;

  /// 当前已完成登录进入游戏世界的在玩玩家总数（State == PLAY）
  size_t playerCount() const;

  /// 向全服所有处于 PLAY 状态的在线玩家广播数据包
  void broadcast(ByteBuffer packet);

  /// 向除指定会话外的所有在线玩家广播数据包（如玩家移动、聊天）
  void broadcastExcept(ByteBuffer packet, const std::shared_ptr<ClientSession>& except);

  /// 向除指定会话ID外的所有在线玩家广播数据包
  void broadcastExceptSessionId(ByteBuffer packet, uint64_t except_session_id);

  /// 向指定会话ID发送数据包
  void sendPacketTo(uint64_t session_id, ByteBuffer packet);

  /// 根据会话ID查找会话
  std::shared_ptr<ClientSession> findSession(uint64_t session_id) const;

  /// 优雅踢出全服玩家
  void kickAll();

  /// 获取当前所有会话快照
  std::vector<std::shared_ptr<ClientSession>> getAllSessions() const;

 private:
  mutable std::mutex mutex_;
  std::unordered_set<std::shared_ptr<ClientSession>> sessions_;
};

}  // namespace mcpe::protocol

#endif  // CLIENTMANAGER_H
