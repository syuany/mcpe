#!/usr/bin/env python3
"""
scripts/test_ping.py - 极速 Server List Ping 验收脚本
用于在无需启动原版 Minecraft 客户端的前提下，毫秒级验证服务端 Sprint 1 实现：
1. 发送 Handshake (Proto 340, NextState=1)
2. 发送 Status Request
3. 接收并解析 Status Response (JSON MOTD & 在线人数)
4. 发送 Ping (时间戳)
5. 接收 Pong (8 字节时间戳)，校验 RTT 往返一致性
"""

import socket
import struct
import json
import time
import sys

def encode_varint(val: int) -> bytes:
    res = bytearray()
    while True:
        b = val & 0x7F
        val >>= 7
        if val != 0:
            b |= 0x80
        res.append(b)
        if val == 0:
            break
    return bytes(res)

def read_varint(s: socket.socket) -> int:
    val = 0
    for i in range(5):
        chunk = s.recv(1)
        if not chunk:
            raise ConnectionResetError("连接被服务端提前关闭")
        b = chunk[0]
        val |= (b & 0x7F) << (7 * i)
        if (b & 0x80) == 0:
            return val
    raise ValueError("VarInt 超过 5 字节上限")

def main():
    host = "127.0.0.1"
    port = 25566
    print(f"[*] 正在连接本地 Minecraft 服务端 {host}:{port} ...")
    
    try:
        sock = socket.create_connection((host, port), timeout=3.0)
    except Exception as e:
        print(f"[-] 无法连接到 {host}:{port} ({e})。请确认服务端已启动！")
        sys.exit(1)

    try:
        # 1. 发送 Handshake 包 (ID=0x00, Proto=340, Host="127.0.0.1", Port=port, NextState=1)
        host_bytes = host.encode('utf-8')
        handshake_payload = (
            encode_varint(0x00) +
            encode_varint(340) +
            encode_varint(len(host_bytes)) + host_bytes +
            struct.pack('>H', port) +
            encode_varint(1)  # Next State = 1 (STATUS)
        )
        sock.sendall(encode_varint(len(handshake_payload)) + handshake_payload)
        print("[+] 1. Handshake (0x00, Proto=340, NextState=1) 已发送")

        # 2. 发送 Status Request 包 (ID=0x00, 无载荷)
        status_req = encode_varint(0x00)
        sock.sendall(encode_varint(len(status_req)) + status_req)
        print("[+] 2. Status Request (0x00) 已发送")

        # 3. 接收 Status Response
        packet_len = read_varint(sock)
        packet_id = read_varint(sock)
        if packet_id != 0x00:
            print(f"[-] 错误：预期 Status Response (0x00)，实际收到 0x{packet_id:02X}")
            sys.exit(1)

        json_len = read_varint(sock)
        raw_json = b""
        while len(raw_json) < json_len:
            chunk = sock.recv(min(4096, json_len - len(raw_json)))
            if not chunk:
                break
            raw_json += chunk

        json_str = raw_json.decode('utf-8')
        data = json.loads(json_str)
        print("\n[✔] 3. 成功接收并解析服务端 Status Response JSON:")
        print(json.dumps(data, indent=2, ensure_ascii=False))

        # 4. 发送 Ping 包
        now_ms = int(time.time() * 1000)
        ping_payload = encode_varint(0x01) + struct.pack('>q', now_ms)
        sock.sendall(encode_varint(len(ping_payload)) + ping_payload)
        print(f"\n[+] 4. Ping (0x01) 时间戳已发送: {now_ms}")

        # 5. 接收 Pong 包
        pong_len = read_varint(sock)
        pong_id = read_varint(sock)
        if pong_id != 0x01:
            print(f"[-] 错误：预期 Pong (0x01)，实际收到 0x{pong_id:02X}")
            sys.exit(1)

        pong_bytes = sock.recv(8)
        echo_ms = struct.unpack('>q', pong_bytes)[0]
        if echo_ms != now_ms:
            print(f"[-] 错误：时间戳不一致！发送 {now_ms}，收到 {echo_ms}")
            sys.exit(1)

        print(f"[✔] 5. Pong (0x01) 成功回显时间戳: {echo_ms}")
        print("\n🎉 =========================================================")
        print("🎉 恭喜！Sprint 1 全部网络连接、分包、状态机与 Ping/Pong 验证通过！")
        print("🎉 打开原版 Minecraft 1.12.2 多人游戏列表即可看到绿色延迟信号与 MOTD！")
        print("🎉 =========================================================")

    finally:
        sock.close()

if __name__ == '__main__':
    main()
