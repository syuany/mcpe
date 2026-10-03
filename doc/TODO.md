# TODO

* [x] 协程 -> 回调函数
* [ ] 测试程序解析
* [x] 内存池优化 PacketFraming::slicePacket（支持基于 std::string_view 零拷贝流式切包）
* [x] 代码注释
* [x] 端口号重构
* [ ] 广播管线零拷贝共享改造：TcpSession 发送管线支持 std::shared_ptr<ByteBuffer> / sendSharedPacket 浅共享广播，单次序列化后供房间多玩家并发引用
