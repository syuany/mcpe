#!/usr/bin/env python3
"""
scripts/test_sprint5_gateway.py - BungeeCord / Velocity IP Forwarding & Portal Routing Integration Test
"""

import os
import socket
import struct
import subprocess
import sys
import time

if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
        sys.stderr.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass

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

def read_string_from_bytes(data: bytes, offset: int = 0):
    val = 0
    bytes_read = 0
    for i in range(5):
        b = data[offset + i]
        bytes_read += 1
        val |= (b & 0x7F) << (7 * i)
        if (b & 0x80) == 0:
            break
    offset += bytes_read
    s = data[offset:offset+val].decode('utf-8')
    return s, offset + val

def read_java_utf(data: bytes, offset: int = 0):
    length = struct.unpack('>H', data[offset:offset+2])[0]
    offset += 2
    s = data[offset:offset+length].decode('utf-8')
    return s, offset + length

class GatewayClient:
    def __init__(self, username: str, host_str: str = "127.0.0.1", port: int = 25567):
        self.username = username
        self.host_str = host_str
        self.port = port
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5.0)
        self.entity_id = -1
        self.uuid = ""

    def handshake_and_login(self):
        # 1. Handshake with customized host string (BungeeCord IP Forwarding payload)
        host_b = self.host_str.encode('utf-8')
        handshake = (
            encode_varint(0x00) +
            encode_varint(340) +
            encode_varint(len(host_b)) + host_b +
            struct.pack('>H', self.port) +
            encode_varint(2)  # LOGIN
        )
        self.sock.sendall(encode_varint(len(handshake)) + handshake)

        # 2. Login Start
        user_b = self.username.encode('utf-8')
        login_start = encode_varint(0x00) + encode_varint(len(user_b)) + user_b
        self.sock.sendall(encode_varint(len(login_start)) + login_start)

        # 3. Read Login Success (0x02)
        p_id, payload = read_packet(self.sock)
        if p_id != 0x02:
            raise RuntimeError(f"Expected Login Success (0x02), got 0x{p_id:02X}")
        u_len = payload[0]
        self.uuid = payload[1:1+u_len].decode('utf-8')
        offset = 1 + u_len
        n_len = payload[offset]
        name = payload[offset+1:offset+1+n_len].decode('utf-8')
        assert name == self.username, f"Username mismatch: {name} != {self.username}"

        # 4. Drain initial join packets until Player Position And Look (0x2F)
        teleport_id = None
        while True:
            p_id, payload = read_packet(self.sock)
            if p_id == 0x23:  # Join Game
                self.entity_id = struct.unpack('>i', payload[:4])[0]
            elif p_id == 0x2F:  # PPAL
                tp_id_bytes = payload[33:]
                val = 0
                for i, b in enumerate(tp_id_bytes):
                    val |= (b & 0x7F) << (7 * i)
                    if (b & 0x80) == 0:
                        break
                teleport_id = val
                break

        # 5. Send Teleport Confirm (0x00)
        tp_confirm = encode_varint(0x00) + encode_varint(teleport_id)
        self.sock.sendall(encode_varint(len(tp_confirm)) + tp_confirm)

    def send_chat(self, msg: str):
        msg_b = msg.encode('utf-8')
        body = encode_varint(0x02) + encode_varint(len(msg_b)) + msg_b
        self.sock.sendall(encode_varint(len(body)) + body)

    def send_position(self, x: float, y: float, z: float, on_ground=True):
        # 0x0E: Player Position And Look (Serverbound)
        body = (
            encode_varint(0x0E) +
            struct.pack('>ddd', x, y, z) +
            struct.pack('>ff', 0.0, 0.0) +
            struct.pack('>?', on_ground)
        )
        self.sock.sendall(encode_varint(len(body)) + body)

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass

def test_gateway_workflow():
    print("=" * 65)
    print("🚀 开始 Sprint 5: BungeeCord IP Forwarding & Portal Routing 集成测试")
    print("=" * 65)

    server_bin = os.path.abspath("bin/server.exe")
    port = 25577

    # Phase 1: Test Forwarding, /server command, and Portal AABB
    env = os.environ.copy()
    env["MCPE_PORT"] = str(port)
    env["MCPE_BUNGEE_FORWARDING"] = "true"
    env.pop("MCPE_BUNGEE_GUARD_TOKEN", None)

    print(f"[*] 启动测试服务端进程 (Port {port})...")
    proc = subprocess.Popen([server_bin], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(1.0)

    try:
        # 1. 验证 BungeeCord IP 穿透与 UUID 保留
        print("\n[*] 测试 1: 验证 BungeeCord IP Forwarding 协议穿透...")
        custom_uuid_hex = "0123456789abcdef0123456789abcdef"
        expected_uuid = "01234567-89ab-cdef-0123-456789abcdef"
        skin_json = '[{"name":"textures","value":"base64skin","signature":"sig123"}]'
        forwarded_host = f"127.0.0.1\x00198.51.100.42\x00{custom_uuid_hex}\x00{skin_json}"

        client = GatewayClient("BungeePlayer", host_str=forwarded_host, port=port)
        client.handshake_and_login()
        print(f"[+] 登录成功! 客户端接收到正版 UUID: {client.uuid}")
        assert client.uuid == expected_uuid, f"UUID 未正确穿透保留! Expected: {expected_uuid}, Got: {client.uuid}"
        print("[✓] IP Forwarding 协议穿透与正版 UUID 验证通过!")

        # 2. 验证 /server <target> 跨服跳转指令
        print("\n[*] 测试 2: 验证 /server survival 快捷指令与 0x18 PluginMessage 联动...")
        client.send_chat("/server survival")

        # 客户端等待接收 0x18 PluginMessage ("BungeeCord", "Connect", "survival")
        client.sock.settimeout(2.5)
        found_plugin_msg = False
        start_t = time.time()
        while time.time() - start_t < 2.5:
            try:
                p_id, payload = read_packet(client.sock)
            except socket.timeout:
                break
            if p_id == 0x18:
                channel, off = read_string_from_bytes(payload, 0)
                subchannel, off = read_java_utf(payload, off)
                target_server, off = read_java_utf(payload, off)
                print(f"[+] 收到 0x18 PluginMessage: channel='{channel}', sub='{subchannel}', target='{target_server}'")
                assert channel == "BungeeCord", f"Channel mismatch: {channel}"
                assert subchannel == "Connect", f"Subchannel mismatch: {subchannel}"
                assert target_server == "survival", f"Target server mismatch: {target_server}"
                found_plugin_msg = True
                break

        assert found_plugin_msg, "未收到 0x18 PluginMessage (BungeeCord:Connect)!"
        print("[✓] /server survival 指令路由验证通过!")

        # 3. 验证空间传送门 AABB 碰撞检测与自动切服
        print("\n[*] 测试 3: 验证空间传送门 AABB 碰撞检测与防抖冷却...")
        # assets/maps/lobby/lobby.ini:
        # [Portal.survival] min = -65.0, 50.0, 5.0; max = -60.0, 56.0, 11.0
        # 移入传送门坐标 (-62.5, 52.0, 8.0)
        client.send_position(-62.5, 52.0, 8.0)

        found_portal_msg = False
        start_t = time.time()
        while time.time() - start_t < 2.5:
            try:
                p_id, payload = read_packet(client.sock)
            except socket.timeout:
                break
            if p_id == 0x18:
                channel, off = read_string_from_bytes(payload, 0)
                subchannel, off = read_java_utf(payload, off)
                target_server, off = read_java_utf(payload, off)
                print(f"[+] 传送门触发 0x18 PluginMessage: channel='{channel}', target='{target_server}'")
                assert channel == "BungeeCord"
                assert subchannel == "Connect"
                assert target_server == "survival"
                found_portal_msg = True
                break

        assert found_portal_msg, "传送门未触发 0x18 PluginMessage!"
        print("[✓] 传送门 AABB 检测触发切服成功!")

        # 4. 验证防抖冷却 (Cooldown)
        print("\n[*] 测试 4: 验证传送门 3 秒防抖冷却机制...")
        client.send_position(-62.0, 52.0, 8.5)
        duplicate_triggered = False
        start_t = time.time()
        while time.time() - start_t < 1.0:
            try:
                p_id, payload = read_packet(client.sock)
                if p_id == 0x18:
                    duplicate_triggered = True
                    break
            except socket.timeout:
                break

        assert not duplicate_triggered, "传送门冷却防抖失效，发生重复触发!"
        print("[✓] 传送门防抖冷却测试通过!")

        client.close()
    finally:
        proc.terminate()
        proc.wait(timeout=3.0)

    # Phase 2: Test Bungee-Guard Token Anti-Spoofing Security
    print("\n" + "=" * 65)
    print("[*] 测试 5: 验证 Bungee-Guard 令牌密钥鉴权与防伪...")
    secret_token = "my-super-secret-token-xyz"
    env["MCPE_BUNGEE_GUARD_TOKEN"] = secret_token
    proc2 = subprocess.Popen([server_bin], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(1.0)

    try:
        # A. 伪造或未携带令牌的非法连接，必须被立即踢出或拒绝
        print("[*] 5.1: 尝试未携带 Bungee-Guard 密钥非法直连...")
        unauthorized_host = f"127.0.0.1\x00198.51.100.42\x00{custom_uuid_hex}\x00{skin_json}"
        blocked = False
        try:
            bad_client = GatewayClient("Hacker", host_str=unauthorized_host, port=port)
            bad_client.handshake_and_login()
        except (ConnectionResetError, RuntimeError, socket.timeout):
            blocked = True
        assert blocked, "未携带 Bungee-Guard 密钥的直连未被服务端拦截！"
        print("[✓] 非法直连被成功拦截断开!")

        # B. 携带正确 Bungee-Guard 令牌 (5 段式协议)
        print("[*] 5.2: 携带合法 Bungee-Guard 令牌连接...")
        authorized_host = f"127.0.0.1\x00198.51.100.42\x00{custom_uuid_hex}\x00{skin_json}\x00{secret_token}"
        good_client = GatewayClient("LegitUser", host_str=authorized_host, port=port)
        good_client.handshake_and_login()
        print(f"[+] 合法认证客户端进服成功! UUID: {good_client.uuid}")
        good_client.close()
        print("[✓] Bungee-Guard 令牌鉴权完全闭环!")

    finally:
        proc2.terminate()
        proc2.wait(timeout=3.0)

    print("\n" + "=" * 65)
    print("🎉 ALL SPRINT 5 GATEWAY & PORTAL TESTS PASSED!")
    print("=" * 65)

if __name__ == "__main__":
    test_gateway_workflow()
