# TODO

* [ ] 协程 -> 回调函数
* [ ] 测试程序解析
* [x] 内存池优化 PacketFraming::slicePacket（支持基于 std::string_view 零拷贝流式切包）
* [ ] 代码注释
* [ ] 端口号重构
* [x] 内存池分片架构（Sharded BufferPool / TLS Cache）：多工作线程绑定独立分片与跨核工作窃取 (Work Stealing)，彻底消除全局单 Head 的多核 CAS 争用瓶颈
* [x] 广播管线零拷贝共享改造（方案 B）：TcpSession 发送管线支持 std::shared_ptr<ByteBuffer> / sendSharedPacket 浅共享广播，单次序列化后供房间多玩家并发引用，彻底消除广播风暴下的 50+ 次无意义 memcpy 与重复分配，实测广播吞吐量提升 3.6x+
* [x] 内存池动态 Chunk 扩容（二级连续 Arena 链）：突破预分配容量时按 4MB 批量动态无锁挂载新 Chunk，杜绝退化为散碎堆分配的性能悬崖