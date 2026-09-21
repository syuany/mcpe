# MCPE 项目敏捷迭代总览看板（Sprint Index & Kanban）

> **项目定位**：基于 C++17/20 与 Asio 的 Minecraft 协议网关与世界同步引擎  
> **开发模式**：敏捷个人看板（Solo Agile Flow），按阶段垂直切片，小步快跑  
> **文档导航**：每个阶段拥有独立可执行的 Sprint 任务清单文件，完成一个勾选一个。  
> **目标协议**：**Minecraft Java Edition 1.12.2（protocol 340）**；所有包 ID、字段类型与区块格式以 [`doc/03_protocol_340_cheatsheet.md`](../03_protocol_340_cheatsheet.md) 为唯一权威参考。

---

## 迭代阶段导航速查

| 阶段文件 | 阶段名称 | 核心主题 | 任务数 | 预估工期 | 状态 |
| :--- | :--- | :--- | :---: | :---: | :---: |
| [01_sprint0_infra.md](file:///home/ubu/dev/my/mcpe/doc/sprints/01_sprint0_infra.md) | **Sprint 0** | **基础设施与叶子组件**：现代 CMake、ByteBuffer、VarInt 编解码、独立单测 | 5 个 | 2~3 天 | `[待开始]` |
| [02_sprint1_network_ping.md](file:///home/ubu/dev/my/mcpe/doc/sprints/02_sprint1_network_ping.md) | **Sprint 1** | **网络基座与列表探测**：Asio 连接生命周期、发送队列、粘包拆包、Server List Ping | 9 个 | 3~4 天 | `[待开始]` |
| [03_sprint2_login_spawn.md](file:///home/ubu/dev/my/mcpe/doc/sprints/03_sprint2_login_spawn.md) | **Sprint 2** | **会话生命周期与虚空**：Login 鉴权、离线 UUID、Join Game、出生点、KeepAlive 保活 | 7 个 | 4~5 天 | `[待开始]` |
| [04_sprint3_chunk_world.md](file:///home/ubu/dev/my/mcpe/doc/sprints/04_sprint3_chunk_world.md) | **Sprint 3** | **区块模型与超平坦**：ChunkSection 紧凑内存、地形生成、**0x20 区块包**、踏下方块 | 6 个 | 4~5 天 | `[待开始]` |
| [05_sprint4_concurrency_aoi.md](file:///home/ubu/dev/my/mcpe/doc/sprints/05_sprint4_concurrency_aoi.md) | **Sprint 4** | **架构解耦与多玩家**：MPSC 任务队列、20 TPS 节拍器、九宫格 AOI、同屏移动与挖方块 | 7 个 | 5~6 天 | `[待开始]` |
| [06_sprint5_perf_tuning.md](file:///home/ubu/dev/my/mcpe/doc/sprints/06_sprint5_perf_tuning.md) | **Sprint 5** | **压测调优与 Profiling**：千人 Headless Bot、perf 火焰图、定长内存池、Sanitizers | 6 个 | 4~5 天 | `[待开始]` |
| **总计** | | **全阶段系统构建** | **40 个微任务** | **约 4~5 周** | |

---

## 个人敏捷开发工作准则

### 1. 任务颗粒度（INVEST 原则）
* 每个子任务严格控制在 **1 ~ 3 小时**。如果某个任务做了半天还没头绪，说明拆分不够细，必须退回继续细化或先做 Spike 探针。
* 严禁跳跃开发：不要在 Sprint 0 还没完成单测时就去写网络和世界同步代码。

### 2. 验收定义（DoD - Definition of Done）
* 每个任务都有明确的 DoD，必须是**可运行、可观测、可判定**的客观结果。
* 编写测试代码或运行验证通过前，不能将任务勾选为 `[x]`。

### 3. 单步提交规范（One Task, One Commit）
* 完成一个 Task 即刻提交一次 Git Commit，提交信息遵循 Angular 规范：
  * `feat(模块)`: 新功能
  * `fix(模块)`: 修复缺陷
  * `test(模块)`: 编写或补充测试
  * `chore(模块)`: 构建配置、依赖调整
  * `docs(模块)`: 文档更新
