# MCPE 重构后架构全景、类职责清单与数据流转总览 (Architecture & Data Flow Summary)

> **文档定位**：系统性总结 Sprint 1/2 架构深度重构与 Netty 零拷贝升级后的工程拓扑、模块职责划分与全链路数据流转。  
> **对标标杆**：工业级 Minecraft 服务端 Cuberite (`cProtocol`, `cClientHandle`, `cServer`, `cByteBuffer`)。

---

## 一、系统整体架构全景 (System Architecture Overview)

系统严格恪守**四层单向依赖架构**（Foundations $\to$ Network $\to$ Protocol $\to$ Domain），底层完全不知晓上层业务，各层通过清晰的抽象接口与所有权模型进行协作：

```
┌────────────────────────────────────────────────────────────────────────┐
│                        4. Domain (领域与世界层)                        │
│          World / Chunk / Entity / Inventory  [Sprint 3+ 重点演进]       │
└───────────────────────────────────▲────────────────────────────────────┘
                                    │ (事件分发 / 状态变更)
┌───────────────────────────────────┴────────────────────────────────────┐
│                       3. Protocol (协议与会话层)                        │
│ ┌────────────────────────────────────────────────────────────────────┐ │
│ │  ClientManager (全局在线会话注册中心、动态在线统计、广播与踢人)     │ │
│ ├────────────────────────────────────────────────────────────────────┤ │
│ │  ClientSession (会话生命周期、状态机驱动、心跳保活、startPlay)     │ │
│ ├────────────────────────────────────────────────────────────────────┤ │
│ │  PacketRouter (对标 cProtocol, 协议路由分发、动态在线 MOTD)        │ │
│ ├────────────────────────────────────────────────────────────────────┤ │
│ │  各状态 Handler (HandshakeHandler, StatusHandler, Login, Play)     │ │
│ ├────────────────────────────────────────────────────────────────────┤ │
│ │  PacketWriter (RAII 自动 VarInt 包长前插与出站封包)                │ │
│ └────────────────────────────────────────────────────────────────────┘ │
└───────────────────────────────────▲────────────────────────────────────┘
                                    │ (SessionCallbacks: onPacket / onDisconnect)
┌───────────────────────────────────┴────────────────────────────────────┐
│                       2. Network (网络传输层)                          │
│ ┌────────────────────────────────────────────────────────────────────┐ │
│ │  TcpServer (Asio 异步监听、非阻塞连接接入、Session 生命周期管理)   │ │
│ ├────────────────────────────────────────────────────────────────────┤ │
│ │  TcpSession (单连接异步 I/O 协程、双工读写、发送缓冲队列)           │ │
│ ├────────────────────────────────────────────────────────────────────┤ │
│ │  PacketFraming (无状态流式定界拆包器、2MB 边界防御、readSlice 切包) │ │
│ └────────────────────────────────────────────────────────────────────┘ │
└───────────────────────────────────▲────────────────────────────────────┘
                                    │ (读写内存切片 / 引用计数)
┌───────────────────────────────────┴────────────────────────────────────┐
│                     1. Foundations (底层基础库)                        │
│  ByteBuffer (Netty 零拷贝切片、COW 写时复制防护、泛型大端读写)         │
│  VarInt (纯算术位运算编解码、无副作用试探解析、零魔数)                 │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 二、各个类的职责清单 (Class Responsibilities Matrix)

### 1. 底层基础库 (`src/common/`)

| 类名 | 核心职责与设计契约 | 对标 Cuberite 组件 | 关键机制与亮点 |
| :--- | :--- | :--- | :--- |
| **`ByteBuffer`** | 统一字节缓冲区，负责所有网络 I/O 字节流的暂存、切片与基础数据类型大端序列化/反序列化。 | `cByteBuffer` | **Netty 零拷贝切片与 COW 防护**：通过 `std::shared_ptr<Storage>` 共享物理内存；`readSlice()` 切片零拷贝；检测到共享时触发写时复制，根除野指针。 |
| **`VarInt`** | Minecraft 协议专用可变长整型（VarInt/VarLong）的无状态独立编解码器。 | `cByteBuffer` 内嵌位操作 | 纯算术位移，无魔数常量（`kContinuationBit`），提供 `decodeVarInt(const char*, size_t)` 供拆包无副作用探测。 |

### 2. 网络传输层 (`src/network/`)

| 类名 | 核心职责与设计契约 | 对标 Cuberite 组件 | 关键机制与亮点 |
| :--- | :--- | :--- | :--- |
| **`TcpServer`** | 管理 TCP 监听套接字（`acceptor_`），负责非阻塞异步接收客户端连接，并绑定生命周期。 | `cServer` / `cNetwork` | 纯网络层抽象，完全不感知 Minecraft 协议。接受连接后创建 `TcpSession` 并注册至 `ClientManager`。 |
| **`TcpSession`** | 单个 TCP 连接的全双工异步通信管理器。管理网络协程、累积接收缓冲区与出站写队列。 | `cSocket` / `cConnection` | 纯网络传输，**严禁包含 Packet ID 或业务逻辑**。通过 `SessionCallbacks` 纯虚接口与上层协议层解耦。 |
| **`PacketFraming`** | Minecraft 变长 VarInt 长度前缀的流式定界拆包器。负责粘包合并与半包探测。 | `cProtocol::ReadPacket` | **无状态纯函数**。2MB 长度防御抗攻击；半包零副作用；成功时直接调用 `recv_buf.readSlice(...)` 实现真正的零拷贝分包。 |
| **`SessionCallbacks`** | 网络传输层通知协议层的抽象回调接口契约（`onPacket`, `onDisconnect`）。 | `cClientHandle` 网络回调 | 彻底斩断 `TcpSession` 对业务层强依赖的关键隔离层。 |

### 3. 协议与会话层 (`src/protocol/`)

| 类名 | 核心职责与设计契约 | 对标 Cuberite 组件 | 关键机制与亮点 |
| :--- | :--- | :--- | :--- |
| **`ClientSession`** | 单个客户端在协议层的主控对象，维护玩家会话的生命周期与状态机转换。 | `cClientHandle` | 继承 `SessionCallbacks`。持有状态机 `ClientState`；管理心跳定时器；通过 `startPlay()` 自驱动驱动生命周期解耦。 |
| **`ClientManager`** | 全局在线会话注册与在线玩家管理中心。 | `cServer::GetClientManager()` | 线程安全管理所有在线 `ClientSession`；维护真实在玩人数 `playerCount()`；提供多播广播 `broadcast()` 与全服踢人 `kickAll()`。 |
| **`PacketRouter`** | 统一协议包分发网关。根据会话当前状态分发至对应 Handler。 | `cProtocol` | 继承 `ProtocolHandler`。向 `ClientManager` 查询真实在玩人数组装动态 MOTD；未消费字节警报与畸形包防御。 |
| **`HandshakeHandler`** | 专职承接 `HANDSHAKING` 根状态。处理客户端握手包（0x00）。 | `cProtocol1122::HandleHandshake` | 单一职责。校验协议版本号（340），驱动状态分支走向 `STATUS` 或 `LOGIN`，彻底消灭越权。 |
| **`StatusHandler`** | 专职承接 `STATUS` 状态（Server List Ping 服务器列表状态查询）。 | `cProtocol::HandleStatus` | 响应 Status Request (0x00) 并回传 JSON MOTD；响应 Ping (0x01) 原样回显客户端时间戳。 |
| **`LoginHandler`** | 专职承接 `LOGIN` 状态（玩家登录鉴权流程）。 | `cProtocol::HandleLogin` | 校验玩家用户名；调用 `AuthUtils` 生成官方离线 UUID；下发 Login Success (0x02) 并触发 `session.startPlay()`。 |
| **`PlayHandler`** | 专职承接 `PLAY` 状态（游戏内正常游玩通信）。 | `cProtocol::HandlePlay` | 下发 Join Game (0x23)、玩家出生点坐标 (0x2F)；例行包白名单静默消费；下发周期性心跳 KeepAlive (0x1F) 并校验回显。 |
| **`PacketWriter`** | RAII 风格的出站数据包序列化构建器。 | `cPacketizer` | 基于栈对象生命周期。自动在作用域析构时为数据包原地前插 VarInt 长度头，并调用 `session.send(...)` 提交网络队列。 |
| **`AuthUtils`** | 离线身份鉴权与 UUID 工具库。 | 官方 UUID 生成工具 | 实现 RFC 4122 v3 MD5 官方离线 UUID 生成算法与 Minecraft 玩家名合规性校验。 |
| **`GameTypes.h`**<br>`PacketTypes.h` | 协议常量、包 ID、维度、游戏模式、服务器配置结构体。 | 全局常量系统 | **彻底消灭代码中所有硬编码裸数字与魔数**。保证强类型安全。 |

---

## 三、当前阶段核心数据流转图谱 (End-to-End Data Flows)

### 数据流 1：入站网络数据流与 Netty 零拷贝拆包链路

展示 TCP 原始字节流如何经过定界拆包、零拷贝切片，最终抵达业务 Handler 的全过程：

```mermaid
sequenceDiagram
    autonumber
    participant Socket as OS TCP Socket
    participant TcpSession as TcpSession (Coro Loop)
    participant Buf as cumulative_buf_ (ByteBuffer)
    participant Framing as PacketFraming
    participant ClientSession as ClientSession
    participant Router as PacketRouter
    participant Handler as Specialized Handler

    Socket->>TcpSession: async_read_some (收到网络字节)
    TcpSession->>Buf: ensureWritableBytes(4096)
    Note over Buf: 若正被在途切片共享(use_count>1)<br/>自动触发 COW 重新分配内存隔离
    TcpSession->>Buf: hasWritten(n) (写入累积缓冲区)
    
    loop 循环拆包消除粘包
        TcpSession->>Framing: slicePacket(cumulative_buf_)
        Framing->>Buf: peekVarIntWithBytes() (无副作用测长)
        alt 字节不够 (半包)
            Framing-->>TcpSession: return FramingError::kIncomplete (游标零移动)
        else 整包已就绪
            Framing->>Buf: retrieve(varint_len) (跳过包长)
            Framing->>Buf: readSlice(packet_len) (零拷贝切片)
            Note over Buf,Framing: 子切片共享底层 Storage 内存<br/>零 memcpy，零二次堆内存分配
            Framing-->>TcpSession: return packet (ByteBuffer 切片)
            TcpSession->>ClientSession: callbacks_->onPacket(packet)
            ClientSession->>Router: handlePacket(packet)
            Router->>Handler: 依据 state() 转交对应 Handler
            Handler->>Handler: 直接从 packet 流式反序列化字段
        end
    end
```

---

### 数据流 2：状态机流转与玩家入服全生命周期

展示客户端从建立连接、握手分支选择、状态流转到游戏内保活的完整生命周期：

```mermaid
flowchart TD
    Start([客户端连接建立]) --> C1[创建 TcpSession & ClientSession]
    C1 --> C2[注册至 ClientManager]
    C2 --> S0[初始状态: HANDSHAKING]

    S0 -->|收到 Packet 0x00| H[HandshakeHandler]
    
    H -->|NextState == 1| S1[切换状态: STATUS]
    H -->|NextState == 2| S2[切换状态: LOGIN]

    subgraph "分支 A: Server List Ping (服务器列表状态查询)"
        S1 --> P0[收到 0x00 Status Request]
        P0 --> ST1[StatusHandler 查询 ClientManager 动态人数]
        ST1 --> ST2[下发 JSON 格式 MOTD 0x00]
        ST2 --> P1[收到 0x01 Ping 时间戳]
        P1 --> ST3[下发 0x01 Pong 原样回显时间戳]
        ST3 --> Disc1([客户端正常断开])
    end

    subgraph "分支 B: 玩家正式登录进服"
        S2 --> L0[收到 0x00 Login Start]
        L0 --> L1[LoginHandler 校验玩家名]
        L1 --> L2[AuthUtils 计算官方离线 UUID]
        L2 --> L3[下发 0x02 Login Success]
        L3 --> L4[ClientSession::startPlay]
        L4 --> S3[切换状态: PLAY]
        L4 --> CM[ClientManager::markPlayerActive]
        
        S3 --> PL1[PlayHandler 下发 0x23 Join Game]
        PL1 --> PL2[PlayHandler 下发 0x2F Player Position And Look]
        PL2 --> PL3[客户端回传 0x00 Teleport Confirm]
        PL3 --> PL4[客户端发送 Client Settings 等例行包]
        PL4 --> Loop[启动周期性 KeepAlive 心跳保活循环]
    end
```

---

### 数据流 3：出站包封装与发送链路 (PacketWriter RAII)

展示服务端如何通过 `PacketWriter` 高效构建出站包，利用头部预留空间（Cheap Prepend）实现 O(1) 长度前缀前插并异步发送：

```mermaid
sequenceDiagram
    autonumber
    participant Handler as 业务 Handler (如 PlayHandler)
    participant Writer as PacketWriter (RAII 栈对象)
    participant Buf as writer.buffer() (ByteBuffer)
    participant Session as TcpSession
    participant Socket as OS Socket

    Handler->>Writer: 创建 PacketWriter(session, packet_id)
    Writer->>Buf: appendVarInt(packet_id) (预留头部8字节后写入包ID)
    Handler->>Writer: 序列化业务载荷 (writeBe, appendString 等)
    Writer->>Buf: 追加 Payload 字段数据
    Handler->>Writer: 作用域结束 / 析构 ~PacketWriter()
    Note over Writer,Buf: 自动前插包长 VarInt 前缀<br/>buf.prependVarInt(buf.readableBytes())<br/>O(1) 原地前插，无内存挪动重分配
    Writer->>Session: session.send(std::move(buf))
    Session->>Session: 压入 write_queue_
    Session->>Socket: asio::async_write (无阻塞异步刷盘发送)
```

---

### 数据流 4：全局会话管理与 MOTD 实时联动链路

展示客户端的进服与离线如何全自动同步至全局会话中心并体现在服务器 MOTD 响应中：

```mermaid
sequenceDiagram
    autonumber
    participant ClientA as 玩家客户端 (Steve)
    participant Manager as ClientManager (全局在线管理)
    participant ClientB as 探测客户端 (Ping)
    participant Router as PacketRouter / StatusHandler

    Note over Manager: 当前在线: 0 人
    ClientA->>Manager: 连接接入: registerSession(session_A)
    ClientA->>ClientA: 完成登录鉴权: Login Success
    ClientA->>Manager: markPlayerActive(session_A, "Steve")
    Note over Manager: 当前在玩: 1 人 (Steve)

    ClientB->>Router: 发送 0x00 Status Request (查询服务器列表)
    Router->>Manager: client_manager_->playerCount()
    Manager-->>Router: 返回真实在玩人数: 1 人
    Router->>ClientB: 下发 MOTD JSON ("online": 1, "max": 100)
    Note over ClientB: 客户端多人列表显示: 1/100 在线

    ClientA->>Manager: 玩家退出 / 网络断开: unregisterSession(id_A)
    Note over Manager: 原子扣减，当前在玩: 0 人
```

---

## 四、重构带来的核心工程收益 (Architectural Wins)

1. **零拷贝与绝对内存安全并存**：
   * 删除了存在悬挂指针风险且重复度 100% 的 `ByteReader`；
   * 在 `ByteBuffer` 内部确立 Netty 风格共享引用计数切片与 Copy-On-Write 防护，彻底消灭了拆包环节的二次堆分配与 payload memcpy。
2. **纯粹的模块单一职责 (SRP)**：
   * `TcpSession` 不再有任何协议解析代码，回归纯粹的网络套接字收发；
   * `HandshakeHandler` 独立承接握手，解除了 `StatusHandler` 的历史越权；
   * `ClientSession::startPlay()` 驱动生命周期，解除了登录与游戏业务的强耦合。
3. **全局会话与动态感知就绪**：
   * `ClientManager` 实现了全局在线会话统一管理，MOTD 不再硬编码虚假在线人数，并具备向全服多播广播（`broadcast`）与全服踢人（`kickAll`）的工业级能力。
4. **彻底告别魔数与硬编码**：
   * `GameTypes.h` 与 `PacketTypes.h` 将所有版本、包 ID、维度、配置全面枚举化与强类型化，代码可读性与健壮性达到工业级水准。
