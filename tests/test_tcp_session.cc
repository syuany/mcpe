#include <gtest/gtest.h>

#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "common/ByteBuffer.h"
#include "network/SessionCallbacks.h"
#include "network/TcpSession.h"

using mcpe::ByteBuffer;
using mcpe::network::TcpSession;

namespace {

// 辅助结构：建立一对本地相连的 TCP Session 与 Client Socket
struct TestTcpFixture {
  asio::io_context io_context;
  asio::executor_work_guard<asio::io_context::executor_type> work_guard;
  asio::ip::tcp::acceptor acceptor;
  asio::ip::tcp::socket client_socket;
  std::shared_ptr<TcpSession> session;
  std::jthread io_thread;

  TestTcpFixture()
      : work_guard(asio::make_work_guard(io_context)),
        acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0)),
        client_socket(io_context) {
    auto endpoint = acceptor.local_endpoint();
    std::atomic<bool> connected{false};

    acceptor.async_accept([this,
                           &connected](const asio::error_code& ec, asio::ip::tcp::socket peer) {
      if (!ec) {
        session = std::make_shared<TcpSession>(std::move(peer));
        session->start();
      }
      connected = true;
    });

    client_socket.async_connect(endpoint, [](const asio::error_code& ec) {
      EXPECT_FALSE(ec) << "Connect failed: " << ec.message();
    });

    io_thread = std::jthread([this]() {
      io_context.run();
    });

    // 等待连接建立完成
    auto start_time = std::chrono::steady_clock::now();
    while (!connected && std::chrono::steady_clock::now() - start_time < std::chrono::seconds(2)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  ~TestTcpFixture() {
    if (session) {
      session->stop();
    }
    asio::error_code ec;
    client_socket.close(ec);
    work_guard.reset();
    io_context.stop();
    if (io_thread.joinable()) {
      io_thread.join();
    }
  }
};

}  // namespace

// ---------------------------------------------------------------------------
// DoD 验证：微秒级连续调用 100 次 send_packet，保序、完整且无 Asio 异常
// ---------------------------------------------------------------------------
TEST(TcpSessionTest, Send100PacketsSequentialAndSafe) {
  TestTcpFixture fixture;
  ASSERT_TRUE(fixture.session != nullptr);

  constexpr int kPacketCount = 100;
  constexpr size_t kPacketSize = sizeof(int32_t);  // 每个包存放 4 字节 int32_t 序号

  // 1. 预先构建 100 个带有递增序号的数据包
  std::vector<ByteBuffer> packets;
  packets.reserve(kPacketCount);
  for (int32_t i = 0; i < kPacketCount; ++i) {
    ByteBuffer buf;
    buf.writeBe<int32_t>(i);
    packets.push_back(std::move(buf));
  }

  // 2. 模拟在同一微秒内高频连续调用 send_packet
  // 紧凑循环连续触发，测试 write_queue 与 is_writing_ 保护机制
  for (int i = 0; i < kPacketCount; ++i) {
    fixture.session->sendPacket(std::move(packets[i]));
  }

  // 3. 对端（client_socket）读取完整的 100 个包的数据（共 400 字节）
  std::vector<uint8_t> recv_data(kPacketCount * kPacketSize);
  asio::error_code ec;
  size_t bytes_read = asio::read(fixture.client_socket, asio::buffer(recv_data), ec);

  ASSERT_FALSE(ec) << "Read failed: " << ec.message();
  ASSERT_EQ(bytes_read, kPacketCount * kPacketSize);

  // 4. 严格校验按序到达与数据完整性
  ByteBuffer recv_buf;
  recv_buf.append(recv_data.data(), recv_data.size());

  for (int32_t i = 0; i < kPacketCount; ++i) {
    auto val = recv_buf.readBe<int32_t>();
    ASSERT_TRUE(val.has_value()) << "Failed to read packet at index " << i;
    EXPECT_EQ(*val, i) << "Packet out of order or corrupted at index " << i;
  }
}

// ---------------------------------------------------------------------------
// 多线程并发高压测试：多个工作线程同时轰炸 send_packet，保证无 Asio 内部断言异常
// ---------------------------------------------------------------------------
TEST(TcpSessionTest, MultiThreadedConcurrentSendNoAssertion) {
  TestTcpFixture fixture;
  ASSERT_TRUE(fixture.session != nullptr);

  constexpr int kThreadCount = 4;
  constexpr int kPacketsPerThread = 25;
  constexpr int kTotalPackets = kThreadCount * kPacketsPerThread;
  constexpr size_t kPacketSize = sizeof(int32_t);

  std::atomic<bool> start_flag{false};
  std::vector<std::jthread> threads;
  threads.reserve(kThreadCount);

  for (int t = 0; t < kThreadCount; ++t) {
    threads.emplace_back([&, t]() {
      while (!start_flag.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      for (int i = 0; i < kPacketsPerThread; ++i) {
        ByteBuffer buf;
        buf.writeBe<int32_t>(t * 1000 + i);
        fixture.session->sendPacket(std::move(buf));
      }
    });
  }

  // 释放栅栏，所有线程在同一瞬间同时下发数据包
  start_flag.store(true, std::memory_order_release);
  for (auto& th : threads) {
    if (th.joinable()) th.join();
  }

  // 接收端读取全部 100 个包（400 字节）
  std::vector<uint8_t> recv_data(kTotalPackets * kPacketSize);
  asio::error_code ec;
  size_t bytes_read = asio::read(fixture.client_socket, asio::buffer(recv_data), ec);

  ASSERT_FALSE(ec) << "Read failed: " << ec.message();
  ASSERT_EQ(bytes_read, kTotalPackets * kPacketSize);
}

// ---------------------------------------------------------------------------
// 验证 SessionCallbacks 纯虚接口事件调度（对齐 Cuberite cCallbacks 架构）
// ---------------------------------------------------------------------------
class MockSessionCallbacks : public mcpe::network::SessionCallbacks {
 public:
  std::atomic<int> packet_count{0};
  std::atomic<bool> disconnected{false};
  std::vector<int32_t> received_values;
  std::mutex mutex;

  void
  onPacket(const std::shared_ptr<TcpSession>&, ByteBuffer packet) override {
    auto id = packet.readVarInt();
    if (id) {
      std::lock_guard<std::mutex> lock(mutex);
      received_values.push_back(*id);
    }
    packet_count++;
  }

  void
  onDisconnected(const std::shared_ptr<TcpSession>&) override {
    disconnected = true;
  }
};

TEST(TcpSessionTest, SessionCallbacksDispatchAndLifecycle) {
  TestTcpFixture fixture;
  ASSERT_TRUE(fixture.session != nullptr);

  auto callbacks = std::make_shared<MockSessionCallbacks>();
  fixture.session->setCallbacks(callbacks);

  // 客户端向服务端发送 2 个包：
  // 包 1：Length=1, ID=0x01
  // 包 2：Length=1, ID=0x02
  ByteBuffer send_buf;
  send_buf.appendVarInt(1);
  send_buf.appendVarInt(0x01);
  send_buf.appendVarInt(1);
  send_buf.appendVarInt(0x02);

  asio::error_code ec;
  asio::write(fixture.client_socket, asio::buffer(send_buf.peek(), send_buf.readableBytes()), ec);
  ASSERT_FALSE(ec);

  // 等待 readerLoop 异步收包并触发 onPacket 回调
  auto start_time = std::chrono::steady_clock::now();
  while (callbacks->packet_count < 2 &&
         std::chrono::steady_clock::now() - start_time < std::chrono::seconds(2)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }

  EXPECT_EQ(callbacks->packet_count.load(), 2);
  {
    std::lock_guard<std::mutex> lock(callbacks->mutex);
    ASSERT_EQ(callbacks->received_values.size(), 2);
    EXPECT_EQ(callbacks->received_values[0], 0x01);
    EXPECT_EQ(callbacks->received_values[1], 0x02);
  }

  // 停止连接，验证触发 onDisconnected 回调
  fixture.session->stop();
  EXPECT_TRUE(callbacks->disconnected.load());
}
