#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <cstdint>
#include <functional>
#include <memory>

#include "network/SessionCallbacks.h"
#include "network/TcpSession.h"

namespace mcpe::network {

/**
 * @brief 高性能异步 TCP 服务器
 *
 * 基于 Asio 实现的非阻塞网络服务器骨架，负责：
 * 1. 绑定并监听调用方指定的端口
 * 2. 持续异步接收客户端 TCP 连接请求（accept）
 * 3. 为新连接配置 TCP 参数（禁用 Nagle 算法降低延迟）并封装为独立的 TcpSession 生命周期对象
 * 4. 支持连接会话工厂（SessionFactory），新连接连入时自动挂接协议业务层（如 ClientSession）
 */
class TcpServer {
 public:
  using PacketHandler = TcpSession::PacketHandler;
  using SessionFactory =
      std::function<std::shared_ptr<SessionCallbacks>(const std::shared_ptr<TcpSession>&)>;

  /**
   * @brief 构造函数：初始化服务器监听器
   * @param io_context Asio 事件循环上下文引用
   * @param port 监听的 TCP 端口号
   */
  TcpServer(asio::io_context& io_context, uint16_t port);

  /** @brief 默认析构函数 */
  ~TcpServer() = default;

  /**
   * @brief 设置连接会话工厂（对齐 Cuberite cServer::OnIncomingConnection 架构设计）
   * @param factory 每当新 TCP 连接到来时被调用，用于创建并绑定专属的业务会话实例（如 ClientSession）
   */
  void
  setSessionFactory(SessionFactory factory) noexcept {
    session_factory_ = std::move(factory);
  }

  const SessionFactory&
  sessionFactory() const noexcept {
    return session_factory_;
  }

  /**
   * @brief 设置全局会话事件监听接口（纯虚接口架构设计）
   * @param callbacks 纯虚事件回调接口指针
   */
  void
  setSessionCallbacks(std::shared_ptr<SessionCallbacks> callbacks) noexcept {
    callbacks_ = std::move(callbacks);
  }

  const std::shared_ptr<SessionCallbacks>&
  sessionCallbacks() const noexcept {
    return callbacks_;
  }

  /**
   * @brief 设置全局数据包到达回调函数（向后兼容）
   * @param handler 数据包处理回调
   */
  void
  setPacketHandler(PacketHandler handler) noexcept {
    packet_handler_ = std::move(handler);
  }

  /**
   * @brief 启动服务器监听
   * 将运行标志置为 true，并注册第一个异步 accept 操作
   */
  void start();

  /**
   * @brief 停止服务器
   * 将运行标志置为 false，并安全关闭底层 acceptor 套接字
   */
  void stop();

 private:
  /**
   * @brief 异步接收客户端连接循环
   * 在事件循环中链式触发 async_accept，实现无阻塞持续接收新连接
   */
  asio::awaitable<void> listenLoop();

  asio::io_context& io_context_;      ///< 关联的 Asio IO 上下文
  asio::ip::tcp::acceptor acceptor_;  ///< 底层 TCP 接受器
  bool is_running_{false};            ///< 服务器运行状态标志
  SessionFactory session_factory_;    ///< 连接会话工厂
  std::shared_ptr<SessionCallbacks> callbacks_;  ///< 会话抽象事件监听器
  PacketHandler packet_handler_;      ///< 业务层数据包处理回调
};

}  // namespace mcpe::network

#endif  // TCPSERVER_H