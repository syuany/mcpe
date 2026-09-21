# Sprint 4: 架构解耦与多玩家同步（Concurrency & AOI）

> **迭代目标**：彻底解耦网络 I/O 与业务计算，设计线程安全的 MPSC 任务管道；实现精准消除漂移的 20 TPS 节拍器；引入九宫格 AOI 空间哈希索引，支持多玩家同屏移动与方块破坏同步。  
> **预估工期**：5 ~ 6 天  
> **总任务数**：7 个微任务

---

## 任务执行看板

- [ ] [TASK-4.1: 跨线程 MPSC 任务队列](#task-41-跨线程-mpsc-任务队列)
- [ ] [TASK-4.2: 精准 20 TPS 主循环与漂移消除](#task-42-精准-20-tps-主循环与漂移消除)
- [ ] [TASK-4.3: 空间哈希与九宫格 AOI 管理器](#task-43-空间哈希与九宫格-aoi-管理器)
- [ ] [TASK-4.4: 玩家进出视野广播](#task-44-玩家进出视野广播)
- [ ] [TASK-4.5: 客户端移动包解析与相对位移广播](#task-45-客户端移动包解析与相对位移广播)
- [ ] [TASK-4.6: 方块挖掘与放置交互同步](#task-46-方块挖掘与放置交互同步)
- [ ] [TASK-4.7: Sprint 4 集成验收与多玩家同服漫游](#task-47-sprint-4-集成验收与多玩家同服漫游)

---

### TASK-4.1: 跨线程 MPSC 任务队列（I/O 与逻辑解耦）
* **任务类型**：并发核心 (Core)
* **预估耗时**：3 小时
* **任务设计指南**：
  * 定义任务事件：`struct NetworkEvent { uint64_t session_id; int32_t packet_id; ByteBuffer payload; };`。
  * 实现线程安全的双缓冲队列（Double Buffering）或无锁 MPSC：
    * 网络线程池调用 `push(event)`，只需极小锁开销推入写队列。
    * 逻辑主线程每个 Tick 开始时调用 `swap_and_pop_all()`，交换读写指针并清空写队列，实现无竞争安全消费。
* **DoD（完成定义）**：
  * 编写多线程测试：4 个生产者线程高频写入，1 个消费者线程读取，开启 `-fsanitize=thread` 编译测试，零数据竞态警告。
* **推荐提交**：`feat(concurrency): implement thread-safe MPSC event queue`

---

### TASK-4.2: 精准 20 TPS（50ms）主循环与漂移消除
* **任务类型**：并发核心 (Core)
* **预估耗时**：2 小时
* **任务设计指南**：
  * 独立启动一个专用的 World 主逻辑线程。
  * 避免累积时钟漂移算法：
    * 维护目标绝对时间点：`auto next_tick = std::chrono::steady_clock::now();`
    * 循环体：
      1. 执行一轮 Tick（消费网络事件、更新物理、生成广播）。
      2. `next_tick += std::chrono::milliseconds(50);`
      3. `std::this_thread::sleep_until(next_tick);`
      4. 若当前时间已经超过 `next_tick`，触发追帧机制或记录抖动报警。
* **DoD（完成定义）**：
  * 运行 1000 次 Tick，统计耗时，平均单 Tick 周期为 $50.0\text{ms} \pm 0.5\text{ms}$。
* **推荐提交**：`feat(world): implement 20 TPS tick loop with drift compensation`

---

### TASK-4.3: 空间哈希与九宫格 AOI 管理器
* **任务类型**：算法设计 (Algorithm)
* **预估耗时**：3 小时
* **任务设计指南**：
  * 维护网格映射：`std::unordered_map<ChunkPos, std::unordered_set<uint64_t>> chunk_players_;`
  * 提供九宫格查询接口：输入 `ChunkPos`，返回该区块及相邻 8 个区块内的所有玩家集合。
  * 玩家跨区块移动（从 `Chunk A` 到 `Chunk B`）：
    * 计算 $Set_A$ 与 $Set_B$ 的九宫格集合。
    * 新增观察者：$Set_B - Set_A$。
    * 移除观察者：$Set_A - Set_B$。
* **DoD（完成定义）**：
  * 编写单测模拟玩家跨越边界，验证差集计算无遗漏。
* **推荐提交**：`feat(world): implement 3x3 grid AOI spatial manager`

---

### TASK-4.4: 玩家进出视野广播（Player List Item / Spawn / Destroy Entity）
* **任务类型**：世界协议 (Business)
* **预估耗时**：3 小时
* **任务设计指南**：
  * 当玩家 A 成为玩家 B 的新观察者时，**必须严格按此顺序**下发两个包：
    * **先**向 B 下发 `Player List Item (0x2E, Action=0, add player)`：`Action: VarInt` + `Number Of Players: VarInt` + `UUID: UUID`（16 字节） + `Name: String(16)` + `Number Of Properties: VarInt`（填 `0`） + `Gamemode: VarInt` + `Ping: VarInt` + `Has Display Name: Boolean`。
      * ⚠️ 漏发（或顺序颠倒）时 Notchian 客户端**不报错、只是"看不见人"**，必须用日志断言包顺序。
    * **再**向 B 下发 `Spawn Player (0x05)`：`Entity ID: VarInt` + `Player UUID`（**16 字节原始 UUID**，不是 36 字符串） + `X/Y/Z: Double` ×3 + `Yaw/Pitch: Angle` + `Entity Metadata`（至少写终结符 `0xFF`）。
  * 当玩家 A 离开玩家 B 的视野范围时：
    * 向 B 下发 `Destroy Entities (0x32)`：`Count: VarInt` + `Entity IDs: VarInt[]`（携带 A 的 EntityID 数组）。
    * 必要时同时下发 `Player List Item (0x2E, Action=4)` 清理 Tab 列表。
* **DoD（完成定义）**：
  * 玩家进出视野时，实体模型在对端屏幕上正常出现和消失，无幽灵实体残留。
* **推荐提交**：`feat(world): broadcast Spawn Player and Destroy Entity packets`

---

### TASK-4.5: 客户端移动包解析与相对位移广播
* **任务类型**：世界协议 (Business)
* **预估耗时**：3 小时
* **任务设计指南**：
  * 消费客户端发来的四个移动包：`Player (0x0C)`（仅 `On Ground: Boolean`）、`Player Position (0x0D)`（`X/Y/Z: Double` + `On Ground`）、`Player Position And Look (0x0E)`（`X/Y/Z: Double` + `Yaw/Pitch: Float` + `On Ground`）、`Player Look (0x0F)`（`Yaw/Pitch: Float` + `On Ground`）。
    * 客户端**静止时每 20 tick 也会发一次 `Player Position`**，可据此实现"久未收到移动包即认为掉线"的移动看门狗。
  * 计算该玩家相对于上一位置的位移向量 $(\Delta X, \Delta Y, \Delta Z)$：
    * 转换为定点数（`Short`，单位 $1/4096$ 方块）：$\text{delta} = (\text{int16})((\text{current} - \text{previous}) \times 4096)$。
    * 若单轴位移绝对值 $< 8.0$ 方块（定点数上限 $32768/4096 = 8$ 格）：向周围观察者广播 `Entity Relative Move (0x26)` 或 `Entity Look And Relative Move (0x27)`。
    * 若位移 $\ge 8.0$ 方块（瞬移）：降级广播绝对坐标包 `Entity Teleport (0x4C)`。
  * 注意：`Entity Look And Relative Move (0x27)` 的 `Yaw/Pitch` 是 `Angle`（1 字节）且为**绝对角度，不是增量**。
* **DoD（完成定义）**：
  * 两人同屏追逐跑动，人物移动平滑连贯，视角旋转同步。
* **避坑指南**：
  * ⚠️ **定点数溢出**：一旦单轴位移超过 8 方块，`Entity Relative Move` 的 `Short` 会溢出翻转，导致人物瞬间倒退跳跃，必须严格有条件降级为 `Entity Teleport (0x4C)`。
* **推荐提交**：`feat(world): handle player movement and broadcast relative position`

---

### TASK-4.6: 方块挖掘与放置交互同步
* **任务类型**：世界协议 (Business)
* **预估耗时**：3 小时
* **任务设计指南**：
  * 处理客户端发来的 `Player Digging (0x14)`：`Status: VarInt`（`2` 为挖穿；`0/1` 为开始/取消）、`Location: Position`（64 位位域打包坐标）、`Face: Byte`。
    * ⚠️ `Status ∈ {3,4,5,6}`（丢物品/交换副手）时 `Location` 恒为 `0/0/0`，**不能当方块坐标处理**。
  * 处理 `Player Block Placement (0x1F)`（放置新方块）：`Location: Position` + `Face: VarInt` + `Hand: VarInt` + 面内光标 `Float` ×3；真实落点 = `Location` 沿 `Face` 法向偏移一格。
  * 逻辑主线程校验修改 `World` 内存中对应的方块 ID。
  * 构造 `Block Change (0x0B)` 数据包（`Location: Position` + `Block ID: VarInt` 全局调色板 state id `(blockId << 4) | meta`），向该方块所在区块九宫格内的所有玩家广播。
  * 实现 `Position` 64 位位域编解码工具：`((x & 0x3FFFFFF) << 38) | ((y & 0xFFF) << 26) | (z & 0x3FFFFFF)`，C++ 中负数需显式符号扩展。
  * 创造模式需响应 `Creative Inventory Action (0x1B)` 并回 `Set Slot (0x16)`，否则玩家空手无法放置方块。
* **DoD（完成定义）**：
  * 玩家 A 破坏一块地面，玩家 B 的屏幕上该方块立即同步消失变为空洞。
* **推荐提交**：`feat(world): synchronize block digging and placement via Block Change`

---

### TASK-4.7: Sprint 4 集成验收与多玩家同服漫游
* **任务类型**：集成验证 (Integration)
* **预估耗时**：1 小时
* **任务目标**：
  * 启动两个 **1.12.2** 客户端，分别登录两个不同昵称的玩家。
  * 两人在平原上碰面，互相观察跑步、跳跃、转头、破坏与搭建方块。
* **DoD（完成定义）**：
  * 开启 `-fsanitize=thread` 编译运行，交互 10 分钟，零数据竞态警告，画面同步流畅。
* **推荐提交**：`chore: verify and pass Sprint 4 Multiplayer Synchronization milestone`

---

## Sprint 4 交付物验收检查单（Review Checklist）

- [ ] 网络 I/O 线程与 World 逻辑计算彻底解耦，无跨线程直接读写。
- [ ] 20 TPS 节拍器保持无累积时间漂移。
- [ ] 九宫格 AOI 将广播复杂度控制在 $O(N \cdot K)$。
- [ ] 相对位移单轴上限 8 格（`Short` 定点数，$1/4096$ 方块），溢出时成功降级为 `Entity Teleport (0x4C)`。
- [ ] 玩家进出视野时包顺序正确：`Player List Item (0x2E, Action=0)` 先于 `Spawn Player (0x05)`，离开时下发 `Destroy Entities (0x32)` 并可用 `Action=4` 清理 Tab 列表。
- [ ] 创造模式物品回填正常（`Creative Inventory Action (0x1B)` → `Set Slot (0x16)`），玩家空手时也能放置方块。
- [ ] 准备进入 [Sprint 5: 压测调优与 Profiling](file:///home/ubu/dev/my/mcpe/doc/sprints/06_sprint5_perf_tuning.md)。
