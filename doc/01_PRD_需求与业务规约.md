# 01_PRD_需求与业务规约 (Product Requirements Document & Business Rules)

> **文档标识**：`MCPE-DOC-PRD-01`  
> **版本**：v1.0.0  
> **更新日期**：2026-09-21  
> **目标产品**：MCPE (High-Performance C++23 Minecraft Protocol Server Engine)  
> **对齐协议**：**Minecraft Java Edition 1.12.2（Protocol Version: 340）**

---

## 一、 产品概述与业务背景

### 1.1 背景与痛点
官方原版 Minecraft 服务端（Mojang Vanilla）及主流社区服务端（Spigot / Paper）均基于 Java 语言构建。在大型多人在线（MMO）、小游戏大厅及公会对抗等高密度并发场景下，存在以下技术痛点：
1. **JVM 运行时沉重与 GC 停顿**：高并发实体移动与网络包高频流转会产生数以万计的短期临时对象，导致 JVM Young GC 频繁甚至触发 Stop-The-World（STW）全局停顿，破坏游戏 50ms (20 TPS) 节拍器。
2. **多线程安全与锁竞争**：Java 传统服务端多采用全服大粗锁或主线程串行模型，无法充分发挥现代多核 CPU 的并行计算吞吐。
3. **内存开销巨大**：每个空载实例常驻内存动辄数百 MB 到上 GB，难以实现高密度的容器化边缘部署与微服务化架构。

### 1.2 产品愿景与定位
MCPE 旨在利用 **现代 C++23 原生编译语言** 的零开销抽象与高性能网络能力，打造一款面向 Minecraft 协议的高性能无头网关与空间同步引擎：
* **原生极限性能**：无 JVM 开销，确定性内存管理，无 GC 停顿，CPU 与内存开销仅为 Java 服务端的 1/5 ~ 1/10。
* **高吞吐网络架构**：基于 Standalone Asio 异步非阻塞 Reactor 模型与 Strand 序列化，单机支撑数千连接并发稳定交互。
* **清晰解耦与微服务生态**：通过 Monorepo 架构与 Go + gRPC 微服务（EnderVault）无缝集成，将重计算空间物理引擎与高并发数据中心（Redis/MySQL）解耦。

---

## 二、 协议兼容性与准入规约

### 2.1 协议版本定义
* **目标客户端**：Minecraft Java Edition **1.12.2** 官方标准发行版。
* **协议版本号（Protocol Number）**：**`340`**。
* **传输层**：标准 TCP / IP 协议，默认监听端口 **`25566`**（避开 Cuberite 的 `25565`）。
* **参考规范**：严谨遵循 wiki.vg Protocol 340 规约（参见离线镜像规范 [`docs/protocol/protocol_340_cheatsheet.md`](protocol/protocol_340_cheatsheet.md)）。

### 2.2 协议版本校验规约
1. 当客户端握手包（`Handshake 0x00`）携带的 `protocol_version != 340` 时：
   - 若状态转移为 `Status`（探测）：正常回传当前服务端的协议版本（340）及 MOTD，客户端列表会显示为黄色或红色版本不兼容提示（符合官方客户端行为）。
   - 若状态转移为 `Login`（登录）：服务端应下发 `Disconnect (Login) 0x00` 数据包，以人类可读的富文本 JSON 告知版本不匹配原因，并立即主动优雅断开 TCP 连接。

---

## 三、 核心业务场景与功能用例规约 (Use Cases)

```
┌─────────┐                ┌──────────────┐                 ┌─────────────┐
│ Client  │                │  MCPE Server │                 │ EnderVault  │
└────┬────┘                └──────┬───────┘                 └──────┬──────┘
     │                            │                                │
     │  [UC-01: Server List Ping] │                                │
     │ ──── Handshake (1) ───────>│                                │
     │ ──── Status Request ──────>│                                │
     │ <─── Status Response (JSON)│                                │
     │ ──── Ping (Timestamp) ────>│                                │
     │ <─── Pong (Timestamp) ─────│                                │
     │                            │                                │
     │  [UC-02: Login Auth]       │                                │
     │ ──── Handshake (2) ───────>│                                │
     │ ──── Login Start (Name) ──>│                                │
     │                            │ ──── gRPC GetPlayerProfile ───>│
     │                            │ <─── Profile & Offline UUID ───│
     │ <─── Login Success (UUID) ─│                                │
     │                            │                                │
     │  [UC-03: Join & Spawn]     │                                │
     │ <─── Join Game (0x23) ─────│                                │
     │ <─── Spawn Position (0x46)─│                                │
     │ <─── Player Pos & Look ────│                                │
     │ ──── Teleport Confirm ────>│                                │
     │                            │                                │
     │  [UC-04: Chunk Streaming]  │                                │
     │ <─── Chunk Data (0x20) ────│                                │
     │                            │                                │
```

### 3.1 UC-01: 服务器列表探测 (Server List Ping & MOTD)
* **前置条件**：客户端在游戏主菜单选择“多人游戏”并添加当前服务器 IP:Port。
* **业务流程**：
  1. 客户端发送 `Handshake (0x00)`，携带目标状态 `NextState = 1 (Status)`。
  2. 客户端发送 `Status Request (0x00)`（空包体）。
  3. 服务端回传 `Status Response (0x00)`，载荷为标准 JSON 字符串，包含：
     - `version`: `{"name": "1.12.2", "protocol": 340}`
     - `players`: `{"max": 100, "online": N, "sample": [...]}`
     - `description`: 富文本/纯文本 MOTD 描述信息。
  4. 客户端立即发送 `Ping (0x01)`，携带 64 位大端微秒级时间戳。
  5. 服务端收到后原封不动构造 `Pong (0x01)` 回传，供客户端计算真实网络 RTT 延迟。
* **完成定义 (DoD)**：真实 1.12.2 官方客户端多人列表能正常刷出绿色 Ping 信号条，显示设定 MOTD 与在线人数。

### 3.2 UC-02: 玩家准入与生命周期认证 (Login Lifecycle)
* **前置条件**：客户端点击进入服务器。
* **业务流程**：
  1. 客户端发送 `Handshake (0x00)`，携带目标状态 `NextState = 2 (Login)`。
  2. 客户端发送 `Login Start (0x00)`，携带玩家名称（UTF-8 String，长度 1~16 字符）。
  3. 服务端对玩家名称进行合规校验（正则表达式 `^[a-zA-Z0-9_]{1,16}$`）。
  4. 离线模式鉴权：
     - 生成离线 UUID：遵循 RFC-4122 v3 规则，执行 `MD5("OfflinePlayer:" + username)` 并规范化为 128 位 UUID。
  5. 服务端回传 `Login Success (0x02)`，告知客户端其分配的 UUID 和 Username。
  6. 会话状态原子跃迁至 `Play (State 3)`。

### 3.3 UC-03: 虚空进服与初始定位 (Join Game & Spawn)
* **前置条件**：会话状态成功迁移至 `Play`。
* **业务流程**：
  1. 服务端下发 `Join Game (0x23)`：
     - 实体 EID（Entity ID，全服自增唯一正整型）。
     - 游戏模式：`1 (Creative)` 或 `0 (Survival)`。
     - 维度标识：`0 (Overworld 主世界)`（4 字节 Int，对齐 Protocol 340 规范）。
     - 难度：`1 (Easy)`，最大玩家数：`100`，世界类型：`"flat"`。
  2. 服务端下发出生点方块坐标 `Spawn Position (0x46)`（X=0, Y=64, Z=0）。
  3. 服务端下发玩家初始坐标与朝向 `Player Position And Look (0x2F)`：
     - 坐标：`X=0.0, Y=64.0, Z=0.0`，朝向：`Yaw=0.0, Pitch=0.0`。
     - 携带服务端生成的 `Teleport ID (VarInt)`。
  4. **确认闭环**：客户端加载场景并就位后，必须向服务端发送 `Teleport Confirm (0x00)`，回传相同的 `Teleport ID`。在收到该确认包前，服务端不接受该玩家的任何自主移动更新。

### 3.4 UC-04: 空间世界与区块流式下发 (Chunk Streaming)
* **前置条件**：玩家完成进服初始化。
* **业务流程**：
  1. 服务端根据玩家当前所在的区块坐标 $(ChunkX, ChunkZ)$，计算其观察视距半径（View Distance，默认 $R=8$）。
  2. 采用螺旋式（Spiral Order）或中心扩散方式，向客户端流式下发 `Chunk Data (0x20)` 数据包。
  3. 每个区块包含垂直方向的若干 `ChunkSection`（16x16x16 方块）：
     - 采用 **超平坦生成规则**：$Y=0$ 为基岩（Bedrock），$Y=1\sim 3$ 为泥土（Dirt），$Y=4$ 为草方块（Grass Block），其余为空气（Air）。
     - 采用调色板（Palette）或直接映射紧凑编码，消除冗余带宽传输。
* **完成定义**：客户端成功渲染实体地面，玩家双脚踏在草方块上，不再陷入虚空下坠。

### 3.5 UC-05: 玩家移动与动作同步 (Player Movement)
* **业务流程**：
  1. 客户端在游戏内跑跳漫游，以每秒约 20 次的频率向上行链路发送移动数据包：
     - `Player Position (0x0E)`：仅改变坐标 $(X, Y, Z, OnGround)$。
     - `Player Look (0x0F)`：仅改变朝向 $(Yaw, Pitch, OnGround)$。
     - `Player Position And Look (0x10)`：同时改变位置与朝向。
  2. 服务端在收到后校验位移合法性（防外挂瞬移校验）。
  3. 广播给该玩家所在的九宫格 AOI 观察者视野内的其他玩家，下发 `Entity Relative Move (0x26)` 或 `Entity Look (0x28)`。

### 3.6 UC-06: 方块挖掘与交互 (Block Digging & Placement)
* **业务流程**：
  1. 玩家左键长按挖掘方块，客户端发送 `Player Digging (0x14)`（Status: 0=开始挖掘, 2=完成挖掘）。
  2. 服务端计算挖掘合法性（触碰距离 < 6.0 格），在世界区块内存中将该坐标方块修改为空气（Air）。
  3. 服务端向受影响区域的所有玩家广播 `Block Change (0x0B)`，更新客户端本地显示。

### 3.7 UC-07: 双向心跳与保活探测 (KeepAlive Heartbeat)
* **业务流程**：
  1. 服务端世界定时器每隔固定周期（默认 $15$ 秒）向客户端发送 `Keep Alive (0x1F)`，包体携带一个随机的 64 位整型 ID（`Long`，Protocol 340 严格规约）。
  2. 客户端收到后必须立即回传相同的 ID：`Keep Alive (0x0B)`。
  3. 服务端记录最后一次心跳回执时间戳；若连续超过 $30$ 秒未收到心跳回传，判定客户端假死或掉线，服务端主动切断 TCP 会话并清理内存实体。

---

## 四、 业务规约与安全防挂边界 (Business & Security Rules)

| 规约编号 | 规则名称 | 详细判定与防御措施 | 违背处罚 |
| :---: | :--- | :--- | :--- |
| **BR-01** | **状态隔离与包合法性校验** | 客户端只能发送当前 `ConnectionState` 下注册的合法 Packet ID。严禁在 `Status` 状态发送 `Play` 阶段包，严禁在未完成登录时发送 `Player Position`。 | 立即断开 TCP 连接并记入恶意日志 |
| **BR-02** | **变长整型长度防 DoS** | VarInt 长度定界帧最大不得超过 5 个字节，单个网络封包的最大净载荷不得超过 **2MB**（`2 * 1024 * 1024` 字节）。 | 立即丢弃并强制断连，防止内存耗尽攻击 |
| **BR-03** | **Teleport ID 闭环强制锁定** | 服务端下发 `Player Position And Look` 后，将玩家标记为“等待传送确认”状态。在此期间忽略客户端的一切移动包，直到收到匹配的 `Teleport Confirm`。 | 丢弃未确认期间的伪造移动包 |
| **BR-04** | **移动速度物理上限检测** | 单个 Tick（50ms）内玩家水平位移不得超过合理阈值（如创造模式飞行 20m/s，生存模式疾跑 10m/s），防止瞬移外挂。 | 触发服务端位置拉回（强制下发校准包） |
| **BR-05** | **触碰距离物理约束** | 方块挖掘/放置、实体交互的欧氏距离 $\sqrt{\Delta x^2 + \Delta y^2 + \Delta z^2} \le 6.0$ 格。 | 拒绝处理该挖掘事件，保持方块原样 |
| **BR-06** | **心跳超时与断线回收** | 客户端在 $30$ 秒内未回传对应的 `KeepAlive` ID。 | 主动优雅断开，广播移除实体，释放内存 |

---

## 五、 非功能性需求 (NFR / Quality Attributes)

### 5.1 性能与吞吐指标
* **服务响应延迟**：
  * Server List Ping 响应往返时间（局域网）P99 $\le 5\text{ms}$。
  * 玩家移动同步端到端内网延迟 P95 $\le 15\text{ms}$。
* **游戏物理节拍**：
  * 主世界循环严格维持 **20 TPS**（每 Tick 50ms），单 Tick 耗时抖动控制在 $\le 3\text{ms}$ 以内。
* **单机并发承载**：
  * 单机单进程在 8 核 16G 标准云主机环境下，承载 **1000+ 在线玩家** 持续移动漫游与世界交互，CPU 占用率低于 70%。
* **网络包吞吐**：
  * 单节点网络 I/O 吞吐能力 $\ge 100,000\text{ packets/sec}$。

### 5.2 资源与内存占用指标
* **常驻静态内存**：服务启动并完成世界初始化后，物理常驻内存 RSS $\le 30\text{MB}$。
* **动态玩家开销**：单玩家驻留常驻内存开销 $\le 500\text{KB}$（包含会话读写缓冲、实体状态及周围视距内的 Chunk 引用）。
* **零内存泄漏**：在 Valgrind / ASan（AddressSanitizer）监控下进行 72 小时千人压测，堆内存无任何异常线性增长。

### 5.3 稳定性与容错
* **非法数据包隔离**：任何客户端构造的残缺、错序、超长或畸形字节流均被 `ByteBuffer` 及 `PacketFraming` 安全拦截，系统核心逻辑**零 Crash**（零崩溃）。
* **优雅启停与热恢复**：支持捕捉操作系统退出信号（`SIGINT` / `SIGTERM`），在 5 秒内完成全服玩家安全保存、踢出通知与连接池排空。
