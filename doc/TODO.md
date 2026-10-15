# TODO

* [x] 协程 -> 回调函数与 Strand 统一调度
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
* [ ] 内存池按需评估恢复 PacketFraming::slicePacket（已归档于 archive/memory_pool/）
* [ ] 每阶段重要技术选型分析补充
