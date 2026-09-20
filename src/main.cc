#include <spdlog/spdlog.h>

#include <asio.hpp>

#include "mcpe/network/TcpServer.h"

int
main() {
  spdlog::set_level(spdlog::level::debug);
  spdlog::info("=========================================");
  spdlog::info(" MCPE Server (Protocol 340 / MC 1.12.2) ");
  spdlog::info("=========================================");

  try {
    asio::io_context io_context;

    // 监听 Minecraft 默认端口 25565
    mcpe::network::TcpServer server(io_context, 25565);
    server.start();

    // 阻塞运行事件循环
    io_context.run();
  } catch (const std::exception& e) {
    spdlog::critical("Server fatal error: {}", e.what());
    return 1;
  }

  return 0;
}
