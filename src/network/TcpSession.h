#ifndef TCPSESSION_H
#define TCPSESSION_H

#include <array>
#include <asio/as_tuple.hpp>   // 提供 asio::as_tuple 打包机制
#include <asio/awaitable.hpp>  // 提供 asio::awaitable<T> 类型支持
#include <asio/co_spawn.hpp>   // 提供 asio::co_spawn 函数
#include <asio/detached.hpp>   // 提供 asio::detached 完成标记
#include <asio/ip/tcp.hpp>
#include <asio/use_awaitable.hpp>  // 提供 asio::use_awaitable
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>

#include "common/ByteBuffer.h"
#include "protocol/ConnectionState.h"

namespace mcpe::network {

/**
 * @brief 单个客户端 TCP 连接的会话生命周期与协议处理类
 *
 * 继承自 std::enable_shared_from_this<TcpSession>，保证在异步回调执行期间会话对象不被析构。
 * 主要职责包括：
 * 1. 管理底层 TCP Socket 生命周期（连接建立、安全断开与资源清理）
 * 2. 异步数据读取与累积缓冲（解决 TCP 粘包与半包）
 * 3. 维护当前协议状态机的流转（HANDSHAKING -> STATUS / LOGIN -> PLAY）
 * 4. 线程安全的写队列（Write Queue）机制，保障连续并发 sendPacket 时的保序且不触发 Asio 并发写断言
 */
class TcpSession : public std::enable_shared_from_this<TcpSession> {
 public:
  /**
   * @brief 构造函数：接管已连接的 socket
   * @param socket 移动传入的底层客户端 TCP socket
   */
  explicit TcpSession(asio::ip::tcp::socket socket);

  /** @brief 析构函数：记录销毁日志并释放连接资源 */
  ~TcpSession();

  // 禁止拷贝（连接资源独占）
  TcpSession(const TcpSession&) = delete;
  TcpSession& operator=(const TcpSession&) = delete;

  /**
   * @brief 启动会话：开启异步读协程循环（readerLoop）
   */
  void start();

  /**
   * @brief 安全关闭会话并断开底层 TCP 套接字连接（幂等操作）
   */
  void stop();

  /** @brief 查询当前会话是否已经停止 */
  bool
  isStopped() const noexcept {
    return is_stopped_;
  }

  /**
   * @brief 获取底层 Socket 引用
   */
  asio::ip::tcp::socket&
  socket() noexcept {
    return socket_;
  }

  /**
   * @brief 获取远端对端地址描述字符串
   */
  const std::string&
  remoteAddress() const noexcept {
    return remote_address_;
  }

  /**
   * @brief 获取当前协议连接状态
   */
  protocol::ConnectionState
  state() const noexcept {
    return state_;
  }

  /**
   * @brief 更新当前协议连接状态（供 PacketRouter 在状态转换时调用）
   * @param new_state 目标协议状态
   */
  void
  setState(protocol::ConnectionState new_state) noexcept {
    state_ = new_state;
  }

  /**
   * @brief 注册完整数据包到达时的业务处理回调
   */
  using PacketHandler =
      std::function<void(const std::shared_ptr<TcpSession>&, ByteBuffer)>;

  void
  setPacketHandler(PacketHandler handler) noexcept {
    packet_handler_ = std::move(handler);
  }

  /**
   * @brief 线程安全的向客户端异步发送数据包
   *
   * 将数据包放入写缓冲队列 write_queue_。若当前无排队写的任务，则自动触发 doWrite 进行链式发送。
   * 支持多线程或当前协程内连续无阻塞调用。
   *
   * @param buffer 准备发送的完整数据包 ByteBuffer
   */
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

  void startKeepAliveTimer();

  void onKeepAliveResponse(int64_t id);

 private:
  /**
   * @brief 异步读循环驱动函数
   * 从 socket 中读取数据写入临时 buffer，追加到 cumulative_buf_，并通过 PacketFraming 循环切包
   */
  asio::awaitable<void> readerLoop();

  /**
   * @brief 异步写队列链式泵送函数
   * 检查 write_queue_，若有待发送数据则发起 async_write，直到队列清空
   */
  void doWrite();

  asio::ip::tcp::socket socket_;  ///< 客户端底层 TCP 套接字
  std::string remote_address_;    ///< 客户端远端地址（IP:Port）缓存字符串

  protocol::ConnectionState state_{protocol::ConnectionState::HANDSHAKING};  ///< 当前协议状态

  std::array<char, 4096> read_buffer_{};  ///< 每次底层 read_some 的临时缓冲区
  ByteBuffer cumulative_buf_;              ///< 接收端累积缓冲区，用于拼凑半包与处理粘包

  std::deque<ByteBuffer> write_queue_;  ///< 发送写缓冲队列，排队待下发的数据包
  bool is_writing_{false};  ///< 当前是否有尚未完成的 async_write 正在飞行中

  bool is_stopped_{false};  ///< 连接是否已被关闭（防止重复关闭或向已关闭连接写入）
  PacketHandler packet_handler_;  ///< 完整数据包到达处理回调

  std::string username_;
  std::string uuid_;
};

}  // namespace mcpe::network

#endif  // TCPSESSION_H