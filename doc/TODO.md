# TODO

* [x] 协程 -> 回调函数与 Strand 统一调度
* [ ] 内存池优化 PacketFraming::slicePacket
* [x] MPSC 双缓冲任务队列与 20 TPS 物理节拍器骨架
* [ ] 广播管线零拷贝共享改造：TcpSession 发送管线支持 std::shared_ptr<ByteBuffer> 浅共享广播
* [ ] CMake 构建体系重构与目标精简
* [ ] 每阶段重要技术选型分析
* [x] 定时器架构分析与 Linux 对比（详见 [linux_timers_and_mcpe_timer_architecture.md](guides/linux_timers_and_mcpe_timer_architecture.md)）
* [ ] test 抄 cuberite 重写
* [x] Schematic 蓝图加载与 96x48x96 浮空岛大厅世界流式加载
* [x] 基于 ChunkPos 九宫格邻居差异集的空间 AOI 局部视界裁剪与进出广播
* [x] 商业大厅虚空跌落平滑拉回复位机制 (Fall Reset)
