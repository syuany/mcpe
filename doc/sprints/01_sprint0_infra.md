# Sprint 0: 基础设施与叶子组件（Infrastructure & Leaf Components）

> **迭代目标**：不接触复杂网络与游戏逻辑，搭建模块化现代 CMake 工程脚手架，完成最底层二进制缓冲 `ByteBuffer` 与变长整型 `VarInt/VarLong` 编解码算法，以 100% 独立单测跑通为验收基准。  
> **预估工期**：2 ~ 3 天  
> **总任务数**：5 个微任务

---

## 任务执行看板

- [ ] [TASK-0.1: 现代 CMake 架构重构与目录分层](#task-01-现代-cmake-架构重构与目录分层)
- [ ] [TASK-0.2: ByteBuffer 核心数据结构设计](#task-02-bytebuffer-核心数据结构设计)
- [ ] [TASK-0.3: ByteBuffer 独立单元测试套件](#task-03-bytebuffer-独立单元测试套件)
- [ ] [TASK-0.4: VarInt / VarLong 编解码算法实现](#task-04-varint--varlong-编解码算法实现)
- [ ] [TASK-0.5: VarInt 边界极限单元测试](#task-05-varint-边界极限单元测试)

---

### TASK-0.1: 现代 CMake 架构重构与目录分层
* **任务类型**：工程基建 (Infra)
* **预估耗时**：1.5 小时
* **任务上下文**：
  * 当前工程根目录下只有 `CMakeLists.txt` 和 `src/main.cc`。为了支持大型项目演进与独立单测，必须将代码拆分为模块化库目标。
* **技术设计指南**：
  * 建立规范目录：`include/mcpe/common/`、`src/common/`、`tests/`。
  * 在根目录 CMakeLists.txt 中定义静态库（或 Object 库）`add_library(mcpe_common ...)`。
  * 使用 `target_include_directories(mcpe_common PUBLIC include)` 导出头文件路径。
  * `main_app` 和未来的单元测试目标只需通过 `target_link_libraries` 链接 `mcpe_common` 即可。
* **DoD（完成定义）**：
  * 在终端执行 `cmake -B build && cmake --build build`，编译零警告。
  * 在 `tests/` 目录下创建一个测试 `hello_test.cc` 并生成测试目标，执行能正常输出。
* **避坑指南**：
  * 避免使用 `file(GLOB ...)` 自动匹配源文件，显式列出 `.cc`，防止新增文件时 CMake 缓存未察觉导致链接失败。
* **推荐提交**：`chore(build): setup modular CMake structure and test targets`

---

### TASK-0.2: `ByteBuffer` 核心数据结构设计
* **任务类型**：底层组件 (Unit)
* **预估耗时**：2.5 小时
* **任务上下文**：
  * 网络流是无格式字节序列，上层业务需要顺序读写定长数值、浮点数以及变长字节数组。
* **技术设计契约（接口规范参考）**：
  * 内部维护容器：`std::vector<uint8_t> buffer_`，以及读游标 `size_t read_pos_` 与写游标 `size_t write_pos_`。
  * 写入接口：
    * `void write_u8(uint8_t val);`
    * `void write_be16(uint16_t val);`
    * `void write_be32(uint32_t val);`
    * `void write_be64(uint64_t val);`
    * `void write_float(float val);`
    * `void write_double(double val);`
    * `void write_bytes(const uint8_t* data, size_t len);`
  * 读取接口（需包含边界安全校验）：
    * `uint8_t read_u8();`
    * `uint16_t read_be16();`
    * `uint32_t read_be32();`
    * `uint64_t read_be64();`
    * `float read_float();`
    * `double read_double();`
  * 辅助接口：`size_t readable_bytes() const;`、`void skip(size_t len);`。
* **DoD（完成定义）**：
  * 类接口符合 RAII 与强异常安全，读取数据量超过 `readable_bytes()` 时有确定性的防御动作（抛出自定义异常或返回错误状态），不会引发 SIGSEGV。
  * 解析 API 分四层且职责不混：① 谓词层 `bool hasBytes(size_t n)`（**`bool` 只该出现在这一层**）→ ② 解析层 `std::expected<T, DecodeError> tryReadXxx() noexcept`（定宽类型只可能 `kIncomplete`；失败零副作用）→ ③ 严格层 `T readXxx()`（前置条件已满足，违反即 fail-fast）→ ④ 事务层 `ByteBuffer::Mark`。
  * ⚠️ `makeSpace` 的扩容判定必须用 `writableBytes() + prependableBytes()`；曾误用 `readableBytes()`，导致"缓冲区写满且未消费"时 Debug 直接 abort、Release 堆越界写。
* **避坑指南**：
  * ⚠️ **浮点数字节序转换**：严禁使用 C 风格强制指针转换 `*(uint32_t*)&f`（这违反 C++ 的 Strict Aliasing Rule，在 `-O2` 优化下会发生未定义行为！）。正确做法是使用 `std::memcpy` 将 `float` 拷贝到 `uint32_t`，用 `htonl` 转换后写入。
* **推荐提交**：`feat(common): implement ByteBuffer with big-endian primitives`

---

### TASK-0.3: `ByteBuffer` 独立单元测试套件
* **任务类型**：测试验证 (Test)
* **预估耗时**：1.5 小时
* **任务目标**：
  * 编写 `tests/test_bytebuffer.cc`，覆盖所有读写接口：
    * 写入 `uint16_t 0x1234`，验证内存直接字节序列是否为 `0x12, 0x34`（大端高位在先）。
    * 写入浮点数 `123.456f`，读取后验证绝对误差小于 `1e-5`。
    * 连续写入 1000 个整型，验证底层 `vector` 自动扩容后读取游标依然准确。
    * 越界测试：只有 2 字节时尝试读取 `be32`，验证防御逻辑生效。
* **DoD（完成定义）**：
  * 编译并运行 `./build/tests/test_bytebuffer`，所有断言（`assert`）通过，无内存泄漏。
* **推荐提交**：`test(common): add comprehensive unit tests for ByteBuffer`

---

### TASK-0.4: `VarInt` / `VarLong` 编解码算法实现
* **任务类型**：核心算法 (Unit)
* **预估耗时**：2 小时
* **任务目标**：
  * 依据 7-bit MSB 变长编码标准（Minecraft 协议基石），在 `mcpe::ByteBuffer` 上实现：
    * `void appendVarInt(int32_t value);` / `void appendVarLong(int64_t value);`
    * `std::expected<int32_t, DecodeError> tryReadVarInt() noexcept;` / `std::expected<int64_t, DecodeError> tryReadVarLong() noexcept;`
    * `std::expected<int32_t, DecodeError> peekVarInt() const noexcept;` / `std::expected<int64_t, DecodeError> peekVarLong() const noexcept;`
    * `int32_t readVarInt();` / `int64_t readVarLong();`（严格版，仅用于前置条件已满足的场景）
  * 定义错误分类：
    `enum class DecodeError : std::uint8_t { kIncomplete, kMalformed };`
* **算法逻辑关键**：
  * 每个字节低 7 位保存数据，第 8 位（MSB）为 1 表示后续还有字节，为 0 表示结束。
  * 解码时最多处理 5 个字节（VarInt）或 10 个字节（VarLong）；**上限判断必须在读第 6/11 个字节之前**，一旦用满上限仍带继续位，立即判定为 `kMalformed`。
* **错误契约（本任务的核心产出，后续拆包器依赖它）**：
  * `kIncomplete` = 数据不足（TCP 半包）。**这是正常控制流**：不得打 warn 日志、不得断连、不得抛异常。
  * `kMalformed` = 畸形/超上限。属于不可信输入，应断开连接。
  * **失败零副作用**：失败时不消费任何字节，且 `std::expected` 里不携带任何可用值（不可能误用一个陈旧的 out 参数），因此"半包 → 原样返回 → 数据补齐后原地重试"永远安全。
  * 提供 `ByteBuffer::Mark`（RAII 读游标快照）：解析多字段包体时，任一字段失败即整体回滚。
  * 对外**不引入异常**（解析热路径零栈展开、零堆分配）；`readVarInt` 严格版违反前置条件时 fail-fast，因为那是调用方的程序 bug。
* **DoD（完成定义）**：
  * 5 字节继续位输入返回 `kMalformed` 且不消费字节；1 字节 `0x80` 返回 `kIncomplete` 且不消费字节。
  * `readVarInt`/`readVarLong` 的空壳与 `// error handle` 注释全部消除，编译零警告。
  * `readVarLong` 的累加器为 `uint64_t`、上限为 10，与 `readVarInt` 区分开。
* **避坑指南**：
  * ⚠️ **移位变量类型**：累加器必须用无符号数 `uint32_t`/`uint64_t`。有符号右移是算术右移（高位补符号位 1），`-1` 会死循环，把内存写爆。
  * ⚠️ **负数表示**：负数（如 `-1`）在计算机中是补码表示，编码后必须占用 5 个字节，不能丢弃高位。
  * ⚠️ **零值**：`0` 必须编成单字节 `0x00`；用 `while (value != 0)` 会编成**空字节流**，必须用 `do { } while`。
  * ⚠️ **移位下标**：`res |= value << (7 * i)` 必须在 `i` 自增**之前**求值，否则整体错位（`128` 会解成 `16384`）。
* **推荐提交**：`feat(common): implement VarInt and VarLong codec with overflow check`

---

### TASK-0.5: `VarInt` 边界极限单元测试
* **任务类型**：测试验证 (Test)
* **预估耗时**：1 小时
* **任务目标**：
  * 编写 `tests/test_varint.cc`，验证官方协议规范中的标准测试向量：
    * `0` $\rightarrow$ `0x00`
    * `1` $\rightarrow$ `0x01`
    * `127` $\rightarrow$ `0x7f`
    * `128` $\rightarrow$ `0x80, 0x01`
    * `255` $\rightarrow$ `0xff, 0x01`
    * `2147483647` (最大正整型) $\rightarrow$ `0xff, 0xff, 0xff, 0xff, 0x07`
    * `-1` $\rightarrow$ `0xff, 0xff, 0xff, 0xff, 0x0f`
    * `-2147483648` (最小负整型) $\rightarrow$ `0x80, 0x80, 0x80, 0x80, 0x08`
  * 官方向量**编码与解码双向**都要验证（`Data_types.txt` 第 175~241 行同时给出了 VarInt 与 VarLong 两张表）。
  * 覆盖错误路径：半包（`kIncomplete`）、超上限（`kMalformed`）、`peek` 不消费、`Mark` 回滚、非最短编码的宽容处理。
* **DoD（完成定义）**：
  * 运行 `timeout 60 ./build/test_varint`（或 `ctest`），全用例通过，程序退出码为 0。
  * 不存在任何"会死循环 / 会 abort"的用例——半包与畸形输入都必须以返回值表达。
  * 在 `-fsanitize=address,undefined` 下运行零报告。
* **推荐提交**：`test(common): add edge case verification for VarInt`

---

## Sprint 0 交付物验收检查单（Review Checklist）

- [ ] 工程目录结构清晰，头文件与实现分离。
- [ ] `mcpe_common` 静态库编译无 Warning。
- [ ] `test_bytebuffer` 与 `test_varint` 单元测试在本地自动化执行通过。
- [ ] 开启 `-fsanitize=address` 编译单测程序，运行无内存越界和泄漏报错。
- [ ] 准备进入 [Sprint 1: 网络基座与列表探测](file:///home/ubu/dev/my/mcpe/doc/sprints/02_sprint1_network_ping.md)。
