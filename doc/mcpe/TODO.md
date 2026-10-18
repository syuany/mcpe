# TODO

* [x] 协程 -> 回调函数与 Strand 统一调度
* [x] 广播管线零拷贝共享改造：ByteBuffer::retainedSlice() 共享底层 Storage + COW 写时复制多播
* [x] CMake 构建体系重构与目标精简（拆分为 root / cmake/Dependencies.cmake / src / tools / tests）
* [x] 新增无会话绑定的 PacketBuilder，统一 EntityManager 与 ChatHandler 封包构建
* [x] 定时器架构分析与 Linux 对比
* [x] Schematic 蓝图加载与浮空岛大厅世界流式加载
* [x] 基于 ChunkPos 九宫格邻居差异集的空间 AOI 局部视界裁剪与进出广播
* [x] 原生支持商业反向代理网关（BungeeCord / Velocity）的 IP Forwarding 协议穿透（解析握手包 \0 提取真实 IP、正版 UUID 与皮肤属性，Bungee-Guard 密钥防伪）
* [x] 跨服传送门路由：大厅空间传送门 AABB 检测与 /server <name> 快捷指令，通过 0x18 Plugin Message (Channel: "BungeeCord") 联动外部网关实现客户端无感切服
* [ ] 内存池按需评估恢复 PacketFraming::slicePacket
* [ ] docker-compose.yml ./deploy/bungee_config.yml:/server/config.yml
