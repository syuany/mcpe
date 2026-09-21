#ifndef TCPSESSION_H
#define TCPSESSION_H

#include <array>
#include <asio/ip/tcp.hpp>
#include <deque>
#include <functional>
#include <memory>
#include <string>

#include "mcpe/common/ByteBuffer.h"
#include "mcpe/protocol/ConnectionState.h"

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

  // 禁止拷贝与赋值（单连接独占所有权）
  TcpSession(const TcpSession&) = delete;
  TcpSession& operator=(const TcpSession&) = delete;

  /**
   * @brief 启动会话
   * 触发第一次底层 async_read_some 异步读取
   */
  void start();

  /**
   * @brief 幂等且安全地关闭连接
   * 标记 is_stopped_ 为 true，并关闭底层 socket，取消所有待处理的异步操作
   */
  void stop();

  /** @brief 获取底层 socket 引用（供测试或低级操作使用） */
  asio::ip::tcp::socket&
  socket() noexcept {
    return socket_;
  }

  /** @brief 获取对端远端 IP:Port 字符串（格式如 "127.0.0.1:54321"） */
  const std::string&
  remoteAddress() const noexcept {
    return remote_address_;
  }

  /** @brief 获取当前连接所处的 Minecraft 协议状态 */
  protocol::ConnectionState
  state() const noexcept {
    return state_;
  }

  /** @brief 设置当前连接的 Minecraft 协议状态（原子状态机流转） */
  void
  setState(protocol::ConnectionState s) noexcept {
    state_ = s;
  }

  using PacketHandler = std::function<void(const std::shared_ptr<TcpSession>&, ByteBuffer)>;

  /** @brief 查询当前会话是否已经停止 */
  bool
  isStopped() const noexcept {
    return is_stopped_;
  }

  /**
   * @brief 设置完整数据包处理回调
   * @param handler 数据包回调处理器
   */
  void
  setPacketHandler(PacketHandler handler) noexcept {
    packet_handler_ = std::move(handler);
  }

  /**
   * @brief 并发安全下发数据包
   *
   * 可在任意线程并发调用。内部通过 asio::post 将写入请求投递至 Session 绑定的 strand/executor，
   * 并利用 write_queue_ 排队发送，避免 Asio 并发 async_write 触发崩溃。
   *
   * @param buffer 待发送的数据包（要求已包含包长前缀及 Packet ID）
   */
  void sendPacket(ByteBuffer buffer);

 private:
  /**
   * @brief 异步读循环驱动函数
   * 从 socket 中读取数据写入临时 buffer，追加到 cumulative_buf_，并通过 PacketFraming 循环切包
   */
  void doRead();

  /**
   * @brief 异步写队列链式泵送函数
   * 检查 write_queue_，若有待发送数据则发起 async_write，直到队列清空
   */
  void doWrite();

  asio::ip::tcp::socket socket_;  ///< 客户端底层 TCP 套接字
  std::string remote_address_;    ///< 客户端远端地址（IP:Port）缓存字符串

  protocol::ConnectionState state_{protocol::ConnectionState::HANDSHAKING};  ///< 当前协议状态

  std::array<char, 4096> read_buffer_;  ///< 每次底层 read_some 的临时栈/静态内存缓冲区

  ByteBuffer cumulative_buf_;  ///< 接收端累积缓冲区，用于拼凑半包与处理粘包

  std::deque<ByteBuffer> write_queue_;  ///< 发送写缓冲队列，排队待下发的数据包
  bool is_writing_{false};  ///< 当前是否有尚未完成的 async_write 正在飞行中

  bool is_stopped_{false};  ///< 连接是否已被关闭（防止重复关闭或向已关闭连接写入）
  PacketHandler packet_handler_;  ///< 完整数据包到达处理回调
};

}  // namespace mcpe::network

#endif  // TCPSESSION_H