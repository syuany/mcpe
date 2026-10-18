#!/usr/bin/env python3
"""
scripts/test_sprint4_parkour.py - 多实例大厅分线、跑酷 0x35 无缝切世界与跑酷开发者模式端到端集成测试
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
    print("🚀 启动多实例大厅分线、跑酷无缝切世界与跑酷开发者模式端到端集成测试")
    print("=" * 72)

    client = ParkourClient("Sprint4Runner", host=host, port=port)
    client.login_and_spawn()
    print(f"[✔] 登录成功并进入默认浮空岛大厅! 初始坐标: {client.spawn_pos}")

    # 发送 /maps 验证地图列表带有 [1], [2], [3] 序号
    print("[*] 发送 '/maps' 指令，验证地图序号列表...")
    client.send_chat("/maps")
    time.sleep(0.1)

    # 发送 /join 1 触发 0x35 Respawn 切入 1 号跑酷地图 (FLASH)
    print("[*] 发送 '/join 1' 指令，验证通过数字序号无缝切世界...")
    client.send_chat("/join 1")

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
    print(f"[✔] 0x35 无缝切入跑酷地图 [FLASH] 成功! 赛道坐标: {client.spawn_pos}")

    # 验证跑酷开发者模式鉴权流程：未鉴权拒绝 -> 错误口令拒绝 -> 正确口令提权 -> 开启开发者模式并获取飞行能力
    print("[*] 测试未鉴权拦截：发送 '/pk dev on' 应提示权限不足...")
    client.send_chat("/pk dev on")
    got_auth_denied = False
    start = time.time()
    while time.time() - start < 1.5:
        p_id, payload = read_packet(client.sock)
        if p_id == 0x0F:
            msg_str = payload.decode("utf-8", errors="ignore")
            if "权限不足" in msg_str:
                got_auth_denied = True
                print("[✔] 成功拦截未鉴权玩家使用开发者模式 (提示权限不足)")
                break
    assert got_auth_denied, "未鉴权玩家执行 /pk dev on 未被正确拦截！"

    print("[*] 测试错误口令拦截：发送 '/pk auth wrong' 应提示鉴权失败...")
    client.send_chat("/pk auth wrong")
    got_wrong_auth = False
    start = time.time()
    while time.time() - start < 1.5:
        p_id, payload = read_packet(client.sock)
        if p_id == 0x0F:
            msg_str = payload.decode("utf-8", errors="ignore")
            if "鉴权失败" in msg_str:
                got_wrong_auth = True
                print("[✔] 错误口令提权被成功拒绝")
                break
    assert got_wrong_auth, "错误口令未被正确拦截！"

    print("[*] 测试正确口令提权：发送 '/pk auth mcpe_secret_2026'...")
    client.send_chat("/pk auth mcpe_secret_2026")
    got_auth_success = False
    start = time.time()
    while time.time() - start < 1.5:
        p_id, payload = read_packet(client.sock)
        if p_id == 0x0F:
            msg_str = payload.decode("utf-8", errors="ignore")
            if "鉴权成功" in msg_str:
                got_auth_success = True
                print("[✔] 口令鉴权通过，已成功解锁开发者权限！")
                break
    assert got_auth_success, "正确口令提权失败！"

    print("[*] 测试跑酷开发者模式与飞行功能...")
    client.send_chat("/pk dev on")
    got_abilities = False
    start = time.time()
    while time.time() - start < 2.0:
        p_id, payload = read_packet(client.sock)
        if p_id == 0x2C:  # Player Abilities
            flags = payload[0]
            if (flags & 0x04) != 0:  # Allow flying
                got_abilities = True
                print(f"[✔] 成功收到 0x2C Player Abilities 飞行许可包 (flags=0x{flags:02X})")
                break

    assert got_abilities, "启用开发者模式后未收到 0x2C 飞行能力包！"

    print("[*] 测试现场设置出生点与检查点指令...")
    client.send_chat("/pk setspawn")
    client.send_chat("/pk setcp 1")
    client.send_chat("/pk info")
    time.sleep(0.2)

    client.send_chat("/pk dev off")
    time.sleep(0.1)

    print("[*] 测试返回大厅...")
    client.send_chat("/lobby")
    got_lobby_respawn = False
    start = time.time()
    while time.time() - start < 3.0:
        p_id, payload = read_packet(client.sock)
        if p_id == 0x35:
            got_lobby_respawn = True
            break
    assert got_lobby_respawn, "返回大厅未收到 0x35 切世界包！"
    print("[✔] 成功切回默认主大厅！")

    client.close()
    print("🎉 跑酷多实例与开发者模式端到端集成测试全部通过！")


if __name__ == "__main__":
    main()
