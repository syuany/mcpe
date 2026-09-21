# Sprint 1: 网络基座与列表探测（Server List Ping）

> **迭代目标**：搭建 Asio 异步网络骨架，设计连接生命周期管理，解决 TCP 粘包与半包问题，跑通协议状态机流转，让原版 Minecraft 1.12.2 客户端在多人游戏列表中成功搜出服务器并显示绿色延迟标。  
> **预估工期**：3 ~ 4 天  
> **保姆级开发教程**：细致到目录与代码的逐步实战指引见 [`doc/tutorial/sprint1_network_ping.md`](../tutorial/sprint1_network_ping.md)。  
> **协议参考**：本阶段全部包 ID、字段类型与字节序以 [`doc/03_protocol_340_cheatsheet.md`](../03_protocol_340_cheatsheet.md) 为准（目标协议 **Minecraft Java Edition 1.12.2 / protocol 340**）。  
> **总任务数**：9 个微任务

---

## 任务执行看板

- [x] [TASK-1.1: Wireshark 抓包分析与报文 Hex 留存](#task-11-wireshark-抓包分析与报文-hex-留存)
- [ ] [TASK-1.2: Asio TcpServer 与 TcpSession 基础连接骨架](#task-12-asio-tcpserver-与-tcpsession-基础连接骨架)
- [ ] [TASK-1.3: 发送队列与并发安全下发](#task-13-发送队列与并发安全下发)
- [ ] [TASK-1.4: 协议数据包拆包器（Packet Framing）](#task-14-协议数据包拆包器packet-framing)
- [ ] [TASK-1.5: 协议状态机框架搭建](#task-15-协议状态机框架搭建)
- [ ] [TASK-1.6: Handshake (0x00) 解析与意图流转](#task-16-handshake-0x00-解析与意图流转)
- [ ] [TASK-1.7: Status Request (0x00) 与 JSON 描述响应](#task-17-status-request-0x00-与-json-描述响应)
- [ ] [TASK-1.8: Ping (0x01) 与 Pong (0x01) 时间戳响应](#task-18-ping-0x01-与-pong-0x01-时间戳响应)
- [ ] [TASK-1.9: Sprint 1 集成验收与原版客户端联调](#task-19-sprint-1-集成验收与原版客户端联调)

---

### [x] TASK-1.1: [Spike] Wireshark 抓包分析与报文 Hex 留存
* **任务类型**：技术探针 (Spike)
* **预估耗时**：1 小时
* **产出文档**：详见 [`doc/packets_hex_dump.md`](../packets_hex_dump.md)
* **任务目标**：
  * 开启 Wireshark 监听 `Loopback: lo` 网卡，端口过滤：`tcp.port == 25565`。
  * 刷新客户端列表，观察 Minecraft 发出的真实 TCP 数据。
  * 提取 `Handshake`、`Status Request`、`Ping`、`Pong` 的原始十六进制字节，留存到 `doc/packets_hex_dump.md` 中。
* **DoD（完成定义）**：
  * 产出 Hex 报文文档，清楚标注每个字节对应的协议字段意义（如哪几个字节是 VarInt 长度，哪几个是 JSON 长度）。
* **推荐提交**：`docs(protocol): record reference hex dump of Server List Ping`

---

### TASK-1.2: Asio `TcpServer` 与 `TcpSession` 基础连接骨架
* **任务类型**：网络核心 (Core)
* **预估耗时**：2.5 小时
* **任务设计指南**：
  * `TcpServer`：持有 `asio::ip::tcp::acceptor`，在构造函数中绑定 `0.0.0.0:25565` 并启动异步 `async_accept` 循环。
  * `TcpSession`：持有 `asio::ip::tcp::socket`，必须继承 `std::enable_shared_from_this<TcpSession>`。
  * 实现 `start()`，发起初始的 `socket.async_read_some(...)`。
* **DoD（完成定义）**：
  * 启动服务，运行 `nc 127.0.0.1 25565` 建立连接，服务端控制台打印连接连入日志。
  * 关闭 `nc`，服务端捕获 `asio::error::eof` 错误码，优雅清理资源，控制台打印析构日志，无内存泄漏。
* **避坑指南**：
  * ⚠️ **智能指针自保原则**：异步操作未完成时，必须确保 `TcpSession` 活在内存中。在传递给 `async_read_some` 的 lambda 中，必须显式捕获 `self = shared_from_this()`，绝对不能只传裸指针 `this`。
* **推荐提交**：`feat(network): implement basic TcpServer and TcpSession lifetime`

---

### TASK-1.3: 发送队列（Write Queue）与并发安全下发
* **任务类型**：网络核心 (Core)
* **预估耗时**：2 小时
* **任务设计指南**：
  * 在 `TcpSession` 中声明发送队列：`std::deque<ByteBuffer> write_queue_`，以及标志位 `bool is_writing_ = false;`。
  * 外部调用 `send_packet(ByteBuffer buf)` 时：
    * 将待发送数据推入 `write_queue_`。
    * 如果 `!is_writing_`，将 `is_writing_ = true`，取出队首启动 `asio::async_write`。
    * 在异步写完成回调中：检查 `write_queue_`，若非空则继续写下一笔；若为空则将 `is_writing_ = false`。
* **DoD（完成定义）**：
  * 模拟在同一微秒内连续调用 100 次 `send_packet`，数据包按顺序被完整发送到对端，不会抛出 Asio 内部并发写断言异常。
* **推荐提交**：`feat(network): implement safe write queue in TcpSession`

---

### TASK-1.4: 协议数据包拆包器（Packet Framing）
* **任务类型**：协议核心 (Core)
* **预估耗时**：3 小时
* **技术与面试专题指南**：详见 [`doc/05_tcp_framing_and_network_interview_qa.md`](../05_tcp_framing_and_network_interview_qa.md)
* **任务设计指南**：
  * 针对 Minecraft 协议格式：`[Packet Length: VarInt] + [Packet ID: VarInt] + [Payload]`。
  * 在 `TcpSession` 中维护一个累积接收缓冲区 `ByteBuffer recv_buf_`。
  * 拆包算法时序：
    1. 每次 `async_read_some` 读到数据后，追加到 `recv_buf_`。
    2. 尝试读取 VarInt 长度 `packet_len`。如果剩余可读数据连一个完整的 VarInt 都读不完，保留当前状态，等待下一次数据到来（半包）。
    3. 如果读取到了 `packet_len`，检查 `recv_buf_.readable_bytes() >= packet_len`。
    4. 若不足，回退读游标，等待更多数据（半包）。
    5. 若满足，切出一个精确长度为 `packet_len` 的独立 Buffer 交给上层路由器，并滑动游标。
    6. 重复步骤 2，直到缓冲区中的粘包全部处理完毕。
* **DoD（完成定义）**：
  * 编写单元测试验证：人为将一个包含 20 字节的数据包切成 3 字节、7 字节、10 字节分批喂入，拆包器能精准在最后一次返回完整包；将两个包拼接一次喂入，能连续返回 2 个包。
* **推荐提交**：`feat(protocol): implement packet framing for length-delimited packets`

---

### TASK-1.5: 协议状态机框架搭建
* **任务类型**：业务架构 (Architecture)
* **预估耗时**：1.5 小时
* **任务设计指南**：
  * 定义枚举：`enum class ConnectionState { HANDSHAKING, STATUS, LOGIN, PLAY };`
  * 在 `TcpSession` 初始化时默认设为 `HANDSHAKING`。
  * 设计数据包路由分发方法：`void handle_packet(int32_t packet_id, ByteBuffer& payload);`，根据当前状态匹配处理逻辑。
* **DoD（完成定义）**：
  * 状态机转移非法时打印 `warn` 日志并切断恶意连接。
  * ⚠️ **1.12.2 客户端例行包白名单必须显式注册为空实现分支**，否则客户端刚进服就被自己的路由器踢下线：`Client Settings (0x04)`、`Plugin Message (0x09)`、`Player Abilities (0x13)`、`Held Item Change (0x1A)`、`Client Status (0x03)`、`Confirm Transaction (0x05)`、`Close Window (0x08)`、`Resource Pack Status (0x18)`。
* **推荐提交**：`feat(protocol): add connection state machine router`

---

### TASK-1.6: `Handshake (0x00)` 解析与意图流转
* **任务类型**：协议业务 (Business)
* **预估耗时**：1.5 小时
* **任务设计指南**：
  * 在 `HANDSHAKING` 状态下匹配 `0x00`。
  * 解析字段：
    * `Protocol Version`: VarInt（1.12.2 为 `340`）
    * `Server Address`: String（客户端输入的服务器域名或 IP）
    * `Server Port`: uint16_t（端口号，如 25565）
    * `Next State`: VarInt（意图状态：`1` 表示状态查询，`2` 表示登录进服）
* **DoD（完成定义）**：
  * 当 `Next State == 1` 时，将 Session 状态原子切换为 `ConnectionState::STATUS`。
  * 打印并记录客户端上报的 `Protocol Version`（1.12.2 应为 `340`）；对非 `340` 的登录请求（`Next State == 2`）回 `Disconnect (login, 0x00)` 并说明版本不符。
* **推荐提交**：`feat(protocol): handle Handshake packet and route next state`

---

### TASK-1.7: `Status Request (0x00)` 与 JSON 描述响应
* **任务类型**：协议业务 (Business)
* **预估耗时**：2 小时
* **任务设计指南**：
  * 在 `STATUS` 状态下收到 `0x00`（无数据载荷）。
  * 构造响应数据包 `Status Response (0x00)`：
    * 组装 JSON 字符串（包含协议版本号 `340`、名称 `"1.12.2"`、在线人数 `0`、最大人数 `100`、MOTD 标语）。
    * 将该字符串写入 Buffer（前缀该字符串的 VarInt 字节长度）。
    * 在外层打包：添加 Packet ID `0x00` 与最外层总长度 VarInt。
* **DoD（完成定义）**：
  * 通过 `send_packet` 下发给客户端，客户端未报格式解析错误。
  * 本阶段**不发送** `Set Compression (0x03)`，一律使用未压缩包格式 `[Length:VarInt][ID:VarInt][Payload]`，压缩优化留到 Sprint 5。
* **推荐提交**：`feat(protocol): respond to status request with MOTD json`

---

### TASK-1.8: `Ping (0x01)` 与 `Pong (0x01)` 时间戳响应
* **任务类型**：协议业务 (Business)
* **预估耗时**：1 小时
* **任务设计指南**：
  * 在 `STATUS` 状态下收到 `Ping (0x01)`，读取其中的 `Payload: Long` 时间戳值（**必须是大端 8 字节 `Long`**，不是 `VarInt`、也不是 4 字节 `Int`）。
  * 立即构造 `Pong (0x01)` 数据包，原封不动地将此 `Long`（8 字节）值封包发回客户端。
* **DoD（完成定义）**：
  * 原样回送时间戳后，客户端计算得出准确的网络往返延迟（RTT）。
  * Ping/Pong 载荷必须按 `Long` 类型读写，在字节流中严格占 **8 字节**（大端）；写成 `VarInt` 或 `Int` 会导致客户端解析错位、RTT 显示异常。
* **推荐提交**：`feat(protocol): handle ping request and echo pong timestamp`

---

### TASK-1.9: Sprint 1 集成验收与原版客户端联调
* **任务类型**：集成验证 (Integration)
* **预估耗时**：1 小时
* **任务目标**：
  * 启动服务端进程。
  * 打开 Minecraft **1.12.2** 客户端，在“多人游戏”中添加 `127.0.0.1:25565`。
* **DoD（完成定义）**：
  * 客户端列表中正常显示绿色延迟信号（例如 `<1ms`）。
  * 显示自定义的 MOTD 描述和在线玩家数 `0/100`。
  * 客户端日志窗口和控制台无报错信息。
* **推荐提交**：`chore: verify and pass Sprint 1 Server List Ping milestone`

---

## Sprint 1 交付物验收检查单（Review Checklist）

- [ ] 网络层代码已与底层 `ByteBuffer` / `VarInt` 解耦。
- [ ] 粘包和半包在各种网络抖动下均能正确切包。
- [ ] 连续多次写操作由发送队列平滑处理，无 Asio 竞态断言。
- [ ] 原版客户端成功刷新出绿色延迟信号。
- [ ] Handshake 中读到的 `Protocol Version` 为 `340`，Status JSON 回填 `"1.12.2"` / `340`。
- [ ] 准备进入 [Sprint 2: 会话生命周期与虚空进服](file:///home/ubu/dev/my/mcpe/doc/sprints/03_sprint2_login_spawn.md)。
