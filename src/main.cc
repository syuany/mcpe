#include <asio.hpp>
#include <spdlog/spdlog.h>

int main() {
    spdlog::info("MCPE Application starting...");
    
    asio::io_context io_context;
    asio::steady_timer timer(io_context, asio::chrono::milliseconds(100));
    timer.async_wait([](const asio::error_code& /*ec*/) {
        spdlog::info("Asio timer triggered successfully!");
    });

    io_context.run();
    spdlog::info("MCPE Application finished.");
    return 0;
}
