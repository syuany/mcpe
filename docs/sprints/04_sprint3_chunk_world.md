# Sprint 3: 区块模型与超平坦地形（Chunk & World Sync）

> **迭代目标**：设计世界与区块紧凑内存布局，掌握空间位运算寻址，实现 **1.12.2 区块位压缩序列化（调色板 + Long 数组）**，批量分发出生点视距区块，让玩家脚下拥有实实在在的方块地面，能够在草地上正常跑跳。  
> **预估工期**：4 ~ 5 天  
> **总任务数**：6 个微任务

---

## 任务执行看板

- [ ] [TASK-3.1: ChunkSection 内存结构与位运算寻址](#task-31-chunksection-内存结构与位运算寻址)
- [ ] [TASK-3.2: 简易超平坦生成器](#task-32-简易超平坦生成器)
- [ ] [TASK-3.3: 空间数学与坐标系转换工具实现](#task-33-空间数学与坐标系转换工具实现)
- [ ] [TASK-3.4: Chunk Data (0x20) 协议二进制序列化](#task-34-chunk-data-0x20-协议二进制序列化)
- [ ] [TASK-3.5: 出生点视距区块批量分发](#task-35-出生点视距区块批量分发)
- [ ] [TASK-3.6: Sprint 3 集成验收与地面行走测试](#task-36-sprint-3-集成验收与地面行走测试)

---

### TASK-3.1: `ChunkSection` 内存结构与位运算寻址
* **任务类型**：世界核心 (Core)
* **预估耗时**：2.5 小时
* **任务设计指南**：
  * 一个 ChunkSection 覆盖 $16 \times 16 \times 16 = 4096$ 个方块。
  * 采用连续一维数组存储：`uint16_t blocks_[4096];`，**存全局调色板 state id** `(blockId << 4) | meta`（1.12.2 中占 13 位，`uint16_t` 足够容纳）。
  * 建议提供 `to_global_palette_state(block_id, meta)` 换算函数，避免在业务代码里散落硬编码魔数。
  * 局部坐标寻址公式：
    `size_t index = (y << 8) | (z << 4) | x;`，其中 $x, y, z \in [0, 15]$（该公式 1.12.2 与 1.8 完全相同，但**必须与序列化时的 `blockNumber` 计算保持一致**）。
* **DoD（完成定义）**：
  * 实现 `set_block(x, y, z, id, meta)` 与 `get_block(x, y, z)`。
  * 编写单测验证局部极值坐标 $(0,0,0)$、$(15,15,15)$ 寻址计算正确，无数组越界。
* **推荐提交**：`feat(world): implement ChunkSection memory layout and bit indexing`

---

### TASK-3.2: 简易超平坦生成器（Flat World Generator）
* **任务类型**：世界业务 (Business)
* **预估耗时**：1.5 小时
* **任务设计指南**：
  * 一个完整垂直柱状 Chunk 包含 16 个 ChunkSection（$Y=0 \sim 255$）。
  * 仅实例化底部的 `ChunkSection 0`：
    * $Y=0$：基岩（Bedrock，state id `(7<<4)|0 = 112`）
    * $Y=1 \sim 2$：泥土（Dirt，state id `48`）
    * $Y=3$：草方块（Grass Block，state id `32`）
    * $Y \ge 4$：空气（Air，state id `0`）
  * 其余 15 个 Section 保持空指针状态，节约内存。
* **DoD（完成定义）**：
  * 实例化生成的区块在 $Y=3$ 为草方块，上方为空。
* **推荐提交**：`feat(world): add superflat chunk generator logic`

---

### TASK-3.3: 空间数学与坐标系转换工具实现
* **任务类型**：基础工具 (Unit)
* **预估耗时**：1.5 小时
* **任务设计指南**：
  * 实现世界绝对坐标 $(X, Z)$ 到区块坐标 $(ChunkX, ChunkZ)$ 及局部坐标 $(LocalX, LocalZ)$ 的数学转换工具。
  * **必须解决负数取整问题**：
    * 错误做法：`int chunk_x = x / 16;`（在 C++ 中 `-1 / 16 == 0`，导致负数区块错位！）。
    * 正确做法：使用算术右移 `int chunk_x = x >> 4;`（在二进制补码下 `-1 >> 4 == -1`，天然符合向下取整 Floor 定义）。
    * 局部坐标提取：`int local_x = x & 15;`。
* **DoD（完成定义）**：
  * 编写单测覆盖：$X=15 \rightarrow Chunk=0, Local=15$；$X=16 \rightarrow Chunk=1, Local=0$；$X=-1 \rightarrow Chunk=-1, Local=15$。断言全部通过。
* **推荐提交**：`feat(world): implement coordinate math with negative floor handling`

---

### TASK-3.4: `Chunk Data (0x20)` 协议二进制序列化
* **任务类型**：协议核心 (Core)
* **预估耗时**：4 小时
* **任务设计指南**：
  * 组装 1.12.2（protocol 340）的区块二进制序列化数据流：
    1. `Chunk X`: `Int`（4 字节）
    2. `Chunk Z`: `Int`（4 字节）
    3. `Ground-Up Continuous`: `Boolean`（新加载区块填 `true`）
    4. `Primary Bit Mask`: `VarInt`（第 0 位为 1 表示 Section 0 存在；只填 Section 0 时为 `0x01`）
    5. `Size`: `VarInt`（后续 `Data` 字节流总长度）
    6. `Data`: `Byte Array`，内容按以下顺序拼接：
       * 每个激活 Section 自底向上依次写入：
         * `Bits Per Block`: `uint8`（固定填 `13`，直接使用全局调色板）
         * `Dummy Palette Length`: `VarInt`（直接模式下恒为 `0`）
         * `Data Array Length`: `VarInt`（13 位时恒为 `832`）
         * `Data Array`: `Long[832]`（**大端 8 字节/个**；每个方块 13 位、低位在前、**允许跨 long**）
         * `Block Light`: `Byte[2048]`（半字节/方块，全填 `0x00`）
         * `Sky Light`: `Byte[2048]`（全填 `0xFF` 保持全亮；**仅主世界发送**）
       * 若 `Ground-Up Continuous == true`，在整列末尾追加 256 字节生物群系（Biome）：索引 `z * 16 | x`，全填 `0x01` 代表平原。
    7. `Number of block entities`: `VarInt`（不支持方块实体时填 `0`）。
  * 跨 long 位压缩打包伪代码：
    `startLong = (idx*13)/64; startOff = (idx*13)%64; endLong = ((idx+1)*13-1)/64; data[startLong] |= v << startOff; if (startLong != endLong) data[endLong] |= v >> (64 - startOff);`
* **DoD（完成定义）**：
  * 数据字节流长度精确等于预期值（魔数自检）：单 Section + 主世界 + ground-up 时 `Size == 1 + 1 + 2 + 832×8 + 2048 + 2048 + 256 == 11012`。
  * 位压缩算法必须有"打包 → 解包往返一致"的单测，覆盖**跨 long 边界**的条目（如 index 4、9、14 …）。
* **避坑指南**：
  * ⚠️ **客户端崩溃陷阱**：光照数组字节数必须与 `Primary Bit Mask` 中为 1 的位数量严格匹配。若掩码声明了多个 Section 但光照数据不足，客户端会直接抛出 `ArrayIndexOutOfBoundsException` 闪退。
  * ⚠️ 1.12.2 **不再有"每方块 2 字节小端"**：方块数据是调色板 + Long 位压缩数组，照抄 1.8 的定长写法会直接导致客户端闪退；Long 数组同样按**大端**写入。
  * ⚠️ **"不跨界 + 填充"是 1.16 才引入的规则**：1.12.2 必须支持条目跨 long（13 位 × 4096 恰好整除 64，故 `Data Array Length` 恒为 832，无需填充）。
  * ⚠️ `Size` 必须与 `Data` 的实际字节数严格相等，且 `Primary Bit Mask` 中 1 的个数必须与 `Data` 里 Section 的个数一致。
* **推荐提交**：`feat(protocol): implement Chunk Data packet serialization`

---

### TASK-3.5: 出生点视距区块批量分发
* **任务类型**：世界业务 (Business)
* **预估耗时**：2 小时
* **任务设计指南**：
  * 在玩家进服发送初始坐标后，以出生区块 $(0, 0)$ 为中心，生成半径为 2 的区块网格（$X \in [-2, 2], Z \in [-2, 2]$，共 25 个区块）。
  * 依次将这 25 个区块序列化为 `Chunk Data (0x20)`，推入 Session 的发送队列下发。
  * 覆盖已加载区块前必须先下发 `Unload Chunk (0x1D)`（`Chunk X: Int` + `Chunk Z: Int`），否则客户端 chunk 缓存泄漏（F3 的 MultiplayerChunkCache 两侧数值会不一致）。
* **DoD（完成定义）**：
  * 25 个区块平滑发送完毕，无网络阻塞或内存暴涨。
  * 重复进入/重发区块时，客户端 F3 的 MultiplayerChunkCache 数值与已下发区块数一致。
* **推荐提交**：`feat(world): send initial chunk grid around spawn point`

---

### TASK-3.6: Sprint 3 集成验收与地面行走测试
* **任务类型**：集成验证 (Integration)
* **预估耗时**：1 小时
* **任务目标**：
  * 进服并将玩家设置为生存模式（Gamemode 0）。
* **DoD（完成定义）**：
  * 玩家站在草方块上，物理重力正常，不会掉下虚空。
  * 玩家可以跑步、起跳、落回地面。
  * 按 F3 键观察当前所处的方块坐标与区块坐标，移动时数值实时更新，无客户端报错。
* **推荐提交**：`chore: verify and pass Sprint 3 Chunk & World Sync milestone`

---

## Sprint 3 交付物验收检查单（Review Checklist）

- [ ] `ChunkSection` 采用紧凑连续内存存储（全局调色板 state id）与位移寻址。
- [ ] 算术右移正确解决了负数坐标向下取整问题。
- [ ] `Chunk Data (0x20)` 的 `Size` 与 `Data` 实际字节数精确匹配（单 section 主世界场景为 11012）。
- [ ] `Bits Per Block` 填 `13` 且 `Dummy Palette Length` 为 `0`（直接全局调色板），Long 数组按大端写入并支持跨 long 打包。
- [ ] 覆盖已加载区块前先下发 `Unload Chunk (0x1D)`，客户端无 chunk 缓存泄漏。
- [ ] 主世界区块含 256 字节生物群系（`Ground-Up Continuous == true` 时），非主世界不发送 Sky Light。
- [ ] 玩家成功站在草地上，世界基础物理生效。
- [ ] 准备进入 [Sprint 4: 架构解耦与多玩家同步](file:///home/ubu/dev/my/mcpe/doc/sprints/05_sprint4_concurrency_aoi.md)。
