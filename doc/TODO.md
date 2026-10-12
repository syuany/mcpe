# TODO

## 核心主线：微服务大厅与多地图跑酷引擎开发任务

### Sprint 3: 玩家移动同步与聊天系统
* [ ] 协议处理：在 `PlayHandler` 中解析玩家上行移动包 (`0x0E Player Position`, `0x0F Player Look`, `0x10 Position And Look`)
* [ ] 状态广播：实现 `Spawn Player (0x05)` 与 `Destroy Entities (0x32)`，让同屏玩家相互可见
* [ ] 位姿同步：按节拍广播 `0x26 Entity Relative Move` 与 `0x28 Entity Look`
* [ ] 聊天系统：消费 `0x02 Chat Message` 并向同世界玩家多播回传 `0x02 Chat (Clientbound)`
* [ ] 基础指令：解析以 `/` 开头的文本，支持 `/lobby` 与 `/maps` 响应

### Sprint 4: 多地图跑酷房间引擎与无缝切世界
* [ ] 地图解析：实现轻量级 WorldEdit `.schematic` 建筑蓝图加载器（zlib inflate + NBT 方块三维数组映射）
* [ ] 房间调度：实现 `RoomManager` 状态机（`WAITING` -> `COUNTDOWN` -> `PLAYING` -> `ENDED`）
* [ ] 无缝切世界：实现 `0x35 Respawn` 数据包下发，打通单连接地图热切换流水线
* [ ] 跑酷规则：实现高精度计时器、掉落安全复位（Fall Reset）与终点 AABB 包围盒检测

### Sprint 5: BungeeCord 集群网关与产品化交付
* [ ] 网关穿透：在 `HandshakeHandler` 中支持 BungeeCord IP Forwarding 协议解析（提取真实玩家 IP 与 UUID）
* [ ] 跨服传送：实现 `0x18 Plugin Message (BungeeCord:Connect)` 协议封装，支持传送门跨服调度
* [ ] 配置与打包：编写 `config.toml` 配置解析器与生产级 `Dockerfile`
* [ ] 性能压测：编写多 Bot 并发压测脚本，输出内存与延迟基准评测报告

---
## 性能与架构持续优化项
* [x] 协程 -> 回调函数与 Strand 统一调度
* [x] 内存池优化 PacketFraming::slicePacket（支持基于 std::string_view 零拷贝流式切包）
* [x] MPSC 双缓冲任务队列与 20 TPS 物理节拍器骨架
* [ ] 广播管线零拷贝共享改造：TcpSession 发送管线支持 std::shared_ptr\<ByteBuffer\> 浅共享广播
* [ ] CMake 构建体系重构与目标精简

* [ ] 每阶段重要技术选型分析
* [x] 定时器架构分析与 Linux 对比（详见 [linux_timers_and_mcpe_timer_architecture.md](guides/linux_timers_and_mcpe_timer_architecture.md)）
* [ ] test 抄 cuberite 重写