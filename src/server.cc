/**
 * @file server.cc
 * @brief MCPE (Minecraft Pocket / Java Edition) 服务端主程序入口
 *
 * 负责启动 Asio 事件循环，在 0.0.0.0:25566 监听原版 Minecraft 1.12.2 (Protocol 340) 客户端连入。
 */

#include <spdlog/spdlog.h>

#include <asio.hpp>
#include <cstdint>
#include <memory>

#include "network/TcpServer.h"
#include "protocol/ClientSession.h"

namespace {
constexpr uint16_t kServerPort = 25566;
}

using namespace mcpe;
using namespace mcpe::network;
using namespace mcpe::protocol;

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
    TcpServer server(io_context, kServerPort);

    // 对齐 Cuberite 架构设计：通过 SessionFactory 为每个连入的 TcpSession 创建独立的 ClientSession
    // 彻底解耦网络传输层（TcpSession / cTCPLink）与 Minecraft 协议业务层（ClientSession / cClientHandle）
    server.setSessionFactory([](const std::shared_ptr<TcpSession>& tcp) {
      return std::make_shared<ClientSession>(tcp);
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
