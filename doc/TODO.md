# TODO

* [ ] 协程 -> 回调函数
* [ ] 测试程序解析
* [x] 内存池优化 PacketFraming::slicePacket（支持基于 std::string_view 零拷贝流式切包）
* [ ] 代码注释
* [ ] 端口号重构
* [x] 内存池分片架构（Sharded BufferPool / TLS Cache）：多工作线程绑定独立分片与跨核工作窃取 (Work Stealing)，彻底消除全局单 Head 的多核 CAS 争用瓶颈
* [ ] 内存池动态 Chunk 扩容（二级连续 Arena 链）：突破预分配容量时按 4MB 批量动态无锁挂载新 Chunk，杜绝退化为散碎堆分配的性能悬崖