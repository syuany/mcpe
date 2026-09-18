# Sprint 5: 压测调优与性能分析（Stress Testing & Profiling）

> **迭代目标**：编写无头压测工具模拟 500+ 并发玩家长连接交互，运用 Linux 原生分析工具（`perf`、火焰图）定位瓶颈，引入定长内存池（Buffer Pool）消灭频繁 `malloc/free`，产出硬核性能调优量化指标。  
> **预估工期**：4 ~ 5 天  
> **总任务数**：6 个微任务

---

## 任务执行看板

- [ ] [TASK-5.1: 纯 C++ 异步无头压测机器人](#task-51-纯-c-异步无头压测机器人)
- [ ] [TASK-5.2: 系统级压测环境配置与优化](#task-52-系统级压测环境配置与优化)
- [ ] [TASK-5.3: Linux perf 采样与火焰图流水线](#task-53-linux-perf-采样与火焰图流水线)
- [ ] [TASK-5.4: 定长内存池设计与实现](#task-54-定长内存池设计与实现)
- [ ] [TASK-5.5: 内存池接入与前后性能指标对比](#task-55-内存池接入与前后性能指标对比)
- [ ] [TASK-5.6: Sanitizers 全方位防御性检查](#task-56-sanitizers-全方位防御性检查)

---

### TASK-5.1: 纯 C++ 异步无头压测机器人（Headless Bot）
* **任务类型**：压测工具 (Tool)
* **预估耗时**：4 小时
* **任务设计指南**：
  * 在 `tests/bot_runner.cc` 中基于 Asio 实现无界面的轻量压力生成器：
    * 支持命令行参数：`--count <N>`、`--host <IP>`、`--port <PORT>`。
    * 并发发起 $N$ 个异步 TCP 连接，自动化完成 Handshake（协议号填 `340`）-> Login Start -> 接收 Login Success / `Join Game (0x23)` 流程。
    * 收到 `Player Position And Look (0x2F)` 后必须回 `Teleport Confirm (0x00)`（`Teleport ID: VarInt`）。
    * 应答服务端 `Keep Alive (0x1F)`：回 `Keep Alive (0x0B)`，载荷为 **`Long`**（8 字节，原样回送）。
    * 消费并丢弃服务端下发的 25 个 `Chunk Data (0x20)` 大包（解包开销本身属于压测负载）。
    * 成功进入游戏后，每个连接以 20Hz 频率持续向服务端发送带有随机漫步坐标的 `Player Position (0x0D)` 数据包（`X/Y/Z: Double` + `On Ground: Boolean`）。
* **DoD（完成定义）**：
  * 命令行输出实时统计：在线机器人总数、当前吞吐量（Packets/sec）。能稳定维持 500 个并发连接漫步 10 分钟。
* **推荐提交**：`feat(tools): build headless benchmark bot runner`

---

### TASK-5.2: 系统级压测环境配置与优化
* **任务类型**：环境调优 (Infra)
* **预估耗时**：1 小时
* **任务设计指南**：
  * 解决 Linux 默认参数限制：
    * 提高文件描述符软硬上限：`ulimit -n 65535`。
    * 开启 TCP 端口快速复用：`sudo sysctl -w net.ipv4.tcp_tw_reuse=1`。
  * 调整服务端 CMake 构建配置：
    * 增加 Release 编译模式并保留调试符号：`-O2 -g -fno-omit-frame-pointer`。
* **DoD（完成定义）**：
  * 在本地同时运行服务端与 1000 机器人压测客户端，无 `Too many open files` 或 `Cannot assign requested address` 报错。
* **推荐提交**：`chore(perf): configure system ulimit and release build flags`

---

### TASK-5.3: Linux `perf` 采样与火焰图流水线
* **任务类型**：性能分析 (Profiling)
* **预估耗时**：2 小时
* **任务设计指南**：
  * 在 500 机器人持续压测的高负载场景下，使用 Linux `perf` 工具对服务端进程进行 CPU 采样：
    ```bash
    perf record -F 99 -p $(pgrep main_app) -g -- sleep 30
    ```
  * 使用 Brendan Gregg 开源的 FlameGraph 脚本生成 SVG 交互式火焰图：
    ```bash
    perf script > out.perf
    ./stackcollapse-perf.pl out.perf > out.folded
    ./flamegraph.pl out.folded > flamegraph_before.svg
    ```
* **DoD（完成定义）**：
  * 产出 `flamegraph_before.svg`，在浏览器中能清晰分析出耗时最长的函数调用栈（重点关注 `malloc/free`、系统调用与反序列化拷贝）。
* **推荐提交**：`docs(perf): capture baseline CPU flamegraph under 500-bot load`

---

### TASK-5.4: 定长内存池（Fixed-size Buffer Pool）设计与实现
* **任务类型**：性能优化 (Optimization)
* **预估耗时**：3 小时
* **任务设计指南**：
  * 解决痛点：火焰图中大量 CPU 被堆分配（`ptmalloc` / `malloc` 锁竞争）占据。
  * 实现基于 Free-List 的定长内存池（如 4096 字节/块）：
    * 提供 `acquire()` 获取可用空闲块，`release()` 回收。
    * 结合智能指针自定义删除器（Custom Deleter）：
      `using PooledBuffer = std::unique_ptr<uint8_t[], BufferDeleter>;`
    * 释放时自动放回内存池，不再调用系统 `free`。
* **DoD（完成定义）**：
  * 编写内存池多线程并发获取与归还单测，验证无内存泄漏、无重复释放。
* **推荐提交**：`feat(common): implement fixed-size Buffer Pool with custom deleter`

---

### TASK-5.5: 内存池接入与前后性能指标对比
* **任务类型**：优化度量 (Benchmark)
* **预估耗时**：2 小时
* **任务设计指南**：
  * 将网络收发与数据包临时缓冲区迁移至 `BufferPool`。
  * 重新施加相同强度的 500 机器人压测，生成优化后的火焰图 `flamegraph_after.svg`。
  * 采集并比对量化指标：
    1. **Tick 耗时抖动**：统计 1000 次 Tick 的最大耗时与标准差（如 $50\text{ms} \pm 8\text{ms} \rightarrow 50\text{ms} \pm 1\text{ms}$）。
    2. **堆分配频次**：通过 `perf` 或 `valgrind` 观测内存申请次数大幅降低。
    3. **CPU 综合占用率**：主进程 CPU 占用明显下降。
* **DoD（完成定义）**：
  * 输出 `doc/benchmark_report.md`，包含清晰的优化前后对比表格与火焰图截屏对比。
* **推荐提交**：`docs(perf): document performance gains from Buffer Pool optimization`

---

### TASK-5.6: Sanitizers 全方位防御性检查
* **任务类型**：代码质量 (Quality)
* **预估耗时**：2 小时
* **任务设计指南**：
  * 启用 AddressSanitizer 与 UndefinedBehaviorSanitizer：
    `-fsanitize=address,undefined -g`
    * 启动压测并在压测过程中强制 kill 机器人端，验证服务端在海量突发断线下的内存清理完整度。
  * 启用 ThreadSanitizer：
    `-fsanitize=thread -g`
    * 运行多玩家同服并发测试，验证跨线程队列与主逻辑读写零竞态。
* **DoD（完成定义）**：
  * 控制台零 ASan 越界/泄漏报警，零 TSan 竞态警告，零 UBSan 未定义行为提示。
* **推荐提交**：`chore(qa): pass all AddressSanitizer and ThreadSanitizer checks`

---

## Sprint 5 交付物验收检查单（Review Checklist）

- [ ] 拥有自主可控的无头压测工具，摆脱对重型图形客户端的依赖。
- [ ] 掌握 `perf` 采样与火焰图性能瓶颈定位分析全流程。
- [ ] 定长内存池成功消除了高频小对象的堆分配开销。
- [ ] 产出量化性能调优报告。
- [ ] 系统通过高强度并发与动态内存消毒器验证。
