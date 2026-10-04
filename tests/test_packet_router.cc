/**
 * @file test_packet_router.cc
 * @brief 协议分发路由器（PacketRouter）与状态机业务集成测试
 *
 * 覆盖测试集包括：
 * 1. 恶意畸形包与非法状态转移切断连接防御验证 (DoD)
 * 2. 1.12.2 客户端进服例行包白名单容忍测试 (参数化覆盖 8 类例行包)
 * 3. 合法数据包流转与业务处理测试：
 *    - TASK-1.6: Handshake (0x00) 解析与状态转移至 STATUS
 *    - TASK-1.7: Status Request (0x00) 状态查询与 MOTD JSON 响应校验
 *    - TASK-1.8: Ping (0x01) 与 Pong (0x01) 8 字节大端时间戳回显及边界测试
 *    - 完整 Server List Ping 协议握手生命周期闭环测试
 */

#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <asio.hpp>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "common/ByteBuffer.h"
#include "network/PacketFraming.h"
#include "network/TcpSession.h"
#include "protocol/ClientSession.h"
#include "protocol/ConnectionState.h"
#include "protocol/PacketRouter.h"
#include "protocol/StatusHandler.h"

using namespace mcpe;
using namespace mcpe::protocol;
using namespace mcpe::network;

namespace {

// 测试夹具：建立本地 Socket 对与 Spdlog 日志拦截
class PacketRouterTest : public ::testing::Test {
 protected:
  asio::io_context io_ctx_;
  asio::executor_work_guard<asio::io_context::executor_type> work_guard_{
      asio::make_work_guard(io_ctx_)
  };
  std::shared_ptr<TcpSession> tcp_session_;
  std::shared_ptr<ClientSession> session_;
  asio::ip::tcp::socket client_sock_{io_ctx_};
  std::jthread io_thread_;

  ByteBuffer client_recv_buf_;

  std::shared_ptr<std::ostringstream> log_stream_;
  spdlog::sink_ptr test_sink_;

  void
  SetUp() override {
    // 1. 同步建立一对本地相连的 TCP Socket
    asio::ip::tcp::acceptor acceptor(
        io_ctx_, asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0)
    );
    client_sock_.connect(acceptor.local_endpoint());
    asio::ip::tcp::socket peer = acceptor.accept();

    tcp_session_ = std::make_shared<TcpSession>(std::move(peer));
    session_ = std::make_shared<ClientSession>(tcp_session_);
    tcp_session_->setCallbacks(session_);

    // 启动后台 IO 线程处理异步写
    io_thread_ = std::jthread([this]() { io_ctx_.run(); });

    client_recv_buf_.retrieveAll();

    // 2. 挂载 ostream sink 捕获 spdlog 输出
    log_stream_ = std::make_shared<std::ostringstream>();
    test_sink_ = std::make_shared<spdlog::sinks::ostream_sink_mt>(*log_stream_);
    spdlog::default_logger()->sinks().push_back(test_sink_);
    spdlog::set_level(spdlog::level::trace);
  }

  void
  TearDown() override {
    if (session_) {
      session_->stop();
    }
    asio::error_code ec;
    client_sock_.close(ec);

    work_guard_.reset();
    io_ctx_.stop();
    if (io_thread_.joinable()) {
      io_thread_.join();
    }

    // 移除测试 sink
    auto& sinks = spdlog::default_logger()->sinks();
    auto it = std::find(sinks.begin(), sinks.end(), test_sink_);
    if (it != sinks.end()) {
      sinks.erase(it);
    }
  }

  // 构造带有 VarInt Packet ID 的测试包
  ByteBuffer
  makePacket(int32_t packet_id, const std::string& dummy_payload = "") {
    ByteBuffer buf;
    buf.appendVarInt(packet_id);
    if (!dummy_payload.empty()) {
      buf.append(dummy_payload.data(), dummy_payload.size());
    }
    return buf;
  }

  // 从对端（client_sock_）接收并解包一个 Minecraft 数据包
  std::optional<ByteBuffer>
  receivePacket(std::chrono::milliseconds timeout = std::chrono::milliseconds(1000)) {
    auto start_time = std::chrono::steady_clock::now();
    std::array<char, 1024> chunk{};

    while (std::chrono::steady_clock::now() - start_time < timeout) {
      auto slice_res = network::PacketFraming::slicePacket(client_recv_buf_);
      if (slice_res) {
        return std::move(*slice_res);
      }

      asio::error_code ec;
      size_t bytes_avail = client_sock_.available(ec);
      if (ec) {
        break;
      }
      if (bytes_avail > 0) {
        size_t to_read = std::min(bytes_avail, chunk.size());
        size_t n = client_sock_.read_some(asio::buffer(chunk.data(), to_read), ec);
        if (!ec && n > 0) {
          client_recv_buf_.append(chunk.data(), n);
          auto slice_res2 = network::PacketFraming::slicePacket(client_recv_buf_);
          if (slice_res2) {
            return std::move(*slice_res2);
          }
        }
      } else {
        if (session_ && session_->isStopped()) {
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
    }

    auto slice_res = network::PacketFraming::slicePacket(client_recv_buf_);
    if (slice_res) {
      return std::move(*slice_res);
    }
    return std::nullopt;
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
// 测试集 3：合法包正常路由分发 (TASK-1.6, TASK-1.7, TASK-1.8)
// ===========================================================================

// [TASK-1.6] HANDSHAKING 状态收到合法 0x00 Handshake，成功流转到 STATUS
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

// ---------------------------------------------------------------------------
// [TASK-1.7] Status Request (0x00) 与 JSON 描述响应测试
// ---------------------------------------------------------------------------

// STATUS 状态收到合法 0x00 StatusRequest，成功下发包含 MOTD 的 JSON 状态响应
TEST_F(PacketRouterTest, LegalStatusRequestRoutesSuccessfully) {
  // 1. Arrange: 构造无载荷的 Status Request 报文 (ID = 0x00)
  auto packet = makePacket(0x00);

  // 2. Act: 分发报文
  PacketRouter::dispatch(session_, ConnectionState::STATUS, std::move(packet));

  // 3. Assert: 验证网络层连接状态良好
  EXPECT_TRUE(session_->socket().is_open());
  EXPECT_FALSE(session_->isStopped());

  // 4. DoD 验证: 客户端接收并校验 Status Response 响应包
  auto resp = receivePacket();
  ASSERT_TRUE(resp.has_value()) << "Client did not receive Status Response packet";

  // 校验 Packet ID = 0x00 (Status Response)
  auto id_res = resp->readVarInt();
  ASSERT_TRUE(id_res.has_value());
  EXPECT_EQ(*id_res, 0x00) << "Expected Status Response Packet ID 0x00";

  // 校验 JSON Payload (String: 带 VarInt 长度前缀)
  auto json_res = resp->readString();
  ASSERT_TRUE(json_res.has_value()) << "Failed to parse JSON string from packet";
  EXPECT_EQ(resp->readableBytes(), 0) << "Packet has trailing garbage bytes";

  const auto& json_str = *json_res;
  // 校验关键字段：版本信息必须严格匹配 1.12.2 与 protocol 340
  EXPECT_NE(json_str.find(R"("version")"), std::string::npos);
  EXPECT_NE(json_str.find(R"("name":"1.12.2")"), std::string::npos);
  EXPECT_NE(json_str.find(R"("protocol":340)"), std::string::npos);

  // 校验玩家人数
  EXPECT_NE(json_str.find(R"("players")"), std::string::npos);
  EXPECT_NE(json_str.find(R"("max":100)"), std::string::npos);
  EXPECT_NE(json_str.find(R"("online":0)"), std::string::npos);

  // 校验 MOTD 标语与颜色代码
  EXPECT_NE(json_str.find(R"("description")"), std::string::npos);
  EXPECT_NE(json_str.find(R"("text")"), std::string::npos);

  // 无告警或错误日志
  EXPECT_EQ(log_stream_->str().find("[warning]"), std::string::npos);
  EXPECT_EQ(log_stream_->str().find("[error]"), std::string::npos);
}

// 直接调用 StatusHandler::handleStatusRequest 单元测试
TEST_F(PacketRouterTest, StatusHandlerDirectInvocation) {
  StatusHandler::handleStatusRequest(session_);

  auto resp = receivePacket();
  ASSERT_TRUE(resp.has_value());

  auto id_res = resp->readVarInt();
  ASSERT_TRUE(id_res.has_value());
  EXPECT_EQ(*id_res, 0x00);

  auto json_res = resp->readString();
  ASSERT_TRUE(json_res.has_value());
  EXPECT_NE(json_res->find(R"("protocol":340)"), std::string::npos);
  EXPECT_NE(json_res->find(R"("1.12.2")"), std::string::npos);
}

// ---------------------------------------------------------------------------
// [TASK-1.8] Ping (0x01) 与 Pong (0x01) 时间戳响应测试
// ---------------------------------------------------------------------------

// STATUS 状态收到合法 0x01 Ping，回送原样 8 字节大端时间戳 Pong 报文
TEST_F(PacketRouterTest, LegalPingRoutesSuccessfully) {
  // 1. Arrange: 构造带 8 字节大端时间戳的 Ping 报文 (ID = 0x01)
  constexpr int64_t kClientTimestamp = 1716300000123LL;
  ByteBuffer packet;
  packet.appendVarInt(0x01);
  packet.writeBe<int64_t>(kClientTimestamp);

  // 2. Act: 分发报文
  PacketRouter::dispatch(session_, ConnectionState::STATUS, std::move(packet));

  // 3. Assert: 验证网络层连接保持正常
  EXPECT_TRUE(session_->socket().is_open());
  EXPECT_FALSE(session_->isStopped());

  // 4. DoD 验证: 客户端接收并校验 Pong 响应包
  auto resp = receivePacket();
  ASSERT_TRUE(resp.has_value()) << "Client did not receive Pong packet";

  // 校验 Packet ID = 0x01 (Pong)
  auto id_res = resp->readVarInt();
  ASSERT_TRUE(id_res.has_value());
  EXPECT_EQ(*id_res, 0x01) << "Expected Pong Packet ID 0x01";

  // 校验 8 字节大端时间戳原样回显
  auto time_echo = resp->readBe<int64_t>();
  ASSERT_TRUE(time_echo.has_value()) << "Failed to read 8-byte int64_t timestamp from Pong";
  EXPECT_EQ(*time_echo, kClientTimestamp) << "Echoed timestamp mismatch!";
  EXPECT_EQ(resp->readableBytes(), 0) << "Pong packet has unexpected trailing bytes";

  // 验证业务日志记录流程闭环
  EXPECT_NE(log_stream_->str().find("Server List Ping flow completed"), std::string::npos);
  EXPECT_EQ(log_stream_->str().find("[warning]"), std::string::npos);
  EXPECT_EQ(log_stream_->str().find("[error]"), std::string::npos);
}

// 直接调用 StatusHandler::handlePing 单元测试
TEST_F(PacketRouterTest, StatusHandlerDirectPingInvocation) {
  constexpr int64_t kTestTimestamp = 9876543210123LL;
  ByteBuffer payload;
  payload.writeBe<int64_t>(kTestTimestamp);

  StatusHandler::handlePing(session_, payload);

  auto resp = receivePacket();
  ASSERT_TRUE(resp.has_value());

  auto id_res = resp->readVarInt();
  ASSERT_TRUE(id_res.has_value());
  EXPECT_EQ(*id_res, 0x01);

  auto time_echo = resp->readBe<int64_t>();
  ASSERT_TRUE(time_echo.has_value());
  EXPECT_EQ(*time_echo, kTestTimestamp);
}

// Ping 8 字节长整型极值与边界值测试（参数化）
class PingBoundaryTest : public PacketRouterTest, public ::testing::WithParamInterface<int64_t> {};

INSTANTIATE_TEST_SUITE_P(
    BoundaryTimestamps,
    PingBoundaryTest,
    ::testing::Values(
        0LL,
        -1LL,
        1LL,
        1716300000123LL,
        -1234567890123LL,
        std::numeric_limits<int64_t>::max(),
        std::numeric_limits<int64_t>::min()
    )
);

TEST_P(PingBoundaryTest, EchoesVariousTimestampsAccurately) {
  const int64_t client_timestamp = GetParam();
  ByteBuffer packet;
  packet.appendVarInt(0x01);
  packet.writeBe<int64_t>(client_timestamp);

  PacketRouter::dispatch(session_, ConnectionState::STATUS, std::move(packet));

  auto resp = receivePacket();
  ASSERT_TRUE(resp.has_value()) << "Failed to receive Pong packet for timestamp: "
                                << client_timestamp;

  auto id_res = resp->readVarInt();
  ASSERT_TRUE(id_res.has_value());
  EXPECT_EQ(*id_res, 0x01);

  auto time_echo = resp->readBe<int64_t>();
  ASSERT_TRUE(time_echo.has_value());
  EXPECT_EQ(*time_echo, client_timestamp);
  EXPECT_EQ(resp->readableBytes(), 0);
}

// [TASK-1.8 防御性测试 1] Ping 载荷为空（0 字节）-> 记录警告并切断连接
TEST_F(PacketRouterTest, MalformedPingEmptyPayloadDisconnects) {
  ByteBuffer packet;
  packet.appendVarInt(0x01);  // Packet ID = 0x01, 但无任何载荷

  PacketRouter::dispatch(session_, ConnectionState::STATUS, std::move(packet));

  EXPECT_FALSE(session_->socket().is_open());
  EXPECT_TRUE(session_->isStopped());
  EXPECT_NE(log_stream_->str().find("[warning]"), std::string::npos);
  EXPECT_NE(log_stream_->str().find("Malformed Ping packet"), std::string::npos);

  auto resp = receivePacket(std::chrono::milliseconds(50));
  EXPECT_FALSE(resp.has_value()) << "Must not send Pong on malformed ping";
}

// [TASK-1.8 防御性测试 2] Ping 载荷仅 4 字节（如误发 32 位 int）-> 拒绝并切断连接
TEST_F(PacketRouterTest, MalformedPingTruncated4BytesPayloadDisconnects) {
  ByteBuffer packet;
  packet.appendVarInt(0x01);
  packet.writeBe<int32_t>(12345);  // 仅 4 字节，不足 8 字节

  PacketRouter::dispatch(session_, ConnectionState::STATUS, std::move(packet));

  EXPECT_FALSE(session_->socket().is_open());
  EXPECT_TRUE(session_->isStopped());
  EXPECT_NE(log_stream_->str().find("[warning]"), std::string::npos);
  EXPECT_NE(log_stream_->str().find("Malformed Ping packet"), std::string::npos);

  auto resp = receivePacket(std::chrono::milliseconds(50));
  EXPECT_FALSE(resp.has_value());
}

// [TASK-1.8 防御性测试 3] Ping 载荷仅 7 字节（比 8 字节少 1 字节截断）-> 拒绝并切断连接
TEST_F(PacketRouterTest, MalformedPingTruncated7BytesPayloadDisconnects) {
  ByteBuffer packet;
  packet.appendVarInt(0x01);
  const char dummy[7] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
  packet.append(dummy, sizeof(dummy));  // 7 字节半包载荷

  PacketRouter::dispatch(session_, ConnectionState::STATUS, std::move(packet));

  EXPECT_FALSE(session_->socket().is_open());
  EXPECT_TRUE(session_->isStopped());
  EXPECT_NE(log_stream_->str().find("[warning]"), std::string::npos);
  EXPECT_NE(log_stream_->str().find("Malformed Ping packet"), std::string::npos);

  auto resp = receivePacket(std::chrono::milliseconds(50));
  EXPECT_FALSE(resp.has_value());
}

// ---------------------------------------------------------------------------
// [TASK-1.7 + TASK-1.8 闭环集成测试] 完整 Server List Ping 握手协议链测试
// Handshake (0x00, NextState=1) -> Status Request (0x00) -> Ping (0x01) -> Pong (0x01)
// ---------------------------------------------------------------------------
TEST_F(PacketRouterTest, FullServerListPingHandshakeFlow) {
  // 1. 客户端发送 Handshake 报文 (NextState = 1 为 STATUS)
  ByteBuffer handshake;
  handshake.appendVarInt(0x00);         // Packet ID = 0x00
  handshake.appendVarInt(340);          // Protocol 340 (1.12.2)
  handshake.appendString("127.0.0.1");  // Host
  handshake.writeBe<uint16_t>(25565);   // Port
  handshake.appendVarInt(1);            // Next State = 1 (STATUS)

  PacketRouter::dispatch(session_, ConnectionState::HANDSHAKING, std::move(handshake));
  EXPECT_EQ(session_->state(), ConnectionState::STATUS);
  EXPECT_TRUE(session_->socket().is_open());

  // 2. 客户端发送 Status Request 报文 (0x00)
  auto status_req = makePacket(0x00);
  PacketRouter::dispatch(session_, ConnectionState::STATUS, std::move(status_req));

  // 接收并检验 Status Response (0x00)
  auto status_resp = receivePacket();
  ASSERT_TRUE(status_resp.has_value());
  auto status_resp_id = status_resp->readVarInt();
  ASSERT_TRUE(status_resp_id.has_value());
  EXPECT_EQ(*status_resp_id, 0x00);

  auto motd_json = status_resp->readString();
  ASSERT_TRUE(motd_json.has_value());
  EXPECT_NE(motd_json->find(R"("version")"), std::string::npos);
  EXPECT_NE(motd_json->find(R"("protocol":340)"), std::string::npos);
  EXPECT_NE(motd_json->find(R"("1.12.2")"), std::string::npos);

  // 3. 客户端发送 Ping 报文 (0x01, 时间戳)
  constexpr int64_t kClientTimestamp = 1716388888000LL;
  ByteBuffer ping_pkt;
  ping_pkt.appendVarInt(0x01);
  ping_pkt.writeBe<int64_t>(kClientTimestamp);

  PacketRouter::dispatch(session_, ConnectionState::STATUS, std::move(ping_pkt));

  // 接收并检验 Pong 回显报文 (0x01)
  auto pong_resp = receivePacket();
  ASSERT_TRUE(pong_resp.has_value());
  auto pong_resp_id = pong_resp->readVarInt();
  ASSERT_TRUE(pong_resp_id.has_value());
  EXPECT_EQ(*pong_resp_id, 0x01);

  auto pong_echo_time = pong_resp->readBe<int64_t>();
  ASSERT_TRUE(pong_echo_time.has_value());
  EXPECT_EQ(*pong_echo_time, kClientTimestamp);
  EXPECT_EQ(pong_resp->readableBytes(), 0);

  // 全流程顺利通过，无警告、无断开
  EXPECT_TRUE(session_->socket().is_open());
  EXPECT_FALSE(session_->isStopped());
  EXPECT_EQ(log_stream_->str().find("[warning]"), std::string::npos);
  EXPECT_EQ(log_stream_->str().find("[error]"), std::string::npos);
  EXPECT_NE(log_stream_->str().find("Server List Ping flow completed"), std::string::npos);
}

// ---------------------------------------------------------------------------
// 优化测试：校验数据包残留未消费字节产生警告日志（未读字节完整性检测，借鉴 Cuberite）
// ---------------------------------------------------------------------------
TEST_F(PacketRouterTest, UnconsumedBytesTriggersWarning) {
  // 构造带有额外垃圾字节的 Ping 包 (ID: 0x01 + 8字节时间戳 + 4字节多余脏数据)
  ByteBuffer pkt;
  pkt.appendVarInt(0x01);
  pkt.writeBe<int64_t>(123456789LL);
  pkt.append("DIRT", 4);

  PacketRouter::dispatch(session_, ConnectionState::STATUS, std::move(pkt));

  // 接收并验证 Pong 回包仍能发出
  auto pong_resp = receivePacket();
  ASSERT_TRUE(pong_resp.has_value());

  // 必须触发了未消费字节的协议告警日志
  EXPECT_NE(
      log_stream_->str().find("unconsumed bytes remaining"),
      std::string::npos
  );
}

// ---------------------------------------------------------------------------
// 测试集 4：ClientSession 纯虚回调与网络层事件直达分发（对标 Cuberite cClientHandle）
// ---------------------------------------------------------------------------
TEST_F(PacketRouterTest, ClientSessionDirectNetworkDispatch) {
  // 启动底层 TcpSession 异步接收循环，验证网络事件直达 ClientSession::onPacket -> PacketRouter
  tcp_session_->start();

  // 客户端构造并通过 socket 真正发送 Handshake 帧: NextState = 1 (STATUS)
  ByteBuffer hs_frame;
  ByteBuffer hs_body;
  hs_body.appendVarInt(0x00);
  hs_body.appendVarInt(340);
  hs_body.appendString("localhost");
  hs_body.writeBe<uint16_t>(25565);
  hs_body.appendVarInt(1);

  hs_frame.appendVarInt(hs_body.readableBytes());
  hs_frame.append(hs_body.peek(), hs_body.readableBytes());

  asio::error_code ec;
  asio::write(client_sock_, asio::buffer(hs_frame.peek(), hs_frame.readableBytes()), ec);
  ASSERT_FALSE(ec);

  // 等待状态流转至 STATUS
  auto start_time = std::chrono::steady_clock::now();
  while (session_->state() != ConnectionState::STATUS &&
         std::chrono::steady_clock::now() - start_time < std::chrono::seconds(2)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT_EQ(session_->state(), ConnectionState::STATUS);

  // 客户端接着发送 Status Request 帧 (ID 0x00)
  ByteBuffer req_frame;
  req_frame.appendVarInt(1);     // length
  req_frame.appendVarInt(0x00);  // Status Request
  asio::write(client_sock_, asio::buffer(req_frame.peek(), req_frame.readableBytes()), ec);
  ASSERT_FALSE(ec);

  // 客户端接收服务端回送的 Status Response
  auto resp = receivePacket();
  ASSERT_TRUE(resp.has_value());
  auto id = resp->readVarInt();
  ASSERT_TRUE(id.has_value());
  EXPECT_EQ(*id, 0x00);
  auto json = resp->readString();
  ASSERT_TRUE(json.has_value());
  EXPECT_NE(json->find("1.12.2"), std::string::npos);
}

// 验证 ClientSession 心跳应答匹配机制
TEST_F(PacketRouterTest, ClientSessionKeepAliveMatching) {
  session_->setState(ConnectionState::PLAY);

  // 正常匹配
  session_->onKeepAliveResponse(12345LL);
  EXPECT_NE(log_stream_->str().find("Keep alive ID mismatch"), std::string::npos);

  // 模拟发送心跳后应答
  // session 拥有独立 entity_id 与 teleport_id 管理
  session_->setEntityId(42);
  EXPECT_EQ(session_->entityId(), 42);

  int32_t tid1 = session_->nextTeleportId();
  EXPECT_EQ(session_->pendingTeleportId(), tid1);
  session_->clearPendingTeleportId();
  EXPECT_EQ(session_->pendingTeleportId(), -1);
}

