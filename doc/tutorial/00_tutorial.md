# 从语法小白到架构思维：Minecraft 集群大厅与跑酷微服务引擎实战指南

> **目标协议**：**Minecraft Java Edition 1.12.2（protocol 340）** —— 全项目统一版本；所有包 ID、字段类型与区块格式以 [`doc/protocol/protocol_340_cheatsheet.md`](../protocol/protocol_340_cheatsheet.md) 为唯一权威参考。  
> **项目定位**：基于 C++23 与 Asio 构建的**高并发集群大厅与多地图跑酷微服务引擎**（常驻内存 $\le 30\text{MB}$，冷启动 $\le 50\text{ms}$，原生兼容 BungeeCord/Velocity 网关反向代理）。  
> **面向读者**：学完现代 C++ 基本语法、读过或了解过 Muduo 网络库，渴望通过一个真实、高复杂度、具备硬核网络并发与微服务特性的项目来提升大型系统架构能力的开发者。  
> **核心原则**：**授人以渔，不写玩具 Demo**。聚焦于架构推演、模块解耦、时序逻辑、接口契约与工业级防屎山红线。

---

## 目录
- [一、 心智转换：从“做习题”到“掌控大型工程”](#一-心智转换从做习题到掌控大型工程)
  - [1.1 为什么你学了语法和 Muduo 依然无从下手？](#11-为什么你学了语法和-muduo-依然无从下手)
  - [1.2 Muduo 与 Asio 的认知映射（Reactor 到 Proactor）](#12-muduo-与-asio-的认知映射reactor-到-proactor)
  - [1.3 核心工程方法论：自顶向下拆解，自底向上构建](#13-核心工程方法论自顶向下拆解自底向上构建)
- [二、 项目全局架构与模块解耦设计](#二-项目全局架构与模块解耦设计)
  - [2.1 现代 C++ 规范工程目录结构](#21-现代-c-规范工程目录结构)
  - [2.2 线程模型与数据流向总览图](#22-线程模型与数据流向总览图)
  - [2.3 调试开发工具箱准备](#23-调试开发工具箱准备)
- [三、 6 阶段保姆级研发路线与避坑指南](#三-6-阶段保姆级研发路线与避坑指南)
  - [阶段 0：基础组件与单测驱动（已完成）](#阶段-0基础组件与单测驱动已完成)
  - [阶段 1：网络基座、分包器与服务器列表展示（已完成）](#阶段-1网络基座分包器与服务器列表展示已完成)
  - [阶段 2：握手鉴权与 5x5 区块流式下发（已完成）](#阶段-2握手鉴权与-5x5-区块流式下发已完成)
  - [阶段 3：玩家移动同步、同屏可见与聊天系统（当前主线）](#阶段-3玩家移动同步同屏可见与聊天系统当前主线)
  - [阶段 4：多地图跑酷房间引擎与 0x35 无缝切世界（进阶核心）](#阶段-4多地图跑酷房间引擎与-0x35-无缝切世界进阶核心)
  - [阶段 5：BungeeCord 集群网关穿透与生产级交付（产品化闭环）](#阶段-5bungeecord-集群网关穿透与生产级交付产品化闭环)
- [四、 权威学习资源与文档速查](#四-权威学习资源与文档速查)
- [五、 大型 C++ 工程能力自查清单（Checklist）](#五-大型-c-工程能力自查清单checklist)

---

## 一、 心智转换：从“做习题”到“掌控大型工程”

### 1.1 为什么你学了语法和 Muduo 依然无从下手？
1. **语法是单词，工程是文章**：学完语法，你懂 `class`、`unique_ptr`、`lambda`，但面对一个庞大系统时，你不知道谁该创建谁、谁持有谁、生命周期何时结束。
2. **Muduo 是现成的轮子，实际业务是复杂的装配厂**：学习 Muduo 时，教程通常把 `EventLoop`、`Channel`、`TcpServer` 都给你写好了，你只需要写个 `onMessage` 打印消息。但真实协议有粘包、半包、压缩、状态机、多线程数据投递、游戏世界状态同步，这之间的**胶水层与分层架构**才是工程的核心。
3. **陷入了“在 server.cc 顺序写代码”的线性思维**：新手容易想一口气从 `socket` 监听写到玩家走路。大型项目的正确打开方式是：**定义清晰的模块边界与接口契约，把不确定的大任务拆解成可独立单元测试的微任务**。

---

### 1.2 Muduo 与 Asio 的认知映射（Reactor 到 Proactor）

| 概念维度 | Muduo（Reactor 模式） | Asio（Proactor 风格模拟） | 迁移理解与心智转变 |
| :--- | :--- | :--- | :--- |
| **事件循环** | `EventLoop::loop()` | `asio::io_context::run()` | 都是阻塞运行的事件驱动引擎，本质相同。 |
| **I/O 事件通知** | `epoll_wait` 返回后，通知 **“可读/可写了”**，必须自己调用 `read(fd, ...)` | 发起 `async_read`，Asio 读完数据后回调通知 **“数据已读到缓冲区了”** | Muduo 是通知就绪；Asio 是通知完成。 |
| **连接抽象** | `TcpConnection` (继承自 `enable_shared_from_this`) | 自定义的 `TcpSession` (同样必须继承 `enable_shared_from_this`) | 生命周期管理哲学完全一致：连接由未完成的异步调用延续！ |
| **监听器** | `Acceptor` | `asio::ip::tcp::acceptor` | 均用于监听端口并生成新的连接套接字。 |
| **缓冲区** | `muduo::net::Buffer`（内部管理 readIndex / writeIndex） | `asio::streambuf` 或自定义 `ByteBuffer` | 游戏二进制协议强烈建议自己封装轻量 `ByteBuffer`，更可控。 |

---

### 1.3 核心工程方法论：自顶向下拆解，自底向上构建

* **自顶向下设计（Top-Down Design）**：在动手写代码前，先画出清晰的数据链路图。
* **自底向上构建（Bottom-Up Implementation）**：先写基础库（`ByteBuffer`、`VarInt`）并用单测锁死，再写分包器与网络驱动，最后构建协议状态机和房间逻辑。

---

## 二、 项目全局架构与模块解耦设计

### 2.1 现代化目录规划与四层红线
```
mcpe/
├── CMakeLists.txt              # 顶层 CMake 构建脚本 (C++23)
├── bin/                        # 产物输出目录
├── lib/                        # 内嵌第三方依赖库（asio, spdlog, googletest 等）
├── doc/                        # 技术文档库
│   ├── tutorial/               # 教程实战系列 (00_tutorial, sprint3, sprint4, sprint5)
│   ├── protocol/               # 协议规范速查
│   └── guides/                 # 架构专题指南
├── src/                        # 源代码 (严格遵循四层架构红线)
│   ├── common/                 # 1. 基础库：ByteBuffer (COW零拷贝), VarInt
│   ├── network/                # 2. 网络传输层：TcpServer, TcpSession, PacketFraming (纯网络无协议逻辑)
│   ├── protocol/               # 3. 协议会话层：PacketRouter, HandshakeHandler, StatusHandler, LoginHandler, PlayHandler
│   ├── world/                  # 4. 空间与世界层：Chunk, ChunkColumn, ChunkSection, SchematicLoader
│   ├── game/                   # 5. 游戏房间层：RoomManager, ParkourRoom, FallReset, Ticker
│   └── server.cc               # 启动入口与信号优雅退出
└── tests/                      # 100% 覆盖率单元测试套件
```

---

### 2.2 线程模型与数据流向总览图

```
              ┌────────────────────────────────────────────────────────┐
              │                   网络 I/O 线程池 (Asio)                │
              │  ┌───────────────┐      ┌───────────────┐              │
客户端 TCP ──►│  │ TcpSession 1  │      │  TcpSession 2 │ ── (网络收发) │
              └──└──────┬────────┘──────└───────┬───────┘──────────────┘
                       │ 解包为 Packet         │ 解包为 Packet
                       ▼                       ▼
              ┌─────────────────────────────────────────────────────────┐
              │          跨线程 MPSC 双缓冲任务队列 (Double-Buffering)   │
              └────────────────────────┬────────────────────────────────┘
                                       │ 批量拉取事件 (Batch Pop)
                                       ▼
              ┌─────────────────────────────────────────────────────────┐
              │               游戏与房间逻辑主线程 (Game World)           │
              │                                                         │
              │   20 TPS 物理节拍器 (每 50ms 触发一次)                    │
              │   ├── 1. 消费所有客户端操作输入 (位移 / 聊天 / 选图)        │
              │   ├── 2. 调度 RoomManager 房间生命周期与倒计时            │
              │   ├── 3. 跑酷判定 (检查点 / Fall Reset 掉落拉回 / 终点结算) │
              │   └── 4. 0x35 无缝切世界 / 实体移动多播 (COW 零拷贝广播)   │
              └────────────────────────┬────────────────────────────────┘
                                       │ 投递响应数据包 (Shared ByteBuffer)
                                       ▼
                              返回各 Session 异步下发
```

---

## 三、 6 阶段保姆级研发路线与避坑指南

---

### 阶段 0：基础组件与单测驱动（已完成）
* **核心成果**：完成 `mcpe::ByteBuffer`（大端读写、Netty 风格切片、写时复制 COW）与 `mcpe::VarInt`（LEB128、`std::expected` 无异常设计），GoogleTest 单元测试 100% 覆盖。

---

### 阶段 1：网络基座、分包器与服务器列表展示（已完成）
* **核心成果**：基于 Standalone Asio 搭建异步 TCP 服务器；设计 `PacketFraming` VarInt 定界流式拆包器；完成 `HandshakeHandler` 与 `StatusHandler`，官方客户端多人游戏列表成功刷新延迟绿条与 MOTD。

---

### 阶段 2：握手鉴权与 5x5 区块流式下发（已完成）
* **核心成果**：打通 `Login Start (0x00)` 与离线 UUID 鉴权；下发 `Join Game (0x23)` 与 `0x2F Player Position And Look`；实现 4096 方块的紧凑 `ChunkSection`（13 位调色板位打包压缩）；在进服时下发以玩家为中心的 5x5 区块矩阵，玩家稳固站在实体地面上。

---

### 阶段 3：玩家移动同步、同屏可见与聊天系统（当前主线）

#### 1. 目标定义（Definition of Done）
* 启动两个 1.12.2 官方客户端 A 和 B 连入服务器：
  * 玩家 A 走动、跳跃、转头，玩家 B 能在屏幕上实时看到 A 的人物模型在跑动与朝向转动；
  * 玩家在聊天框打字，同大厅所有玩家均能实时收到格式化的聊天信息；
  * 支持输入快捷指令（如 `/lobby`, `/maps`），服务端能做出正确的命令应答。

#### 2. 核心架构与协议契约
1. **移动数据包消费**：
   * 客户端以 20Hz 频率上行：`Player Position (0x0E)`、`Player Look (0x0F)`、`Player Position And Look (0x10)`；
   * 网络线程只做解包，通过 MPSC 队列投递到主线程。
2. **同屏实体生成与销毁**：
   * 新玩家连入：向同房间其他玩家下发 `Spawn Player (0x05)`；
   * 玩家离线/离开房间：下发 `Destroy Entities (0x32)`。
   * ⚠️ **协议顺序铁律**：`Spawn Player (0x05)` 发送前，**必须先发送 `Player List Item (0x2E, Action=0)`**，否则官方客户端会直接忽略实体渲染，导致“看不见人”！
3. **增量位移广播**：
   * 单轴相对位移 $< 8.0$ 格时：下发 `Entity Relative Move (0x26)` 或 `Entity Look And Relative Move (0x27)`；
   * 相对位移为定点数（单位 $\frac{1}{4096}$ 方块）：`delta = (int16_t)((current - previous) * 4096)`；
   * 若位移 $\ge 8.0$ 格（传送或网络卡顿大跨步）：降级发送绝对坐标包 `Entity Teleport (0x4C)`。
4. **聊天多播与指令分流**：
   * 消费客户端上行 `Chat Message (0x02)`；
   * 若文本以 `/` 开头：交由指令解析器处理（不广播）；
   * 若为普通文本：封装为富文本 JSON `{"text": "<张三> 你好！"}`，通过客户端 `0x02 Chat (Clientbound)` 多播。

* **实战独立教程**：详见 [`doc/tutorial/sprint3_movement_chat.md`](sprint3_movement_chat.md)。

---

### 阶段 4：多地图跑酷房间引擎与 0x35 无缝切世界（进阶核心）

#### 1. 目标定义（Definition of Done）
* 服务端内置或外置加载多张 WorldEdit `.schematic` 建筑蓝图跑酷地图（如新手跑酷、高空螺旋跑酷）。
* 玩家在大厅中走到跑酷传送门或输入 `/join spiral`：
  * 客户端屏幕黑屏刷新约 0.2 秒，**单 TCP 连接不中断**，瞬间切入跑酷地图！
  * 踩中起点方块开始计时；
  * 玩家失手跌落跑道（$Y < \text{fall\_y}$）时，**不死亡、不扣血**，服务端自动下发瞬移包将玩家拉回起点或最近检查点，计时不中断；
  * 玩家踩中终点包围盒：广播通关成绩（精确到毫秒），3 秒后下发 `0x35` 切回大厅世界。

#### 2. 核心架构与协议契约
1. **WorldEdit `.schematic` 蓝图加载器**：
   * 文件格式本质为 Gzip 压缩的 NBT 复合标签；
   * 提取 `Width` (X)、`Height` (Y)、`Length` (Z) 及 `Blocks` / `Data` 数组；
   * 将三维方块数组直接灌入内存 `Chunk` 结构中，加载极速，开箱即用。
2. **`RoomManager` 房间有限状态机**：
   * `WAITING`（等待玩家 / 选图） -> `COUNTDOWN`（倒数 3 秒） -> `PLAYING`（竞速中） -> `ENDED`（通报成绩与回收）。
3. **0x35 无缝切世界时序**：
   * 步骤 1：下发 `0x35 Respawn`（通知客户端清空当前世界渲染缓存）；
   * 步骤 2：流式下发目标跑道周围的 `0x20 Chunk Data` 数据包；
   * 步骤 3：下发 `0x2F Player Position And Look`（把玩家瞬移到起点）。
4. **跑酷游戏规则系统**：
   * 起点触发：记录 `std::chrono::steady_clock::now()`；
   * 掉落熔断保护（Fall Reset）：消费位移包时检测 $Y < \text{fall\_reset\_y}$，触发瞬移；
   * 终点 AABB 判定：坐标落入检测盒内即为通关。

* **实战独立教程**：详见 [`doc/tutorial/sprint4_parkour_room.md`](sprint4_parkour_room.md)。

---

### 阶段 5：BungeeCord 集群网关穿透与生产级交付（产品化闭环）

#### 1. 目标定义（Definition of Done）
* 具备作为集群微服务大厅“即插即用”的能力：在 BungeeCord / Velocity 代理配置文件中填入本项目端口，启动即用。
* 玩家踩大厅中的“生存服传送门”，服务端发送切服指令，外部网关自动把玩家无缝接入外部 Java 生存服。
* 提供官方 `Dockerfile`，支持 `docker run` 一键秒级启动。
* 编写 Headless Bot 压测脚本，输出千人并发漫游测试报告（内存对比：Java 1.5GB vs C++ 25MB）。

#### 2. 核心架构与协议契约
1. **BungeeCord IP Forwarding 协议穿透**：
   * 在 `HandshakeHandler` 中解析握手包 `server_address`（形如 `host\0client_ip\0client_uuid\0skin_json`）；
   * 提取真实公网 IP 与正版 UUID，保证反作弊与皮肤展示。
2. **`0x18 Plugin Message` 跨服传送**：
   * 发送通道为 `"BungeeCord"` 的插件数据包；
   * 载荷内容：`writeString("Connect")` + `writeString(target_server_name)`；
   * 代理自动接管并将玩家路由至目标服。
3. **容器化与基准压测报告**：
   * 单静态二进制，无任何外部动态库依赖；
   * 输出量化压测基准文档，直接作为简历王炸亮点。

* **实战独立教程**：详见 [`doc/tutorial/sprint5_cluster_gateway.md`](sprint5_cluster_gateway.md)。

---

## 四、 权威学习资源与文档速查

1. **本地全站离线协议存档**：[`doc/reference/wiki.vg/Protocol.html`](../reference/wiki.vg/Protocol.html)
2. **本项目协议速查表**：[`doc/protocol/protocol_340_cheatsheet.md`](../protocol/protocol_340_cheatsheet.md)
3. **Cuberite 工业标杆对照**：[`doc/guides/cuberite_architecture_guide.md`](../guides/cuberite_architecture_guide.md)
4. **并发与网络底层指南**：[`doc/guides/modern_cpp_concurrency_guide.md`](../guides/modern_cpp_concurrency_guide.md)

---

## 五、 大型 C++ 工程能力自查清单（Checklist）

* [ ] 是否做到了网络 I/O 线程与游戏逻辑主线程彻底解耦？
* [ ] 广播数据包是否实现了基于 COW `ByteBuffer` 的零拷贝浅共享？
* [ ] 相对位移增量计算是否严格防御了 $\ge 8.0$ 格的溢出并降级传送？
* [ ] `Spawn Player` 发送前是否严格保证了 `Player List Item` 先行到达？
* [ ] 编译是否严格保持 GCC 14 `-Wall -Wextra -Wpedantic` 零告警？
* [ ] 整个生命周期是否存在裸 `new`/`delete` 或未受控的悬垂指针？
