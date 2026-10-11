#!/usr/bin/env python3
# scripts/test_login.py - 极速 Login & Void Spawn 进服与心跳保活验证脚本
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

def read_string(s: socket.socket) -> str:
    length = read_varint(s)
    data = b""
    while len(data) < length:
        chunk = s.recv(length - len(data))
        if not chunk:
            raise ConnectionResetError("Socket closed while reading string")
        data += chunk
    return data.decode("utf-8")

def recv_exact(s: socket.socket, n: int) -> bytes:
    buf = b""
    while len(buf) < n:
        chunk = s.recv(n - len(buf))
        if not chunk:
            raise ConnectionResetError(f"Socket closed while reading {n} bytes")
        buf += chunk
    return buf

print("[*] 正在连接本地服务端 127.0.0.1:25566 ...")
sock = socket.create_connection(('127.0.0.1', 25566), timeout=15.0)

# ==========================================
# 1. 发送 Handshake 包 (NextState = 2, LOGIN)
# ==========================================
host = b"127.0.0.1"
handshake = (
    encode_varint(0x00) +               # Packet ID = 0x00
    encode_varint(340) +                # Protocol 340 (MC 1.12.2)
    encode_varint(len(host)) + host +   # Host
    struct.pack('>H', 25566) +          # Port
    encode_varint(2)                    # NextState = 2 (LOGIN)
)
sock.sendall(encode_varint(len(handshake)) + handshake)
print("[+] 1. Handshake (NextState=2) 发送完成")

# ==========================================
# 2. 发送 Login Start 包 (Username = "Steve")
# ==========================================
username = "Steve"
user_bytes = username.encode('utf-8')
login_start = (
    encode_varint(0x00) +
    encode_varint(len(user_bytes)) + user_bytes
)
sock.sendall(encode_varint(len(login_start)) + login_start)
print(f"[+] 2. Login Start (Username='{username}') 发送完成")

# ==========================================
# 3. 接收 Login Success (0x02)
# ==========================================
p_len = read_varint(sock)
p_id = read_varint(sock)
assert p_id == 0x02, f"Expected 0x02 (Login Success), got 0x{p_id:02X}"
uuid_str = read_string(sock)
name_str = read_string(sock)
print(f"\n[✔] 3. 收到 Login Success! UUID: {uuid_str}, Name: {name_str}")
assert uuid_str == "5627dd98-e6be-3c21-b8a8-e92344183641", f"UUID 不匹配: {uuid_str}"
assert name_str == "Steve", f"玩家名不匹配: {name_str}"

# ==========================================
# 4. 接收 Join Game (0x23)
# ==========================================
p_len = read_varint(sock)
p_id = read_varint(sock)
assert p_id == 0x23, f"Expected 0x23 (Join Game), got 0x{p_id:02X}"
entity_id = struct.unpack('>i', recv_exact(sock, 4))[0]
gamemode = struct.unpack('>B', recv_exact(sock, 1))[0]
dimension = struct.unpack('>i', recv_exact(sock, 4))[0] # ⚠️ 必须是 4 字节 Int
difficulty = struct.unpack('>B', recv_exact(sock, 1))[0]
max_players = struct.unpack('>B', recv_exact(sock, 1))[0]
level_type = read_string(sock)
reduced_debug = struct.unpack('>?', recv_exact(sock, 1))[0]

print(f"\n[✔] 4. 收到 Join Game (0x23):")
print(f"    - EntityID: {entity_id}, Gamemode: {gamemode}")
print(f"    - Dimension: {dimension} (4-byte Int), Difficulty: {difficulty}")
print(f"    - LevelType: '{level_type}', ReducedDebug: {reduced_debug}")
assert dimension == 0, f"Dimension 预期为 0(主世界), 实际为 {dimension}"

# ==========================================
# 5. 接收 Player Position And Look (0x2F)
# ==========================================
while True:
    p_len = read_varint(sock)
    p_id = read_varint(sock)
    if p_id == 0x2F:
        break
    else:
        id_len = len(encode_varint(p_id))
        recv_exact(sock, p_len - id_len)
x, y, z = struct.unpack('>ddd', recv_exact(sock, 24))
yaw, pitch = struct.unpack('>ff', recv_exact(sock, 8))
flags = struct.unpack('>B', recv_exact(sock, 1))[0]
teleport_id = read_varint(sock)

print(f"\n[✔] 5. 收到 Player Position And Look (0x2F):")
print(f"    - Coordinates: ({x:.1f}, {y:.1f}, {z:.1f})")
print(f"    - Yaw: {yaw:.1f}, Pitch: {pitch:.1f}, Flags: 0x{flags:02X}")
print(f"    - Teleport ID: {teleport_id}")
assert teleport_id > 0, "Teleport ID 必须大于 0"

# ==========================================
# 6. 回传 Teleport Confirm (0x00) 与 Client Settings (0x04)
# ==========================================
tp_confirm = encode_varint(0x00) + encode_varint(teleport_id)
sock.sendall(encode_varint(len(tp_confirm)) + tp_confirm)
print(f"[+] 6. Teleport Confirm (ID={teleport_id}) 已回传")

# 模拟客户端例行包 Client Settings (0x04)
client_settings = (
    encode_varint(0x04) +
    encode_varint(len("zh_CN")) + b"zh_CN" + # locale
    struct.pack('>B', 10) +                  # view distance
    encode_varint(0) +                       # chat mode
    struct.pack('>?', True) +                # chat colors
    struct.pack('>B', 0x7F) +                # skin parts
    encode_varint(1)                         # main hand (right)
)
sock.sendall(encode_varint(len(client_settings)) + client_settings)
print("[+] 7. 模拟客户端发送例行包 Client Settings (0x04)")

# ==========================================
# 7. 心跳保活测试：等待并应答服务端的 Keep Alive (0x1F)
# ==========================================
print("\n[*] 8. 等待服务端下发周期性心跳 (最多等待 12 秒) ...")
sock.settimeout(12.0)
keep_alive_success = False

start_time = time.time()
while time.time() - start_time < 12.0:
    try:
        p_len = read_varint(sock)
        p_id = read_varint(sock)
        if p_id == 0x1F:
            ka_id = struct.unpack('>q', recv_exact(sock, 8))[0]
            print(f"[✔] 收到服务端 Keep Alive (0x1F), ID: {ka_id}")
            
            # 回传 Keep Alive (0x0B)
            echo_payload = encode_varint(0x0B) + struct.pack('>q', ka_id)
            sock.sendall(encode_varint(len(echo_payload)) + echo_payload)
            print(f"[+] Keep Alive (0x0B) 原样回送成功 (ID: {ka_id})")
            keep_alive_success = True
            break
        else:
            print(f"[i] 收到其他数据包 0x{p_id:02X}, 跳过载荷 {p_len - 1} 字节")
            recv_exact(sock, p_len - 1)
    except socket.timeout:
        break

assert keep_alive_success, "未在 12 秒内收到服务端的有效 Keep Alive (0x1F) 心跳！"

print("\n" + "=" * 55)
print("🎉 恭喜！Sprint 2 进服流水线与保活闭环 100% 验收通过！")
print("=" * 55)
sock.close()