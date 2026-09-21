#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include <asio.hpp>
#include <memory>
#include <sstream>

#include "mcpe/common/ByteBuffer.h"
#include "mcpe/network/TcpSession.h"
#include "mcpe/protocol/ConnectionState.h"
#include "mcpe/protocol/PacketRouter.h"

using namespace mcpe;
using namespace mcpe::protocol;
using namespace mcpe::network;

namespace {

// 测试夹具：建立本地 Socket 对与 Spdlog 日志拦截
class PacketRouterTest : public ::testing::Test {
 protected:
  asio::io_context io_ctx_;
  std::shared_ptr<TcpSession> session_;
  asio::ip::tcp::socket client_sock_{io_ctx_};

  std::shared_ptr<std::ostringstream> log_stream_;
  spdlog::sink_ptr test_sink_;

  void SetUp() override {
    // 1. 同步建立一对本地相连的 TCP Socket
    asio::ip::tcp::acceptor acceptor(
        io_ctx_,
        asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0)
    );
    client_sock_.connect(acceptor.local_endpoint());
    asio::ip::tcp::socket peer = acceptor.accept();

    session_ = std::make_shared<TcpSession>(std::move(peer));

    // 2. 挂载 ostream sink 捕获 spdlog 输出
    log_stream_ = std::make_shared<std::ostringstream>();
    test_sink_ = std::make_shared<spdlog::sinks::ostream_sink_mt>(*log_stream_);
    spdlog::default_logger()->sinks().push_back(test_sink_);
    spdlog::set_level(spdlog::level::trace);
  }

  void TearDown() override {
    if (session_) {
      session_->stop();
    }
    asio::error_code ec;
    client_sock_.close(ec);

    // 移除测试 sink
    auto& sinks = spdlog::default_logger()->sinks();
    auto it = std::find(sinks.begin(), sinks.end(), test_sink_);
    if (it != sinks.end()) {
      sinks.erase(it);
    }
  }

  // 构造带有 VarInt Packet ID 的测试包
  ByteBuffer makePacket(int32_t packet_id, const std::string& dummy_payload = "") {
    ByteBuffer buf;
    buf.appendVarInt(packet_id);
    if (!dummy_payload.empty()) {
      buf.append(dummy_payload.data(), dummy_payload.size());
    }
    return buf;
  }
};

}  // namespace

// ===========================================================================
// 测试集 1：恶意/非法包与非法状态切断连接验证 (DoD)
// ===========================================================================

// 1. 畸形数据包（无法读取 Packet ID）-> 切断连接并打印 warn 日志
TEST_F(PacketRouterTest, MalformedPacketDisconnects) {
  ByteBuffer empty_buf;
  PacketRouter::dispatch(session_, ConnectionState::HANDSHAKING, std::move(empty_buf));

  EXPECT_FALSE(session_->socket().is_open()) << "Malformed packet must close socket";
  EXPECT_TRUE(session_->isStopped());
  EXPECT_NE(log_stream_->str().find("[warning]"), std::string::npos) << "Warn log expected";
}

// 2. HANDSHAKING 状态下收到非 0x00 非法包（如 0x01）-> 切断连接并打印 warn
TEST_F(PacketRouterTest, IllegalPacketInHandshakingDisconnects) {
  auto packet = makePacket(0x01);
  PacketRouter::dispatch(session_, ConnectionState::HANDSHAKING, std::move(packet));

  EXPECT_FALSE(session_->socket().is_open()) << "Illegal packet in HANDSHAKING must disconnect";
  EXPECT_TRUE(session_->isStopped());
  EXPECT_NE(log_stream_->str().find("[warning]"), std::string::npos);
  EXPECT_NE(log_stream_->str().find("Unknown packet in HANDSHAKING"), std::string::npos);
}

// 3. STATUS 状态下收到未知包（如 0x02，仅允许 0x00 与 0x01）-> 切断连接并打印 warn
TEST_F(PacketRouterTest, IllegalPacketInStatusDisconnects) {
  auto packet = makePacket(0x02);
  PacketRouter::dispatch(session_, ConnectionState::STATUS, std::move(packet));

  EXPECT_FALSE(session_->socket().is_open()) << "Illegal packet in STATUS must disconnect";
  EXPECT_TRUE(session_->isStopped());
  EXPECT_NE(log_stream_->str().find("[warning]"), std::string::npos);
  EXPECT_NE(log_stream_->str().find("Unknown packet in STATUS"), std::string::npos);
}

// 4. LOGIN 状态下收到未处理包 -> 切断连接并打印 warn
TEST_F(PacketRouterTest, IllegalPacketInLoginDisconnects) {
  auto packet = makePacket(0x00);
  PacketRouter::dispatch(session_, ConnectionState::LOGIN, std::move(packet));

  EXPECT_FALSE(session_->socket().is_open()) << "Unhandled packet in LOGIN must disconnect";
  EXPECT_TRUE(session_->isStopped());
  EXPECT_NE(log_stream_->str().find("[warning]"), std::string::npos);
}

// 5. PLAY 状态下收到非白名单未知包（如 0xFF）-> 切断连接并打印 warn
TEST_F(PacketRouterTest, UnhandledPlayPacketDisconnects) {
  auto packet = makePacket(0xFF);
  PacketRouter::dispatch(session_, ConnectionState::PLAY, std::move(packet));

  EXPECT_FALSE(session_->socket().is_open()) << "Unhandled PLAY packet must disconnect";
  EXPECT_TRUE(session_->isStopped());
  EXPECT_NE(log_stream_->str().find("[warning]"), std::string::npos);
  EXPECT_NE(log_stream_->str().find("Unhandled PLAY packet 0xFF"), std::string::npos);
}

// 6. 非法状态枚举转移 -> 切断连接并打印 warn
TEST_F(PacketRouterTest, InvalidConnectionStateDisconnects) {
  auto packet = makePacket(0x00);
  PacketRouter::dispatch(session_, static_cast<ConnectionState>(99), std::move(packet));

  EXPECT_FALSE(session_->socket().is_open());
  EXPECT_TRUE(session_->isStopped());
  EXPECT_NE(log_stream_->str().find("[warning]"), std::string::npos);
}

// ===========================================================================
// 测试集 2：1.12.2 例行包白名单测试（参数化覆盖 8 个例行包）
// ===========================================================================

class RoutinePacketTest : public PacketRouterTest, public ::testing::WithParamInterface<int32_t> {};

INSTANTIATE_TEST_SUITE_P(
    WhitelistRoutinePackets,
    RoutinePacketTest,
    ::testing::Values(
        0x03,  // Client Status
        0x04,  // Client Settings
        0x05,  // Confirm Transaction
        0x08,  // Close Window
        0x09,  // Plugin Message
        0x13,  // Player Abilities
        0x18,  // Resource Pack Status
        0x1A   // Held Item Change
    )
);

TEST_P(RoutinePacketTest, WhitelistPacketShouldNotDisconnect) {
  int32_t packet_id = GetParam();
  auto packet = makePacket(packet_id, "mock_dummy_payload");

  PacketRouter::dispatch(session_, ConnectionState::PLAY, std::move(packet));

  // DoD 验收：白名单例行包绝不能被踢下线！
  EXPECT_TRUE(session_->socket().is_open())
      << "Routine packet 0x" << std::hex << packet_id << " should NOT disconnect client!";
  EXPECT_FALSE(session_->isStopped());

  // 白名单包绝不能产生 warn 警告日志
  EXPECT_EQ(log_stream_->str().find("[warning]"), std::string::npos)
      << "Routine packet 0x" << std::hex << packet_id << " should not trigger warn logs!";
}

// ===========================================================================
// 测试集 3：合法包正常路由分发
// ===========================================================================

// HANDSHAKING 状态收到合法 0x00 Handshake，成功流转到 STATUS
TEST_F(PacketRouterTest, LegalHandshakeTransitionToStatus) {
  ByteBuffer packet;
  packet.appendVarInt(0x00);         // Packet ID = 0x00 (Handshake)
  packet.appendVarInt(340);          // Protocol Version = 340 (1.12.2)
  packet.appendString("127.0.0.1");  // Server Address
  packet.writeBe<uint16_t>(25565);   // Server Port
  packet.appendVarInt(1);            // Next State = 1 (STATUS)

  PacketRouter::dispatch(session_, ConnectionState::HANDSHAKING, std::move(packet));

  EXPECT_TRUE(session_->socket().is_open());
  EXPECT_FALSE(session_->isStopped());
  EXPECT_EQ(session_->state(), ConnectionState::STATUS);
}

// STATUS 状态收到合法 0x00 StatusRequest，连接保持正常
TEST_F(PacketRouterTest, LegalStatusRequestRoutesSuccessfully) {
  auto packet = makePacket(0x00);
  PacketRouter::dispatch(session_, ConnectionState::STATUS, std::move(packet));

  EXPECT_TRUE(session_->socket().is_open());
  EXPECT_FALSE(session_->isStopped());
}

// STATUS 状态收到合法 0x01 Ping，连接保持正常
TEST_F(PacketRouterTest, LegalPingRoutesSuccessfully) {
  ByteBuffer packet;
  packet.appendVarInt(0x01);
  packet.writeBe<int64_t>(123456789LL);  // 客户端时间戳

  PacketRouter::dispatch(session_, ConnectionState::STATUS, std::move(packet));

  EXPECT_TRUE(session_->socket().is_open());
  EXPECT_FALSE(session_->isStopped());
}
