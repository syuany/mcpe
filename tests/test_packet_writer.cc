/**
 * @file test_packet_writer.cc
 * @brief RAII 发包器（PacketWriter）单元测试
 *
 * 验证类似 Cuberite cPacketizer 的 RAII 发包设计：
 * 1. 作用域析构自动前插长度并安全异步发包
 * 2. 显式 flush() 触发发送且析构不重复发包
 * 3. cancel() 放弃发包保障异常安全
 * 4. Move-only 移动语义保证所有权唯一且仅发送一次
 * 5. 复合数据类型（VarInt, BigEndian, String）打包正确性
 */

#include <gtest/gtest.h>

#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "common/ByteBuffer.h"
#include "network/PacketWriter.h"
#include "network/TcpSession.h"

using mcpe::ByteBuffer;
using mcpe::network::PacketWriter;
using mcpe::network::TcpSession;

namespace {

struct TcpPairFixture {
  asio::io_context io_context;
  asio::executor_work_guard<asio::io_context::executor_type> work_guard;
  asio::ip::tcp::acceptor acceptor;
  asio::ip::tcp::socket client_socket;
  std::shared_ptr<TcpSession> session;
  std::jthread io_thread;

  TcpPairFixture()
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

    auto start_time = std::chrono::steady_clock::now();
    while (!connected && std::chrono::steady_clock::now() - start_time < std::chrono::seconds(2)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  ~TcpPairFixture() {
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

  ByteBuffer
  readBytes(size_t count) {
    std::vector<char> raw(count);
    asio::error_code ec;
    asio::read(client_socket, asio::buffer(raw), ec);
    EXPECT_FALSE(ec) << "Read failed: " << ec.message();
    ByteBuffer buf;
    buf.append(raw.data(), raw.size());
    return buf;
  }
};

}  // namespace

// 测试 1：离开作用域自动封包并前插包长发送
TEST(PacketWriterTest, AutoSendOnScopeExit) {
  TcpPairFixture fixture;
  ASSERT_TRUE(fixture.session != nullptr);

  {
    // 构造 PacketWriter：写入 Packet ID 0x01 (VarInt)
    PacketWriter writer(fixture.session, 0x01);
    writer.writeBe<int64_t>(9876543210LL);
    // 离开作用域，writer 析构，自动计算长度 (1+8=9) 并前插 VarInt 9 发出
  }

  // 接收端应读取到：VarInt(9) + VarInt(0x01) + int64_t(9876543210LL)
  // VarInt(9) 占 1 字节, VarInt(0x01) 占 1 字节, int64 占 8 字节，共 10 字节
  ByteBuffer recv = fixture.readBytes(10);
  auto len = recv.readVarInt();
  ASSERT_TRUE(len.has_value());
  EXPECT_EQ(*len, 9);

  auto packet_id = recv.readVarInt();
  ASSERT_TRUE(packet_id.has_value());
  EXPECT_EQ(*packet_id, 0x01);

  auto timestamp = recv.readBe<int64_t>();
  ASSERT_TRUE(timestamp.has_value());
  EXPECT_EQ(*timestamp, 9876543210LL);
}

// 测试 2：显式 cancel() 取消发送，析构不触发 sendPacket
TEST(PacketWriterTest, CancelSuppressesSending) {
  TcpPairFixture fixture;
  ASSERT_TRUE(fixture.session != nullptr);

  {
    PacketWriter writer(*fixture.session, 0x05);
    writer.appendString("ShouldNotSend");
    writer.cancel();
    EXPECT_FALSE(writer.isActive());
  }

  // 紧接着发送一个正常包
  {
    PacketWriter writer(*fixture.session, 0x09);
    writer.appendString("Hello");
  }

  // 接收端只应该读取到第 2 个包
  // 长度: 1 (ID 0x09) + 1 (str len 5) + 5 ("Hello") = 7 字节
  // 加上包长前缀 VarInt(7) 1 字节，共 8 字节
  ByteBuffer recv = fixture.readBytes(8);
  auto len = recv.readVarInt();
  ASSERT_TRUE(len.has_value());
  EXPECT_EQ(*len, 7);

  auto packet_id = recv.readVarInt();
  ASSERT_TRUE(packet_id.has_value());
  EXPECT_EQ(*packet_id, 0x09);

  auto str = recv.readString();
  ASSERT_TRUE(str.has_value());
  EXPECT_EQ(*str, "Hello");
}

// 测试 3：Move-only 移动语义保证唯一发送
TEST(PacketWriterTest, MoveConstructorTransfersOwnership) {
  TcpPairFixture fixture;
  ASSERT_TRUE(fixture.session != nullptr);

  {
    PacketWriter writer1(fixture.session, 0x02);
    writer1.appendString("Alice");

    // 移动至 writer2
    PacketWriter writer2 = std::move(writer1);
    EXPECT_FALSE(writer1.isActive());
    EXPECT_TRUE(writer2.isActive());

    writer2.writeBe<uint16_t>(1234);
  }

  // 接收端只收到一个完整的包
  // ID 0x02 (1B) + String "Alice" (1B len + 5B) + uint16 (2B) = 9 字节载荷
  // 包长 VarInt(9) 1 字节，总计 10 字节
  ByteBuffer recv = fixture.readBytes(10);
  EXPECT_EQ(*recv.readVarInt(), 9);
  EXPECT_EQ(*recv.readVarInt(), 0x02);
  EXPECT_EQ(*recv.readString(), "Alice");
  EXPECT_EQ(*recv.readBe<uint16_t>(), 1234);
}

// 测试 4：显式 flush() 手动提前发送，析构不会二次发送
TEST(PacketWriterTest, ExplicitFlushSendsOnce) {
  TcpPairFixture fixture;
  ASSERT_TRUE(fixture.session != nullptr);

  {
    PacketWriter writer(fixture.session, 0x10);
    writer.writeBe<int32_t>(42);
    writer.flush();
    EXPECT_FALSE(writer.isActive());
    // 析构时不会再次发送
  }

  ByteBuffer recv = fixture.readBytes(6);
  EXPECT_EQ(*recv.readVarInt(), 5);
  EXPECT_EQ(*recv.readVarInt(), 0x10);
  EXPECT_EQ(*recv.readBe<int32_t>(), 42);
}

// 测试 5：PacketBuilder 无会话独立封包器测试
TEST(PacketWriterTest, PacketBuilderStandaloneFraming) {
  mcpe::network::PacketBuilder builder(0x0F);
  builder.appendString(R"({"text":"Hello"})");
  builder.appendByte(1);

  ByteBuffer pkt = builder.build();
  auto total_len = pkt.readVarInt();
  ASSERT_TRUE(total_len.has_value());
  EXPECT_EQ(*total_len, static_cast<int32_t>(pkt.readableBytes()));

  auto pid = pkt.readVarInt();
  ASSERT_TRUE(pid.has_value());
  EXPECT_EQ(*pid, 0x0F);

  auto text = pkt.readString();
  ASSERT_TRUE(text.has_value());
  EXPECT_EQ(*text, R"({"text":"Hello"})");

  auto pos = pkt.readBe<uint8_t>();
  ASSERT_TRUE(pos.has_value());
  EXPECT_EQ(*pos, 1);
  EXPECT_EQ(pkt.readableBytes(), 0u);
}

// 测试 6：Java writeUTF 双字节大端编码与 0x18 PluginMessage 跨服封包测试
TEST(PacketWriterTest, JavaUtfEncodingAndBungeeConnect) {
  mcpe::network::PacketBuilder builder(0x18);
  builder.appendString("BungeeCord");
  builder.appendJavaUtf("Connect");
  builder.appendJavaUtf("survival");

  ByteBuffer pkt = builder.build();
  auto total_len = pkt.readVarInt();
  ASSERT_TRUE(total_len.has_value());
  EXPECT_EQ(*total_len, static_cast<int32_t>(pkt.readableBytes()));

  auto pid = pkt.readVarInt();
  ASSERT_TRUE(pid.has_value());
  EXPECT_EQ(*pid, 0x18);

  // Channel: Minecraft VarInt 长度前缀字符串
  auto channel = pkt.readString();
  ASSERT_TRUE(channel.has_value());
  EXPECT_EQ(*channel, "BungeeCord");

  // Subchannel: Java writeUTF (2 字节大端长度 + 原始字节)
  auto sub_len = pkt.readBe<uint16_t>();
  ASSERT_TRUE(sub_len.has_value());
  EXPECT_EQ(*sub_len, 7u);
  EXPECT_EQ(pkt.retrieveAsString(*sub_len), "Connect");

  // Target: Java writeUTF (2 字节大端长度 + 原始字节)
  auto target_len = pkt.readBe<uint16_t>();
  ASSERT_TRUE(target_len.has_value());
  EXPECT_EQ(*target_len, 8u);
  EXPECT_EQ(pkt.retrieveAsString(*target_len), "survival");

  EXPECT_EQ(pkt.readableBytes(), 0u);
}
