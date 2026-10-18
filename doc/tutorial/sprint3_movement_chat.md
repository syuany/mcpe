# Sprint 3 保姆级实战开发教程：玩家移动同步、同屏实体可见性与双向聊天系统 (Movement, Entity Visibility & Chat Multicast)

> **目标协议**：**Minecraft Java Edition 1.12.2（Protocol Version: 340）**  
> **前置里程碑**：Sprint 0（基础组件）、Sprint 1（网络基座与 Server List Ping）、Sprint 2（登录鉴权与 5x5 区块空间渲染）已全部验收通过。  
> **本阶段核心目标**：从“孤身一人静态世界”跃迁至“多玩家动态同步联机与大厅社交”！彻底解耦网络 I/O 线程与游戏逻辑主线程；设计基于双缓冲的 MPSC 跨线程任务通道与精准 20 TPS 物理节拍器；消费客户端位移与朝向数据包，广播同屏人物模型生成、增量相对位移与转头动作；打通聊天多播与快捷指令分流管道！  
> **教学设计原则**：**透彻讲清“是什么、为什么、怎么做”**。恪守四层架构红线，零裸写魔数，零未受控并发竞态。  
> **预估工期**：3 ~ 4 天 | **核心任务数**：5 个微任务

---

## 目录

- [一、 心智模型跃迁与架构原理（是什么与为什么）](#一-心智模型跃迁与架构原理是什么与为什么)
  - [1.1 为什么严禁网络线程直接读写游戏世界？（并发安全深度解析）](#11-为什么严禁网络线程直接读写游戏世界并发安全深度解析)
  - [1.2 为什么采用单写入者主循环（Actor 模式）+ 外部 MPSC 管道？](#12-为什么采用单写入者主循环actor-模式--外部-mpsc-管道)
  - [1.3 什么是 20 TPS 节拍器？为什么简单 sleep 会导致灾难性漂移？](#13-什么是-20-tps-节拍器为什么简单-sleep-会导致灾难性漂移)
  - [1.4 状态同步模型：为什么在局域网/互联网中必须使用增量编码？](#14-状态同步模型为什么在局域网互联网中必须使用增量编码)
- [二、 空间数学与二进制底层协议硬核推演（协议物理详解）](#二-空间数学与二进制底层协议硬核推演协议物理详解)
  - [2.1 同屏可见性铁律：为什么跳过 Player List Item 会导致“隐形人”？](#21-同屏可见性铁律为什么跳过-player-list-item-会导致隐形人)
  - [2.2 相对位移定点数运算（1/4096 方块）与 8.0 格临界溢出推导](#22-相对位移定点数运算14096-方块与-80-格临界溢出推导)
  - [2.3 视角量化（Angle 字节）与绝对朝向规范](#23-视角量化angle-字节与绝对朝向规范)
  - [2.4 聊天消息协议（ChatComponent JSON AST 与快捷指令分流）](#24-聊天消息协议chatcomponent-json-ast-与快捷指令分流)
- [三、 严格四层架构拓扑与模块职责划分](#三-严格四层架构拓扑与模块职责划分)
  - [3.1 跨线程读写管道与数据流向时序图](#31-跨线程读写管道与数据流向时序图)
  - [3.2 单 Tick 核心流水线（Tick Pipeline）](#32-单-tick-核心流水线tick-pipeline)
  - [3.3 新增/修改模块文件蓝图](#33-新增修改模块文件蓝图)
- [四、 任务逐步实现指南（怎么做：接口契约与代码落地）](#四-任务逐步实现指南怎么做接口契约与代码落地)
  - [TASK-3.1: 跨线程 MPSC 任务队列与双缓冲交换 (`EventQueue.h`)](#task-31-跨线程-mpsc-任务队列与双缓冲交换-eventqueueh)
  - [TASK-3.2: 精准 20 TPS 物理节拍器与漂移补偿 (`WorldTicker.h/.cc`)](#task-32-精准-20-tps-物理节拍器与漂移补偿-worldtickerhcc)
  - [TASK-3.3: 客户端移动包解析与坐标合法性校验 (`PlayHandler.cc`)](#task-33-客户端移动包解析与坐标合法性校验-playhandlercc)
  - [TASK-3.4: 玩家进出视野广播与实体生命周期 (`EntityManager.h/.cc`)](#task-34-玩家进出视野广播与实体生命周期-entitymanagerhcc)
  - [TASK-3.5: 聊天多播与快捷指令路由系统 (`ChatHandler.h/.cc`)](#task-35-聊天多播与快捷指令路由系统-chathandlerhcc)
- [五、 工业级避坑指南与高频面试核心考点](#五-工业级避坑指南与高频面试核心考点)
- [六、 验收门禁与端到端联调规范 (DoD)](#六-验收门禁与端到端联调规范-dod)

---

## 一、 心智模型跃迁与架构原理（是什么与为什么）

### 1.1 为什么严禁网络线程直接读写游戏世界？（并发安全深度解析）

* **是什么**：
  在 Asio 异步架构中，`io_context::run()` 通常运行在独立的 I/O 线程池中。当客户端上传移动数据包时，网络库触发 `TcpSession::doRead()`，完成定界拆包。
* **为什么（痛点与危害）**：
  新手最容易犯的致命错误是：在网络回调中直接调用 `world.update_player_position(player_id, x, y, z)` 并遍历其他在线玩家发送广播包。
  ```
  [错误示范：并发读写导致未定义行为]
  I/O 线程 1 ──► 正在遍历在线玩家列表广播移动包 ──────────────┐ (内存冲突！)
  I/O 线程 2 ──► 另一个玩家恰好断开连接，正在执行 erase() ──┴─► 迭代器失效 / SIGSEGV 崩溃！
  ```
  1. **数据竞态（Data Race）**：多线程并发修改同一个 `std::unordered_map` 或实体对象，引发未定义行为（UB）；
  2. **死锁与锁竞争泥潭**：若为了线程安全给 `World` 加一把大读写锁（`std::shared_mutex`），当在线人数升高时，多个网络线程频繁争抢互斥锁，导致 I/O 线程大面积阻塞挂起，Asio 的异步吞吐优势彻底丧失。

---

### 1.2 为什么采用单写入者主循环（Actor 模式）+ 外部 MPSC 管道？

* **是什么**：
  **单写入者原则（Single-Writer Principle）**：整个游戏世界的状态（玩家坐标、实体列表、世界时间、计分板）**有且仅由一个唯一的逻辑主线程负责修改**。
  网络线程只负责“解包”，解出纯数据结构后作为事件投递到外部管道。
* **为什么（优势）**：
  1. **完全消除业务加锁**：逻辑主线程在执行游戏逻辑时，世界数据是 100% 线程私有的，不需要加任何互斥锁，CPU 缓存命中率（L1/L2 Cache）极高；
  2. **确定性与可复现性（Determinism）**：逻辑帧以固定顺序推进，杜绝多线程时序错乱导致的幽灵位移或状态不一致；
  3. **架构解耦**：网络层退化为纯粹的 I/O 适配器，即使未来更换网络库（如从 Asio 换为 io_uring），游戏世界逻辑一行都不用改。

---

### 1.3 什么是 20 TPS 节拍器？为什么简单 sleep 会导致灾难性漂移？

* **是什么**：
  Minecraft 的物理时间以 **TPS（Ticks Per Second，每秒刻数）** 计量。标准节拍为 **20 TPS**，即每个游戏物理刻的间隔必须严格维持在 **50 毫秒（50,000,000 纳秒）**。
* **为什么不能直接固定休眠 50ms（固定 Sleep 的累积漂移崩塌）**：
  若在物理主循环中写成 `tick()` 之后直接调用固定时长的 `sleep_for(50ms)`，系统将面临灾难性的**累积时间漂移（Cumulative Drift）**：
  - 设单帧业务计算开销为 $\Delta t_{work} = 5\text{ms}$，操作系统线程调度切换延迟为 $\Delta t_{sched} = 1\text{ms}$；
  - 实际物理循环的单 Tick 真实耗时将膨胀为 $50 + 5 + 1 = 56\text{ms}$；
  - 运行仅 1 小时后，逻辑世界时间相比真实物理时间将严重滞后数分钟，直接破坏客户端移动插值平滑度并导致心跳超时误踢！
* **怎么做（漂移消除与恒定步长算法）**：
  采用**基于单调时钟（`std::chrono::steady_clock`）与时间累加器（Accumulator）的高精度追帧算法**，按恒定 50ms 步长消耗累加时间，结合防死亡螺旋熔断阈值（如最多追 5 帧 / 250ms），并动态休眠剩余余量。

---

### 1.4 状态同步模型：为什么在局域网/互联网中必须使用增量编码？

* **是什么**：
  状态同步通常有两种形式：
  - **全量同步（Absolute State）**：每次都下发完整的绝对坐标 $(X, Y, Z, Yaw, Pitch)$（每个坐标 Double 占 8 字节，Float 占 4 字节，单包载荷 $>30$ 字节）；
  - **增量同步（Delta Encoding）**：仅下发相对于上一次状态的差值 $(\Delta X, \Delta Y, \Delta Z)$。
* **为什么**：
  在大厅中，若有 100 名玩家同屏奔跑，每秒产生 20 次位移广播：
  - 若使用全量包：$100 \times 100 \times 20 \times 34\text{ bytes} \approx 6.8\text{ MB/s}$（上行带宽直接被打爆！）；
  - 若使用 1.12.2 的定点数增量包：增量仅占 Short（2 字节），结合变长整型 EID，单包体积缩小 70% 以上，极大缓解网络拥塞。

---

## 二、 空间数学与二进制底层协议硬核推演（协议物理详解）

---

### 2.1 同屏可见性铁律：为什么跳过 Player List Item 会导致“隐形人”？

在开发 MC 服务端时，很多初学者实现完“玩家加入下发 `Spawn Player (0x05)`”后，进服却发现**屏幕上空无一人**，但服务端日志明明显示发包成功。

* **是什么（官方客户端渲染断言契约）**：
  Notchian 官方客户端（Java 原版 Client）在接收到 `Spawn Player (0x05)` 时，内部渲染管线存在严格的前置断言契约：
  - 客户端通过数据包携带的玩家 UUID，在本地玩家信息哈希表（`playerInfoMap`）中检索对应的 `NetworkPlayerInfo`（包含皮肤材质、用户名文本与游戏模式）；
  - **静默丢弃陷阱**：若表中检索不到该 UUID（即服务端尚未同步该玩家的 Profile），客户端本地渲染器将直接静默忽略该实体的 3D 模型创建与画面渲染，既不报错也不抛出异常，直接表现为“看不见的隐形人”！
* **协议铁律**：
  **向客户端下发 `Spawn Player (0x05)` 之前，必须先下发携带该玩家 UUID 的 `Player List Item (0x2E, Action=0)`！**
  
```
┌────────────────────────────────────────────────────────────────────────┐
│ 客户端接收数据包严格时序                                                  │
├────────────────────────────────────────────────────────────────────────┤
│ 1. 0x2E Player List Item (Action: 0 Add Player, 携带 UUID 与用户名)     │
│    └─► 客户端将玩家存入本地 playerInfoMap                               │
│ 2. 0x05 Spawn Player (携带相同 UUID 与初始坐标 XYZ)                     │
│    └─► 客户端成功匹配 playerInfoMap，并在对应坐标生成渲染人物模型！        │
└────────────────────────────────────────────────────────────────────────┘
```

#### 数据包物理二进制结构速查：
1. **`0x2E Player List Item (Clientbound)`**：
   - `Action`: **`VarInt(0)`**（`0 = Add Player`）
   - `Number of Players`: **`VarInt(1)`**
   - 玩家条目数据：
     - `UUID`: **`16 Bytes`**（原始二进制 UUID，大端存储）
     - `Name`: **`String`**（玩家用户名，如 `"Steve"`）
     - `Number of Properties`: **`VarInt(0)`**（皮肤属性，无皮肤时填 0）
     - `Gamemode`: **`VarInt(1)`**（`1 = Creative`, `2 = Adventure`）
     - `Ping`: **`VarInt(5)`**（延迟毫秒数）
     - `Has Display Name`: **`Boolean(false)`**
2. **`0x05 Spawn Player (Clientbound)`**：
   - `Entity ID`: **`VarInt`**（全服自增唯一正整型 EID）
   - `Player UUID`: **`16 Bytes`**
   - `X`: **`Double`**（8 字节大端 IEEE 754 浮点）
   - `Y`: **`Double`**
   - `Z`: **`Double`**
   - `Yaw`: **`Byte`**（Angle 字节）
   - `Pitch`: **`Byte`**（Angle 字节）
   - `Entity Metadata`: **`Byte(0xFF)`**（固定以 `0xFF` 结束符表示无附加元数据）

---

### 2.2 相对位移定点数运算（1/4096 方块）与 8.0 格临界溢出推导

* **是什么**：
  在 Minecraft 1.12.2 中，为了压缩带宽，`Entity Relative Move (0x26)` 中的位移量并不是 Float，也不是 1.8 时代的 Byte，而是 **有符号 16 位短整型（`int16_t` / `Short`）定点数**。
  其度量单位为：**$\frac{1}{4096}$ 个方块**。

* **数学转换公式**：
  设玩家当前帧坐标为 $X_{\text{curr}}$，上一帧坐标为 $X_{\text{prev}}$，位移物理量为 $\Delta X = X_{\text{curr}} - X_{\text{prev}}$。
  编码为协议整数：
  $$\Delta X_{\text{encoded}} = \text{static\_cast}<\text{int16\_t}>\left( \Delta X \times 4096.0 \right)$$
  客户端解码物理量：
  $$\Delta X = \frac{\Delta X_{\text{encoded}}}{4096.0}$$

* **为什么存在 8.0 格临界溢出？**
  `int16_t` 的数值范围是 $[-32768, 32767]$。
  计算单轴能够表示的最大物理位移：
  $$\Delta X_{\max} = \frac{32767}{4096.0} \approx 7.999755859375 \text{ 方块} < 8.0 \text{ 方块}$$
  $$\Delta X_{\min} = \frac{-32768}{4096.0} = -8.0 \text{ 方块}$$
  
  > ⚠️ **关键防御规则**：
  > 如果玩家单 Tick 位移的绝对值 **$|\Delta X| \ge 8.0$、或 $|\Delta Y| \ge 8.0$、或 $|\Delta Z| \ge 8.0$**（例如发生传送、被敲击击退、或者网络卡顿后积攒的大跨步）：
  > **如果强行将截断值填入 0x26，会导致高位溢出翻转符号，玩家在其他客户端屏幕上会发生严重的“闪回反向抽搐”！**
  > **正确做法**：单轴位移 $\ge 8.0$ 格时，**必须降级下发绝对坐标包 `0x4C Entity Teleport`**！

#### 物理封包对比速查：
* **`0x26 Entity Relative Move`**：
  - `Entity ID`: `VarInt`
  - `Delta X`: `Short (int16_t)`
  - `Delta Y`: `Short (int16_t)`
  - `Delta Z`: `Short (int16_t)`
  - `On Ground`: `Boolean`
* **`0x4C Entity Teleport`（降级绝对包）**：
  - `Entity ID`: `VarInt`
  - `X`: `Double`, `Y`: `Double`, `Z`: `Double`
  - `Yaw`: `Byte Angle`, `Pitch`: `Byte Angle`
  - `On Ground`: `Boolean`

---

### 2.3 视角量化（Angle 字节）与绝对朝向规范

* **是什么**：
  客户端上行的偏航角（Yaw）和俯仰角（Pitch）是 32 位浮点数（Float，单位：度 $0.0 \sim 360.0^\circ$）。
  但在下行实体朝向包中，为了节约带宽，Mojang 将其压缩为 **单字节整数 `uint8_t`（协议称之为 Angle）**。

* **数学量化算法**：
  一个圆周 $360^\circ$ 被均匀划分为 256 份。
  $$\text{Angle} = \text{static\_cast}<\text{uint8\_t}>\left( \frac{\text{degrees}}{360.0f} \times 256.0f \right)$$
  - 精度损失：$\frac{360^\circ}{256} \approx 1.406^\circ$。人眼在屏幕上观察远距离模型时，无法感知到 $1.4^\circ$ 的细微阶梯，完全具备视觉平滑性。
* **数据包说明**：
  - `0x28 Entity Look`：仅朝向改变，位移未变（`Entity ID: VarInt` + `Yaw: Angle` + `Pitch: Angle` + `On Ground: Bool`）；
  - `0x27 Entity Look And Relative Move`：朝向与位移同时改变。
  - **重要细节**：无论是 0x28 还是 0x27，其携带的 **Yaw 与 Pitch 均是绝对朝向**，不是增量！

---

### 2.4 聊天消息协议（ChatComponent JSON AST 与快捷指令分流）

* **是什么**：
  - **上行包 `0x02 Chat Message (Serverbound)`**：
    载荷为 `VarInt` 定界的 UTF-8 字符串（长度 $1 \sim 256$ 字符）。
  - **下行包 `0x0F Chat Message (Clientbound)`**：
    载荷为：
    1. `JSON Data`: `String`（Minecraft 标准富文本格式）
    2. `Position`: `Byte`（`0 = 聊天框 Chat Box`, `1 = 系统消息 System Message`, `2 = 快捷栏上方 Actionbar`）

* **JSON 规范与颜色样式**：
  原版客户端不接受裸纯文本，必须是合法的 JSON 对象：
  ```json
  {"text": "<Steve> 大家好，欢迎来到大厅！", "color": "yellow", "bold": false}
  ```
* **快捷指令分流机制**：
  如果玩家输入的内容以斜杠 `'/'` 开头（如 `/help`, `/lobby`, `/lobbies`）：
  - 服务端**绝对不能将其广播给同房间的其他玩家**（否则会污染公屏聊天）；
  - 应当由 `CommandRouter` 拦截解析，并单播回传执行结果：
    ```json
    {"text": "[系统] 当前活跃大厅分线: lobby-1 (12/50), lobby-2 (5/50)", "color": "green"}
    ```

---

## 三、 严格四层架构拓扑与模块职责划分

---

### 3.1 跨线程读写管道与数据流向时序图

整个 Sprint 3 的核心数据流转严格遵循以下时序，任何跨层越权都是架构红线：

```mermaid
sequenceDiagram
    autonumber
    actor ClientA as 客户端 A
    participant Network as TcpSession (Asio 线程)
    participant Queue as EventQueue (双缓冲管道)
    participant Ticker as WorldTicker (20 TPS 主线程)
    participant EntityMgr as EntityManager (世界空间管理)
    actor ClientB as 客户端 B

    ClientA->>Network: 发送 0x10 Player Position And Look
    Network->>Network: PacketFraming 定界拆包
    Network->>Queue: push(PlayerMoveEvent) [短暂加锁投递]
    Note over Queue: 事件暂存在 write_queue_ 中

    rect rgb(240, 248, 255)
    Note over Ticker: 50ms 节拍器触发 (Tick 开始)
    Ticker->>Queue: swap() [微秒级交换读写指针]
    Queue-->>Ticker: 返回待处理的 read_queue_
    Ticker->>EntityMgr: 消费 PlayerMoveEvent，更新 ClientA 坐标
    EntityMgr->>EntityMgr: 计算 ClientA 相对位移 (ΔX, ΔY, ΔZ)
    EntityMgr->>Network: 构造 0x27 Look And Relative Move (Shared ByteBuffer)
    end

    Network->>ClientB: async_write 广播给同房间的 ClientB
    Note over ClientB: 客户端 B 屏幕上看到 ClientA 平滑奔跑转头！
```

---

### 3.2 单 Tick 核心流水线（Tick Pipeline）

在 `WorldTicker::tick()` 的 50ms 周期内，逻辑严格划分为以下**四大阶段**，顺序不可颠倒：

1. **阶段 1：事件收取与消费（Event Ingestion）**
   - 交换 `EventQueue`，处理所有外部投递的事件：玩家登入、玩家断开、玩家位移、玩家聊天；
2. **阶段 2：世界状态更新（World Simulation）**
   - 更新实体坐标与朝向，更新大厅与房间计时器；
   - 校验防作弊速度上限（若单 Tick 位移 $> 15.0$ 格，判定非法瞬移并触发拉回）；
3. **阶段 3：空间观察者与增量广播（AOI & Delta Multicast）**
   - 遍历产生位移的玩家，计算其与同房间观察者的相对位移；
   - 序列化生成增量包（`0x26`/`0x27`/`0x28` 或降级 `0x4C`）；
   - 使用 `shared_ptr<ByteBuffer>` 将数据包推入各目标会话的发送管道；
4. **阶段 4：心跳保活与超时清理（KeepAlive & Cleanup）**
   - 每 15 秒向空闲玩家发送 `KeepAlive (0x1F)`；清理连续 30 秒无响应的死连接。

---

### 3.3 新增/修改模块文件蓝图

```
src/
├── common/
│   ├── SpatialTypes.h         # [基石] Vector3d, Rotation, BlockPos 空间原语
│   └── EventQueue.h           # [核心] MPSC 双缓冲无锁/低锁跨线程任务队列
├── world/
│   ├── WorldTicker.h/.cc      # [核心] 20 TPS 物理主循环与单调时钟漂移补偿
│   └── EntityManager.h/.cc    # [核心] 玩家实体生命周期、坐标管理与位移广播
├── protocol/
│   ├── ChatHandler.h/.cc      # [核心] 聊天 JSON 序列化、富文本多播与指令路由
│   ├── PlayHandler.cc         # [修改] 增加 0x0E/0x0F/0x10 移动包与 0x02 聊天包解码
│   └── PacketTypes.h          # [修改] 补齐 0x05, 0x26, 0x27, 0x28, 0x2E, 0x32, 0x4C 数据包 ID
└── server.cc                  # [修改] 启动并挂接 WorldTicker 线程
```

---

## 四、 任务逐步实现指南（怎么做：接口契约与设计落地）

---

### TASK-3.1: 跨线程 MPSC 任务队列与双缓冲低锁拓扑设计 (`EventQueue.h`)

* **MPSC 并发拓扑架构（Multi-Producer Single-Consumer）**：
  - **生产者角色**：多个 Asio 网络 I/O 线程，在解析出客户端操作事件后并发推入；
  - **消费者角色**：唯一的世界物理主线程 `WorldTicker`，在每个 50ms Tick 开始时批量收取；
* **双缓冲设计（Double-Buffering）与微秒级交换契约**：
  - **为什么拒绝标准 `std::queue + mutex`**：传统队列在高并发下，消费者每 Pop 一个元素都要加锁解锁一次，主线程与 I/O 线程争锁极度剧烈；
  - **双缓冲内存拓扑**：内部维护写入队列 `write_queue_` 与读取队列 `read_queue_`；
  - **推入契约（push）**：网络线程推入事件时仅需持有互斥锁数纳秒，将事件移入 `write_queue_` 尾部后即刻释放；
  - **交换契约（swap）**：逻辑主线程在单 Tick 开始时调用 `swap()`，在互斥锁极短保护下瞬间交换双缓冲底层指针（耗时 $\approx 10\text{ns}$）。随后主线程在**完全无锁状态下安全遍历读取队列**，彻底消除高并发下的锁争用与 CPU Cache 颠簸；
* **强类型事件多态载荷设计**：
  - 运用 `std::variant` 零开销值语义多态包装各类事件：`PlayerMoveEvent`、`PlayerChatEvent`、`PlayerJoinEvent`、`PlayerLeaveEvent`；
  - **消除原始类型偏执（Primitive Obsession）**：移动事件显式复用 `SpatialTypes.h` 中的 `Vector3d` 与 `Rotation` 空间强类型，并携带 `has_position`、`has_look`、`on_ground` 等状态标志位。

---

### TASK-3.2: 精准 20 TPS 物理节拍器与漂移补偿算法设计 (`WorldTicker.h/.cc`)

* **恒定步长物理循环架构（Fixed Timestep）**：
  - 物理世界刻严格锁定为 20 TPS，每个 Tick 间隔为恒定 50ms（`kTickInterval = 50'000'000ns`）；
  - 游戏世界的逻辑推进与网络包收发完全解耦；
* **单调时钟时间累加器（Accumulator）追帧算法**：
  - 借助 `std::chrono::steady_clock` 记录当前时间与上一帧时间的时间差，累加进 `accumulator`；
  - **恒定步长消耗循环**：只要 `accumulator >= kTickInterval`，持续调用 `tick()` 并扣除 50ms 间隔；
* **防死亡螺旋保护屏障（Anti-Death-Spiral Safeguard）**：
  - 当宿主机发生突发性系统冻结或重载时，`accumulator` 可能会暴涨至数秒；
  - 若无限制追帧，主线程将连续执行数十次 `tick()`，导致当前帧再次超时，进而陷入永远无法追上时间的“死亡螺旋”；
  - **熔断设计**：设定累加器上限阈值 `kMaxAccumulator = 250ms`（最多追 5 帧），超限时强制截断并记录日志告警，保护服务器平稳降级；
* **高精度动态休眠余量**：在追帧完毕后，计算当前帧已消耗的时间，使用 `sleep_for` 休眠剩余余量，平衡 CPU 占用率与节拍精度。

---

### TASK-3.3: 客户端移动包解析与空间坐标契约 (`PlayHandler.cc`)

* **三态移动数据包反序列化契约**：
  - `0x0E Player Position`：包含 3 个 Double 大端浮点数 $(X, Y, Z)$ 与 1 字节 `on_ground` 标志（`has_position=true, has_look=false`）；
  - `0x0F Player Look`：包含 2 个 Float 大端浮点数 $(Yaw, Pitch)$ 与 1 字节 `on_ground` 标志（`has_position=false, has_look=true`）；
  - `0x10 Player Position And Look`：包含完整 3 个 Double 坐标、2 个 Float 朝向与 `on_ground` 标志（`has_position=true, has_look=true`）；
* **数据包过滤与跨线程解耦分流**：
  - **两阶段传送未确认屏障**：若当前会话的 `Teleport ID` 尚未收到匹配的确认包，静默丢弃客户端上行的移动包，防范网络延迟引发的幽灵拉回；
  - **基础合法性防御**：校验坐标数值是否为 NaN 或 Inf，若非法直接拒绝；
  - 纯数据解包后构造 `PlayerMoveEvent`，通过 MPSC 队列投递至主线程，网络线程严禁直接修改实体状态。

---

### TASK-3.4: 玩家进出视野广播与增量位移同步算法设计 (`EntityManager.h/.cc`)

* **玩家实体生命周期模型（`PlayerEntity`）**：
  - 空间状态：维护当前坐标 `position`、朝向 `rotation`、着地状态 `on_ground`；
  - 差分同步基准：维护上一次同步给客户端的历史坐标 `last_synced_pos` 与朝向 `last_synced_rot`，以及增量脏标记 `dirty`；
* **进服与离线双向可见性编排（AOI Life Cycle）**：
  - **新玩家连入时序铁律**：
    1. 为新玩家分配单调自增的正整数实体 ID（EID）；
    2. 严格按时序先广播 `Player List Item (0x2E, Action=0)`，再广播 `Spawn Player (0x05)`；
    3. 双向展示：向所有既有玩家展示新玩家模型，同时向新玩家展示所有既有玩家的模型；
  - **玩家离开时序**：
    1. 向同大厅其他玩家下发 `Destroy Entities (0x32)` 携带该玩家 EID，彻底销毁客户端模型，杜绝幽灵替身；
    2. 下发 `Player List Item (0x2E, Action=4)` 从 Tab 列表中移除该玩家条目；
* **8.0 格定点数增量位移广播算法与降级设计**：
  - 差分位移计算：$\Delta X = X - X_{last}$，$\Delta Y = Y - Y_{last}$，$\Delta Z = Z - Z_{last}$；
  - **8.0 格物理临界判定**：
    - 1.12.2 协议增量包将位移编码为 16 位有符号整型（Short，单位 $\frac{1}{4096}$ 方块）；
    - 若单轴位移绝对值 $\ge 8.0$ 格（或发生了超大瞬移），Short 定点数将发生符号翻转与溢出；
    - **降级分支**：若溢出，降级下发包含绝对 Double 坐标的 `0x4C Entity Teleport`；
    - **增量分支**：若未溢出且位移与朝向均有变化，下发 `0x27 Look And Relative Move`；若仅有位移变化，下发 `0x26 Relative Move`；若仅有朝向变化，下发 `0x28 Entity Look`；
  - **COW 零拷贝多播**：主线程仅序列化一次数据包，通过 `std::shared_ptr<ByteBuffer>` 推入其余玩家的异步发送队列，底层共享同一只读内存，极致节省系统带宽与内存吞吐。

---

### TASK-3.5: 聊天富文本多播与指令路由系统架构设计 (`ChatHandler.h/.cc`)

* **客户端聊天上行消费（0x02 Chat Message Serverbound）**：
  - 读取变长 UTF-8 文本载荷（长度上限 256 字符）；
* **指令分流路由（Command Dispatching）**：
  - 若消息以斜杠 `/` 开头，截断广播，路由至指令解析系统：
    - `/help`：系统提示可用指令帮助；
    - `/lobby [id]`：切换至指定大厅分线；
    - `/lobbies`：系统富文本输出活跃大厅分线列表与在线人数；
    - 未知指令：下发友好帮助提示；
  - 指令应答采用系统提示级别下发，仅发送者自身可见，不打扰其他玩家；
* **普通聊天富文本多播（0x0F Chat Message Clientbound）**：
  - 富文本 JSON 转义防御：对消息内容进行 JSON 字符转义（防御引号、换行符破坏 JSON 语法）；
  - 格式化为标准富文本 JSON 树（`{"text": "<玩家名> 消息内容", "color": "white"}`）；
  - 设置 Position 为聊天栏（`position = 0`），使用共享 Buffer 多播给同房间全体在线成员（包含发送者自身）。

---

## 五、 工业级避坑指南与高频面试核心考点

### 5.1 避坑指南（踩坑血泪经验）
* ⚠️ **Angle 强转的越界与符号错误**：
  * 计算 Yaw 的 Angle 字节时：`(uint8_t)((yaw / 360.0f) * 256.0f)`。
  * 若误用带符号 `int8_t` 且未做正数归一化，当角度为负数时，强转为无符号网络包可能导致朝向向后翻转 $180^\circ$。
* ⚠️ **`Spawn Player` 里的 EID 必须为正整型且全局自增**：
  * Minecraft 客户端底层用 EID 作为哈希表的 Key。如果 EID 为 0、负数，或者与自己的 EID 冲突，客户端会立即出现视角抖动甚至丢弃渲染。
* ⚠️ **玩家断开时必须发送 `0x32 Destroy Entities`**：
  * 玩家下线不仅要从服务端数据结构移除，**必须向其他在线玩家下发 `0x32 Destroy Entities`**。
  * 若漏发该包，其他玩家屏幕上会永远残留一个不会动、无法交互的“幽灵替身（Ghost Model）”！

### 5.2 高频面试核心考点与标准作答
* **Q1：为什么不在网络线程加锁直接更新游戏世界？**
  * **作答**：加锁会导致细粒度锁竞争剧烈，破坏 Asio Reactor 线程的高并发吞吐，甚至产生难以排查的死锁。采用 Actor 单写入者模型，通过 MPSC 双缓冲队列批量投递，彻底消除了业务主线程的锁开销，保证了世界模拟的确定性。
* **Q2：相对位移同步为什么限定在 8.0 格以内？**
  * **作答**：1.12.2 协议为了极致节省带宽，将相对位移量压缩为 16 位 Short 定点数（单位 $\frac{1}{4096}$ 方块）。Short 的最大值 32767 除以 4096 约为 7.999 格。当单次位移超限时，强行填入会导致有符号整数溢出翻转，造成客户端严重瞬移错位，因此必须动态降级下发绝对坐标包 `Entity Teleport (0x4C)`。
* **Q3：高并发多播广播如何优化内存开销？**
  * **作答**：当大厅有 500 人时，直接为每人序列化一份数据包会引发 500 次堆分配。我们利用支持 COW（写时复制）的 `ByteBuffer` 与 `std::shared_ptr`，在主线程仅序列化一次，向 500 个会话发送管道推入共享指针，底层通过浅拷贝共享内存，吞吐量提升显著。
* **Q4：Minecraft 服务端架构中，为什么没有通用的 `game/` 层，而是以 `world/` 作为顶层领域？（Cuberite 工业级沙盒架构考点）**
  * **作答**：在工业级体素沙盒引擎（如 Cuberite 与 Notchian 原版）中，`World` 本身就是领域驱动设计（DDD）的**核心聚合根（Aggregate Root）**。Minecraft 的所有核心机制（20 TPS 物理节拍、Chunk 地形管理与流式加载、实体生命周期、红石方块状态机）都是严格围绕“单世界物理空间”展开运作的。设立平行的 `game/` 层不仅职责模糊（违背单一职责 SRP），还会导致双头管理与循环依赖。将物理主循环 `WorldTicker` 与实体管理器 `EntityManager` 归位至 `world/` 层，使架构四层边界清澈纯粹。
* **Q5：为什么坚决拒绝原始类型偏执（Primitive Obsession），将坐标封装为 `SpatialTypes.h` 并提升至 `common/` 层？**
  * **作答**：在移动与空间计算中，若裸写 `double x, y, z; float yaw, pitch`，会带来三大严重弊端：① 函数参数列表膨胀且极易传错顺序（如 yaw/pitch 颠倒）；② 无法附带向量加减乘、距离计算、定点数转码等空间数学语义；③ 网络层解包与世界层存储若各自定义独立的 Vector，会导致无谓的中间转换冗余。将 `Vector3d`、`Rotation`、`BlockPos`、`ChunkPos` 作为基础空间原语固化在 `src/common/SpatialTypes.h`，能让底层基础库、协议层与世界领域层零摩擦复用，保障类型安全与心智统一。

---

## 六、 验收门禁与端到端联调规范 (DoD)

在宣布 Sprint 3 完成前，必须通过以下全部验收条件：

1. **编译门禁**：GCC 14 `-Wall -Wextra -Wpedantic` 零告警通过编译；
2. **单元测试**：`ctest` 自动化测试通过（覆盖 `EventQueue` 交换测试、定点数转换测试、Angle 量化测试）；
3. **真实多端实机联调**：
   - 启动两个 Minecraft 1.12.2 官方客户端 A（`Steve`）和 B（`Alex`）同时连入服务器；
   - A 在草方块上奔跑、起跳、转头，B 屏幕上的 A 实体动作平滑连贯，转头视角实时同步；
   - A 在聊天框输入 `hello from Steve`，B 的聊天栏实时弹出白色文字 `<Steve> hello from Steve`；
   - A 输入 `/maps`，A 屏幕显示系统绿色提示，B 屏幕不受任何影响；
   - 关闭客户端 A，客户端 B 屏幕上的 A 实体应在当前帧立即消失，且 Tab 列表中 A 的名字被移除。
