#!/usr/bin/env python3
"""
scripts/test_lobby_integration.py - Phase 4 大厅地图流式下发、AOI 跨区块互见与虚空跌落保护端到端集成测试
"""

import socket
import struct
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
            raise ConnectionResetError("Socket closed prematurely while reading VarInt")
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

class LobbyClient:
    def __init__(self, username: str, host="127.0.0.1", port=25566):
        self.username = username
        self.host = host
        self.port = port
        self.sock = socket.create_connection((host, port), timeout=5.0)
        self.entity_id = -1
        self.uuid = ""
        self.spawn_pos = None
        self.chunk_count = 0

    def login_and_spawn(self):
        # 1. Handshake (State=2 LOGIN)
        host_b = self.host.encode('utf-8')
        handshake = (
            encode_varint(0x00) +
            encode_varint(340) +
            encode_varint(len(host_b)) + host_b +
            struct.pack('>H', self.port) +
            encode_varint(2)
        )
        self.sock.sendall(encode_varint(len(handshake)) + handshake)

        # 2. Login Start
        user_b = self.username.encode('utf-8')
        login_start = encode_varint(0x00) + encode_varint(len(user_b)) + user_b
        self.sock.sendall(encode_varint(len(login_start)) + login_start)

        # 3. Read Login Success (0x02)
        p_id, payload = read_packet(self.sock)
        assert p_id == 0x02, f"Expected Login Success (0x02), got 0x{p_id:02X}"
        u_len = payload[0]
        self.uuid = payload[1:1+u_len].decode('utf-8')

        # 4. Drain initial join packets, collect chunks & spawn position
        teleport_id = None
        start_time = time.time()
        while time.time() - start_time < 5.0:
            p_id, payload = read_packet(self.sock)
            if p_id == 0x23:  # Join Game
                self.entity_id = struct.unpack('>i', payload[:4])[0]
            elif p_id == 0x20:  # Chunk Data
                self.chunk_count += 1
            elif p_id == 0x2F:  # Player Position And Look
                # double x, y, z, float yaw, pitch, byte flags, VarInt tp_id
                x, y, z = struct.unpack('>ddd', payload[0:24])
                self.spawn_pos = (x, y, z)
                tp_id_bytes = payload[33:]
                val = 0
                for i, b in enumerate(tp_id_bytes):
                    val |= (b & 0x7F) << (7 * i)
                    if (b & 0x80) == 0:
                        break
                teleport_id = val
                break

        assert teleport_id is not None, "Failed to receive Player Position And Look (0x2F)"

        # 5. Send Teleport Confirm (0x00)
        tp_confirm = encode_varint(0x00) + encode_varint(teleport_id)
        self.sock.sendall(encode_varint(len(tp_confirm)) + tp_confirm)

    def send_move_and_look(self, x: float, y: float, z: float, yaw: float, pitch: float, on_ground=True):
        # 0x0E: Player Position And Look (Serverbound)
        body = (
            encode_varint(0x0E) +
            struct.pack('>ddd', x, y, z) +
            struct.pack('>ff', yaw, pitch) +
            struct.pack('>?', on_ground)
        )
        self.sock.sendall(encode_varint(len(body)) + body)

    def send_chat(self, msg: str):
        msg_b = msg.encode('utf-8')
        body = encode_varint(0x02) + encode_varint(len(msg_b)) + msg_b
        self.sock.sendall(encode_varint(len(body)) + body)

    def close(self):
        self.sock.close()

def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 25566
    host = sys.argv[2] if len(sys.argv) > 2 else "127.0.0.1"

    print("=" * 70)
    print("🚀 启动 Phase 4: 大厅蓝图、AOI 九宫格空间裁剪与虚空跌落集成验收测试")
    print(f"   目标服务器: {host}:{port}")
    print("=" * 70)

    # -------------------------------------------------------------
    # 测试 1: 客户端进入大厅 & 地图区块流式下发验证
    # -------------------------------------------------------------
    print("\n[*] 测试 1: 客户端进服 & 大厅区块下发验证...")
    client_a = LobbyClient("LobbyTesterA", host=host, port=port)
    client_a.login_and_spawn()
    print(f"[+] 客户端 A 登录成功! EID={client_a.entity_id}, 收到区块数={client_a.chunk_count}")
    print(f"[+] 服务端指定出生点: {client_a.spawn_pos}")

    # 验证是否收到了 7x7 (49个) 区块下发
    assert client_a.chunk_count >= 25, f"下发区块过少 ({client_a.chunk_count})，预期至少覆盖核心九宫格"
    # 验证出生点落在浮空岛喷泉广场中央 (48.5, 34.0, 48.5)
    assert abs(client_a.spawn_pos[0] - 48.5) < 0.1, f"出生点 X 不匹配: {client_a.spawn_pos[0]}"
    assert abs(client_a.spawn_pos[1] - 34.0) < 0.1, f"出生点 Y 不匹配: {client_a.spawn_pos[1]}"
    assert abs(client_a.spawn_pos[2] - 48.5) < 0.1, f"出生点 Z 不匹配: {client_a.spawn_pos[2]}"
    print("[✔] 测试 1 通过: 成功流式加载浮空岛大厅区块并定位于 (48.5, 34.0, 48.5)！")

    # -------------------------------------------------------------
    # 测试 2: 虚空跌落保护 (Fall Reset)
    # -------------------------------------------------------------
    print("\n[*] 测试 2: 虚空跌落保护 (Fall Reset) 自动拉回复位验证...")
    # 客户端 A 掉落到 Y = 10.0 (< 20.0)
    client_a.send_move_and_look(48.5, 10.0, 48.5, 0.0, 0.0, on_ground=False)
    client_a.sock.settimeout(2.0)
    reset_received = False
    reset_pos = None

    start = time.time()
    while time.time() - start < 2.0:
        try:
            p_id, payload = read_packet(client_a.sock)
            if p_id == 0x2F:  # Player Position And Look (Serverbound forced reset)
                x, y, z = struct.unpack('>ddd', payload[0:24])
                reset_pos = (x, y, z)
                reset_received = True
                # 收到复位后必须向服务端确认 Teleport Confirm
                tp_id = payload[33]  # 单字节 VarInt
                confirm = encode_varint(0x00) + encode_varint(tp_id)
                client_a.sock.sendall(encode_varint(len(confirm)) + confirm)
                break
        except socket.timeout:
            break

    assert reset_received, "坠入虚空后未收到服务端 0x2F 强制复位拉回数据包！"
    assert abs(reset_pos[0] - 48.5) < 0.1 and abs(reset_pos[1] - 34.0) < 0.1, f"复位位置错误: {reset_pos}"
    print(f"[✔] 测试 2 通过: 坠入 Y=10.0 虚空被平滑拉回出生点 {reset_pos}！")

    # -------------------------------------------------------------
    # 测试 3: 九宫格 AOI 跨区块互见动态广播 (Spawn / Destroy)
    # -------------------------------------------------------------
    print("\n[*] 测试 3: 九宫格 AOI 邻居进入与移出时序验证...")
    # Client A 位于区块 (3, 3) 广场中心 (48.5, 34.0, 48.5)
    # 启动 Client B (LobbyTesterB)
    client_b = LobbyClient("LobbyTesterB", host=host, port=port)
    client_b.login_and_spawn()
    print(f"[+] 客户端 B 登录成功! EID={client_b.entity_id}")

    # Client A 应该看到 Client B 产生在同一九宫格内 (0x05 Spawn Player)
    client_a.sock.settimeout(2.0)
    saw_b_spawn = False
    start = time.time()
    while time.time() - start < 2.0:
        try:
            p_id, payload = read_packet(client_a.sock)
            if p_id == 0x05:
                eid = payload[0]
                if eid == client_b.entity_id:
                    saw_b_spawn = True
                    break
        except socket.timeout:
            break

    assert saw_b_spawn, "Client A 未收到同区块内 Client B 的 0x05 Spawn Player 包！"
    print("[✔] Client A 成功感知同九宫格内 Client B 的 SpawnPlayer")

    # 现在让 Client B 移动到远端区块 (区块 X=10, Z=10 -> 坐标 160.0, 34.0, 160.0)
    print("[*] Client B 远行跨越区块边界至 (160.0, 34.0, 160.0)...")
    client_b.send_move_and_look(160.0, 34.0, 160.0, 0.0, 0.0, on_ground=True)

    # 等待 1-2 Tick 物理循环处理，Client A 应该收到 0x32 Destroy Entities (Client B 移出九宫格)
    saw_b_destroy = False
    start = time.time()
    while time.time() - start < 2.0:
        try:
            p_id, payload = read_packet(client_a.sock)
            if p_id == 0x32:
                count = payload[0]
                eid = payload[1]
                if eid == client_b.entity_id:
                    saw_b_destroy = True
                    break
        except socket.timeout:
            break

    assert saw_b_destroy, "Client B 离开九宫格 AOI 后，Client A 未收到 0x32 Destroy Entities！"
    print(f"[✔] Client A 成功接收 0x32 Destroy Entities 销毁 Client B (EID={client_b.entity_id})！")

    # 稍等 1 个 Tick 确保状态推进
    time.sleep(0.1)

    # 现在让 Client B 重新走回中心区块 (48.5, 34.0, 48.5)
    print("[*] Client B 重新返回中心九宫格 (48.5, 34.0, 48.5)...")
    client_b.send_move_and_look(48.5, 34.0, 48.5, 0.0, 0.0, on_ground=True)

    saw_b_respawn = False
    start = time.time()
    while time.time() - start < 2.0:
        try:
            p_id, payload = read_packet(client_a.sock)
            print(f"[Client A Recv] Packet 0x{p_id:02X}, len={len(payload)}")
            if p_id == 0x05:
                eid = payload[0]
                if eid == client_b.entity_id:
                    saw_b_respawn = True
                    break
        except socket.timeout:
            break

    assert saw_b_respawn, "Client B 重新回到九宫格 AOI 后，Client A 未收到重新 0x05 Spawn Player！"
    print("[✔] 测试 3 通过: 九宫格 AOI 动态进出视界 (Enter -> Spawn, Leave -> Destroy) 逻辑 100% 闭环！")

    client_a.close()
    client_b.close()

    print("\n" + "=" * 70)
    print("🎉 恭喜！Phase 4 大厅地图与九宫格 AOI 全部端到端集成测试完美通过！")
    print("=" * 70)

if __name__ == "__main__":
    main()
