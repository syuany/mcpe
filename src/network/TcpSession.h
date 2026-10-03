#ifndef TCPSESSION_H
#define TCPSESSION_H

#include <asio/as_tuple.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_awaitable.hpp>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>

#include "common/ByteBuffer.h"
#include "protocol/ConnectionState.h"

namespace mcpe::network {

/// 单个客户端 TCP 会话，负责网络 I/O、分包粘包处理与协议状态流转
class TcpSession : public std::enable_shared_from_this<TcpSession> {
 public:
  using PacketHandler = std::function<void(const std::shared_ptr<TcpSession>&, ByteBuffer)>;

  explicit TcpSession(asio::ip::tcp::socket socket);
  ~TcpSession();

  TcpSession(const TcpSession&) = delete;
  TcpSession& operator=(const TcpSession&) = delete;

  void start();
  void stop();

  bool
  isStopped() const noexcept {
    return is_stopped_;
  }
  asio::ip::tcp::socket&
  socket() noexcept {
    return socket_;
  }
  const std::string&
  remoteAddress() const noexcept {
    return remote_address_;
  }

  protocol::ConnectionState
  state() const noexcept {
    return state_;
  }
  void
  setState(protocol::ConnectionState new_state) noexcept {
    state_ = new_state;
  }

  void
  setPacketHandler(PacketHandler handler) noexcept {
    packet_handler_ = std::move(handler);
  }

  /// 线程安全的异步发包（内部经由队列串行化发送）
  void sendPacket(ByteBuffer buffer);

  const std::string&
  username() const noexcept {
    return username_;
  }
  void
  setUsername(std::string name) {
    username_ = std::move(name);
  }

  const std::string&
  uuid() const noexcept {
    return uuid_;
  }
  void
  setUuid(std::string uuid) {
    uuid_ = std::move(uuid);
  }

  int32_t
  entityId() const noexcept {
    return entity_id_;
  }
  void
  setEntityId(int32_t id) noexcept {
    entity_id_ = id;
  }

  int32_t
  nextTeleportId() noexcept {
    pending_teleport_id_ = ++teleport_counter_;
    return pending_teleport_id_;
  }
  int32_t
  pendingTeleportId() const noexcept {
    return pending_teleport_id_;
  }
  void
  clearPendingTeleportId() noexcept {
    pending_teleport_id_ = -1;
  }

  // 启动 10 秒周期性心跳保活协程
  void startKeepAliveTimer();

  // 收到客户端心跳回送
  void onKeepAliveResponse(int64_t id);

 private:
  asio::awaitable<void> readerLoop();
  void doWrite();

  asio::awaitable<void> keepAliveLoop();
  void sendKeepAlive();

 private:
  asio::ip::tcp::socket socket_;
  std::string remote_address_;

  protocol::ConnectionState state_{protocol::ConnectionState::HANDSHAKING};
  ByteBuffer cumulative_buf_;  // 接收端累积缓冲区，用于解决粘包与半包

  std::deque<ByteBuffer> write_queue_;  // 待发送数据包队列
  bool is_writing_{false};              // 是否有正在进行的 async_write，防止并发写入
  bool is_stopped_{false};

  PacketHandler packet_handler_;

  std::string username_;
  std::string uuid_;
  int32_t entity_id_{0};

  int32_t teleport_counter_{0};
  int32_t pending_teleport_id_{-1};

  // 心跳保活组件
  asio::steady_timer keep_alive_timer_;
  int64_t last_keep_alive_id_{0};
  std::chrono::steady_clock::time_point last_alive_time_;
  bool awaiting_keep_alive_{false};
};

}  // namespace mcpe::network

#endif  // TCPSESSION_H