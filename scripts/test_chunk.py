#!/usr/bin/env python3
# scripts/test_chunk.py - Sprint 3 超平坦区块下发与黄金魔数 11012 自动化验证脚本
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

def main():
    print("[*] 正在连接本地服务端 127.0.0.1:25566 ...")
    sock = socket.create_connection(('127.0.0.1', 25566), timeout=15.0)

    # 1. Handshake
    host = b"127.0.0.1"
    handshake = (
        encode_varint(0x00) +
        encode_varint(340) +
        encode_varint(len(host)) + host +
        struct.pack('>H', 25566) +
        encode_varint(2)
    )
    sock.sendall(encode_varint(len(handshake)) + handshake)
    print("[+] 1. Handshake (NextState=2) 发送完成")

    # 2. Login Start
    user_bytes = b"Steve"
    login_start = (
        encode_varint(0x00) +
        encode_varint(len(user_bytes)) + user_bytes
    )
    sock.sendall(encode_varint(len(login_start)) + login_start)
    print("[+] 2. Login Start 发送完成")

    # 3. Login Success (0x02)
    p_len = read_varint(sock)
    p_id = read_varint(sock)
    assert p_id == 0x02, f"Expected 0x02 (Login Success), got 0x{p_id:02X}"
    uuid_str = read_string(sock)
    name_str = read_string(sock)
    print(f"[✔] 3. 收到 Login Success: {name_str} ({uuid_str})")

    # 4. Join Game (0x23)
    p_len = read_varint(sock)
    p_id = read_varint(sock)
    assert p_id == 0x23, f"Expected 0x23 (Join Game), got 0x{p_id:02X}"
    entity_id = struct.unpack('>i', recv_exact(sock, 4))[0]
    gamemode = struct.unpack('>B', recv_exact(sock, 1))[0]
    dimension = struct.unpack('>i', recv_exact(sock, 4))[0]
    difficulty = struct.unpack('>B', recv_exact(sock, 1))[0]
    max_players = struct.unpack('>B', recv_exact(sock, 1))[0]
    level_type = read_string(sock)
    reduced_debug = struct.unpack('>?', recv_exact(sock, 1))[0]
    print(f"[✔] 4. 收到 Join Game: EntityID={entity_id}, Dimension={dimension}")

    # 5. 校验 5x5 区块网格 (25 个 0x20 Chunk Data 报文)
    print("[*] 5. 正在接收并校验 25 个 Chunk Data (0x20) 报文 ...")
    chunks_received = 0
    expected_chunks = {(cx, cz) for cx in range(-2, 3) for cz in range(-2, 3)}

    while chunks_received < 25:
        p_len = read_varint(sock)
        p_id = read_varint(sock)
        assert p_id == 0x20, f"Expected 0x20 (Chunk Data), got 0x{p_id:02X}"

        cx, cz = struct.unpack('>ii', recv_exact(sock, 8))
        ground_up = struct.unpack('>?', recv_exact(sock, 1))[0]
        bitmask = read_varint(sock)
        data_size = read_varint(sock)

        assert ground_up is True, f"Ground-Up 预期为 True，实际为 {ground_up}"
        assert bitmask == 0x01, f"Bitmask 预期为 0x01 (Section 0)，实际为 {bitmask}"
        assert data_size == 11012, f"Data Size 严格预期为黄金魔数 11012，实际为 {data_size}"

        payload = recv_exact(sock, data_size)
        block_entities = read_varint(sock)
        assert block_entities == 0, f"Block Entities 预期为 0，实际为 {block_entities}"

        chunks_received += 1
        expected_chunks.discard((cx, cz))
        print(f"    - [{chunks_received:02d}/25] Chunk ({cx:+d}, {cz:+d}) 校验通过! (Payload: {data_size} bytes)")

    assert len(expected_chunks) == 0, f"未接收全所有视距区块: 缺失 {expected_chunks}"
    print("[✔] 25 个超平坦视距区块全部接收完毕并校验通过！")

    # 6. 接收 Player Position And Look (0x2F)
    p_len = read_varint(sock)
    p_id = read_varint(sock)
    assert p_id == 0x2F, f"Expected 0x2F (PPAL), got 0x{p_id:02X}"
    x, y, z = struct.unpack('>ddd', recv_exact(sock, 24))
    yaw, pitch = struct.unpack('>ff', recv_exact(sock, 8))
    flags = struct.unpack('>B', recv_exact(sock, 1))[0]
    teleport_id = read_varint(sock)
    print(f"[✔] 6. 收到 Player Position And Look: ({x:.1f}, {y:.1f}, {z:.1f}), TeleportID: {teleport_id}")

    # 回送 Teleport Confirm
    tp_confirm = encode_varint(0x00) + encode_varint(teleport_id)
    sock.sendall(encode_varint(len(tp_confirm)) + tp_confirm)
    print(f"[+] 7. Teleport Confirm (ID={teleport_id}) 回送完成")

    print("\n" + "=" * 55)
    print("🎉 恭喜！Sprint 3 ChunkSerializer & 区块分发 100% 验收通过！")
    print("=" * 55)
    sock.close()

if __name__ == '__main__':
    main()
