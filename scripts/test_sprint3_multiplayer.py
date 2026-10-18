#!/usr/bin/env python3
"""
scripts/test_sprint3_multiplayer.py - Sprint 3 多客户端移动同步、聊天多播与同屏可见性集成测试
"""

import socket
import struct
import time
import sys

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

def read_packet(s: socket.socket):
    p_len = read_varint(s)
    p_id = read_varint(s)
    # 计算剩余载荷长度 (注意 p_len 包含 p_id 的字节长度)
    id_bytes_len = len(encode_varint(p_id))
    payload_len = p_len - id_bytes_len
    payload = recv_exact(s, payload_len) if payload_len > 0 else b""
    return p_id, payload

class MockClient:
    def __init__(self, username: str, host="127.0.0.1", port=25566):
        self.username = username
        self.host = host
        self.port = port
        self.sock = socket.create_connection((host, port), timeout=5.0)
        self.entity_id = -1
        self.uuid = ""
        self.spawn_pos = (0.0, 0.0, 0.0)

    def login_and_spawn(self):
        # 1. Handshake
        host_b = self.host.encode('utf-8')
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
        assert p_id == 0x02, f"Expected Login Success (0x02), got 0x{p_id:02X}"
        offset = 0
        u_len = payload[0]  # VarInt len
        self.uuid = payload[1:1+u_len].decode('utf-8')
        offset = 1 + u_len
        n_len = payload[offset]
        name = payload[offset+1:offset+1+n_len].decode('utf-8')
        assert name == self.username

        # 4. Drain initial join packets until PPAL (0x2F)
        teleport_id = None
        while True:
            p_id, payload = read_packet(self.sock)
            if p_id == 0x23:  # Join Game
                self.entity_id = struct.unpack('>i', payload[:4])[0]
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

        # 5. Send Teleport Confirm
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
        # 0x02: Chat Message (Serverbound)
        msg_b = msg.encode('utf-8')
        body = encode_varint(0x02) + encode_varint(len(msg_b)) + msg_b
        self.sock.sendall(encode_varint(len(body)) + body)

    def close(self):
        self.sock.close()

def main():
    print("=" * 60)
    print("🚀 开始 Sprint 3 多人实时互通端到端测试...")
    print("=" * 60)

    # 1. 启动 Steve 客户端进入大厅
    print("[*] 正在连接客户端 A (Steve)...")
    steve = MockClient("Steve")
    steve.login_and_spawn()
    print(f"[+] Steve 进服成功! EID: {steve.entity_id}, UUID: {steve.uuid}")

    # 给主循环 1 Tick 消化
    time.sleep(0.1)

    # 2. 启动 Alex 客户端进入大厅
    print("[*] 正在连接客户端 B (Alex)...")
    alex = MockClient("Alex")
    alex.login_and_spawn()
    print(f"[+] Alex 进服成功! EID: {alex.entity_id}, UUID: {alex.uuid}")
    assert steve.entity_id != alex.entity_id, "EID 必须全局自增唯一且互不冲突！"

    # 3. 验证同屏可见性时序
    # Steve 必须收到 Alex 的 0x2E (Player List Item) 与 0x05 (Spawn Player)
    print("\n[*] 验证阶段 1: 同屏可见性互见时序...")
    steve.sock.settimeout(2.0)
    steve_saw_alex_list = False
    steve_saw_alex_spawn = False

    start = time.time()
    while time.time() - start < 2.0:
        try:
            p_id, payload = read_packet(steve.sock)
            if p_id == 0x2E:
                steve_saw_alex_list = True
                print("[✔] Steve 收到 Alex 的 0x2E (Player List Item)")
            elif p_id == 0x05:
                # 检查 EID
                eid = struct.unpack('>B', payload[0:1])[0]  # VarInt (单字节)
                if eid == alex.entity_id:
                    steve_saw_alex_spawn = True
                    print(f"[✔] Steve 收到 Alex 的 0x05 (Spawn Player, EID={eid})")
            if steve_saw_alex_list and steve_saw_alex_spawn:
                break
        except socket.timeout:
            break

    assert steve_saw_alex_list, "Steve 未收到 Alex 的 Player List Item (0x2E)！"
    assert steve_saw_alex_spawn, "Steve 未收到 Alex 的 Spawn Player (0x05)！"

    # 4. 验证移动差分同步 (Steve 跑动，Alex 接收位移)
    print("\n[*] 验证阶段 2: 玩家移动差分广播 (0x26 / 0x27)...")
    alex.sock.settimeout(2.0)
    sx, sy, sz = steve.spawn_pos
    steve.send_move_and_look(sx + 1.0, sy, sz, 90.0, 0.0, on_ground=True)

    alex_saw_move = False
    start = time.time()
    while time.time() - start < 2.0:
        try:
            p_id, payload = read_packet(alex.sock)
            if p_id in (0x26, 0x27):
                eid = payload[0]
                if eid == steve.entity_id:
                    alex_saw_move = True
                    pkt_name = "0x27 (Look & Rel Move)" if p_id == 0x27 else "0x26 (Rel Move)"
                    print(f"[✔] Alex 屏幕收到 Steve (EID={eid}) 的位移广播: {pkt_name}")
                    break
        except socket.timeout:
            break

    assert alex_saw_move, "Alex 未能在 2 秒内收到 Steve 的移动差分广播！"

    # 5. 验证公屏聊天多播广播
    print("\n[*] 验证阶段 3: 公屏聊天广播...")
    steve.send_chat("Hello from Steve!")
    
    alex_saw_chat = False
    start = time.time()
    while time.time() - start < 2.0:
        try:
            p_id, payload = read_packet(alex.sock)
            if p_id == 0x0F:
                # payload: VarInt str_len + str + byte pos
                chat_json = payload[1:-1].decode('utf-8', errors='ignore')
                if "<Steve> Hello from Steve!" in chat_json:
                    alex_saw_chat = True
                    print(f"[✔] Alex 收到公屏聊天 (0x0F): {chat_json}")
                    break
        except socket.timeout:
            break

    assert alex_saw_chat, "Alex 未收到 Steve 的公屏聊天！"

    # 6. 验证快捷指令分流 (Steve 发送 /lobbies，仅 Steve 收到，Alex 不受干扰)
    print("\n[*] 验证阶段 4: 快捷指令拦截与单播分流...")
    steve.send_chat("/lobbies")

    steve_saw_cmd = False
    start = time.time()
    while time.time() - start < 2.0:
        try:
            p_id, payload = read_packet(steve.sock)
            if p_id == 0x0F:
                chat_json = payload[1:-1].decode('utf-8', errors='ignore')
                if "大厅分线" in chat_json:
                    steve_saw_cmd = True
                    print(f"[✔] Steve 收到指令单播回复 (0x0F): {chat_json}")
                    break
        except socket.timeout:
            break

    assert steve_saw_cmd, "Steve 未收到 /lobbies 指令的系统回复！"

    # 6.5. 验证 /fly 临时飞行模式开启与关闭
    print("\n[*] 验证阶段 4.5: /fly 飞行权限与能力包 (0x2C) 切换验证...")
    steve.send_chat("/fly")

    steve_got_fly_packet = False
    steve_got_fly_msg = False
    start = time.time()
    while time.time() - start < 2.0:
        try:
            p_id, payload = read_packet(steve.sock)
            if p_id == 0x2C:  # Player Abilities
                flags = payload[0]
                if flags & 0x04:  # Allow Flying
                    steve_got_fly_packet = True
                    print(f"[✔] Steve 收到 0x2C Player Abilities 飞行能力包: flags=0x{flags:02X}")
            elif p_id == 0x0F:
                chat_json = payload[1:-1].decode('utf-8', errors='ignore')
                if "飞行模式已开启" in chat_json:
                    steve_got_fly_msg = True
                    print(f"[✔] Steve 收到飞行开启系统提示: {chat_json}")
            if steve_got_fly_packet and steve_got_fly_msg:
                break
        except socket.timeout:
            break

    assert steve_got_fly_packet, "Steve 未收到 0x2C 开启飞行能力数据包！"
    assert steve_got_fly_msg, "Steve 未收到飞行模式已开启系统提示！"

    # 关闭飞行
    steve.send_chat("/fly off")
    steve_got_fly_off = False
    start = time.time()
    while time.time() - start < 2.0:
        try:
            p_id, payload = read_packet(steve.sock)
            if p_id == 0x2C:
                flags = payload[0]
                if not (flags & 0x04):
                    steve_got_fly_off = True
                    print(f"[✔] Steve 收到 0x2C 关闭飞行能力包: flags=0x{flags:02X}")
                    break
        except socket.timeout:
            break
    assert steve_got_fly_off, "Steve 未收到 0x2C 关闭飞行能力数据包！"

    # 6.6. 验证 /setspawn 与 /spawn 出生点重设与传送
    print("\n[*] 验证阶段 4.6: /setspawn 与 /spawn 出生点更新与传送验证...")
    # Steve 移动至目标出生点 (-56.0, 52.0, 8.0)
    steve.send_move_and_look(-56.0, 52.0, 8.0, 90.0, 0.0)
    time.sleep(0.05)
    steve.send_chat("/setspawn")

    steve_got_setspawn_msg = False
    start = time.time()
    while time.time() - start < 2.0:
        try:
            p_id, payload = read_packet(steve.sock)
            if p_id == 0x0F:
                chat_json = payload[1:-1].decode('utf-8', errors='ignore')
                if "成功将大厅出生点更新为" in chat_json:
                    steve_got_setspawn_msg = True
                    print(f"[✔] Steve 收到 /setspawn 成功设置回显: {chat_json}")
                    break
        except socket.timeout:
            break
    assert steve_got_setspawn_msg, "Steve 未收到 /setspawn 成功设置回显！"

    # Steve 移动走
    steve.send_move_and_look(0.0, 60.0, 0.0, 0.0, 0.0)
    time.sleep(0.05)

    # Steve 执行 /spawn 传送回出生点
    steve.send_chat("/spawn")
    steve_got_teleport = False
    steve_got_spawn_msg = False
    start = time.time()
    while time.time() - start < 2.0:
        try:
            p_id, payload = read_packet(steve.sock)
            if p_id == 0x2F:  # Player Position And Look
                tp_x, tp_y, tp_z = struct.unpack('>ddd', payload[0:24])
                if abs(tp_x - (-56.0)) < 0.1 and abs(tp_y - 52.0) < 0.1 and abs(tp_z - 8.0) < 0.1:
                    steve_got_teleport = True
                    print(f"[✔] Steve 收到 0x2F 传送回大厅出生点 ({tp_x:.1f}, {tp_y:.1f}, {tp_z:.1f})")
            elif p_id == 0x0F:
                chat_json = payload[1:-1].decode('utf-8', errors='ignore')
                if "已传送回大厅出生点" in chat_json:
                    steve_got_spawn_msg = True
                    print(f"[✔] Steve 收到 /spawn 传送成功提示: {chat_json}")
            if steve_got_teleport and steve_got_spawn_msg:
                break
        except socket.timeout:
            break
    assert steve_got_teleport, "Steve 未收到 0x2F 传送回出生点数据包！"
    assert steve_got_spawn_msg, "Steve 未收到 /spawn 成功提示！"

    # 7. 验证退服销毁广播
    print("\n[*] 验证阶段 5: 玩家退服实体销毁广播 (0x32)...")
    steve.close()

    alex_saw_destroy = False
    start = time.time()
    while time.time() - start < 2.0:
        try:
            p_id, payload = read_packet(alex.sock)
            if p_id == 0x32:  # Destroy Entities
                # count: VarInt, eid: VarInt
                count = payload[0]
                eid = payload[1]
                if eid == steve.entity_id:
                    alex_saw_destroy = True
                    print(f"[✔] Alex 收到销毁实体广播 0x32: count={count}, EID={eid}")
                    break
        except socket.timeout:
            break

    assert alex_saw_destroy, "Alex 未能在 Steve 断开后收到 0x32 Destroy Entities 广播！"

    alex.close()

    print("\n" + "=" * 60)
    print("🎉 恭喜！Sprint 3 第三阶段全部功能 100% 验收通过！")
    print("   1. 双向进服可见性时序 (0x2E -> 0x05) 正常")
    print("   2. 20 TPS 物理节拍器与定点数相对位移广播 (0x26/0x27) 正常")
    print("   3. 聊天消息富文本 JSON 多播广播正常")
    print("   4. 快捷指令 /lobbies 分流单播正常")
    print("   5. 玩家退服实时实体销毁 (0x32) 正常")
    print("=" * 60)

if __name__ == "__main__":
    main()
