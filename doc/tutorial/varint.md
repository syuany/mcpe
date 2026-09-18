已把你的底座摸清了。先说结论：**TASK-0.4 不是从零开始写，而是"填掉两个空壳 + 补一个 VarLong"**，而且它顺带能修掉你现在编译里的两个警告。

## 一、当前真实状态（先盘点，别重复劳动）

| 位置 | 现状 |
| :--- | :--- |
| `include/mcpe/common/ByteBuffer.h:16` | 已有 `static constexpr size_t kMaxVarIntBytes = 5;` |
| `include/mcpe/common/ByteBuffer.h:87,89` | **已声明** `writeVarInt(int32_t)` / `readVarInt()` |
| `src/common/ByteBuffer.cc:132-134` | **两个空壳**，我强制重编后实测出两个警告 |
| VarLong | 头文件里**完全没有**，需要你新增（含 `kMaxVarLongBytes = 10`） |
| 测试 | GoogleTest，25 个用例当前全通过（`cd build && ctest`），二进制在 `build/test_bytebuffer` |
| CMakeLists | 源文件**显式列出**（`add_library(mcpe_common STATIC src/common/ByteBuffer.cc)`），新增 `.cc` 必须手动加进去 |

实测到的两个警告（这就是 TASK-0.4 的"顺带收益"，也是 Sprint 0 检查单里"零 Warning"的拦路虎）：

```
ByteBuffer.cc:132: warning: unused parameter 'x' [-Wunused-parameter]
ByteBuffer.cc:134: warning: no return statement in function returning non-void [-Wreturn-type]   ← Release 下是 UB，返回垃圾值
```

## 二、参考资料（全在本地，按优先级）

**① 唯一权威算法 —— 读这一节就够**

```bash
sed -n '116,242p' doc/reference/wiki.vg/Data_types.txt
```

* 第 **116~174 行**：`VarInt and VarLong` 正文 + 官方 Java 读写伪代码（`readVarInt` / `readVarLong` / `writeVarInt` / `writeVarLong` 四个函数全在）
* 第 **172~174 行**：明确说明**这不是 Protocol Buffers 的 VarInt**（无 ZigZag，负数一律占满 5 字节）——照抄 protobuf 实现必错
* 第 **175~205 行**：**官方 VarInt 测试向量表**（TASK-0.5 要的就是它）
* 第 **208~241 行**：**官方 VarLong 测试向量表**，含 `LONG_MAX`、`-1`（**10 字节**）、`LONG_MIN`，以及一个特别有价值的 `-2147483648` 作为 VarLong 时是 10 字节（`0x80 0x80 0x80 0x80 0xf8 0xff 0xff 0xff 0xff 0x01`）——同一数值 VarInt ≠ VarLong，正好用来验证你没把两个实现写混

保真版（含表格原文）：`doc/reference/wiki.vg/Data_types.html`

**② 项目内已沉淀的要点（省你重新推导）**

| 文件 | 看什么 |
| :--- | :--- |
| `doc/03_protocol_340_cheatsheet.md` §5.1 | VarInt/VarLong 三条铁律 |
| 同上 §3 / §4.8 | **谁在用 VarInt**：Packet Length、Packet ID、String 长度、Chunk 的 Primary Bit Mask、Block ID、Entity ID —— 决定你的 read 函数会被谁调用 |
| `doc/00_tutorial.md` 阶段 0 | 避坑三条：5 字节 DoS、无符号移位、负数 5 字节 |
| `doc/02_cast_cheatsheet.md` §3.1 | 为什么 `static_cast<uint32_t>(int32_t负数)` 是安全的模运算——正是编码循环第一步的依据 |
| `doc/reference/wiki.vg/Protocol.txt` 的 `Packet format` 章节 | VarInt 在真实包结构里的位置（拆包器怎么用它） |

**③ 一个反面提示**：别去 `doc/reference/minecraft-data/1.12.2/protocol.json` 找算法，里面 `varint` 只标注为 `"native"`，对 TASK-0.4 **零参考价值**。那份数据的价值在 Sprint 3 查 `1.12/blocks.json` 的方块 ID。

## 三、动手前先定两个接口决策（比写循环重要）

**决策 A：放哪？**
文档 TASK-0.4 写的是 `mcpe::common` 命名空间下的自由函数，但你的代码实际是 `mcpe::ByteBuffer` 的成员函数（且已声明）。**建议按现有代码做成成员函数**（`writeVarInt/readVarInt/writeVarLong/readVarLong`），然后把 `doc/sprints/01_sprint0_infra.md` 的签名改成一致——不要维护两套 API；顺带把"`mcpe::common`"这个命名空间口径统一掉。

**决策 B：错误契约（关键，不现在定 Sprint 1 必返工）**

你的拆包器（TASK-1.4）会遇到**两种"读不出来"**，语义完全不同：

| 情况 | 正确反应 |
| :--- | :--- |
| 读了 6 个字节 MSB 还是 1（畸形/恶意） | 抛异常 → 断连 |
| 缓冲区只剩 2 字节（TCP 半包） | **等更多数据**，不能抛异常，也不能消费掉这 2 字节 |

第二点直接暴露一个现存缺口：`ByteBuffer` **没有 readerIndex 回退接口**（只有固定宽度的 `peekInt*`），所以 `readVarInt` 一旦读了一半发现数据不够，就没法把游标退回去。**建议现在就加一个不消费的三态版本**，例如：

```cpp
enum class VarIntResult { kOk, kIncomplete, kMalformed };
VarIntResult peekVarInt(int32_t& out) const;   // 不移动 readerIndex_
```

否则 TASK-1.4 一定会回来改 `ByteBuffer` 的公有接口。

另外注意一处不一致：DoD 要求抛 `std::runtime_error`，但 `ByteBuffer` 现有读函数清一色用 `assert`——**`assert` 在 Release 下会被 `NDEBUG` 抹掉**，网络输入路径绝不能用它兜底（会静默越界读）。建议统一成 `mcpe::ProtocolError : std::runtime_error`。

## 四、建议的执行顺序（约 2 小时）

1. **20 分钟**：读上面 ① 的 116~242 行，把官方伪代码逐行翻译成"人话算法"，先别写 C++。
2. **15 分钟**：定接口——头文件加 `kMaxVarLongBytes`、`writeVarLong`、`readVarLong`、错误类型、以及决策 B 的 `peekVarInt`。
3. **40 分钟**：实现编码（`uint32_t`/`uint64_t` 无符号累加 + 逻辑右移，负数靠"值不为 0 就继续"自然跑到 5/10 字节）。
4. **30 分钟**：实现解码。**边界检查要放在读第 6/11 个字节之前**——wiki.vg 的 Java 参考版是先读后判，会多消费 1 个字节，你的实现比它更严谨（这点可以写进注释当加分项）。
5. **30 分钟**：建 `tests/test_varint.cc` + CMake 新 target `test_varint`（照抄 `test_bytebuffer` 的写法），向量用官方表，另外补四类负例：截断输入、6 字节畸形、10 字节边界的 VarLong、以及**编解码往返一致性**（随机 10 万个值 round-trip）。

**验收命令**（对应 DoD）：

```bash
cmake --build build --target test_varint && ./build/test_bytebuffer   # 或 ctest
# 零警告自检（Sprint 0 检查单要求）
cmake --build build 2>&1 | grep -i warning
# ASan 自检（检查单要求）
cmake -B build-asan -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -g" && cmake --build build-asan && ctest --test-dir build-asan
```

## 五、三个最可能踩的坑

1. **`writeVarInt` 的 `x` 参数是 `int32_t`**：右移必须转 `uint32_t` 做逻辑右移。有符号右移是算术右移，负数会高位补 1 变成死循环（`doc/00_tutorial.md` 阶段 0 已强调）。
2. **VarInt 是"7 位小端分组"，不是大端**：别和你的 `appendInt32`（大端）混淆——同一个 buffer 里两者混着用，测试时一定要按字节逐个断言（`0x80 0x01` 而不是 `0x01 0x80`）。
3. **`readVarInt` 的空壳现在返回垃圾值**：在你去修它之前，任何调用它的代码在 Release 下都是未定义行为；所以第一步先把这两个空壳填掉或临时 `assert(false)`，别让它在构建里静默留着。

需要我直接动手吗？我可以按上面的方案把 TASK-0.4 + 0.5 落地（含 CMake 接线与全部测试向量），也可以只写头文件接口 + `test_varint.cc` 骨架，把算法实现留给你自己练手——你挑一个。