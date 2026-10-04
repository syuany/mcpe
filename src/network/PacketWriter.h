#ifndef PACKETWRITER_H
#define PACKETWRITER_H

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

#include "common/ByteBuffer.h"

namespace mcpe::network {

class TcpSession;

/**
 * @brief 基于 RAII 机制的数据包自动打包与发送器（借鉴 Cuberite cPacketizer 架构设计）
 *
 * 泛型支持 TcpSession 与 ClientSession 等任意具备 sendPacket(ByteBuffer) 契约的会话对象。
 * 1. 构造即写入 Packet ID：自动将 VarInt 类型的 Packet ID 写入底层缓冲区
 * 2. RAII 自动封包与下发：当局部 PacketWriter 对象离开作用域析构时，
 *    自动通过 Cheap Prepend 原位前插包长 VarInt 前缀，并投递至 Session 发送队列（零内存拷贝）
 * 3. 杜绝遗漏与错发：消除开发者手动计算包长、前插 VarInt 以及显式调用 sendPacket 的心智负担
 * 4. 异常安全与取消支持：支持 cancel() 在逻辑分支中止时不发送残包
 */
template <typename SessionType>
class BasicPacketWriter {
 public:
  explicit BasicPacketWriter(std::shared_ptr<SessionType> session, int32_t packet_id)
      : session_(session.get()), session_shared_(std::move(session)), packet_id_(packet_id) {
    init();
  }

  explicit BasicPacketWriter(SessionType& session, int32_t packet_id)
      : session_(&session), packet_id_(packet_id) {
    init();
  }

  ~BasicPacketWriter() {
    flush();
  }

  // Move-only 语义
  BasicPacketWriter(const BasicPacketWriter&) = delete;
  BasicPacketWriter& operator=(const BasicPacketWriter&) = delete;

  BasicPacketWriter(BasicPacketWriter&& other) noexcept
      : session_(other.session_),
        session_shared_(std::move(other.session_shared_)),
        packet_id_(other.packet_id_),
        buffer_(std::move(other.buffer_)),
        is_active_(other.is_active_) {
    other.is_active_ = false;
    other.session_ = nullptr;
  }

  BasicPacketWriter& operator=(BasicPacketWriter&& other) noexcept {
    if (this != &other) {
      flush();
      session_ = other.session_;
      session_shared_ = std::move(other.session_shared_);
      packet_id_ = other.packet_id_;
      buffer_ = std::move(other.buffer_);
      is_active_ = other.is_active_;
      other.is_active_ = false;
      other.session_ = nullptr;
    }
    return *this;
  }

  /// 显式放弃当前数据包发送（析构时不再触发 sendPacket）
  void cancel() noexcept {
    is_active_ = false;
  }

  /// 是否仍处于待发送激活状态
  bool isActive() const noexcept {
    return is_active_;
  }

  /// 获取底层 ByteBuffer 引用
  ByteBuffer& buffer() noexcept { return buffer_; }
  const ByteBuffer& buffer() const noexcept { return buffer_; }

  template <typename T>
  void writeBe(T value) {
    buffer_.writeBe<T>(value);
  }

  void appendVarInt(int32_t value) { buffer_.appendVarInt(value); }
  void appendVarLong(int64_t value) { buffer_.appendVarLong(value); }
  void appendString(std::string_view str) { buffer_.appendString(str); }
  void appendByte(uint8_t byte) { buffer_.appendByte(byte); }
  void append(const void* data, size_t len) { buffer_.append(data, len); }
  void append(std::string_view str) { buffer_.append(str); }

  int32_t packetId() const noexcept { return packet_id_; }

  /// 手动提前完成封包下发
  void flush() {
    if (is_active_ && session_ != nullptr && buffer_.readableBytes() > 0) {
      is_active_ = false;
      buffer_.prependVarInt(buffer_.readableBytes());
      session_->sendPacket(std::move(buffer_));
    }
  }

 private:
  void init() {
    buffer_.appendVarInt(packet_id_);
    is_active_ = true;
  }

  SessionType* session_{nullptr};
  std::shared_ptr<SessionType> session_shared_{nullptr};
  int32_t packet_id_{0};
  ByteBuffer buffer_;
  bool is_active_{false};
};

template <typename SessionType>
BasicPacketWriter(std::shared_ptr<SessionType>, int32_t) -> BasicPacketWriter<SessionType>;

template <typename SessionType>
BasicPacketWriter(SessionType&, int32_t) -> BasicPacketWriter<SessionType>;

template <typename SessionType = TcpSession>
using PacketWriter = BasicPacketWriter<SessionType>;

}  // namespace mcpe::network

#endif  // PACKETWRITER_H
