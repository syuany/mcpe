# Sprint 1 保姆级实战开发教程：网络基座与列表探测（Server List Ping）

> **目标协议**：Minecraft Java Edition 1.12.2（Protocol 340）  
> **当前阶段**：Sprint 1（TASK-1.2 至 TASK-1.9）  
> **面向读者**：刚完成 Sprint 0 基础设施（`ByteBuffer` / `VarInt`），面对网络层和协议层无从下手、不知道“**在哪个目录建什么文件**”、“**代码怎么组织**”、“**怎么验证每一步**”的开发者。  
> **文档宗旨**：细致到每一个文件路径、每一段接口声明、每一行 CMake 配置、每一条验证命令，带你平滑跨过网络编程与游戏网关门槛！

---

## 目录

- [一、 全局文件与目录蓝图（一眼看清在哪个目录建什么）](#一-全局文件与目录蓝图一眼看清在哪个目录建什么)
- [二、 数据流向总览：一个网络包的完整一生](#二-数据流向总览一个网络包的完整一生)
- [三、 任务逐步实现指南](#三-任务逐步实现指南)
  - [TASK-1.2: Asio TcpServer 与 TcpSession 基础连接骨架](#task-12-asio-tcpserver-与-tcpsession-基础连接骨架)
  - [TASK-1.3: 发送队列（Write Queue）与并发安全下发](#task-13-发送队列write-queue与并发安全下发)
  - [TASK-1.4: 协议数据包拆包器（Packet Framing）与单测](#task-14-协议数据包拆包器packet-framing与单测)
  - [TASK-1.5: 协议状态机框架搭建（ConnectionState 与 PacketRouter）](#task-15-协议状态机框架搭建connectionstate-与-packetrouter)
  - [TASK-1.6: Handshake (0x00) 解析与意图流转](#task-16-handshake-0x00-解析与意图流转)
  - [TASK-1.7: Status Request (0x00) 与 JSON 描述响应](#task-17-status-request-0x00-与-json-描述响应)
  - [TASK-1.8: Ping (0x01) 与 Pong (0x01) 时间戳响应](#task-18-ping-0x01-与-pong-0x01-时间戳响应)
  - [TASK-1.9: Sprint 1 集成验收与原版客户端联调](#task-19-sprint-1-集成验收与原版客户端联调)
- [四、 自动化调试利器：Python 极速探测验证脚本](#四-自动化调试利器python-极速探测验证脚本)
- [五、 Sprint 1 避坑与面试高频考点速查表](#五-sprint-1-避坑与面试高频考点速查表)

---

## 一、 全局文件与目录蓝图（一眼看清在哪个目录建什么）

在动手敲代码前，先把整个 Sprint 1 需要新建与修改的文件树印在脑海中。**严格遵循工程分层规范**：

```
mcpe/
├── CMakeLists.txt                              <-- 【修改】注册网络库、协议库、新测试目标
├── include/mcpe/
│   ├── common/
│   │   └── ByteBuffer.h                        <-- 【已完成】Sprint 0 产出
│   ├── network/                                <-- 【新建目录】网络 I/O 抽象层
│   │   ├── TcpSession.h                        <-- [TASK-1.2, 1.3] 连接生命周期与读写队列
│   │   ├── TcpServer.h                         <-- [TASK-1.2] Acceptor 端口监听
│   │   └── PacketFraming.h                     <-- [TASK-1.4] VarInt 长度定界拆包器
│   └── protocol/                               <-- 【新建目录】协议业务层
│       ├── ConnectionState.h                   <-- [TASK-1.5] 4 种连接状态枚举
│       ├── PacketRouter.h                      <-- [TASK-1.5] 状态机分发路由器
│       └── StatusHandler.h                     <-- [TASK-1.6, 1.7, 1.8] Ping/Pong/MOTD 业务
├── src/
│   ├── common/
│   │   └── ByteBuffer.cc                       <-- 【已完成】Sprint 0 产出
│   ├── network/                                <-- 【新建目录】网络层实现
│   │   ├── TcpSession.cc                       <-- [TASK-1.2, 1.3] 异步读写、生命周期
│   │   ├── TcpServer.cc                        <-- [TASK-1.2] 异步接收连接
│   │   └── PacketFraming.cc                    <-- [TASK-1.4] 拆包算法实现
│   ├── protocol/                               <-- 【新建目录】协议层实现
│   │   ├── PacketRouter.cc                     <-- [TASK-1.5] 协议分发逻辑
│   │   └── StatusHandler.cc                    <-- [TASK-1.6, 1.7, 1.8] 握手、JSON组包、时间戳回显
│   └── main.cc                                 <-- 【修改】组装启动 TcpServer
├── tests/
│   ├── test_bytebuffer.cc                      <-- 【已完成】
│   ├── test_varint.cc                          <-- 【已完成】
│   └── test_framing.cc                         <-- [TASK-1.4] 拆包器单测（粘包/半包）
└── scripts/
    └── test_ping.py                            <-- [TASK-1.9] 免开游戏的极速联调脚本
```

---

## 二、 数据流向总览：一个网络包的完整一生

```
物理网卡
  │ (TCP 字节流)
  ▼
[asio::ip::tcp::socket::async_read_some]
  │ 读入临时 4KB 缓冲区
  ▼
[TcpSession::recv_buf_] (累积接收缓冲区 ByteBuffer)
  │
  ▼ 循环调用拆包算法
[PacketFraming::slicePacket(recv_buf_)]
  ├── 返回 kIncomplete: 字节不够（半包），保留缓冲区，静默等下一次 read
  ├── 返回 kMalformed:  数据非法（VarInt > 5 字节），切断连接
  └── 返回 完整独立包 ByteBuffer (包含 PacketID + Payload)
        │
        ▼ 递交协议路由器
      [PacketRouter::dispatch(session, packet)]
        │ 根据 session->state() 匹配处理分支:
        ├── HANDSHAKING 状态: 收到 0x00 -> 解析握手，NextState=1 转入 STATUS
        └── STATUS 状态:
              ├── 收到 0x00 (Status Request) -> 回发 0x00 (JSON MOTD 描述)
              └── 收到 0x01 (Ping)           -> 回发 0x01 (原样 8 字节 Pong)
                    │
                    ▼ 发送响应数据
                  [TcpSession::sendPacket(ByteBuffer buf)]
                    │ 推入 write_queue_ 发送队列
                    ▼
                  [asio::async_write] -> 物理网卡送出
```

---

## 三、 任务逐步实现指南

---

### TASK-1.2: Asio `TcpServer` 与 `TcpSession` 基础连接骨架

#### 1. 目标与设计契约
* 创建并管理单个客户端连接生命周期（`TcpSession`）。
* 创建监听套接字并循环接收新连接（`TcpServer`）。
* **核心契约**：
  1. `TcpSession` 必须继承 `std::enable_shared_from_this<TcpSession>`。
  2. 在任何发起异步操作的 lambda 回调中，必须捕获 `auto self = shared_from_this();`，确保回调未完成时 session 对象不会被销毁。
  3. 对端断开连接（触发 `asio::error::eof` 或 `asio::error::connection_reset`）时，优雅关闭 socket 并打印日志，绝不抛出未捕获异常。

#### 2. 在哪个目录创建什么文件
1. 创建头文件：`include/mcpe/network/TcpSession.h`
2. 创建源文件：`src/network/TcpSession.cc`
3. 创建头文件：`include/mcpe/network/TcpServer.h`
4. 创建源文件：`src/network/TcpServer.cc`

#### 3. 详细代码实现

##### ① `include/mcpe/network/TcpSession.h`
```cpp
#pragma once

#include <asio.hpp>
#include <memory>
#include <array>
#include "mcpe/common/ByteBuffer.h"

namespace mcpe::network {

class TcpSession : public std::enable_shared_from_this<TcpSession> {
 public:
  explicit TcpSession(asio::ip::tcp::socket socket);
  ~TcpSession();

  // 禁用拷贝与移动
  TcpSession(const TcpSession&) = delete;
  TcpSession& operator=(const TcpSession&) = delete;

  // 启动连接生命周期（开始监听网络读取）
  void start();

  // 优雅关闭并释放连接
  void stop();

  // 获取套接字底层引用（仅在初始化时使用）
  asio::ip::tcp::socket& socket() noexcept { return socket_; }

  // 获取对端 IP:Port 字符串描述（打印日志用）
  const std::string& remoteAddress() const noexcept { return remote_address_; }

 private:
  void doRead();

  asio::ip::tcp::socket socket_;
  std::string remote_address_;
  
  // 临时读缓冲区：每次 async_read_some 接收的最底层存储
  std::array<char, 4096> read_buffer_;

  // 标记是否已停止
  bool is_stopped_{false};
};

}  // namespace mcpe::network
```

##### ② `src/network/TcpSession.cc`
```cpp
#include "mcpe/network/TcpSession.h"
#include <spdlog/spdlog.h>

namespace mcpe::network {

TcpSession::TcpSession(asio::ip::tcp::socket socket)
    : socket_(std::move(socket)) {
  asio::error_code ec;
  const auto endpoint = socket_.remote_endpoint(ec);
  if (!ec) {
    remote_address_ = fmt::format("{}:{}", endpoint.address().to_string(), endpoint.port());
  } else {
    remote_address_ = "unknown";
  }
  spdlog::info("[Session] Connected from {}", remote_address_);
}

TcpSession::~TcpSession() {
  spdlog::info("[Session] Destroyed: {}", remote_address_);
}

void TcpSession::start() {
  doRead();
}

void TcpSession::stop() {
  if (is_stopped_) return;
  is_stopped_ = true;

  asio::error_code ec;
  socket_.close(ec);
  if (ec) {
    spdlog::debug("[Session] Error closing socket {}: {}", remote_address_, ec.message());
  }
}

void TcpSession::doRead() {
  // ⚠️ 关键避坑点：必须显式捕获 self 保活！如果只捕获 this，连接断开异步销毁时会触发野指针奔溃！
  auto self = shared_from_this();
  socket_.async_read_some(
      asio::buffer(read_buffer_),
      [this, self](const asio::error_code& ec, std::size_t bytes_transferred) {
        if (is_stopped_) return;

        if (!ec) {
          spdlog::debug("[Session] Read {} bytes from {}", bytes_transferred, remote_address_);
          // 后续在此处将 read_buffer_ 喂给 cumulative 缓冲区（TASK-1.4）
          
          // 继续投递下一次读
          doRead();
        } else {
          if (ec == asio::error::eof || ec == asio::error::connection_reset) {
            spdlog::info("[Session] Client disconnected: {}", remote_address_);
          } else {
            spdlog::warn("[Session] Read error on {}: {}", remote_address_, ec.message());
          }
          stop();
        }
      });
}

}  // namespace mcpe::network
```

##### ③ `include/mcpe/network/TcpServer.h`
```cpp
#pragma once

#include <asio.hpp>
#include <cstdint>
#include <memory>
#include "mcpe/network/TcpSession.h"

namespace mcpe::network {

class TcpServer {
 public:
  TcpServer(asio::io_context& io_context, uint16_t port);
  ~TcpServer() = default;

  void start();
  void stop();

 private:
  void doAccept();

  asio::io_context& io_context_;
  asio::ip::tcp::acceptor acceptor_;
  bool is_running_{false};
};

}  // namespace mcpe::network
```

##### ④ `src/network/TcpServer.cc`
```cpp
#include "mcpe/network/TcpServer.h"
#include <spdlog/spdlog.h>

namespace mcpe::network {

TcpServer::TcpServer(asio::io_context& io_context, uint16_t port)
    : io_context_(io_context),
      acceptor_(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)) {
  spdlog::info("[TcpServer] Listening on 0.0.0.0:{}", port);
}

void TcpServer::start() {
  is_running_ = true;
  doAccept();
}

void TcpServer::stop() {
  is_running_ = false;
  asio::error_code ec;
  acceptor_.close(ec);
}

void TcpServer::doAccept() {
  // 为新接入连接准备套接字
  acceptor_.async_accept([this](const asio::error_code& ec, asio::ip::tcp::socket socket) {
    if (!is_running_) return;

    if (!ec) {
      // 开启 TCP_NODELAY 禁用 Nagle 算法，这对游戏低延迟通信至关重要！
      asio::error_code opt_ec;
      socket.set_option(asio::ip::tcp::no_delay(true), opt_ec);

      auto session = std::make_shared<TcpSession>(std::move(socket));
      session->start();
    } else {
      spdlog::error("[TcpServer] Accept error: {}", ec.message());
    }

    // 循环监听下一个接入
    doAccept();
  });
}

}  // namespace mcpe::network
```

##### ⑤ 修改 `src/main.cc`
```cpp
#include <asio.hpp>
#include <spdlog/spdlog.h>
#include "mcpe/network/TcpServer.h"

int main() {
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
```

##### ⑥ 修改根目录 `CMakeLists.txt`
在 `CMakeLists.txt` 中将网络层源文件加入或组装为一个新的 `mcpe_network` 模块：
```cmake
# ----------------------------------------------
# 模块2：mcpe_network 静态库
# ----------------------------------------------
add_library(mcpe_network STATIC
    src/network/TcpSession.cc
    src/network/TcpServer.cc
)

target_include_directories(mcpe_network PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(mcpe_network PUBLIC mcpe_common)

# 修改 main 目标的链接项
target_link_libraries(main PRIVATE mcpe_network mcpe_common)
```

#### 4. 验证命令与预期结果
```bash
# 1. 编译项目
cmake --build build -j4

# 2. 终端 1：启动服务器
./build/main

# 3. 终端 2：使用 nc 模拟连入与断开
nc 127.0.0.1 25565
# 在 nc 中随便敲几个字符回车，然后按 Ctrl+C 退出
```
* **控制台预期输出**：
  ```text
  [info] [TcpServer] Listening on 0.0.0.0:25565
  [info] [Session] Connected from 127.0.0.1:xxxxx
  [debug] [Session] Read 5 bytes from 127.0.0.1:xxxxx
  [info] [Session] Client disconnected: 127.0.0.1:xxxxx
  [info] [Session] Destroyed: 127.0.0.1:xxxxx
  ```
* 看到连接创建与析构打印，无内存泄漏与野指针崩溃，TASK-1.2 宣告顺利完成！

---

### TASK-1.3: 发送队列（Write Queue）与并发安全下发

#### 1. 为什么必须有发送队列？
> [!CAUTION]
> **Asio 致命陷阱**：在同一个 `asio::ip::tcp::socket` 上，**绝对不允许并发调用 `async_write`**！如果在前一次 `async_write` 的回调尚未触发前再次调用 `async_write`，Asio 内部缓冲区指针会错乱，触发断言直接终止程序。

因此，`TcpSession` 必须内部实现一个**发送队列（FIFO）**：
1. 外部调用 `sendPacket(ByteBuffer buf)`，将数据包塞入队尾。
2. 检查 `is_writing_` 标记。如果当前没有正在进行的写操作，取出队头并开启 `async_write`。
3. 当写操作完成回调执行时，弹出队首。若队列中还有剩余包，继续写下一个；若已发空，将 `is_writing_` 复位为 `false`。

#### 2. 在哪个目录修改什么文件
1. 修改头文件：`include/mcpe/network/TcpSession.h`
2. 修改源文件：`src/network/TcpSession.cc`

#### 3. 详细代码实现

##### ① 在 `TcpSession.h` 中追加声明
```cpp
// 在 include/mcpe/network/TcpSession.h 中：
#include <deque>

class TcpSession : public std::enable_shared_from_this<TcpSession> {
 public:
  // ... 保留原有接口 ...

  // 发送一个完整序列化好的数据包（支持多包连续安全调用）
  void sendPacket(ByteBuffer buffer);

 private:
  void doRead();
  void doWrite();

  // 发送队列与状态
  std::deque<ByteBuffer> write_queue_;
  bool is_writing_{false};
};
```

##### ② 在 `TcpSession.cc` 中实现队列逻辑
```cpp
// 在 src/network/TcpSession.cc 中追加：

void TcpSession::sendPacket(ByteBuffer buffer) {
  if (is_stopped_ || buffer.readableBytes() == 0) return;

  // 如果跨线程调用 sendPacket，建议通过 asio::post 派发至 socket 绑定的 executor
  asio::post(socket_.get_executor(), [this, self = shared_from_this(), buf = std::move(buffer)]() mutable {
    if (is_stopped_) return;

    write_queue_.push_back(std::move(buf));
    if (!is_writing_) {
      is_writing_ = true;
      doWrite();
    }
  });
}

void TcpSession::doWrite() {
  if (write_queue_.empty()) {
    is_writing_ = false;
    return;
  }

  // 取出队首待发送数据
  const auto& front_buf = write_queue_.front();
  auto self = shared_from_this();

  asio::async_write(
      socket_,
      asio::buffer(front_buf.peek(), front_buf.readableBytes()),
      [this, self](const asio::error_code& ec, std::size_t bytes_transferred) {
        if (is_stopped_) return;

        if (!ec) {
          // 弹出已发完的包
          write_queue_.pop_front();
          // 继续投递下一包
          doWrite();
        } else {
          spdlog::warn("[Session] Write error to {}: {}", remote_address_, ec.message());
          stop();
        }
      });
}
```

#### 4. 验证方式
在 `TcpSession::start()` 中连续调用 5 次 `sendPacket` 模拟并发下发：
```cpp
ByteBuffer b1; b1.append("HELLO\n", 6);
ByteBuffer b2; b2.append("WORLD\n", 6);
sendPacket(b1);
sendPacket(b2);
```
用 `nc 127.0.0.1 25565` 连接，能够完整、有序收到 `HELLO` 与 `WORLD` 字符串，且服务端无任何断言崩溃。

---

### TASK-1.4: 协议数据包拆包器（Packet Framing）与单测

#### 1. 为什么把拆包算法抽离为纯函数？
如果将拆包逻辑全部揉进 `TcpSession` 的 Socket 回调中，单测会极其困难（必须 mock 网络 I/O）。  
**现代 C++ 优秀架构设计**：抽离出独立的无状态或纯逻辑模块 `PacketFraming`，入参为 `ByteBuffer& recv_buf`，专门负责切出完整的单包。

#### 2. 在哪个目录创建什么文件
1. 创建头文件：`include/mcpe/network/PacketFraming.h`
2. 创建源文件：`src/network/PacketFraming.cc`
3. 创建单测：`tests/test_framing.cc`
4. 修改 `CMakeLists.txt`

#### 3. 详细代码实现

##### ① `include/mcpe/network/PacketFraming.h`
```cpp
#pragma once

#include <expected>
#include <optional>
#include "mcpe/common/ByteBuffer.h"

namespace mcpe::network {

enum class FramingError {
  kIncomplete,        // 半包：数据未到齐，保留游标，等待更多数据
  kMalformedLength,   // 畸形：VarInt 超过 5 字节
  kPacketTooLarge     // 超长攻击：超过协议规定的单包上限（如 2MB）
};

class PacketFraming {
 public:
  // 单包最大合法上限（2MB，防止恶意连接发动 OOM 攻击）
  static constexpr size_t kMaxPacketSize = 2097151;

  /**
   * 从累积接收缓冲区中尝试切出一个完整的数据包。
   * 
   * 契约保证：
   * - 若返回 kIncomplete，recv_buf 的读游标绝不移动（零副作用）！
   * - 若成功，返回包含 [Packet ID + Payload] 的新 ByteBuffer，recv_buf 游标自动划过该包。
   */
  static std::expected<ByteBuffer, FramingError> slicePacket(ByteBuffer& recv_buf);
};

}  // namespace mcpe::network
```

##### ② `src/network/PacketFraming.cc`
```cpp
#include "mcpe/network/PacketFraming.h"

namespace mcpe::network {

std::expected<ByteBuffer, FramingError> PacketFraming::slicePacket(ByteBuffer& recv_buf) {
  if (recv_buf.readableBytes() == 0) {
    return std::unexpected(FramingError::kIncomplete);
  }

  // 1. 事务性预读包长 VarInt（成败都不移动游标）
  const auto length_res = recv_buf.peekVarInt();
  if (!length_res) {
    if (length_res.error() == DecodeError::kIncomplete) {
      return std::unexpected(FramingError::kIncomplete);
    }
    return std::unexpected(FramingError::kMalformedLength);
  }

  const int32_t packet_len = *length_res;
  if (packet_len <= 0 || static_cast<size_t>(packet_len) > kMaxPacketSize) {
    return std::unexpected(FramingError::kPacketTooLarge);
  }

  // 2. 计算包长 VarInt 自身所占的字节数
  // 利用 peek 算出 VarInt 字节跨度：在 peekVarInt 成功的前提下，直接计算偏移
  size_t varint_len = 0;
  for (size_t i = 0; i < recv_buf.readableBytes(); ++i) {
    varint_len++;
    if ((static_cast<uint8_t>(recv_buf.peek()[i]) & 0x80) == 0) {
      break;
    }
  }

  // 3. 校验整包（VarInt 头 + Payload 载荷）是否全部到达
  const size_t total_packet_bytes = varint_len + static_cast<size_t>(packet_len);
  if (recv_buf.readableBytes() < total_packet_bytes) {
    // 载荷半包：继续静默等待
    return std::unexpected(FramingError::kIncomplete);
  }

  // 4. 整包已到齐！先消费掉最外层的 VarInt 包长
  recv_buf.retrieve(varint_len);

  // 5. 提取出长度为 packet_len 的独立包载荷
  ByteBuffer packet;
  packet.append(recv_buf.peek(), packet_len);
  recv_buf.retrieve(packet_len);

  return packet;
}

}  // namespace mcpe::network
```

##### ③ 在 `TcpSession` 中集成拆包驱动
修改 `include/mcpe/network/TcpSession.h`，添加累积缓冲区 `cumulative_buf_`：
```cpp
// 在 TcpSession 类内私有成员添加：
ByteBuffer cumulative_buf_;
```
在 `src/network/TcpSession.cc` 的 `doRead()` 中串联拆包：
```cpp
#include "mcpe/network/PacketFraming.h"

// 在 async_read_some 成功回调内：
if (!ec) {
  // 1. 将本次读到的网络碎片追加进累积缓冲区
  cumulative_buf_.append(read_buffer_.data(), bytes_transferred);

  // 2. 循环切包（解决 TCP 粘包）
  while (true) {
    auto frame_res = PacketFraming::slicePacket(cumulative_buf_);
    if (!frame_res) {
      if (frame_res.error() == FramingError::kIncomplete) {
        // 半包：等待下一次 async_read_some，退出循环
        break;
      }
      // 畸形包或超大包：判定恶意流量，立即断开连接！
      spdlog::error("[Session] Malformed packet from {}, disconnect!", remote_address_);
      stop();
      return;
    }

    // 成功切出一个完整包！
    ByteBuffer packet = std::move(*frame_res);
    // 后续在此处交由 PacketRouter 处理（TASK-1.5）
    spdlog::debug("[Session] Sliced complete packet with {} bytes", packet.readableBytes());
  }

  // 3. 继续投递异步读
  doRead();
}
```

##### ④ 编写单元测试 `tests/test_framing.cc`
```cpp
#include <gtest/gtest.h>
#include "mcpe/network/PacketFraming.h"

using namespace mcpe;
using namespace mcpe::network;

// 测试 1：完整单包直接切出
TEST(PacketFramingTest, SingleCompletePacket) {
  ByteBuffer buf;
  // 构造包：Length=3, ID=0x00, Payload="AB"
  buf.appendVarInt(3);
  buf.appendByte(0x00);
  buf.append("AB", 2);

  auto res = PacketFraming::slicePacket(buf);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->readableBytes(), 3);
  EXPECT_EQ(buf.readableBytes(), 0);  // 缓冲区已被恰好消费完
}

// 测试 2：TCP 粘包（两个包拼在一起，连续切出 2 个包）
TEST(PacketFramingTest, StickingPackets) {
  ByteBuffer buf;
  // 包 1：Length=2, Payload="P1"
  buf.appendVarInt(2);
  buf.append("P1", 2);
  // 包 2：Length=2, Payload="P2"
  buf.appendVarInt(2);
  buf.append("P2", 2);

  auto p1 = PacketFraming::slicePacket(buf);
  ASSERT_TRUE(p1.has_value());
  EXPECT_EQ(p1->retrieveAllAsString(), "P1");

  auto p2 = PacketFraming::slicePacket(buf);
  ASSERT_TRUE(p2.has_value());
  EXPECT_EQ(p2->retrieveAllAsString(), "P2");

  EXPECT_EQ(buf.readableBytes(), 0);
}

// 测试 3：TCP 半包（分批喂入，最后一次才返回包）
TEST(PacketFramingTest, TruncatedHalfPacket) {
  ByteBuffer buf;
  
  // 模拟包总长 5 字节
  // 第 1 次：只喂入了包长 VarInt (1 字节)
  buf.appendVarInt(4);
  auto r1 = PacketFraming::slicePacket(buf);
  EXPECT_EQ(r1.error(), FramingError::kIncomplete);
  EXPECT_EQ(buf.readableBytes(), 1); // 游标绝不回退或乱滑

  // 第 2 次：喂入 2 字节载荷（仍不够 4 字节）
  buf.append("AB", 2);
  auto r2 = PacketFraming::slicePacket(buf);
  EXPECT_EQ(r2.error(), FramingError::kIncomplete);

  // 第 3 次：补全最后 2 字节
  buf.append("CD", 2);
  auto r3 = PacketFraming::slicePacket(buf);
  ASSERT_TRUE(r3.has_value());
  EXPECT_EQ(r3->retrieveAllAsString(), "ABCD");
  EXPECT_EQ(buf.readableBytes(), 0);
}
```

##### ⑤ 更新 `CMakeLists.txt`
```cmake
# 将 PacketFraming 加入 mcpe_network
add_library(mcpe_network STATIC
    src/network/TcpSession.cc
    src/network/TcpServer.cc
    src/network/PacketFraming.cc
)

# 注册 test_framing 单测
add_executable(test_framing tests/test_framing.cc)
target_link_libraries(test_framing PRIVATE mcpe_network mcpe_common GTest::gtest_main)
set_target_properties(test_framing PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
gtest_discover_tests(test_framing)
```

#### 4. 验证命令
```bash
cmake --build build --target test_framing
ctest --test-dir build -R test_framing --output-on-failure
```
单测 100% 通过，证明拆包器能够完美对抗真实网络上的粘包与半包！

---

### TASK-1.5: 协议状态机框架搭建（ConnectionState 与 PacketRouter）

#### 1. 协议状态机原理
Minecraft 客户端与服务端的交互是一个严格的有向状态机：
```
[HANDSHAKING] (初始状态)
      │
      ├── NextState = 1 ──► [STATUS] (列表状态与 Ping 探测)
      └── NextState = 2 ──► [LOGIN]  (登录鉴权) ──► [PLAY] (进入游戏世界)
```
不同的状态下，相同的 Packet ID 具有完全不同的业务含义（例如 `0x00` 在 HANDSHAKING 下是 Handshake，在 STATUS 下是 Status Request，在 LOGIN 下是 Login Start，在 PLAY 下是 Teleport Confirm）！

#### 2. 在哪个目录创建什么文件
1. 创建头文件：`include/mcpe/protocol/ConnectionState.h`
2. 创建头文件：`include/mcpe/protocol/PacketRouter.h`
3. 创建源文件：`src/protocol/PacketRouter.cc`

#### 3. 详细代码实现

##### ① `include/mcpe/protocol/ConnectionState.h`
```cpp
#pragma once

#include <cstdint>
#include <string_view>

namespace mcpe::protocol {

enum class ConnectionState : uint8_t {
  HANDSHAKING = 0,
  STATUS = 1,
  LOGIN = 2,
  PLAY = 3
};

constexpr std::string_view toString(ConnectionState state) noexcept {
  switch (state) {
    case ConnectionState::HANDSHAKING: return "HANDSHAKING";
    case ConnectionState::STATUS:      return "STATUS";
    case ConnectionState::LOGIN:       return "LOGIN";
    case ConnectionState::PLAY:        return "PLAY";
  }
  return "UNKNOWN";
}

}  // namespace mcpe::protocol
```

##### ② `include/mcpe/protocol/PacketRouter.h`
```cpp
#pragma once

#include <memory>
#include "mcpe/common/ByteBuffer.h"
#include "mcpe/protocol/ConnectionState.h"

namespace mcpe::network {
class TcpSession;
}

namespace mcpe::protocol {

class PacketRouter {
 public:
  /**
   * 将切包后的原始数据包派发到对应的状态机业务处理函数
   */
  static void dispatch(const std::shared_from_this_t<network::TcpSession>& session,
                       ConnectionState state,
                       ByteBuffer packet);
};

}  // namespace mcpe::protocol
```

##### ③ `src/protocol/PacketRouter.cc`
```cpp
#include "mcpe/protocol/PacketRouter.h"
#include <spdlog/spdlog.h>
#include "mcpe/network/TcpSession.h"
#include "mcpe/protocol/StatusHandler.h"

namespace mcpe::protocol {

void PacketRouter::dispatch(const std::shared_ptr<network::TcpSession>& session,
                            ConnectionState state,
                            ByteBuffer packet) {
  // 1. 读取 Packet ID
  auto id_res = packet.readVarInt();
  if (!id_res) {
    spdlog::warn("[Router] Failed to read packet ID from {}", session->remoteAddress());
    session->stop();
    return;
  }

  const int32_t packet_id = *id_res;
  spdlog::debug("[Router] State: {}, PacketID: 0x{:02X}, PayloadBytes: {}",
                toString(state), packet_id, packet.readableBytes());

  // 2. 根据状态路由
  switch (state) {
    case ConnectionState::HANDSHAKING:
      if (packet_id == 0x00) {
        StatusHandler::handleHandshake(session, packet);
      } else {
        spdlog::warn("[Router] Unknown packet in HANDSHAKING: 0x{:02X}", packet_id);
        session->stop();
      }
      break;

    case ConnectionState::STATUS:
      if (packet_id == 0x00) {
        StatusHandler::handleStatusRequest(session);
      } else if (packet_id == 0x01) {
        StatusHandler::handlePing(session, packet);
      } else {
        spdlog::warn("[Router] Unknown packet in STATUS: 0x{:02X}", packet_id);
        session->stop();
      }
      break;

    case ConnectionState::LOGIN:
      spdlog::info("[Router] LOGIN state packet 0x{:02X} (Deferred to Sprint 2)", packet_id);
      break;

    case ConnectionState::PLAY:
      // ⚠️ 白名单忽略分支：防止客户端发来例行包导致服务端崩溃断连
      switch (packet_id) {
        case 0x04: // Client Settings
        case 0x09: // Plugin Message
        case 0x13: // Player Abilities
        case 0x1A: // Held Item Change
          spdlog::debug("[Router] Ignored routine PLAY packet 0x{:02X}", packet_id);
          break;
        default:
          spdlog::warn("[Router] Unhandled PLAY packet 0x{:02X}", packet_id);
          break;
      }
      break;
  }
}

}  // namespace mcpe::protocol
```

##### ④ 在 `TcpSession` 中绑定状态
在 `include/mcpe/network/TcpSession.h` 中增加：
```cpp
#include "mcpe/protocol/ConnectionState.h"

// 在 TcpSession 内：
public:
  protocol::ConnectionState state() const noexcept { return state_; }
  void setState(protocol::ConnectionState s) noexcept { state_ = s; }

private:
  protocol::ConnectionState state_{protocol::ConnectionState::HANDSHAKING};
```

---

### TASK-1.6: `Handshake (0x00)` 解析与意图流转

#### 1. 协议定义（详见 `doc/03_protocol_340_cheatsheet.md`）
* **Packet ID**: `0x00`
* **字段排列**：
  1. `Protocol Version`: `VarInt`（1.12.2 原版客户端上报的值为 `340`）
  2. `Server Address`: `String`（客户端填写的连入地址，如 `"127.0.0.1"`）
  3. `Server Port`: `Unsigned Short`（端口号，**大端 2 字节**）
  4. `Next State`: `VarInt`（`1` 为查状态 Status，`2` 为登录进服 Login）

#### 2. 在哪个目录创建什么文件
1. 创建头文件：`include/mcpe/protocol/StatusHandler.h`
2. 创建源文件：`src/protocol/StatusHandler.cc`

#### 3. 详细代码实现

##### ① `include/mcpe/protocol/StatusHandler.h`
```cpp
#pragma once

#include <memory>
#include "mcpe/common/ByteBuffer.h"

namespace mcpe::network {
class TcpSession;
}

namespace mcpe::protocol {

class StatusHandler {
 public:
  // [TASK-1.6] 处理 Handshake 握手包
  static void handleHandshake(const std::shared_ptr<network::TcpSession>& session,
                             ByteBuffer& payload);

  // [TASK-1.7] 处理 Status Request 状态查询请求
  static void handleStatusRequest(const std::shared_ptr<network::TcpSession>& session);

  // [TASK-1.8] 处理 Ping 延迟探测请求
  static void handlePing(const std::shared_ptr<network::TcpSession>& session,
                         ByteBuffer& payload);
};

}  // namespace mcpe::protocol
```

##### ② `src/protocol/StatusHandler.cc`（Handshake 部分）
```cpp
#include "mcpe/protocol/StatusHandler.h"
#include <spdlog/spdlog.h>
#include "mcpe/network/TcpSession.h"

namespace mcpe::protocol {

void StatusHandler::handleHandshake(const std::shared_ptr<network::TcpSession>& session,
                                   ByteBuffer& payload) {
  // 1. 解析 Protocol Version (VarInt)
  auto version_res = payload.readVarInt();
  // 2. 解析 Server Address (String)
  auto host_res = payload.readString();
  // 3. 解析 Server Port (大端 uint16_t)
  auto port_res = payload.readBe<uint16_t>();
  // 4. 解析 Next State (VarInt)
  auto next_state_res = payload.readVarInt();

  if (!version_res || !host_res || !port_res || !next_state_res) {
    spdlog::warn("[StatusHandler] Malformed handshake packet from {}", session->remoteAddress());
    session->stop();
    return;
  }

  const int32_t protocol_version = *version_res;
  const std::string& host = *host_res;
  const uint16_t port = *port_res;
  const int32_t next_state = *next_state_res;

  spdlog::info("[Handshake] Client {} -> ProtoVer: {}, Host: {}:{}, NextState: {}",
               session->remoteAddress(), protocol_version, host, port, next_state);

  if (next_state == 1) {
    // 切换到 STATUS 状态
    session->setState(ConnectionState::STATUS);
  } else if (next_state == 2) {
    // 切换到 LOGIN 状态（Sprint 2 处理）
    session->setState(ConnectionState::LOGIN);
    spdlog::info("[Handshake] Client wants to LOGIN. ProtocolVersion: {}", protocol_version);
  } else {
    spdlog::warn("[Handshake] Invalid next state {} from {}", next_state, session->remoteAddress());
    session->stop();
  }
}

}  // namespace mcpe::protocol
```

---

### TASK-1.7: `Status Request (0x00)` 与 JSON 描述响应

#### 1. 协议定义
* 客户端发来的 `Status Request (0x00)`：无载荷（`Payload` 长度为 0）。
* 服务端返回的 `Status Response (0x00)`：
  1. `Packet ID`: `0x00` (VarInt)
  2. `JSON Response`: `String`（前缀字符串 VarInt 字节长度，后跟 UTF-8 JSON 文本）
* JSON 内容规范：
  ```json
  {
    "version": {
      "name": "1.12.2",
      "protocol": 340
    },
    "players": {
      "max": 100,
      "online": 0
    },
    "description": {
      "text": "§aA High Performance C++ MC Server §7[Powered by Asio]"
    }
  }
  ```
  * `§a` 是 Minecraft 原版颜色码（绿色），`§7` 是灰色。
  * `protocol` 必须是整数 `340`，`name` 必须对应 `"1.12.2"`，否则客户端会显示红叉“客户端版本不兼容”！

#### 2. 在哪个目录实现
在 `src/protocol/StatusHandler.cc` 中实现 `handleStatusRequest`。

#### 3. 详细代码实现
```cpp
// 在 src/protocol/StatusHandler.cc 中：
#include <format> // C++23 std::format

void StatusHandler::handleStatusRequest(const std::shared_ptr<network::TcpSession>& session) {
  spdlog::debug("[StatusHandler] Responding to Status Request from {}", session->remoteAddress());

  // 1. 组装 JSON 字符串（无需额外引入三方 JSON 库，直接原生字符串模板高效构建）
  const std::string json_payload = std::format(
      R"({{"version":{{"name":"1.12.2","protocol":340}},"players":{{"max":100,"online":0}},"description":{{"text":"§aA High Performance C++ Server §7[Asio/C++23]"}}}})");

  // 2. 构造包体：[Packet ID (0x00)] + [String (VarInt len + JSON)]
  ByteBuffer body;
  body.appendVarInt(0x00);         // Packet ID = 0x00
  body.appendString(json_payload); // 自动追加 VarInt 长度前缀与字符串内容

  // 3. 封装外层包：[Total Packet Length (VarInt)] + [Body]
  ByteBuffer packet;
  packet.appendVarInt(static_cast<int32_t>(body.readableBytes()));
  packet.append(body.peek(), body.readableBytes());

  // 4. 下发给客户端
  session->sendPacket(std::move(packet));
}
```

---

### TASK-1.8: `Ping (0x01)` 与 `Pong (0x01)` 时间戳响应

#### 1. 为什么这步决定客户端是否显示“绿色信号条”？
客户端收到 JSON 之后，紧接着会发送 `Ping (0x01)`。  
客户端记录发送时刻，并在收到服务端的 `Pong (0x01)` 之后，计算 $\text{RTT} = T_{\text{recv}} - T_{\text{send}}$。只有正确收到了 Pong，多人游戏列表才会显示绿色延迟信号条（如 `<1ms` 或 `15ms`）！

> [!CAUTION]
> **底层避坑点：类型必须是 8 字节大端整数（Long）**  
> `Ping` 和 `Pong` 的载荷是原生的 8 字节大端整数（`int64_t`），**绝对不是 VarInt，也不是 4 字节 Int**！写错会导致客户端解析游标错位，列表无限转圈菊花或报“Ping 失败”。

#### 2. 在哪个目录实现
在 `src/protocol/StatusHandler.cc` 中实现 `handlePing`。

#### 3. 详细代码实现
```cpp
// 在 src/protocol/StatusHandler.cc 中：

void StatusHandler::handlePing(const std::shared_ptr<network::TcpSession>& session,
                               ByteBuffer& payload) {
  // 1. 读取 8 字节大端时间戳
  auto time_res = payload.readBe<int64_t>();
  if (!time_res) {
    spdlog::warn("[StatusHandler] Malformed Ping packet from {}", session->remoteAddress());
    session->stop();
    return;
  }

  const int64_t client_time = *time_res;
  spdlog::debug("[StatusHandler] Ping timestamp: {} from {}", client_time, session->remoteAddress());

  // 2. 封装 Pong (0x01) 响应包
  // 包体：Packet ID 0x01 (VarInt) + 8 字节原样时间戳
  ByteBuffer body;
  body.appendVarInt(0x01);
  body.writeBe<int64_t>(client_time);

  // 外层包裹包长 VarInt
  ByteBuffer packet;
  packet.appendVarInt(static_cast<int32_t>(body.readableBytes()));
  packet.append(body.peek(), body.readableBytes());

  // 3. 发送 Pong
  session->sendPacket(std::move(packet));

  // 4. Minecraft 规范：发完 Pong 后，列表探测流程圆满结束，连接可随客户端主动关闭或保持
  spdlog::info("[StatusHandler] Server List Ping flow completed for {}", session->remoteAddress());
}
```

---

### TASK-1.9: Sprint 1 集成验收与原版客户端联调

#### 1. 完善 `CMakeLists.txt`
将所有网络层与协议层源文件纳入 CMake 构建体系：
```cmake
# ----------------------------------------------
# 模块2：mcpe_network 静态库
# ----------------------------------------------
add_library(mcpe_network STATIC
    src/network/TcpSession.cc
    src/network/TcpServer.cc
    src/network/PacketFraming.cc
)
target_include_directories(mcpe_network PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(mcpe_network PUBLIC mcpe_common)

# ----------------------------------------------
# 模块3：mcpe_protocol 静态库
# ----------------------------------------------
add_library(mcpe_protocol STATIC
    src/protocol/PacketRouter.cc
    src/protocol/StatusHandler.cc
)
target_include_directories(mcpe_protocol PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(mcpe_protocol PUBLIC mcpe_common mcpe_network)

# ----------------------------------------------
# 可执行文件 main
# ----------------------------------------------
target_link_libraries(main PRIVATE mcpe_protocol mcpe_network mcpe_common)
```

#### 2. 在 `TcpSession.cc` 接收完整包后调用 `PacketRouter`
```cpp
// 在 src/network/TcpSession.cc 的 slicePacket 循环内部：
#include "mcpe/protocol/PacketRouter.h"

// 当成功切出完整包后：
ByteBuffer packet = std::move(*frame_res);
protocol::PacketRouter::dispatch(shared_from_this(), state_, std::move(packet));
```

---

## 四、 自动化调试利器：Python 极速探测验证脚本

在打开笨重的 Minecraft 客户端之前，先用下面这个 40 行的自动化 Python 脚本瞬间验证整个 Server List Ping 流程！

创建脚本文件：`scripts/test_ping.py`

```python
#!/usr/bin/env python3
# scripts/test_ping.py - 极速 Server List Ping 验收脚本
import socket
import struct
import json
import time

def encode_varint(val: int) -> bytes:
    res = bytearray()
    while True:
        b = val & 0x7F
        val >>= 7
        if val != 0:
            b |= 0x80
        res.append(b)
        if val == 0:
            break
    return bytes(res)

def read_varint(s: socket.socket) -> int:
    val = 0
    for i in range(5):
        b = s.recv(1)[0]
        val |= (b & 0x7F) << (7 * i)
        if (b & 0x80) == 0:
            return val
    raise ValueError("VarInt too big")

print("[*] 正在连接本地 127.0.0.1:25565 ...")
sock = socket.create_connection(('127.0.0.1', 25565), timeout=3.0)

# 1. 发送 Handshake 包 (ID=0x00, Proto=340, Host="127.0.0.1", Port=25565, NextState=1)
host_bytes = b"127.0.0.1"
handshake_payload = (
    encode_varint(0x00) +
    encode_varint(340) +
    encode_varint(len(host_bytes)) + host_bytes +
    struct.pack('>H', 25565) +
    encode_varint(1)
)
sock.sendall(encode_varint(len(handshake_payload)) + handshake_payload)
print("[+] Handshake 已发送")

# 2. 发送 Status Request 包 (ID=0x00, 无载荷)
status_req = encode_varint(0x00)
sock.sendall(encode_varint(len(status_req)) + status_req)
print("[+] Status Request 已发送")

# 3. 接收 Status Response
packet_len = read_varint(sock)
packet_id = read_varint(sock)
assert packet_id == 0x00, f"Expected 0x00, got {packet_id}"
json_len = read_varint(sock)
raw_json = sock.recv(json_len).decode('utf-8')
data = json.loads(raw_json)
print("\n[✔] 成功解析服务端返回的 MOTD JSON:")
print(json.dumps(data, indent=2, ensure_ascii=False))

# 4. 发送 Ping 包
now_ms = int(time.time() * 1000)
ping_payload = encode_varint(0x01) + struct.pack('>q', now_ms)
sock.sendall(encode_varint(len(ping_payload)) + ping_payload)

# 5. 接收 Pong 包
pong_len = read_varint(sock)
pong_id = read_varint(sock)
assert pong_id == 0x01, f"Expected 0x01, got {pong_id}"
echo_ms = struct.unpack('>q', sock.recv(8))[0]
assert echo_ms == now_ms, f"Timestamp mismatch: {echo_ms} vs {now_ms}"
print(f"\n[✔] Ping/Pong 往返成功！时间戳校验一致 ({now_ms})")
print("🎉 恭喜！Sprint 1 全部网络与协议流程验证 100% 通过！")
sock.close()
```

运行验证：
```bash
python3 scripts/test_ping.py
```
当终端打印出漂亮的 JSON 和 `[✔] Ping/Pong 往返成功` 时，说明你的服务端已经完全符合原版协议规范！此时打开游戏客户端，刷新多人游戏，就会看到绿色的延迟信号和漂亮的 MOTD！

---

## 五、 Sprint 1 避坑与面试高频考点速查表

| 序号 | 经典踩坑场景 | 根因分析 | 工业级解决方案 |
| :--- | :--- | :--- | :--- |
| **1** | **程序运行随机崩溃**（野指针访问或内存破坏） | 在 Asio 异步回调 lambda 中直接捕获裸指针 `[this]`，当客户端异常切断导致 session 析构后，回调仍在执行并访问已销毁的成员。 | **智能指针保活原则**：Session 继承 `enable_shared_from_this`，在异步回调中一律捕获 `self = shared_from_this()`。 |
| **2** | **Asio 内部断言崩溃** | 在同一个 socket 上并发调用 `async_write`。 | **引入发送队列**：维护 `std::deque<ByteBuffer>` 与 `is_writing_` 标记，严格串行化异步写。 |
| **3** | **客户端报“协议错误”或断连** | VarInt 长度未处理半包；直接把不完整的字节当作有效长度消费，导致后续所有字节偏移错位。 | **事务性回滚**：使用 `ByteBuffer::peekVarInt()` 与 `Mark` 机制，半包时绝不消费任何字节，等待更多数据到来。 |
| **4** | **多人游戏列表无限菊花，算不出延迟** | 将 `Ping/Pong` 的时间戳当作 `VarInt` 或 `Int`（4 字节）解析。 | 协议铁律：Ping/Pong 载荷严格为 **8 字节大端定长整数**（`int64_t`），用 `readBe<int64_t>()` / `writeBe<int64_t>()` 读写。 |
| **5** | **客户端显示红叉“不兼容的协议版本”** | Status JSON 中的 `protocol` 填错（例如填成 1.8 的 47），或者没有对应 `"name": "1.12.2"`。 | 严格填报 Protocol ID `340` 和名称 `"1.12.2"`。 |
