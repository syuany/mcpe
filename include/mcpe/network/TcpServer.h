#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <netdb.h>

#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <cstdint>

#include "mcpe/network/TcpSession.h"

namespace mcpe::network {

/**
 * @brief 高性能异步 TCP 服务器
 *
 * 基于 Asio 实现的非阻塞网络服务器骨架，负责：
 * 1. 绑定并监听调用方指定的端口
 * 2. 持续异步接收客户端 TCP 连接请求（accept）
 * 3. 为新连接配置 TCP 参数（禁用 Nagle 算法降低延迟）并封装为独立的 TcpSession 生命周期对象
 */
class TcpServer {
 public:
  using PacketHandler = TcpSession::PacketHandler;

  /**
   * @brief 构造函数：初始化服务器监听器
   * @param io_context Asio 事件循环上下文引用
   * @param port 监听的 TCP 端口号
   */
  TcpServer(asio::io_context& io_context, uint16_t port);

  /** @brief 默认析构函数 */
  ~TcpServer() = default;

  /**
   * @brief 设置全局数据包到达回调函数
   * 当新连接建立并切出完整数据包时，将自动调用此处理器
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
  PacketHandler packet_handler_;      ///< 业务层数据包处理回调
};

}  // namespace mcpe::network

#endif  // TCPSERVER_H