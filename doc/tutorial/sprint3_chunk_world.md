# Sprint 3 保姆级实战开发教程：区块内存模型与超平坦地形下发（Chunk & World Sync）

> **目标协议**：Minecraft Java Edition 1.12.2（Protocol 340）  
> **前置里程碑**：Sprint 1（网络基座与 Server List Ping）、Sprint 2（登录鉴权与虚空漫游）及全链路四层解耦重构（Netty COW 切片、PacketWriter RAII、ClientManager）已全部验收通过。  
> **本阶段核心目标**：从“虚空漂浮”跃迁至“脚踏实地”！构建世界领域层（Domain Layer）内存模型，实现 1.12.2 协议专有的 13 位跨 Long 直接调色板位压缩算法，完成超平坦世界生成器与 0x20 区块数据包序列化，向进服玩家成片分发出生点网格，让玩家稳固着陆在阳光明媚的草方块大地上！  
> **预估工期**：4 ~ 5 天 | **总任务数**：6 个核心微任务

---

## 目录

- [一、 心智模型跃迁：从“网络流”到“三维空间几何”](#一-心智模型跃迁从网络流到三维空间几何)
- [二、 严格四层架构拓扑与新建文件蓝图](#二-严格四层架构拓扑与新建文件蓝图)
- [三、 空间数学与二进制底层原理推演（数学硬核）](#三-空间数学与二进制底层原理推演数学硬核)
  - [3.1 坐标系映射与负数算术右移陷阱](#31-坐标系映射与负数算术右移陷阱)
  - [3.2 空间局部性寻址公式设计](#32-空间局部性寻址公式设计)
  - [3.3 1.12.2 全局调色板与 13 位跨 Long 位压缩算法](#33-1122-全局调色板与-13-位跨-long-位压缩算法)
  - [3.4 光照排布与单 Section 黄金字节数魔数（10756）](#34-光照排布与单-section-黄金字节数魔数10756)
- [四、 任务逐步实现指南（代码与接口契约）](#四-任务逐步实现指南代码与接口契约)
  - [TASK-3.1: 空间数学与坐标系工具 (`WorldTypes.h`)](#task-31-空间数学与坐标系工具-worldtypesh)
  - [TASK-3.2: `ChunkSection` 连续内存模型与寻址 (`ChunkSection.h/.cc`)](#task-32-chunksection-连续内存模型与寻址-chunksectionhcc)
  - [TASK-3.3: `Chunk` 垂直柱状容器与超平坦生成器 (`Chunk.h/.cc`, `FlatWorldGenerator.h/.cc`)](#task-33-chunk-垂直柱状容器与超平坦生成器-chunkhcc-flatworldgeneratorhcc)
  - [TASK-3.4: 协议层 `Chunk Data (0x20)` 二进制序列化 (`ChunkSerializer.h/.cc`)](#task-34-协议层-chunk-data-0x20-二进制序列化-chunkserializerhcc)
  - [TASK-3.5: 出生点视距网格下发与玩家物理着陆 (`PlayHandler.cc`)](#task-35-出生点视距网格下发与玩家物理着陆-playhandlercc)
  - [TASK-3.6: 单元测试覆盖与自动化验收脚本 (`test_chunk.cc`, `test_chunk.py`)](#task-36-单元测试覆盖与自动化验收脚本-test_chunkcc-test_chunkpy)
- [五、 工业级避坑指南与高频面试核心考点](#五-工业级避坑指南与高频面试核心考点)
- [六、 工业标杆（Cuberite）深度架构源码对照](#六-工业标杆cuberite深度架构源码对照)

---

## 一、 心智模型跃迁：从“网络流”到“三维空间几何”

在 Sprint 1 与 Sprint 2 中，我们关注的焦点是 **字节流生命周期** 与 **会话状态机**（握手、分帧、加解密、心跳）。客户端虽然能够进服，但屏幕上是一片荒芜深邃的虚空，玩家稍有不慎就会掉入 $Y < 0$ 的深渊不断掉血。

**Sprint 3 标志着项目正式踏入 MMORPG / 游戏引擎的核心腹地——空间计算与世界数据结构。**
* 现实世界是连续的三维欧氏空间；
* Minecraft 的世界被离散化为三层网格系统：
  1. **方块（Block）**：最小离散单元，坐标 $(X, Y, Z)$；
  2. **区块子段（ChunkSection）**：$16 \times 16 \times 16 = 4096$ 个方块的立方体，内存连续排布与网络传输的基本单元；
  3. **区块列（Chunk Column）**：垂直方向由 16 个 ChunkSection 垂直堆叠而成（$Y=0 \sim 255$），覆盖 $16 \times 256 \times 16$ 区域。

```
       Y=256 ┌───────────────────────────┐
             │ ChunkSection 15 (16x16x16)│
             ├───────────────────────────┤
             │           ......          │
             ├───────────────────────────┤
             │ ChunkSection 1  (16x16x16)│
             ├───────────────────────────┤
        Y=0  │ ChunkSection 0  (16x16x16)│ ──► [基岩(Y=0) / 泥土(Y=1..2) / 草方块(Y=3)]
             └───────────────────────────┘
                16 x 16 (X, Z 轴网格)
```

---

## 二、 严格四层架构拓扑与新建文件蓝图

依据本项目确立的不可逾越的 [MCPE 工业级架构规范](../../AGENTS.md)，本 Sprint 新增的领域逻辑必须严格收敛在 **第四层（Domain Layer `src/world/`）**，网络与协议层只负责调度与序列化，绝不可让网络套接字直接渗透进世界模型！

```
mcpe/
├── CMakeLists.txt                                  <-- 注册新增的 world 目标与单元测试
├── src/
│   ├── common/                                     <-- 【第一层: 基础工具】ByteBuffer, VarInt
│   ├── network/                                    <-- 【第二层: 网络传输】TcpSession, PacketFraming, PacketWriter
│   ├── protocol/                                   <-- 【第三层: 协议与会话】
│   │   ├── PlayHandler.h / .cc                     <-- 【修改】在玩家进服时下发 5x5 区块网格
│   │   └── ChunkSerializer.h / .cc                 <-- 【新建】专职将 Chunk 内存序列化为 0x20 二进制流
│   └── world/                                      <-- 【第四层: 纯游戏领域层 (新建目录)】
│       ├── WorldTypes.h                            <-- 【新建】空间坐标原语 (BlockPos, ChunkPos) 与数学位运算
│       ├── ChunkSection.h / .cc                    <-- 【新建】16x16x16 单段立方体连续内存与寻址
│       ├── Chunk.h / .cc                           <-- 【新建】16 个 Section 柱状聚合与生物群系存储
│       └── FlatWorldGenerator.h / .cc              <-- 【新建】超平坦地形生成器业务逻辑
├── tests/
│   └── test_chunk.cc                               <-- 【新建】坐标数学、Section 寻址与位压缩编解码往返单测
└── scripts/
    └── test_chunk.py                               <-- 【新建】Python 自动化抓包验证 0x20 Chunk Data 报文
```

---

## 三、 空间数学与二进制底层原理推演（数学硬核）

### 3.1 坐标系映射与负数算术右移陷阱

一个世界绝对坐标 $(X, Y, Z)$ 落在哪个区块 $(ChunkX, ChunkZ)$ 中？
在数学上，区块坐标是世界坐标除以 16 并**向下取整（Floor）**：
$$\text{ChunkX} = \lfloor X / 16 \rfloor$$

> ⚠️ **新手灾难性陷阱**：
> C/C++ 语言标准规定，有符号整数的除法 `/` 采用 **Truncate toward zero（向零截断）**！
> * $15 / 16 = 0$（正确，属于区块 0）
> * $-1 / 16 = 0$（**错误！** 数学上 $\lfloor -1 / 16 \rfloor = -1$，但在 C++ 中算成了 0！这会导致负数坐标的玩家看到的区块错位整整 16 格！）

**工业级最优解法**：在二进制补码表示下，使用**算术右移（Arithmetic Right Shift）**：
```cpp
constexpr int32_t toChunkCoord(int32_t val) noexcept {
    return val >> 4; // 计算机硬件级天然而正确的向下取整！例如 -1 >> 4 == -1
}
constexpr int32_t toLocalCoord(int32_t val) noexcept {
    return val & 0x0F; // 等价于 val mod 16，提取 0~15 的局部坐标
}
```

### 3.2 空间局部性寻址公式设计

每个 `ChunkSection` 拥有 $16 \times 16 \times 16 = 4096$ 个方块。如何将局部三维坐标 $(x, y, z)$（其中 $x, y, z \in [0, 15]$）映射为一维数组索引？
Minecraft 官方协议规定其存储寻址公式为：
$$\text{Index} = (y \ll 8) \mid (z \ll 4) \mid x$$

**硬件性能原理（Cache-Friendly）**：
该排布保证了 $X$ 轴相邻方块在物理内存中也是连续紧邻的。在光线追踪、网格渲染或物理碰撞检测遍历一行方块时，CPU L1 Cache Line（64 字节）可以一次性载入 32 个相邻方块状态，缓存命中率接近 100%。

### 3.3 1.12.2 全局调色板与 13 位跨 Long 位压缩算法

在 Minecraft 1.12.2（Protocol 340）中，方块状态由 **全局调色板 State ID** 表示：
$$\text{StateID} = (\text{BlockID} \ll 4) \mid \text{Metadata}$$
在 1.12.2 中，BlockID 最大为 255，Meta 最大为 15，因此最大状态值为 $(255 \ll 4) \mid 15 = 4095$（实际扩展上限为 8191），占用 **13 个二进制位（Bits）**。

#### 调色板模式判定：
* 当 `BitsPerBlock <= 8` 时使用间接调色板（带有动态映射数组）；
* 当 `BitsPerBlock >= 9` 时，Minecraft 强制进入 **直接调色板模式（Direct Global Palette）**：
  * 固定设置 `BitsPerBlock = 13`；
  * `Dummy Palette Length = 0`（调色板长度直接填 0，无任何映射映射表）；
  * 每个方块的值直接写入 13 位的全局 State ID！

#### 为什么 1.12.2 允许跨 Long 且无需填充？
$$4096 \text{ 方块} \times 13 \text{ 位} = 53248 \text{ 位}$$
$$53248 \div 64 = 832 \text{ 个 } 64 \text{ 位整数 (uint64\_t)}$$
**数字完全整除！** 因此 `Data Array Length` 恒等于 **832**。
注意：条目允许跨越两个 `uint64_t` 的边界（1.16+ 引入的不跨边界与 Padding 填充规则在 1.12.2 中**严禁使用**，否则客户端闪退！）。

#### 位压缩打包核心算法推导：
```cpp
constexpr int kBits = 13;
constexpr int kBlocks = 4096;
constexpr int kLongs = kBlocks * kBits / 64; // 832
uint64_t data[kLongs] = {0};

for (int idx = 0; idx < kBlocks; ++idx) {
    uint16_t state_id = blocks_[idx];
    uint64_t value = static_cast<uint64_t>(state_id) & ((1ULL << kBits) - 1);
    
    int start_long   = (idx * kBits) / 64;
    int start_offset = (idx * kBits) % 64;
    int end_long     = ((idx + 1) * kBits - 1) / 64;
    
    data[start_long] |= (value << start_offset);
    if (start_long != end_long) {
        // 跨越 64 位整数边界，将高位溢出部分塞入下一个 long 的最低位
        data[end_long] |= (value >> (64 - start_offset));
    }
}
```
> ⚠️ **网络字节序**：每个 `uint64_t` 发送到客户端前，必须转换为 **大端序（Big-Endian）** 写入！

### 3.4 光照排布与单 Section 黄金字节数魔数（10756）

每个 Section 紧随方块数据之后的是光照数据：
1. **Block Light（方块光）**：4096 个半字节（4-bit nibble）= 2048 字节。平坦世界未放置火把，全填 `0x00`；
2. **Sky Light（天空光）**：4096 个半字节 = 2048 字节。主世界阳光普照，全填 `0xFF`（即两个方块的天空光均为 15 最亮值）；
   * 注意：天空光**仅在主世界维度下发**，若在下界（Nether）下发会导致客户端错位闪退！

#### 黄金自检魔数推导（单 Section + 主世界）：
| 字段名称 | 协议类型 | 字节大小 | 说明 |
| :--- | :--- | :--- | :--- |
| **Bits Per Block** | `Unsigned Byte` | 1 字节 | 固定填 13 |
| **Dummy Palette Length** | `VarInt` | 1 字节 | 直接模式固定填 0 |
| **Data Array Length** | `VarInt` | 2 字节 | 832 的 VarInt 编码为 `[0xC0, 0x06]` |
| **Data Array** | `Long[832]` | $832 \times 8 = 6656$ 字节 | 大端序位压缩数据 |
| **Block Light** | `Byte[2048]` | 2048 字节 | 全 0x00 |
| **Sky Light** | `Byte[2048]` | 2048 字节 | 全 0xFF |
| **单 Section 小计** | - | **10756 字节** | $(1 + 1 + 2 + 6656 + 2048 + 2048)$ |

若该区块是全新加载（`Ground-Up Continuous == true`），数据末尾必须追加：
* **Biomes（生物群系）**：$16 \times 16 = 256$ 字节（全填 `0x01` 代表 Plains 平原）。
因此，一个包含单段 Section 0 的超平坦区块的总负载数据大小为：
$$\text{Data Size} = 10756 \times 1 + 256 = \mathbf{11012} \text{ 字节}$$
**如果你的序列化器产出的 Payload 大小不等于 11012，100% 存在 Bug，客户端必崩溃！**

---

## 四、 任务逐步实现指南（代码与接口契约）

### TASK-3.1: 空间数学与坐标系工具 (`WorldTypes.h`)
* **任务类型**：世界核心基石
* **位置**：`src/world/WorldTypes.h`
* **接口设计契约**：

```cpp
#ifndef MCPE_WORLD_WORLD_TYPES_H
#define MCPE_WORLD_WORLD_TYPES_H

#include <cstdint>
#include <functional>

namespace mcpe::world {

struct ChunkPos {
  int32_t x;
  int32_t z;

  constexpr bool operator==(const ChunkPos& other) const noexcept {
    return x == other.x && z == other.z;
  }
};

struct BlockPos {
  int32_t x;
  int32_t y;
  int32_t z;

  constexpr ChunkPos toChunkPos() const noexcept {
    return ChunkPos{x >> 4, z >> 4};
  }

  constexpr int32_t localX() const noexcept { return x & 0x0F; }
  constexpr int32_t localY() const noexcept { return y; }
  constexpr int32_t localZ() const noexcept { return z & 0x0F; }
  constexpr int32_t sectionIndex() const noexcept { return y >> 4; }
  constexpr int32_t localSectionY() const noexcept { return y & 0x0F; }
};

/** @brief 空间局部性寻址：(y << 8) | (z << 4) | x */
constexpr size_t sectionBlockIndex(int32_t x, int32_t y, int32_t z) noexcept {
  return (static_cast<size_t>(y & 0x0F) << 8) |
         (static_cast<size_t>(z & 0x0F) << 4) |
         static_cast<size_t>(x & 0x0F);
}

/** @brief 方块状态转换工具：消除裸数字魔数 */
constexpr uint16_t toGlobalState(uint16_t block_id, uint8_t meta = 0) noexcept {
  return static_cast<uint16_t>((block_id << 4) | (meta & 0x0F));
}

namespace BlockStates {
  inline constexpr uint16_t kAir = toGlobalState(0, 0);
  inline constexpr uint16_t kStone = toGlobalState(1, 0);
  inline constexpr uint16_t kGrass = toGlobalState(2, 0);
  inline constexpr uint16_t kDirt = toGlobalState(3, 0);
  inline constexpr uint16_t kBedrock = toGlobalState(7, 0);
}

} // namespace mcpe::world

template <>
struct std::hash<mcpe::world::ChunkPos> {
  size_t operator()(const mcpe::world::ChunkPos& pos) const noexcept {
    return (static_cast<size_t>(pos.x) * 3129871) ^ static_cast<size_t>(pos.z);
  }
};

#endif // MCPE_WORLD_WORLD_TYPES_H
```

---

### TASK-3.2: `ChunkSection` 连续内存模型与寻址 (`ChunkSection.h/.cc`)
* **任务类型**：世界核心容器
* **位置**：`src/world/ChunkSection.h`, `src/world/ChunkSection.cc`
* **设计指南**：
  * 单个 Section 严格使用 `uint16_t blocks_[4096]` 存储全局调色板 State ID；
  * 提供 `setBlock` 与 `getBlock`，内部通过 `sectionBlockIndex` 寻址；
  * 实现 `packToBits13(std::span<uint64_t, 832> out)` 函数，专职执行跨 Long 位压缩打包；
  * 统计非空气方块数量（`non_air_count_`），用于后续空 Section 自动剪枝优化。

```cpp
// src/world/ChunkSection.h
#ifndef MCPE_WORLD_CHUNK_SECTION_H
#define MCPE_WORLD_CHUNK_SECTION_H

#include <array>
#include <cstdint>
#include <span>

#include "world/WorldTypes.h"

namespace mcpe::world {

class ChunkSection {
 public:
  static constexpr size_t kBlockCount = 4096;
  static constexpr size_t kBitsPerBlock = 13;
  static constexpr size_t kDataLongs = (kBlockCount * kBitsPerBlock) / 64; // 832

  ChunkSection();

  void setBlock(int32_t x, int32_t y, int32_t z, uint16_t state_id);
  [[nodiscard]] uint16_t getBlock(int32_t x, int32_t y, int32_t z) const;

  /** @brief 将方块数据压缩为 1.12.2 的 832 个 64 位大端整数 */
  void packToBits13(std::span<uint64_t, kDataLongs> out_longs) const;

  [[nodiscard]] size_t nonAirCount() const noexcept { return non_air_count_; }
  [[nodiscard]] bool isEmpty() const noexcept { return non_air_count_ == 0; }

 private:
  std::array<uint16_t, kBlockCount> blocks_;
  size_t non_air_count_{0};
};

} // namespace mcpe::world

#endif // MCPE_WORLD_CHUNK_SECTION_H
```

---

### TASK-3.3: `Chunk` 垂直柱状容器与超平坦生成器 (`Chunk.h/.cc`, `FlatWorldGenerator.h/.cc`)
* **任务类型**：世界领域业务
* **位置**：`src/world/Chunk.h`, `src/world/FlatWorldGenerator.h`
* **设计指南**：
  * `Chunk` 持有 16 个 `std::unique_ptr<ChunkSection>` 数组；
  * 惰性分配：只有含方块的 Section 才会实例化，其余 15 个保持 `nullptr` 节约服务器内存；
  * `primaryBitMask()` 方法：计算哪些 Section 非空（对于超平坦平原，仅 Section 0 存在，BitMask 为 `0x0001`）；
  * `FlatWorldGenerator::generate(Chunk& chunk)` 填充规则：
    * $Y=0$：基岩 (`BlockStates::kBedrock`)
    * $Y=1 \sim 2$：泥土 (`BlockStates::kDirt`)
    * $Y=3$：草方块 (`BlockStates::kGrass`)
    * $Y \ge 4$：空气 (`BlockStates::kAir`)
  * 生物群系数组初始化为 `0x01`（Plains）。

---

### TASK-3.4: 协议层 `Chunk Data (0x20)` 二进制序列化 (`ChunkSerializer.h/.cc`)
* **任务类型**：协议核心
* **位置**：`src/protocol/ChunkSerializer.h`, `src/protocol/ChunkSerializer.cc`
* **设计指南**：
  * 专职负责将 `Chunk` 领域对象翻译为 Minecraft 1.12.2 的 `0x20` 封包格式；
  * 利用 `mcpe::network::PacketWriter` RAII 构建器实现无重分配出站包构建；
  * 严格遵循协议封包字节流顺序：
    1. `Chunk X`: `writeBe<int32_t>(chunk.x())`
    2. `Chunk Z`: `writeBe<int32_t>(chunk.z())`
    3. `Ground-Up Continuous`: `writeBe<uint8_t>(ground_up ? 1 : 0)`
    4. `Primary Bit Mask`: `appendVarInt(chunk.primaryBitMask())`
    5. `Data Size`: `appendVarInt(payload_size)`
    6. `Data Payload`:
       - 遍历 16 个 Section，若 BitMask 第 $i$ 位为 1：
         - `writeBe<uint8_t>(13)` (Bits Per Block)
         - `appendVarInt(0)` (Dummy Palette Length)
         - `appendVarInt(832)` (Data Array Length)
         - 832 个 `uint64_t`（按大端写入：`writeBe<uint64_t>`）
         - 2048 字节 Block Light（全部追加 `0x00`）
         - 2048 字节 Sky Light（全部追加 `0xFF`）
       - 若 `ground_up == true`，末尾追加 256 字节 Biomes
    7. `Number of Block Entities`: `appendVarInt(0)`
  * **在函数出口加上静态断言式校验**：
    `assert(actual_size == expected_size);` 保证自洽性。

---

### TASK-3.5: 出生点视距网格下发与玩家物理着陆 (`PlayHandler.cc`)
* **任务类型**：会话业务联动
* **位置**：`src/protocol/PlayHandler.cc`
* **设计指南**：
  * **避坑警告**：Minecraft 客户端**绝不会渲染没有相邻邻居的孤立区块**！若只发 1 个区块，客户端会显示空气但可踩踏；
  * 解决方案：以玩家出生区块 $(0, 0)$ 为中心，批量生成并下发 $5 \times 5$ 的区块矩阵：
    $$X \in [-2, 2], \quad Z \in [-2, 2] \quad (\text{共 } 25 \text{ 个区块})$$
  * 时序调整：
    1. 下发 `Join Game (0x23)`；
    2. 循环 25 次，生成超平坦区块并下发 `Chunk Data (0x20)`；
    3. 下发 `Player Position And Look (0x2F)`：
       * 坐标设置为：$X=0.5, Y=4.0, Z=0.5$（站在 $Y=3$ 草方块正上方中间）；
       * 玩家受重力落于草地上，不再跌入虚空！

---

### TASK-3.6: 单元测试覆盖与自动化验收脚本 (`test_chunk.cc`, `test_chunk.py`)
* **单元测试必须验证**：
  1. 负数坐标向下取整单测（$X=-1 \rightarrow Chunk=-1, Local=15$；$X=-16 \rightarrow Chunk=-1, Local=0$；$X=-17 \rightarrow Chunk=-2, Local=15$）；
  2. 寻址极限坐标：$(0,0,0)$ 与 $(15,15,15)$ 寻址正确，无越界；
  3. 13 位压缩打包与解包**往返无损单测**（覆盖跨 Long 边界的条目，如 index 4、9、14 等）；
  4. 序列化数据包 Payload 长度断言等于 11012 字节。
* **自动化测试脚本**：编写 `scripts/test_chunk.py`，模拟真实客户端进服，在收到 `Join Game` 后连续读取并成功校验 25 个 `Chunk Data (0x20)` 报文，校验无解码异常。

---

## 五、 工业级避坑指南与高频面试核心考点

### 避坑一：客户端抛出 `ArrayIndexOutOfBoundsException` 当场闪退
* **根本诱因**：`Primary Bit Mask` 声明的有效 Section 数量与后续 `Data` 字节流实际长度脱节！客户端解析区块时根据 Bit Mask 中的二进制 1 计算后续 Section 个数。如果掩码声明了 2 个，但实际只写了 1 个 Section 的数据，客户端解析光照时数组越界闪退。

### 避坑二：满屏乱方块或错乱纹理
* **根本诱因**：
  1. 混淆了 1.8（2 字节小端方块）与 1.12.2（13 位大端 Long 数组）；
  2. 位压缩时没有将 `uint64_t` 转换为大端序；
  3. 光照数组少写了 1 个字节导致整体字节错位。

### 避坑三：重复发送相同区块导致客户端内存泄漏
* **根本诱因**：如果对已经加载的区块重复发送 `Ground-Up Continuous = true` 的数据包，Notchian 客户端不会释放旧的渲染网格，导致 F3 界面中的 `MultiplayerChunkCache` 泄漏膨胀。
* **正确做法**：如果需要刷新区块，必须先发送 `Unload Chunk (0x1D)`（携带 `Chunk X` 与 `Chunk Z`），再重新发送 `Chunk Data (0x20)`。

---

## 六、 工业标杆（Cuberite）深度架构源码对照

| 维度 | Cuberite 工业级实现 (`cChunk`, `cChunkMap`) | 本项目现代 C++23 升级版 (`mcpe::world`) |
| :--- | :--- | :--- |
| **内存组织** | 采用裸数组 `cChunkDef::BlockTypes` 配合宏函数寻址 | 采用现代 C++ 强类型 `std::array` 与 `constexpr` 空间函数，零运行时开销 |
| **空段处理** | 动态分配 16 个段指针，空段填 `nullptr` | 采用 `std::unique_ptr<ChunkSection>` 严密生命周期管理，杜绝内存泄漏 |
| **数据包组装** | 在 `cProtocol_1_12_2::SendChunkData` 中多次 `memcpy` 拼接 | 采用 Netty 风格 `ByteBuffer` 与 `PacketWriter` 预留头部直接构造，极大降低系统调用开销 |
| **坐标数学** | 包含部分历史 `Floor` 浮点转换兼容逻辑 | 彻底统一为 CPU 算术右移（`>> 4`）与掩码运算（`& 0x0F`），性能登峰造极 |

---

> **结语**：完成 Sprint 3 后，你的服务端将正式具备游戏引擎的空间世界渲染能力！玩家在客户端中将看到蓝天白云下平整无垠的绿色草地，脚踏实地漫步。下一阶段我们将进入高并发与游戏节拍器的核心——**Sprint 4: 20 TPS 节拍器与九宫格 AOI 视距广播**！
