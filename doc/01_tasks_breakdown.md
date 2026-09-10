# Minecraft 协议网关与世界同步引擎：敏捷任务拆解与执行看板 (WBS)

> **文档定位**：本项目（MCPE）的工业级敏捷开发执行手册。  
> **目标协议**：**Minecraft Java Edition 1.12.2（protocol 340）**。所有包 ID、字段类型与区块格式以 [`doc/03_protocol_340_cheatsheet.md`](03_protocol_340_cheatsheet.md) 为唯一权威参考（来源：<https://c4k3.github.io/wiki.vg/Protocol.html>）。  
> **核心原则**：**纵向切片（Vertical Slicing）**、**小步快跑（INVEST 原则）**、**客观度量（Strict DoD）**、**绝不直接贴业务实现代码**。  
> **使用方法**：每个任务粒度控制在 **1 ~ 3 小时**。按顺序推进，完成一个打一个勾 `[x]`，并执行推荐的 `git commit`。  
> 📁 **分阶段独立文件速查**：本看板已同时拆分为单阶段独立文件存储于 [`doc/sprints/`](file:///home/ubu/dev/my/mcpe/doc/sprints/00_overview.md) 目录下，支持按阶段专注查看。

---

## 迭代路线总览（Milestone Roadmap）

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│ Sprint 0: 基础设施与叶子组件 (单测驱动：CMake、ByteBuffer、VarInt)                        │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ Sprint 1: 协议基座与列表探测 (Asio 异步通信、粘包拆包、Server List Ping、MOTD 展示)        │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ Sprint 2: 会话生命周期与虚空进服 (Login 鉴权、UUID、Join Game、出生点、KeepAlive 保活)     │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ Sprint 3: 区块模型与超平坦地形 (ChunkSection 紧凑内存、地形生成、0x20 区块包、踏下方块)     │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ Sprint 4: 架构解耦与多玩家同步 (MPSC 队列、20 TPS 节拍器、九宫格 AOI、同屏移动/挖掘同步)    │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ Sprint 5: 压测机器人与性能调优 (千人 Headless Bot、perf 火焰图、定长内存池、Sanitizers)    │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Sprint 0: 基础设施与叶子组件（工期预估：2~3 天）

> **目标**：不接触复杂网络与游戏逻辑，搭建模块化工程脚手架，完成最底层二进制缓冲与变长整型算法，以单测为 100% 验收基准。

---

### [ ] TASK-0.1: 现代 CMake 架构重构与目录分层
* **任务类型**：工程基建 (Infra)
* **预估耗时**：1.5 小时
* **任务目标**：
  * 将项目拆分为 `include/mcpe/`（对外头文件）、`src/`（实现源文件）、`tests/`（测试）。
  * 在 CMakeLists.txt 中创建模块化目标：`mcpe_common`（静态库或 OBJECT 库），让 `main_app` 与后续的单元测试程序均可链接该库，避免重复编译。
* **DoD（完成定义）**：
  * 执行 `cmake -B build && cmake --build build` 零警告通过。
  * `tests/` 目录下能成功编译出独立的可执行测试程序并运行输出。
* **易错点**：
  * 不要使用 `file(GLOB ...)` 暴力收集源文件，现代 CMake 推荐显式列出 `.cc`，避免新增文件时 CMake 缓存不更新。
* **推荐提交**：`chore(build): setup modular CMake structure and test targets`

---

### [ ] TASK-0.2: `ByteBuffer` 核心数据结构设计
* **任务类型**：底层组件 (Unit)
* **预估耗时**：2.5 小时
* **任务目标**：
  * 设计并实现 `ByteBuffer` 类，内部基于 `std::vector<uint8_t>` 管理读写游标（`read_pos_` 与 `write_pos_`）。
  * 实现大端字节序读写接口：`write_u8`、`write_be16`、`write_be32`、`write_be64`、`write_float`、`write_double` 及其对应的 `read_*` 接口。
  * 提供 `readable_bytes()`、`skip()`、`retrieve()` 游标滑动接口。
* **DoD（完成定义）**：
  * 类接口符合 RAII 与强异常安全，读取越界时能通过抛出自定义异常或返回 `bool` 保证程序不发生未定义行为。
* **易错点**：
  * 浮点数转字节序：严禁指针强转 `*(uint32_t*)&f`（违反 Strict Aliasing）；必须使用 `std::memcpy` 拷贝到位域或整型后再进行大端转换。
* **推荐提交**：`feat(common): implement ByteBuffer with big-endian primitives`

---

### [ ] TASK-0.3: `ByteBuffer` 独立单元测试套件
* **任务类型**：测试验证 (Test)
* **预估耗时**：1.5 小时
* **任务目标**：
  * 在 `tests/test_bytebuffer.cc` 中编写针对 `ByteBuffer` 的全覆盖测试：
    * 基本整数（`0x1234` 写入后按字节检查是否为 `0x12, 0x34`）。
    * IEEE-754 浮点数极值测试（`0.0f`, `-1.5f`, `NaN`, `Infinity`）。
    * 缓冲区动态扩容与游标读写交替测试。
    * 越界保护测试（缓冲区只有 2 字节时尝试读取 `be32` 必须拦截）。
* **DoD（完成定义）**：
  * 运行 `./build/tests/test_bytebuffer`，所有断言通过，终端输出绿色成功标识。
* **推荐提交**：`test(common): add comprehensive unit tests for ByteBuffer`

---

### [ ] TASK-0.4: `VarInt` / `VarLong` 编解码算法实现
* **任务类型**：核心算法 (Unit)
* **预估耗时**：2 小时
* **任务目标**：
  * 依据 Minecraft 协议规范（7-bit MSB 变长编码），实现：
    * `void write_varint(ByteBuffer& buf, int32_t value);`
    * `int32_t read_varint(ByteBuffer& buf);`
    * `void write_varlong(ByteBuffer& buf, int64_t value);`
    * `int64_t read_varlong(ByteBuffer& buf);`
* **DoD（完成定义）**：
  * 编码逻辑完全符合 [wiki.vg Data Types](https://wiki.vg/Data_types) 规范。
  * 解码时当遇到超过 5 个字节（VarInt）或 10 个字节（VarLong）且最高位仍为 1 时，立即抛出协议异常。
* **易错点**：
  * 移位变量务必声明为无符号整型（`uint32_t`），避免有符号数算术右移产生高位补 1 死循环。
  * 负数（如 `-1`）必须能被正确编码为 5 字节（`0xFF 0xFF 0xFF 0xFF 0x0F`）。
* **推荐提交**：`feat(common): implement VarInt and VarLong codec with overflow check`

---

### [ ] TASK-0.5: `VarInt` 边界极限单元测试
* **任务类型**：测试验证 (Test)
* **预估耗时**：1 小时
* **任务目标**：
  * 编写 `tests/test_varint.cc`，覆盖官方标准测试用例：
    * `0` -> `0x00`
    * `1` -> `0x01`
    * `127` -> `0x7f`
    * `128` -> `0x80, 0x01`
    * `255` -> `0xff, 0x01`
    * `2147483647` (Int Max) -> `0xff, 0xff, 0xff, 0xff, 0x07`
    * `-1` -> `0xff, 0xff, 0xff, 0xff, 0x0f`
    * `-2147483648` (Int Min) -> `0x80, 0x80, 0x80, 0x80, 0x08`
* **DoD（完成定义）**：
  * 所有官方测试向量编码解码双向可逆，测试程序退出码为 0。
* **推荐提交**：`test(common): add edge case verification for VarInt`

---

## Sprint 1: 协议基座与列表探测（工期预估：3~4 天）

> **目标**：搭建 Asio 异步网络骨架，解决 TCP 粘包半包，跑通协议状态机流转，让原版 Minecraft 1.12.2 客户端在多人游戏列表中搜出服务器。

---

### [ ] TASK-1.1: [Spike] Wireshark 抓包分析与报文 Hex 留存
* **任务类型**：技术探针 (Spike)
* **预估耗时**：1 小时
* **任务目标**：
  * 开启 Wireshark 监听 `Loopback: lo`，端口 `25565`。
  * 启动任意现成的 Minecraft 服务端（或模拟客户端请求），刷新列表，捕获握手与 Ping 过程。
  * 将抓包捕获的十六进制字节流保存到 `doc/packets_hex_dump.md` 中，作为后续解析比对的标准基准。
  * 与本地存档 [`doc/reference/wiki.vg/Server_List_Ping.html`](reference/wiki.vg/Server_List_Ping.html)、[`Protocol.txt`](reference/wiki.vg/Protocol.txt) 中的字段表逐字段对照，确认每个字节的含义。
* **DoD（完成定义）**：
  * 掌握 `Handshake`、`Status Request`、`Ping`、`Pong` 四个数据包的原始十六进制字节组成。
  * 抓包结果与本地存档中的字段定义完全一致（协议号应为 `340`）。
* **推荐提交**：`docs(protocol): record reference hex dump of Server List Ping`

---

### [ ] TASK-1.2: Asio `TcpServer` 与 `TcpSession` 基础连接骨架
* **任务类型**：网络核心 (Core)
* **预估耗时**：2.5 小时
* **任务目标**：
  * 编写 `TcpServer`：管理 `asio::ip::tcp::acceptor`，异步接收连接。
  * 编写 `TcpSession`：继承 `std::enable_shared_from_this<TcpSession>`，管理单个 Socket 的生命周期。
  * 实现 `start()`，开启循环异步读取 `socket.async_read_some(...)`。
* **DoD（完成定义）**：
  * 启动服务，使用终端运行 `nc 127.0.0.1 25565` 连接，服务端打印连接接入日志；`nc` 退出后，服务端能捕获 EOF 并正常析构 Session（无内存泄漏）。
* **易错点**：
  * 所有的 Asio 回调 lambda 捕获列表中，必须捕获 `self = shared_from_this()`，严禁只捕获 `this`。
* **推荐提交**：`feat(network): implement basic TcpServer and TcpSession lifetime`

---

### [ ] TASK-1.3: 发送队列（Write Queue）与并发安全下发
* **任务类型**：网络核心 (Core)
* **预估耗时**：2 小时
* **任务目标**：
  * 在 `TcpSession` 中实现基于发送队列的数据下发机制。
  * 解决在同一个 socket 上重叠发起多次 `async_write` 导致的未定义行为。
* **DoD（完成定义）**：
  * 设计 `send_packet(const ByteBuffer& buf)` 接口。
  * 当写操作正在进行时，新数据暂存队列；写操作完成后自动取队首发送，直到队列清空。
* **易错点**：
  * 必须保证队列在网络线程上下文中的访问安全性（可通过 `asio::strand` 或统一由 io_context 线程调度）。
* **推荐提交**：`feat(network): implement thread-safe write queue in TcpSession`

---

### [ ] TASK-1.4: 协议数据包拆包器（Packet Framing）
* **任务类型**：协议核心 (Core)
* **预估耗时**：3 小时
* **任务目标**：
  * 在 `TcpSession` 内部维护一块累积接收缓冲区。
  * 按照 `[Packet Length: VarInt] + [Packet ID: VarInt] + [Payload]` 格式实现拆包逻辑：
    * 数据不足以读取一个完整 VarInt 长度时，暂存等待（半包）。
    * 缓冲区数据小于 `Packet Length` 时，暂存等待（半包）。
    * 缓冲区数据大于等于 `Packet Length` 时，切出完整的独立包交给上层，并滑动读指针；循环此操作（粘包）。
* **DoD（完成定义）**：
  * 编写模拟测试：人为将一个数据包拆成 2 字节多次喂入，能正确还原；将 3 个数据包拼接一次喂入，能依次解析出 3 个独立包。
* **推荐提交**：`feat(protocol): implement packet framing for length-delimited packets`

---

### [ ] TASK-1.5: 协议状态机（State Machine）框架搭建
* **任务类型**：业务设计 (Architecture)
* **预估耗时**：1.5 小时
* **任务目标**：
  * 定义连接生命周期状态枚举：
    `enum class ConnectionState { HANDSHAKING, STATUS, LOGIN, PLAY };`
  * 在 `TcpSession` 中维护当前状态。
  * 设计数据包路由器：根据当前状态将解析出的 `PacketID` 分发给对应的处理函数。
* **DoD（完成定义）**：
  * 收到非当前状态允许的数据包时，打印告警日志并主动切断连接。
  * ⚠️ **1.12.2 例外白名单**：`Client Settings (0x04)`、`Plugin Message (0x09)`、`Player Abilities (0x13)`、`Held Item Change (0x1A)`、`Client Status (0x03)`、`Confirm Transaction (0x05)`、`Close Window (0x08)`、`Resource Pack Status (0x18)` 等客户端"例行公事"包必须**显式注册为空实现分支**。否则客户端刚进服就会被自己的路由器踢下线。
  * 路由表以 `std::array<Handler, N>` 或 `std::unordered_map` 形式集中登记，禁止散落的 `if (id == ...)` 链。
* **推荐提交**：`feat(protocol): add connection state machine router`

---

### [ ] TASK-1.6: `Handshake (0x00)` 解析与意图流转
* **任务类型**：协议业务 (Business)
* **预估耗时**：1.5 小时
* **任务目标**：
  * 在 `HANDSHAKING` 状态下监听 Packet ID `0x00`。
  * 解析字段：`Protocol Version` (VarInt)、`Server Address` (String)、`Server Port` (unsigned short)、`Next State` (VarInt)。
* **DoD（完成定义）**：
  * 若 `Next State == 1`，将状态机切换为 `ConnectionState::STATUS`。
  * 若 `Next State == 2`，将状态机切换为 `ConnectionState::LOGIN`。
  * 记录并打印客户端上报的 `Protocol Version`：1.12.2 客户端应为 `340`；对非 `340` 的登录请求，直接回 `Disconnect (login, 0x00)` 并说明版本不符。
* **推荐提交**：`feat(protocol): handle Handshake packet and route next state`

---

### [ ] TASK-1.7: `Status Request (0x00)` 与 JSON 描述响应
* **任务类型**：协议业务 (Business)
* **预估耗时**：2 小时
* **任务目标**：
  * 在 `STATUS` 状态下监听 Packet ID `0x00`（无 Payload）。
  * 构造 `Status Response (0x00)`：组装包含协议版本号 `340`、版本名 `"1.12.2"`、最大人数、在线人数、MOTD 的 JSON 字符串。
  * 按照 Minecraft 规范：将 JSON 转为前缀 VarInt 长度的字符串，打包成 Packet 发回。
* **DoD（完成定义）**：
  * 数据包经由拆包器下发，格式正确无遗漏字段。
* **推荐提交**：`feat(protocol): respond to status request with MOTD json`

---

### [ ] TASK-1.8: `Ping (0x01)` 与 `Pong (0x01)` 时间戳响应
* **任务类型**：协议业务 (Business)
* **预估耗时**：1 小时
* **任务目标**：
  * 监听客户端发来的 `Ping (0x01)`，读取其中的 **`Long`（8 字节）** 时间戳载荷。
  * 立即构造 `Pong (0x01)` 数据包，将该 `Long` 原样发回客户端。
* **DoD（完成定义）**：
  * Ping/Pong 完成后，服务端安全关闭该临时查询连接（或等待客户端主动关闭）。
* **推荐提交**：`feat(protocol): handle ping request and echo pong timestamp`

---

### [ ] TASK-1.9: Sprint 1 集成验收与原版客户端联调
* **任务类型**：集成验证 (Integration)
* **预估耗时**：1 小时
* **任务目标**：
  * 启动编译出的 `main_app`，监听 `0.0.0.0:25565`。
  * 打开 Minecraft **1.12.2** 客户端 -> 多人游戏 -> 添加服务器 `127.0.0.1:25565`。
* **DoD（完成定义）**：
  * 客户端列表中成功显示绿色信号延迟（Ping 值正常，如 `<5ms`）。
  * 显示出自定义的 MOTD 内容与在线人数 `0/100`。
  * 客户端控制台与服务端终端**无任何抛错或警告**。
* **推荐提交**：`chore: verify and pass Sprint 1 Server List Ping milestone`

---

## Sprint 2: 会话生命周期与虚空进服（工期预估：4~5 天）

> **目标**：打通 `LOGIN` 到 `PLAY` 状态流转，实现离线 UUID 计算，发送进服必要数据包（`Join Game 0x23` → `Player Position And Look 0x2F` → `Teleport Confirm` 闭环），让玩家人物成功载入虚空世界，心跳稳定不掉线。

> **1.12.2 迁移增量**：本 Sprint 的相对包 ID 与字段全部更新到 protocol 340（`Join Game` 由 `0x01`→`0x23`、出生点包由 `0x08`→`0x2F` 并新增 `Teleport ID`、`Keep Alive` 由 VarInt 变为 `Long`），并要求路由器显式容忍客户端例行包（见 TASK-1.5 白名单）。

---

### [ ] TASK-2.1: `Login Start (0x00)` 解析与玩家名校验
* **任务类型**：协议业务 (Business)
* **预估耗时**：1.5 小时
* **任务目标**：
  * 在 `LOGIN` 状态下接收 Packet ID `0x00`。
  * 读取玩家用户名（String，最大长度 16 字符）。
* **DoD（完成定义）**：
  * 若用户名为空或包含非法字符（只允许字母、数字、下划线），断开连接。
  * 正确打印日志：`Player [Username] is attempting to login`。
* **推荐提交**：`feat(login): parse Login Start packet and validate player name`

---

### [ ] TASK-2.2: 离线模式 UUID 生成算法实现
* **任务类型**：基础算法 (Unit)
* **预估耗时**：1.5 小时
* **任务目标**：
  * 实现符合 Java 原版规范的离线 UUID 生成逻辑：
    `Hash = MD5("OfflinePlayer:" + username)`
  * 将 16 字节哈希值格式化为 36 字符的标准 UUID 字符串（如 `c0611e38-460d-3277-bc67-083f2e1329a2`）。
* **DoD（完成定义）**：
  * 针对用户名 `"Steve"` 测试，生成的 UUID 必须与原版服务端生成的结果完全一致。
* **推荐提交**：`feat(auth): implement standard offline UUID generation`

---

### [ ] TASK-2.3: `Login Success (0x02)` 响应与状态切入 `PLAY`
* **任务类型**：协议业务 (Business)
* **预估耗时**：1 小时
* **任务目标**：
  * 构造 `Login Success (0x02)` 数据包：写入格式化后的 UUID 字符串与 Username。
  * 发送该数据包，并将当前 Session 的协议状态切换至 `ConnectionState::PLAY`。
* **DoD（完成定义）**：
  * 客户端进入握手后半段，游戏界面由 "Logging in..." 转入 "Loading world..."。
* **推荐提交**：`feat(login): send Login Success and transition state to PLAY`

---

### [ ] TASK-2.4: `Join Game (0x23)` 世界参数包组装
* **任务类型**：世界协议 (Business)
* **预估耗时**：2.5 小时
* **任务目标**：
  * 按照 1.12.2 协议下发 `Join Game (0x23)`：
    * `Entity ID`: 全局自增正整数（`Int`，如 1）
    * `Gamemode`: `Unsigned Byte`（设为 1，创造模式）
    * `Dimension`: **`Int`（4 字节，设为 0，主世界）** —— ⚠️ 1.12.2 不是 1.8 的 `int8`
    * `Difficulty`: `Unsigned Byte`（设为 1，简单）
    * `Max Players`: `Unsigned Byte`（如 100）
    * `Level Type`: `String`（设为 `"flat"`）
    * `Reduced Debug Info`: `bool`（设为 `false`）
* **DoD（完成定义）**：
  * 字段类型与字节序严格对齐，包体无错位。
  * 用 Wireshark 抓包逐字节核对 `Dimension` 占 4 字节。
* **推荐提交**：`feat(play): construct and send Join Game packet`

---

### [ ] TASK-2.5: `Player Position And Look (0x2F)` 出生点下发 + `Teleport Confirm` 闭环
* **任务类型**：世界协议 (Business)
* **预估耗时**：2.5 小时
* **任务目标**：
  * 紧随 `Join Game` 之后，向客户端下发初始出生点坐标：
    * $X=0.0$ (`double`), $Y=64.0$ (`double`), $Z=0.0$ (`double`)
    * $Yaw=0.0f$ (`float`), $Pitch=0.0f$ (`float`)
    * `Flags`: `0x00` (`uint8`，标记所有坐标为绝对值)
    * **`Teleport ID`: `VarInt`（1.9+ 新增字段）** —— 维护会话级自增计数器，每下发一次 PPAL 自增 1
  * 在 PLAY 路由中消费客户端回传的 `Teleport Confirm (0x00)`（载荷为 `Teleport ID: VarInt`），记录确认状态并打印 debug 日志。
* **DoD（完成定义）**：
  * 客户端顺利跳过 "Downloading Terrain" 加载界面，游戏画面渲染出深蓝色虚空与太阳/月亮，人物成功生成。
  * 服务端日志中能看到客户端回传的 Teleport ID 与下发的 ID 一致。
* **易错点**：
  * 坐标必须是 `double`（8 字节），绝对不能写成 `float`（4 字节）；`Flags` 必须是 `0x00`。
  * 漏写 `Teleport ID` 会让客户端解码 PPAL 时读到越界数据并断连——这是 1.12.2 相对 1.8 最容易漏掉的新增字段。
* **推荐提交**：`feat(play): send initial Player Position And Look packet`

---

### [ ] TASK-2.6: `Keep Alive` 心跳保活定时器机制
* **任务类型**：网络核心 (Core)
* **预估耗时**：2.5 小时
* **任务目标**：
  * 在 Session 中挂载 `asio::steady_timer`，每隔 10 秒向客户端发送一次 **`Keep Alive (0x1F)`**（载荷为 **`Long`（8 字节）** 的毫秒时间戳）。
  * 监听客户端回发的 **`Keep Alive (0x0B)`**，比对 ID 是否匹配。
  * 记录最后一次收到心跳的时间，若超过 20 秒未收到响应，则判定超时并主动关闭连接。
* **DoD（完成定义）**：
  * 玩家进入虚空后，静置等待 1 分钟，依然连接稳定，不会被客户端红字 `Timed out` 踢出。
* **易错点**：
  * ⚠️ 1.12.2 的 Keep Alive 载荷是 **`Long`**（1.8 是 `VarInt`）。类型写错会导致流解析错位，客户端抛 `DecoderException` 并断连，而服务端日志上毫无异常。
* **推荐提交**：`feat(network): implement periodic KeepAlive timer and timeout detector`

---

### [ ] TASK-2.7: Sprint 2 集成验收与虚空漫步测试
* **任务类型**：集成验证 (Integration)
* **预估耗时**：1 小时
* **任务目标**：
  * 客户端连接进服，按键盘空格键（创造模式飞行），可以在虚空中四处飞翔。
  * 按 F3 键调出调试面板，观察并确认坐标系、帧率、内存占用正常。
* **DoD（完成定义）**：
  * 客户端与服务端均稳定运行，无崩溃、无断开、无日志报错。
* **推荐提交**：`chore: verify and pass Sprint 2 Void World milestone`

---

## Sprint 3: 区块模型与超平坦地形（工期预估：4~5 天）

> **目标**：设计世界与区块紧凑内存模型，掌握空间位运算寻址，实现 1.12.2 区块位压缩序列化（调色板 + Long 数组），让玩家脚下拥有实实在在的地面。

---

### [ ] TASK-3.1: `ChunkSection` 内存结构与位运算寻址
* **任务类型**：世界核心 (Core)
* **预估耗时**：2.5 小时
* **任务目标**：
  * 设计 `ChunkSection` 类，内部使用一维定长数组 `uint16_t blocks_[4096]` 存储单个 $16 \times 16 \times 16$ 小立方体的方块状态（存**全局调色板 state id** `(blockId << 4) | meta`，1.12.2 中占 13 位，`uint16_t` 足够）。
  * 提供根据局部坐标 $(x, y, z)$ 读写方块的接口。
  * 提供 `to_global_palette_state(block_id, meta)` / 反解函数，禁止在业务代码里散落硬编码魔数。
* **DoD（完成定义）**：
  * 寻址必须使用空间高效的位运算：`index = (y << 8) | (z << 4) | x`（该公式 1.12.2 与 1.8 一致，务必与序列化时的 `blockNumber` 计算保持一致）。
  * 编写微型测试用例，验证局部坐标边界 $(0,0,0)$ 到 $(15,15,15)$ 转换索引无越界。
* **推荐提交**：`feat(world): implement ChunkSection memory layout and bit indexing`

---

### [ ] TASK-3.2: 简易超平坦生成器（Flat World Generator）
* **任务类型**：世界业务 (Business)
* **预估耗时**：1.5 小时
* **任务目标**：
  * 实现纯平地形生成逻辑，填充底部的 `ChunkSection 0`（括号内为全局调色板 state id）：
    * $Y=0$：基岩（Bedrock，`blockId=7` → state id `112`）
    * $Y=1 \sim 2$：泥土（Dirt，`blockId=3` → state id `48`）
    * $Y=3$：草方块（Grass Block，`blockId=2` → state id `32`）
    * $Y \ge 4$：空气（Air，state id `0`）
* **DoD（完成定义）**：
  * 16 个 Section 中只有 Section 0 包含有效方块数据，其余 Section 保持空指针或全空气状态以节约内存。
* **推荐提交**：`feat(world): add superflat chunk generator logic`

---

### [ ] TASK-3.3: 空间数学与坐标系转换工具实现
* **任务类型**：基础工具 (Unit)
* **预估耗时**：1.5 小时
* **任务目标**：
  * 编写 `Position` 工具函数，实现世界绝对坐标 $(X, Z)$ 到区块坐标 $(ChunkX, ChunkZ)$ 及局部坐标 $(LocalX, LocalZ)$ 的双向转换。
* **DoD（完成定义）**：
  * 必须使用算术右移处理负数：`chunk_x = x >> 4`，确保 $-1$ 被正确映射为区块 $-1$。
  * 局部坐标提取：`local_x = x & 15`。
  * 编写包含正数、负数、零的坐标转换单测并全部通过。
* **推荐提交**：`feat(world): implement coordinate math with negative floor handling`

---

### [ ] TASK-3.4: `Chunk Data (0x20)` 协议二进制序列化（1.12.2 全新格式）
* **任务类型**：协议核心 (Core)
* **预估耗时**：4 小时
* **任务目标**：
  * 严格按 1.12.2 规范组装单个 Chunk 的二进制数据流（详见 [`doc/03_protocol_340_cheatsheet.md`](03_protocol_340_cheatsheet.md) §6）：
    1. 计算 `Primary Bit Mask`（`VarInt`，只有 Section 0 时为 `0x01`）。
    2. 对每个激活 Section 自底向上写入：
       1. `Bits Per Block`（`uint8`，固定填 `13`，使用直接全局调色板）
       2. `Dummy Palette Length`（`VarInt`，直接模式下恒为 `0`）
       3. `Data Array Length`（`VarInt`，13 位时恒为 `832`）
       4. `Data Array`：`Long[832]`，**大端写入**；每个方块 13 位、低位在前、**允许跨 long**
       5. `Block Light`（2048 字节，半字节/方块，全 `0x00`）
       6. `Sky Light`（2048 字节，全 `0xFF`；**仅主世界发送**）
    3. 写入生物群系（仅 `Ground-Up Continuous == true` 时）：256 字节，索引 `z * 16 | x`，全填 `0x01`（平原）
    4. 写入 `Number of block entities`（`VarInt`，填 `0`）
    5. 计算 `Size` 并封装外层 Packet。
* **DoD（完成定义）**：
  * 生成的数据字节流大小与 `Bitmask` 计算出的预期值分毫不差：单 Section + 主世界 + ground-up 时，`Size == 1 + 1 + 2 + 832×8 + 2048 + 2048 + 256 == 11012`。
  * 位压缩算法必须有单测：对 4096 个方块的 state id 做"打包 → 解包"往返，结果完全一致（重点覆盖**跨越 long 边界**的条目，如 index 4、9、14 …）。
* **易错点**：
  * ⚠️ 1.12.2 **不再有"每方块 2 字节小端"**：方块数据是调色板 + Long 位压缩数组，且 Long 是**大端**。照抄 1.8 写法会直接导致客户端闪退。
  * ⚠️ "不跨界 + 填充"是 **1.16** 才引入的规则；1.12.2 必须支持条目跨 long。13 位 × 4096 恰好整除 64，无需填充。
* **推荐提交**：`feat(protocol): implement Chunk Data packet serialization`

---

### [ ] TASK-3.5: 出生点视距区块批量分发（含 `Unload Chunk`）
* **任务类型**：世界业务 (Business)
* **预估耗时**：2 小时
* **任务目标**：
  * 玩家进服成功并下发出生点坐标后，服务端计算以玩家出生 Chunk $(0, 0)$ 为中心、半径为 2 的区域（$X \in [-2, 2], Z \in [-2, 2]$，共 25 个区块）。
  * 遍历这 25 个区块，依次序列化为 `Chunk Data (0x20)` 数据包发送给客户端。
  * 实现 `Unload Chunk (0x1D)`（`Chunk X: Int` + `Chunk Z: Int`）下发接口，并在"覆盖已加载区块"的路径上先行调用（否则客户端 chunk 缓存泄漏）。
* **DoD（完成定义）**：
  * 数据包连续下发，发送队列平稳消化无阻塞。
  * 客户端 F3 的 MultiplayerChunkCache 两侧数值一致（无 chunk 泄漏）。
* **推荐提交**：`feat(world): send initial chunk grid around spawn point`

---

### [ ] TASK-3.6: Sprint 3 集成验收与地面行走测试
* **任务类型**：集成验证 (Integration)
* **预估耗时**：1 小时
* **任务目标**：
  * 客户端启动进服，将玩家游戏模式改为生存模式（0）。
* **DoD（完成定义）**：
  * 玩家脚下出现翠绿的草方块地面，重力生效，玩家稳稳站在地面上，不再向下坠落。
  * 玩家可以在草地上正常跑步、跳跃、按 F3 查看当前的方块坐标（Block X, Y, Z）和区块坐标，客户端无任何异常。
* **推荐提交**：`chore: verify and pass Sprint 3 Chunk & World Sync milestone`

---

## Sprint 4: 架构解耦与多玩家同步（工期预估：5~6 天）

> **目标**：彻底解耦网络 I/O 与业务计算，设计 MPSC 任务管道与精准 20 TPS 节拍器；引入九宫格 AOI 空间索引，实现多玩家同屏看见、移动与方块破坏同步。

---

### [ ] TASK-4.1: 跨线程 MPSC 任务队列（I/O 与逻辑解耦）
* **任务类型**：并发核心 (Core)
* **预估耗时**：3 小时
* **任务目标**：
  * 设计跨线程任务事件：包含 `session_id`、`packet_id`、数据载荷。
  * 实现线程安全的多生产者单消费者（MPSC）队列（第一版可采用 `std::mutex` + 双缓冲队列，读写分离交换指针）。
  * Asio 网络线程只负责读写与拆包，将完整 Packet 作为事件 `push` 进队列；逻辑主线程单向拉取消费。
* **DoD（完成定义）**：
  * 编写多线程压入、单线程弹出的并发测试，开启 `-fsanitize=thread` 编译运行，零数据竞态警告。
* **推荐提交**：`feat(concurrency): implement thread-safe MPSC event queue`

---

### [ ] TASK-4.2: 精准 20 TPS（50ms）主循环与漂移消除
* **任务类型**：并发核心 (Core)
* **预估耗时**：2 小时
* **任务目标**：
  * 在独立的主逻辑线程中运行单线程 `World Tick` 循环。
  * 消除时钟漂移：每次循环前计算 `next_tick_point = last_tick_point + 50ms`，扣除逻辑执行时间后进行 `sleep_until`。
* **DoD（完成定义）**：
  * 主线程连续空转 1000 次 Tick，统计耗时，平均每个 Tick 耗时稳定在 $50.00\text{ms} \pm 0.5\text{ms}$，无累积漂移。
* **推荐提交**：`feat(world): implement 20 TPS tick loop with drift compensation`

---

### [ ] TASK-4.3: 空间哈希与九宫格 AOI（Area of Interest）管理器
* **任务类型**：算法设计 (Algorithm)
* **预估耗时**：3 小时
* **任务目标**：
  * 设计 `AOIManager`：
    * 建立以 `ChunkPos(x, z)` 为键的哈希索引，记录每个 Chunk 内的所有玩家列表。
    * 提供查询函数：输入一个 Chunk 坐标，返回以其为中心的周围 $3 \times 3$（九宫格）内的所有玩家列表。
  * 实现跨区块移动事件：当玩家跨越区块时，计算新旧九宫格的差集（进入视野的新玩家、离开视野的旧玩家）。
* **DoD（完成定义）**：
  * 编写独立单元测试，模拟玩家从 $(0, 0)$ 走到 $(1, 0)$，能够精准算出需要新增观察者与移除观察者的玩家集合。
* **推荐提交**：`feat(world): implement 3x3 grid AOI spatial manager`

---

### [ ] TASK-4.4: 玩家进出视野广播（Player List Item / Spawn / Destroy Entity）
* **任务类型**：世界协议 (Business)
* **预估耗时**：3 小时
* **任务目标**：
  * 当玩家 A 进入玩家 B 的九宫格视野时：
    * **先**向 B 发送 `Player List Item (0x2E, Action=0, add player)`：`UUID`(16 字节) + `Name` + `Number Of Properties = 0` + `Gamemode` + `Ping` + `Has Display Name = false`。
    * **再**向 B 发送 `Spawn Player (0x05)`：`Entity ID: VarInt` + `Player UUID`(**16 字节原始 UUID**) + 坐标(`Double×3`) + `Yaw/Pitch`(`Angle`) + `Entity Metadata`（至少写终结符 `0xFF`）。
  * 当玩家 A 离开玩家 B 的视野或断开连接时：
    * 向 B 发送 `Destroy Entities (0x32)`：`Count: VarInt` + `Entity IDs: VarInt[]`。
    * 必要时同时下发 `Player List Item (0x2E, Action=4, remove player)` 清空 Tab 列表。
* **DoD（完成定义）**：
  * 数据下发通过跨线程投递回对应 Session 的发送队列中。
  * ⚠️ 顺序错了（先 Spawn 后 List Item）客户端**不会报错也不会显示人物**，必须用日志断言两者顺序。
* **推荐提交**：`feat(world): broadcast Spawn Player and Destroy Entity packets`

---

### [ ] TASK-4.5: 客户端移动包解析与相对位移广播
* **任务类型**：世界协议 (Business)
* **预估耗时**：3 小时
* **任务目标**：
  * 主线程解析客户端发来的移动包（1.12.2 的四个"serverbound movement packets"）：
    * `Player (0x0C)`：仅 `On Ground: Boolean`
    * `Player Position (0x0D)`：`X/Y/Z: Double` + `On Ground`（静止时每 20 tick 也会发一次）
    * `Player Position And Look (0x0E)`：`X/Y/Z: Double` + `Yaw/Pitch: Float` + `On Ground`
    * `Player Look (0x0F)`：`Yaw/Pitch: Float` + `On Ground`
  * 计算该玩家相对于上一 Tick 的位移差 $(\Delta X, \Delta Y, \Delta Z)$：
    * 若单轴位移绝对值 $< 8.0$ 方块：向九宫格内的其他玩家广播 `Entity Relative Move (0x26)`（`Short` 定点数，`delta = (int16)((cur - prev) * 4096)`）或 `Entity Look And Relative Move (0x27)`。
    * 若单轴位移绝对值 $\ge 8.0$ 方块（发生瞬移）：降级广播绝对坐标包 `Entity Teleport (0x4C)`。
  * 参考 wiki.vg 的"moving too fast"判定：$\Delta x^2 + \Delta y^2 + \Delta z^2 - (v_x^2+v_y^2+v_z^2) > 100$（滑翔翼 300）时打警告日志并回拉坐标。
* **DoD（完成定义）**：
  * 双开客户端，A 走动时，B 的屏幕上能平滑看到 A 的人物在平原上跑步、跳跃、转头。
* **易错点**：
  * ⚠️ 相对位移是 **`Short`（$1/4096$ 方块，上限 8 格）**，不是 1.8 的 `Byte`（$1/32$，上限 4 格）。用旧语义会导致人物坐标跳动甚至倒退。
  * `Entity Look And Relative Move` 的 `Yaw/Pitch` 是 **`Angle`（1 字节）且为绝对角度**，不是增量。
* **推荐提交**：`feat(world): handle player movement and broadcast relative position`

---

### [ ] TASK-4.6: 方块挖掘与放置交互同步
* **任务类型**：世界协议 (Business)
* **预估耗时**：3 小时
* **任务目标**：
  * 监听客户端发来的 `Player Digging (0x14)`：`Status: VarInt`（`2` 表示挖穿，`0/1` 是开始/取消）、`Location: Position`（64 位位域打包坐标）、`Face: Byte`。
    * ⚠️ `Status ∈ {3,4,5,6}`（丢物品/交换副手）时 `Location` 恒为 `0/0/0`，**不能当方块坐标处理**。
  * 监听 `Player Block Placement (0x1F)`：`Location: Position` + `Face: VarInt` + `Hand: VarInt` + 面内光标 `Float×3`；真实落点 = `Location` 沿 `Face` 法向偏移一格。
  * 主逻辑线程修改世界内存中该坐标对应的方块状态。
  * 构造 `Block Change (0x0B)` 数据包（`Location: Position` + `Block ID: VarInt` 全局调色板 state id），向该区块九宫格内的所有玩家广播。
  * 实现 `Position` 类型的 64 位位域编解码工具（`((x & 0x3FFFFFF) << 38) | ((y & 0xFFF) << 26) | (z & 0x3FFFFFF)`，注意 C++ 中负数需显式符号扩展）。
  * 处理创造模式物品：响应 `Creative Inventory Action (0x1B)`，回 `Set Slot (0x16)`。
* **DoD（完成定义）**：
  * 客户端 A 敲掉一块草方块，地面出现一个坑；客户端 B 屏幕上的该方块立即同步消失。
* **推荐提交**：`feat(world): synchronize block digging and placement via Block Change`

---

### [ ] TASK-4.7: Sprint 4 集成验收与多玩家同服漫游
* **任务类型**：集成验证 (Integration)
* **预估耗时**：1 小时
* **任务目标**：
  * 启动两个 Minecraft 客户端（用户名分别为 Steve 和 Alex），共同登入服务器。
* **DoD（完成定义）**：
  * 两人能互相看见对方，彼此追逐移动流畅，无人物倒退或抽搐。
  * 互相可以在对方面前挖方块和搭方块，世界状态完美一致。
  * 开启 `-fsanitize=thread` 运行整个过程，无任何数据竞态报错。
* **推荐提交**：`chore: verify and pass Sprint 4 Multiplayer Synchronization milestone`

---

## Sprint 5: 压测机器人与性能调优（工期预估：4~5 天）

> **目标**：编写无头压测工具模拟数百并发玩家，运用 Linux 原生分析工具定位瓶颈，引入定长内存池完成优化，产出硬核性能指标。

---

### [ ] TASK-5.1: 纯 C++ 异步无头压测机器人（Headless Bot）
* **任务类型**：压测工具 (Tool)
* **预估耗时**：4 小时
* **任务目标**：
  * 编写脱离渲染界面的纯异步压测工具 `tests/bot_runner.cc`：
    * 基于 Asio 异步连接池，支持命令行参数 `--count 500`。
    * 并发发起 500 个 TCP 连接，模拟原版流程发送 Handshake（协议号 `340`）-> Login Start -> 接收 Login Success 与 Join Game。
    * 收到 `Player Position And Look (0x2F)` 时回 `Teleport Confirm (0x00)`。
    * 应答 `Keep Alive (0x1F)`：回 `Keep Alive (0x0B)`（`Long` 载荷）。
    * 进服成功后，挂载定时器以 20Hz 频率随机生成位移，持续向服务端发送 `Player Position (0x0D)` 包。
    * 消费并丢弃 25 个 `Chunk Data (0x20)` 包（解包开销本身就是压测负载的一部分）。
* **DoD（完成定义）**：
  * 能稳定维持 500+ 连接并发漫步，统计并打印服务端的响应率。
  * 机器人全程不被"版本不符/心跳超时"踢出。
* **推荐提交**：`feat(tools): build headless benchmark bot runner`

---

### [ ] TASK-5.2: 系统级压测环境配置与优化
* **任务类型**：环境调优 (Infra)
* **预估耗时**：1 小时
* **任务目标**：
  * 修改 Linux 文件描述符上限：`ulimit -n 65535`。
  * 调整内核参数避免端口耗尽：开启 `net.ipv4.tcp_tw_reuse = 1`。
  * 编译服务端 Release 带符号构建模式：`-O2 -g -fno-omit-frame-pointer`。
* **DoD（完成定义）**：
  * 服务端与压测客户端在单机启动 1000 个连接无 `Too many open files` 或 `Address already in use` 错误。
* **推荐提交**：`chore(perf): configure system ulimit and release build flags`

---

### [ ] TASK-5.3: Linux `perf` 采样与火焰图（FlameGraph）流水线
* **任务类型**：性能分析 (Profiling)
* **预估耗时**：2 小时
* **任务目标**：
  * 在 500 机器人高频移动压测下，使用 Linux `perf` 对服务端主进程进行调用栈采样：
    `perf record -F 99 -p $(pgrep main_app) -g -- sleep 30`
  * 转换为火焰图：使用 FlameGraph 脚本生成可交互的 `flamegraph_before.svg`。
* **DoD（完成定义）**：
  * 成功在浏览器中打开 SVG 火焰图，清晰识别出占用 CPU 最多的热点函数（重点观察内存分配与数据拷贝）。
* **推荐提交**：`docs(perf): capture baseline CPU flamegraph under 500-bot load`

---

### [ ] TASK-5.4: 定长内存池（Fixed-size Buffer Pool）设计与实现
* **任务类型**：性能优化 (Optimization)
* **预估耗时**：3 小时
* **任务目标**：
  * 分析火焰图：高频数据包反序列化导致频繁调用系统的 `malloc` 与 `free`，造成锁竞争与堆碎片。
  * 实现基于 Free-List 的定长内存池（如 4KB 规格块）：
    * 提供 `acquire()` 获取预分配块，`release()` 归还给空闲链表。
    * 采用智能指针自定义 Deleter，实现 `std::unique_ptr<uint8_t[], BufferDeleter>` 自动回收。
* **DoD（完成定义）**：
  * 内存池单测通过，多线程申请与归还无内存泄漏。
* **推荐提交**：`feat(common): implement fixed-size Buffer Pool with custom deleter`

---

### [ ] TASK-5.5: 内存池接入与前后性能指标对比
* **任务类型**：优化度量 (Benchmark)
* **预估耗时**：2 小时
* **任务目标**：
  * 将网络收发与数据包临时缓冲区迁移至 `BufferPool`。
  * 重新施加 500 机器人压测，抓取优化后的火焰图 `flamegraph_after.svg`。
  * 统计对比指标：
    1. **Tick 抖动范围**：优化前 vs 优化后（如 $50\text{ms} \pm 8\text{ms} \rightarrow 50\text{ms} \pm 1\text{ms}$）。
    2. **CPU 占用率**：主进程占用下降百分比。
    3. **堆分配次数**：通过 `valgrind --tool=massif` 观测减少量。
* **DoD（完成定义）**：
  * 在 `doc/benchmark_report.md` 中形成量化对比表格与结论。
* **推荐提交**：`docs(perf): document performance gains from Buffer Pool optimization`

---

### [ ] TASK-5.6: Sanitizers 全方位防御性检查（动态清零）
* **任务类型**：代码质量 (Quality)
* **预估耗时**：2 小时
* **任务目标**：
  * 开启 `-fsanitize=address,undefined` 编译，在压测机器人断开与重连时观察是否有内存泄漏或越界。
  * 开启 `-fsanitize=thread` 编译，运行 10 分钟多玩家交互，验证是否有竞态。
* **DoD（完成定义）**：
  * 两个 Sanitizer 运行测试下控制台输出均为零 Warning、零 Error。
* **推荐提交**：`chore(qa): pass all AddressSanitizer and ThreadSanitizer checks`

---

## 个人进度看板跟踪表

| 迭代阶段 | 规划任务数 | 已完成 | 剩余 | 当前状态 | 预计完成日期 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Sprint 0: 基础设施与叶子组件** | 5 | 0 | 5 | 待开始 | Day 1 ~ Day 3 |
| **Sprint 1: 协议基座与列表探测** | 9 | 0 | 9 | 待开始 | Day 4 ~ Day 7 |
| **Sprint 2: 会话生命周期与虚空** | 7 | 0 | 7 | 待开始 | Day 8 ~ Day 12 |
| **Sprint 3: 区块模型与超平坦** | 6 | 0 | 6 | 待开始 | Day 13 ~ Day 17 |
| **Sprint 4: 架构解耦与多玩家** | 7 | 0 | 7 | 待开始 | Day 18 ~ Day 23 |
| **Sprint 5: 压测调优与 Profiling** | 6 | 0 | 6 | 待开始 | Day 24 ~ Day 28 |
| **总计** | **40 个微任务** | **0** | **40** | **准备启动** | **预计 4~5 周** |
