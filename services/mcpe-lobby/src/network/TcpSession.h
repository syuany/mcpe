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
#include "network/SessionCallbacks.h"

namespace mcpe::network {

/**
 * @brief 纯粹的传输层单个客户端 TCP 会话（严格对齐 Cuberite cTCPLink 架构设计）
 *
 * 职责彻底归位：
 * 1. 管理底层 TCP Socket 读写生命周期
 * 2. 累积接收流并驱动 PacketFraming 完成数据包边界切分
 * 3. 维护待发送 ByteBuffer 队列并保证 Asio async_write 串行化安全
 * 4. 触发 SessionCallbacks 抽象事件回调（完全脱离任何 Minecraft 协议与玩家状态业务）
 */
class TcpSession : public std::enable_shared_from_this<TcpSession> {
 public:
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

  /// 设置事件回调监听器（对齐 Cuberite cTCPLink::cCallbacks 纯虚接口架构设计）
  void
  setCallbacks(std::shared_ptr<SessionCallbacks> callbacks) noexcept {
    callbacks_ = std::move(callbacks);
  }

  const std::shared_ptr<SessionCallbacks>&
  callbacks() const noexcept {
    return callbacks_;
  }

  /// 线程安全的异步发包（内部经由队列串行化发送）
  void sendPacket(ByteBuffer buffer);

 private:
  asio::awaitable<void> readerLoop();
  void doWrite();

 private:
  asio::ip::tcp::socket socket_;
  std::string remote_address_;

  ByteBuffer cumulative_buf_;           // 接收端累积缓冲区，用于解决粘包与半包
  std::deque<ByteBuffer> write_queue_;  // 待发送数据包队列
  bool is_writing_{false};              // 是否有正在进行的 async_write，防止并发写入
  bool is_stopped_{false};

  std::shared_ptr<SessionCallbacks> callbacks_;
};

}  // namespace mcpe::network

#endif  // TCPSESSION_H