#ifndef CLIENTSESSION_H
#define CLIENTSESSION_H

#include <asio/as_tuple.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "common/ByteBuffer.h"
#include "network/SessionCallbacks.h"
#include "protocol/ConnectionState.h"

namespace mcpe::network {
class TcpSession;
}

namespace mcpe::protocol {

class ClientManager;
class ProtocolHandler;

/**
 * @brief Minecraft 客户端业务会话（严格对标 Cuberite cClientHandle 架构设计）
 *
 * 核心架构定位：
 * 1. 业务层核心实体：内聚玩家状态、身份鉴权、世界传送与心跳保活
 * 2. 实现 network::SessionCallbacks 纯虚接口，作为底层传输层（TcpSession）的直接事件监听器
 * 3. 弱引用关联底层 network::TcpSession，消除循环引用内存泄漏风险（DAG 拓扑）
 * 4. 彻底解耦网络传输层（纯 I/O 与粘包拆包）与 Minecraft 协议业务逻辑
 */
class ClientSession : public network::SessionCallbacks,
                      public std::enable_shared_from_this<ClientSession> {
 public:
  explicit ClientSession(std::shared_ptr<network::TcpSession> tcp_session);
  ~ClientSession() override;

  ClientSession(const ClientSession&) = delete;
  ClientSession& operator=(const ClientSession&) = delete;

  // --- network::SessionCallbacks 纯虚接口实现 ---
  void onPacket(const std::shared_ptr<network::TcpSession>& session, ByteBuffer packet) override;
  void onDisconnected(const std::shared_ptr<network::TcpSession>& session) override;

  // --- 传输层代理与底层网络操作 ---
  void sendPacket(ByteBuffer buffer);
  void stop();
  bool isStopped() const noexcept;
  const std::string& remoteAddress() const noexcept;
  asio::ip::tcp::socket& socket();
  std::shared_ptr<network::TcpSession> tcpSession() const noexcept;

  // --- Minecraft 协议连接状态机 ---
  ConnectionState state() const noexcept { return state_; }
  void setState(ConnectionState new_state) noexcept { state_ = new_state; }

  /// 鉴权成功后流转至 PLAY 阶段并触发玩家进服生命周期（彻底解耦 LoginHandler 与 PlayHandler）
  void startPlay();

  // --- 玩家身份与实体元数据 ---
  const std::string& username() const noexcept { return username_; }
  void setUsername(std::string name) { username_ = std::move(name); }

  const std::string& uuid() const noexcept { return uuid_; }
  void setUuid(std::string uuid) { uuid_ = std::move(uuid); }

  int32_t entityId() const noexcept { return entity_id_; }
  void setEntityId(int32_t id) noexcept { entity_id_ = id; }

  // --- 世界传送位置确认机制 ---
  int32_t nextTeleportId() noexcept {
    pending_teleport_id_ = ++teleport_counter_;
    return pending_teleport_id_;
  }
  int32_t pendingTeleportId() const noexcept { return pending_teleport_id_; }
  void clearPendingTeleportId() noexcept { pending_teleport_id_ = -1; }
  void setPendingTeleportId(int32_t id) noexcept { pending_teleport_id_ = id; }

  // --- 全局会话管理器关联 ---
  void setClientManager(std::weak_ptr<ClientManager> mgr) noexcept {
    client_manager_ = std::move(mgr);
  }
  std::shared_ptr<ClientManager> clientManager() const noexcept;

  // --- 协议多态解析器（对标 Cuberite cProtocol）---
  void setProtocolHandler(std::shared_ptr<ProtocolHandler> handler) noexcept {
    protocol_handler_ = std::move(handler);
  }
  const std::shared_ptr<ProtocolHandler>& protocolHandler() const noexcept {
    return protocol_handler_;
  }

  // --- PLAY 阶段心跳保活组件（对齐 Cuberite cClientHandle 心跳机制）---
  void startKeepAliveTimer();
  void onKeepAliveResponse(int64_t id);

 private:
  void cancelKeepAlive();
  asio::awaitable<void> keepAliveLoop();
  void sendKeepAlive();

 private:
  std::weak_ptr<network::TcpSession> tcp_session_;
  std::weak_ptr<ClientManager> client_manager_;
  std::string remote_address_;
  asio::steady_timer keep_alive_timer_;
  std::shared_ptr<ProtocolHandler> protocol_handler_;

  ConnectionState state_{ConnectionState::HANDSHAKING};
  std::string username_;
  std::string uuid_;
  int32_t entity_id_{0};

  int32_t teleport_counter_{0};
  int32_t pending_teleport_id_{-1};

  int64_t last_keep_alive_id_{0};
  std::chrono::steady_clock::time_point last_alive_time_;
  bool awaiting_keep_alive_{false};
};

}  // namespace mcpe::protocol

#endif  // CLIENTSESSION_H
