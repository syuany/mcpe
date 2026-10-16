# TODO

* [x] 协程 -> 回调函数与 Strand 统一调度（Sprint 3.5 已完成ネットワーク层 C++20 无栈协程化）
* [x] MPSC 双缓冲任务队列与 20 TPS 物理节拍器骨架
* [x] 广播管线零拷贝共享改造：ByteBuffer::retainedSlice() 共享底层 Storage + COW 写时复制多播
* [x] CMake 构建体系重构与目标精简（拆分为 root / cmake/Dependencies.cmake / src / tools / tests）
* [x] 剥离重型地图解析器（ZipArchive / NbtReader / AnvilLoader / SchematicLoader）至 tools/map_baker 离线烘焙工具链，消除 SchematicLoader 175 行重复 NbtReader，实现 .mcpe_map <1ms 零解压冷启动
* [x] 新增无会话绑定的 PacketBuilder，统一 EntityManager 与 ChatHandler 封包构建
* [x] 闭环 20 TPS 单写入者并发边界：ChatHandler 从 EntityManager 读取权威坐标，Fall Reset 同步推入 EventQueue 更新 SpatialGrid
* [x] 定时器架构分析与 Linux 对比（详见 [linux_timers_and_mcpe_timer_architecture.md](guides/linux_timers_and_mcpe_timer_architecture.md)）
* [x] Schematic 蓝图加载与浮空岛大厅世界流式加载
* [x] 基于 ChunkPos 九宫格邻居差异集的空间 AOI 局部视界裁剪与进出广播
* [x] 商业大厅虚空跌落平滑拉回复位机制 (Fall Reset)
* [x] Sprint 4: 双层 INI 配置文件体系（`src/settings.ini` + `assets/maps/<map>.ini`，支持 `/setmaxplayers` 与 `/setspawn` 持久化）
* [x] Sprint 4: 多实例并发 Actor 引擎（`src/game/IGameInstance` + `src/game/InstanceManager`，只读共享 `World` 区块内存 + 每实例独立 20 TPS 物理线程）
* [x] Sprint 4: 大厅满员自动裂变分线（`lobby-1..N`）与 `LoginHandler` 全服总人数硬上限 `0x00 Disconnect` 准入拦截
* [x] Sprint 4: 多人同赛道随进随跑跑酷小游戏（`src/game/parkour/ParkourInstance` 起点/检查点/终点 AABB 检测、个人毫秒计时、Fall Reset 拉回检查点与 `0x35 Respawn` 单连接无缝切世界）
* [x] Sprint 5: 生产级 Multi-Stage 极简 Dockerfile（<=30MB 镜像、非 root 用户运行）与 docker-compose.yml 一键集群编排
* [x] Sprint 5: 千人无头 Bot 漫游发压套件（scripts/benchmark_swarm.py）与基准量化分析报告（doc/benchmark_report_sprint5.md）
* [x] Sprint 5: 保姆级集群网关与交付实战教程（doc/tutorial/sprint5_cluster_gateway.md）
* [ ] 内存池按需评估恢复 PacketFraming::slicePacket（已归档于 archive/memory_pool/）
