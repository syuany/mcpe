#!/usr/bin/env python3
"""
scripts/test_sprint4_parkour.py - Sprint 4 多实例大厅分线、settings.ini 与跑酷 0x35 无缝切世界端到端集成测试
"""

import socket
import struct
import sys
import time


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
            raise ConnectionResetError("Socket closed while reading VarInt")
        b = chunk[0]
        val |= (b & 0x7F) << (7 * i)
        if (b & 0x80) == 0:
            return val
    raise ValueError("VarInt exceeds 5 bytes")


def recv_exact(s: socket.socket, n: int) -> bytes:
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise ConnectionResetError(f"Socket closed while reading {n} bytes")
        buf += chunk
    return buf


def read_packet(s: socket.socket):
    p_len = read_varint(s)
    p_id = read_varint(s)
    id_bytes_len = len(encode_varint(p_id))
    payload_len = p_len - id_bytes_len
    payload = recv_exact(s, payload_len) if payload_len > 0 else b""
    return p_id, payload


class ParkourClient:
    def __init__(self, username: str, host="127.0.0.1", port=25566):
        self.username = username
        self.host = host
        self.port = port
        self.sock = socket.create_connection((host, port), timeout=5.0)
        self.entity_id = -1
        self.spawn_pos = None

    def login_and_spawn(self):
        host_b = self.host.encode("utf-8")
        handshake = (
            encode_varint(0x00)
            + encode_varint(340)
            + encode_varint(len(host_b))
            + host_b
            + struct.pack(">H", self.port)
            + encode_varint(2)
        )
        self.sock.sendall(encode_varint(len(handshake)) + handshake)

        user_b = self.username.encode("utf-8")
        login_start = encode_varint(0x00) + encode_varint(len(user_b)) + user_b
        self.sock.sendall(encode_varint(len(login_start)) + login_start)

        p_id, payload = read_packet(self.sock)
        assert p_id == 0x02, f"Expected Login Success (0x02), got 0x{p_id:02X}"

        teleport_id = None
        start_time = time.time()
        while time.time() - start_time < 5.0:
            p_id, payload = read_packet(self.sock)
            if p_id == 0x23:
                self.entity_id = struct.unpack(">i", payload[:4])[0]
            elif p_id == 0x2F:
                x, y, z = struct.unpack(">ddd", payload[0:24])
                self.spawn_pos = (x, y, z)
                tp_id_bytes = payload[33:]
                val = 0
                for i, b in enumerate(tp_id_bytes):
                    val |= (b & 0x7F) << (7 * i)
                    if (b & 0x80) == 0:
                        break
                teleport_id = val
                break

        assert teleport_id is not None, "Did not receive initial 0x2F Player Position And Look"
        tp_confirm = encode_varint(0x00) + encode_varint(teleport_id)
        self.sock.sendall(encode_varint(len(tp_confirm)) + tp_confirm)

    def send_chat(self, msg: str):
        msg_b = msg.encode("utf-8")
        body = encode_varint(0x02) + encode_varint(len(msg_b)) + msg_b
        self.sock.sendall(encode_varint(len(body)) + body)

    def send_move(self, x: float, y: float, z: float, on_ground=True):
        body = (
            encode_varint(0x0D)
            + struct.pack(">ddd", x, y, z)
            + struct.pack(">?", on_ground)
        )
        self.sock.sendall(encode_varint(len(body)) + body)

    def close(self):
        self.sock.close()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 25566
    host = sys.argv[2] if len(sys.argv) > 2 else "127.0.0.1"

    print("=" * 72)
    print("🚀 启动 Sprint 4: 多实例大厅分线 & 0x35 跑酷无缝切世界集成测试")
    print("=" * 72)

    client = ParkourClient("Sprint4Runner", host=host, port=port)
    client.login_and_spawn()
    print(f"[✔] 登录成功并进入大厅分线! 初始坐标: {client.spawn_pos}")

    # 发送 /join spiral 触发 0x35 Respawn 切入螺旋跑酷地图
    print("[*] 发送 '/join spiral' 指令，验证 0x35 Respawn 无缝切世界...")
    client.send_chat("/join spiral")

    got_respawn = False
    got_parkour_spawn = False
    start = time.time()
    while time.time() - start < 3.0:
        p_id, payload = read_packet(client.sock)
        if p_id == 0x35:
            got_respawn = True
        elif p_id == 0x2F and got_respawn:
            x, y, z = struct.unpack(">ddd", payload[0:24])
            client.spawn_pos = (x, y, z)
            got_parkour_spawn = True
            tp_id = payload[33]
            confirm = encode_varint(0x00) + encode_varint(tp_id)
            client.sock.sendall(encode_varint(len(confirm)) + confirm)
            break

    assert got_respawn, "未收到 0x35 Respawn 切世界数据包！"
    assert got_parkour_spawn, "切入跑酷地图后未收到 0x2F 起点定位包！"
    print(f"[✔] 0x35 无缝切世界成功! 跑酷起点坐标: {client.spawn_pos}")

    # 依次穿过起点 (32.5, 33.0, 35.0) -> 检查点 1 (36.0, 41.0, 23.0) -> 终点 (32.5, 51.0, 32.5)
    client.send_move(32.5, 33.0, 35.0)
    time.sleep(0.1)
    client.send_move(36.0, 41.0, 23.0)
    time.sleep(0.1)
    client.send_move(32.5, 51.0, 32.5)

    got_finish_announce = False
    start = time.time()
    while time.time() - start < 2.0:
        p_id, payload = read_packet(client.sock)
        if p_id == 0x0F:
            text = payload.decode("utf-8", errors="ignore")
            if "通关赛道" in text:
                got_finish_announce = True
                print(f"[✔] 收到终点通关广播: {text}")
                break

    assert got_finish_announce, "穿过终点 AABB 后未收到通关成绩广播！"
    client.close()
    print("🎉 Sprint 4 端到端集成测试全部通过！")


if __name__ == "__main__":
    main()
