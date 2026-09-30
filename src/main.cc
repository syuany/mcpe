/**
 * @file main.cc
 * @brief MCPE (Minecraft Pocket / Java Edition) 服务端主程序入口
 *
 * 负责启动 Asio 事件循环，在 0.0.0.0:25566 监听原版 Minecraft 1.12.2 (Protocol 340) 客户端连入。
 */

#include <spdlog/spdlog.h>

#include <asio.hpp>
#include <cstdint>

#include "network/TcpServer.h"
#include "protocol/PacketRouter.h"

namespace {
constexpr uint16_t kServerPort = 25566;
}

int
main() {
  // 设置全局日志输出级别为 DEBUG，便于开发与调试观测
  spdlog::set_level(spdlog::level::debug);
  spdlog::info("=========================================");
  spdlog::info(" MCPE Server (Protocol 340 / MC 1.12.2) ");
  spdlog::info("=========================================");

  try {
    // 1. 初始化核心 Asio IO 上下文
    asio::io_context io_context;

    // 2. 使用独立端口，避免与 Cuberite 的 25565 冲突
    mcpe::network::TcpServer server(io_context, kServerPort);

    // 注入协议层路由器回调：保持网络层与业务协议层的低耦合
    server.setPacketHandler([](const std::shared_ptr<mcpe::network::TcpSession>& session,
                               mcpe::ByteBuffer packet) {
      mcpe::protocol::PacketRouter::dispatch(session, session->state(), std::move(packet));
    });

    server.start();

    // 3. 阻塞主线程，运行异步网络事件循环
    io_context.run();
  } catch (const std::exception& e) {
    spdlog::critical("Server fatal error: {}", e.what());
    return 1;
  }

  return 0;
}
