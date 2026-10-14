#!/usr/bin/env python3
"""
scripts/benchmark_swarm.py - Minecraft 1.12.2 (Protocol 340) 高并发异步发压器

定位与设计：
1. 纯 Python 3 asyncio 异步协程池驱动，单机轻松支撑 1,000 ~ 3,000+ 轻量虚拟玩家连接；
2. 跨平台支持 (Linux / Windows / macOS)，支持压测本地或云端任意实现 1.12.2 协议的服务端 (MCPE / Cuberite / Paper)；
3. 支持三大核心测试场景：
   - limbo: 挂机/排队大厅模式 (连接就绪后仅维持心跳，测量纯连接与内存扩展曲线)；
   - crowd: 同屏高密度跑动模式 (周期发送微小位移广播，激发 O(N^2) / 局部九宫格分发压力)；
   - storm: 快速连接重连风暴模式 (短时连接并断开，测试 Acceptor 与握手管道峰值承载)；
4. 实时动态输出 PPS、吞吐带宽、在线水位与 KeepAlive P50/P95/P99 响应延迟指标。
"""

import argparse
import asyncio
import collections
import math
import os
import random
import struct
import sys
import time

if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
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


def decode_varint(data: bytes, offset: int = 0) -> tuple[int, int]:
    val = 0
    bytes_read = 0
    for i in range(5):
        if offset + bytes_read >= len(data):
            raise ValueError("Buffer underflow while decoding VarInt")
        b = data[offset + bytes_read]
        bytes_read += 1
        val |= (b & 0x7F) << (7 * i)
        if (b & 0x80) == 0:
            return val, bytes_read
    raise ValueError("VarInt exceeds 5 bytes")


async def read_varint_from_stream(reader: asyncio.StreamReader) -> int:
    val = 0
    for i in range(5):
        chunk = await reader.readexactly(1)
        b = chunk[0]
        val |= (b & 0x7F) << (7 * i)
        if (b & 0x80) == 0:
            return val
    raise ValueError("VarInt exceeds 5 bytes")


def encode_string(s: str) -> bytes:
    encoded = s.encode("utf-8")
    return encode_varint(len(encoded)) + encoded


class SwarmMetrics:
    def __init__(self):
        self.connected_bots = 0
        self.peak_connected_bots = 0
        self.spawned_bots = 0
        self.total_spawned_bots = 0
        self.login_failed = 0
        self.disconnected_bots = 0
        self.total_packets_rx = 0
        self.total_packets_tx = 0
        self.total_bytes_rx = 0
        self.total_bytes_tx = 0
        self.broadcast_move_rx = 0
        self.chat_rx = 0
        self.keep_alive_latencies = collections.deque(maxlen=2000)

        # 速率计算采样窗口
        self.last_sample_time = time.time()
        self.last_bytes_rx = 0
        self.last_bytes_tx = 0
        self.last_packets_rx = 0
        self.last_packets_tx = 0

    def record_keep_alive(self, latency_ms: float):
        self.keep_alive_latencies.append(latency_ms)

    def get_latency_percentiles(self) -> tuple[float, float, float, float]:
        if not self.keep_alive_latencies:
            return 0.0, 0.0, 0.0, 0.0
        sorted_lat = sorted(self.keep_alive_latencies)
        count = len(sorted_lat)
        avg = sum(sorted_lat) / count
        p50 = sorted_lat[int(count * 0.50)]
        p95 = sorted_lat[min(count - 1, int(count * 0.95))]
        p99 = sorted_lat[min(count - 1, int(count * 0.99))]
        return avg, p50, p95, p99

    def sample_rates(self) -> tuple[float, float, float, float]:
        now = time.time()
        dt = max(now - self.last_sample_time, 0.001)
        rx_pps = (self.total_packets_rx - self.last_packets_rx) / dt
        tx_pps = (self.total_packets_tx - self.last_packets_tx) / dt
        rx_kbs = (self.total_bytes_rx - self.last_bytes_rx) / (1024.0 * dt)
        tx_kbs = (self.total_bytes_tx - self.last_bytes_tx) / (1024.0 * dt)

        self.last_sample_time = now
        self.last_packets_rx = self.total_packets_rx
        self.last_packets_tx = self.total_packets_tx
        self.last_bytes_rx = self.total_bytes_rx
        self.last_bytes_tx = self.total_bytes_tx

        return rx_pps, tx_pps, rx_kbs, tx_kbs


class BotClient:
    def __init__(
        self,
        bot_id: int,
        host: str,
        port: int,
        metrics: SwarmMetrics,
        mode: str = "limbo",
        move_rate_hz: float = 10.0,
        stay_seconds: float = 3.0,
    ):
        self.bot_id = bot_id
        self.username = f"Bot_{bot_id:04d}"
        self.host = host
        self.port = port
        self.metrics = metrics
        self.mode = mode
        self.move_rate_hz = move_rate_hz
        self.stay_seconds = stay_seconds

        self.reader: asyncio.StreamReader | None = None
        self.writer: asyncio.StreamWriter | None = None
        self.entity_id = -1
        self.pos = (48.5, 34.0, 48.5)
        self.yaw = 0.0
        self.pitch = 0.0
        self.is_spawned = False
        self.is_running = False
        self.tasks: list[asyncio.Task] = []
        self.last_keepalive_sent_time = 0.0

    async def run(self):
        self.is_running = True
        try:
            self.reader, self.writer = await asyncio.open_connection(self.host, self.port)
            self.metrics.connected_bots += 1
            self.metrics.peak_connected_bots = max(self.metrics.peak_connected_bots, self.metrics.connected_bots)

            # 1. 发送握手包 (Handshake, Packet ID 0x00, State 2: Login)
            host_b = self.host.encode("utf-8")
            handshake = (
                encode_varint(0x00)
                + encode_varint(340)  # Protocol 340 (1.12.2)
                + encode_varint(len(host_b))
                + host_b
                + struct.pack(">H", self.port)
                + encode_varint(2)  # Next State: 2 (Login)
            )
            await self._send_packet(handshake)

            # 2. 发送登录请求 (Login Start, Packet ID 0x00)
            user_b = self.username.encode("utf-8")
            login_start = encode_varint(0x00) + encode_varint(len(user_b)) + user_b
            await self._send_packet(login_start)

            # 3. 运行接收包主循环
            if self.mode == "storm":
                # 持续模式：连接并在 stay_seconds 后主动断开
                asyncio.create_task(self._storm_timer())

            await self._packet_loop()

        except (asyncio.CancelledError, ConnectionResetError, BrokenPipeError):
            pass
        except Exception:
            self.metrics.login_failed += 1
        finally:
            await self.close()

    async def _storm_timer(self):
        await asyncio.sleep(self.stay_seconds)
        await self.close()

    async def _send_packet(self, body: bytes):
        if not self.writer or self.writer.is_closing():
            return
        frame = encode_varint(len(body)) + body
        self.writer.write(frame)
        await self.writer.drain()
        self.metrics.total_packets_tx += 1
        self.metrics.total_bytes_tx += len(frame)

    async def _packet_loop(self):
        while self.is_running and self.reader and not self.reader.at_eof():
            try:
                pkt_len = await read_varint_from_stream(self.reader)
                if pkt_len <= 0 or pkt_len > 2097152:  # 2MB 防护上限
                    break
                payload = await self.reader.readexactly(pkt_len)
                self.metrics.total_packets_rx += 1
                self.metrics.total_bytes_rx += pkt_len + len(encode_varint(pkt_len))

                pid, id_len = decode_varint(payload, 0)
                body = payload[id_len:]

                await self._handle_packet(pid, body)
            except (asyncio.IncompleteReadError, ConnectionResetError):
                break

    async def _handle_packet(self, pid: int, body: bytes):
        # 0x02: Login Success
        if pid == 0x02 and not self.is_spawned:
            pass

        # 0x23: Join Game (Play 状态)
        elif pid == 0x23:
            if len(body) >= 4:
                self.entity_id = struct.unpack(">i", body[:4])[0]

        # 0x2F: Player Position And Look (客户端出生 / 传送坐标锁定)
        elif pid == 0x2F:
            if len(body) >= 33:
                x, y, z = struct.unpack(">ddd", body[0:24])
                self.pos = (x, y, z)
                yaw, pitch = struct.unpack(">ff", body[24:32])
                self.yaw, self.pitch = yaw, pitch
                tp_id_bytes = body[33:]
                tp_id, _ = decode_varint(tp_id_bytes, 0)

                # 回复 0x00 Teleport Confirm
                tp_confirm = encode_varint(0x00) + encode_varint(tp_id)
                await self._send_packet(tp_confirm)

                if not self.is_spawned:
                    self.is_spawned = True
                    self.metrics.spawned_bots += 1
                    self.metrics.total_spawned_bots += 1

                    # 如果是高密度跑动模式，开启主动位置更新协程
                    if self.mode == "crowd":
                        self.tasks.append(asyncio.create_task(self._crowd_movement_loop()))

        # 0x1F: Keep Alive (Play 状态心跳包，1.12.2 为 8 字节 int64)
        elif pid == 0x1F:
            if len(body) >= 8:
                keepalive_id = struct.unpack(">q", body[:8])[0]
                now = time.time()
                if self.last_keepalive_sent_time > 0:
                    rtt_ms = (now - self.last_keepalive_sent_time) * 1000.0
                    self.metrics.record_keep_alive(rtt_ms)

                self.last_keepalive_sent_time = now
                # 回显应答 0x0B (Serverbound Keep Alive: ID + int64)
                ack = encode_varint(0x0B) + struct.pack(">q", keepalive_id)
                await self._send_packet(ack)

        # 0x26 / 0x27 / 0x4C: 位移广播包
        elif pid in (0x26, 0x27, 0x4C):
            self.metrics.broadcast_move_rx += 1

        # 0x0F: Chat Message
        elif pid == 0x0F:
            self.metrics.chat_rx += 1

    async def _crowd_movement_loop(self):
        delay = 1.0 / max(self.move_rate_hz, 1.0)
        step = 0
        base_x, base_y, base_z = self.pos

        while self.is_running and self.is_spawned:
            await asyncio.sleep(delay)
            step += 1

            # 模拟在原地围绕 1~2 格半径缓慢转圈漫游
            angle = (step % 360) * (math.pi / 180.0)
            cur_x = base_x + 1.2 * math.cos(angle)
            cur_z = base_z + 1.2 * math.sin(angle)
            cur_yaw = float((step * 10) % 360)

            # 0x0E: Player Position And Look (Serverbound)
            body = (
                encode_varint(0x0E)
                + struct.pack(">ddd", cur_x, base_y, cur_z)
                + struct.pack(">ff", cur_yaw, 0.0)
                + struct.pack(">?", True)  # on_ground
            )
            await self._send_packet(body)

    async def close(self):
        if not self.is_running:
            return
        self.is_running = False
        for t in self.tasks:
            t.cancel()
        if self.is_spawned:
            self.metrics.spawned_bots = max(0, self.metrics.spawned_bots - 1)
        self.metrics.connected_bots = max(0, self.metrics.connected_bots - 1)
        self.metrics.disconnected_bots += 1

        if self.writer:
            try:
                self.writer.close()
                await self.writer.wait_closed()
            except Exception:
                pass
            self.writer = None


class SwarmOrchestrator:
    def __init__(self, args):
        self.args = args
        self.metrics = SwarmMetrics()
        self.bots: list[BotClient] = []
        self.bot_tasks: list[asyncio.Task] = []
        self.running = True

    async def run(self):
        print("=" * 70)
        print("  MCPE 高性能 Minecraft 1.12.2 (Protocol 340) 异步集群发压套件")
        print("=" * 70)
        print(f" • 目标服务器   : {self.args.host}:{self.args.port}")
        print(f" • 并发目标人数 : {self.args.bots} Bots")
        print(f" • 发压测试模式 : {self.args.mode.upper()}")
        print(f" • 梯级接入速率 : {self.args.rate} bots/s")
        print(f" • 压测持续时长 : {self.args.duration} s")
        if self.args.mode == "crowd":
            print(f" • 移动发包频率 : {self.args.move_hz} Hz")
        print("=" * 70)

        # 启动状态监控大盘定时器
        monitor_task = asyncio.create_task(self._dashboard_loop())

        # 启动 Bot 并发爬坡接入
        launcher_task = asyncio.create_task(self._launch_bots())

        try:
            await asyncio.sleep(self.args.duration)
        except asyncio.CancelledError:
            pass
        finally:
            self.running = False
            launcher_task.cancel()
            monitor_task.cancel()
            print("\n\n[*] 正在平稳断开所有虚拟机器人连接...")
            for b in self.bots:
                await b.close()
            print("[+] 全部连接已释放，正在生成最终测评报告...\n")
            self._print_final_summary()

    async def _launch_bots(self):
        interval = 1.0 / max(self.args.rate, 1.0)
        for i in range(self.args.bots):
            if not self.running:
                break
            bot = BotClient(
                bot_id=i + 1,
                host=self.args.host,
                port=self.args.port,
                metrics=self.metrics,
                mode=self.args.mode,
                move_rate_hz=self.args.move_hz,
                stay_seconds=self.args.stay,
            )
            self.bots.append(bot)
            t = asyncio.create_task(bot.run())
            self.bot_tasks.append(t)
            await asyncio.sleep(interval)

    async def _dashboard_loop(self):
        start_time = time.time()
        while self.running:
            await asyncio.sleep(1.0)
            elapsed = time.time() - start_time
            rx_pps, tx_pps, rx_kbs, tx_kbs = self.metrics.sample_rates()
            avg_lat, p50_lat, p95_lat, p99_lat = self.metrics.get_latency_percentiles()

            status_line = (
                f"\r[{elapsed:04.0f}s/{self.args.duration}s] "
                f"在线: {self.metrics.connected_bots:4d}/{self.args.bots:4d} | "
                f"就绪: {self.metrics.spawned_bots:4d} | "
                f"入站: {rx_pps:6.0f} pps ({rx_kbs/1024.0:4.2f} MB/s) | "
                f"出站: {tx_pps:5.0f} pps ({tx_kbs:5.1f} KB/s) | "
                f"心跳P95: {p95_lat:4.1f}ms"
            )
            sys.stdout.write(status_line)
            sys.stdout.flush()

    def _print_final_summary(self):
        avg_lat, p50_lat, p95_lat, p99_lat = self.metrics.get_latency_percentiles()
        print("=" * 70)
        print("                  压测执行结果综合指标大盘")
        print("=" * 70)
        print(f" • 峰值并发在线连接数 : {self.metrics.peak_connected_bots}")
        print(f" • 累计入服就绪人数   : {self.metrics.total_spawned_bots}")
        print(f" • 登录异常/失败次数  : {self.metrics.login_failed}")
        print(f" • 正常断开/下线总数  : {self.metrics.disconnected_bots}")
        print("-" * 70)
        print(f" • 累计下行数据量(Rx) : {self.metrics.total_bytes_rx / (1024*1024):.2f} MB ({self.metrics.total_packets_rx} pkts)")
        print(f" • 累计上行数据量(Tx) : {self.metrics.total_bytes_tx / (1024*1024):.2f} MB ({self.metrics.total_packets_tx} pkts)")
        print(f" • 接收移动广播包数   : {self.metrics.broadcast_move_rx}")
        print(f" • 接收聊天多播包数   : {self.metrics.chat_rx}")
        print("-" * 70)
        print(f" • 心跳 RTT 平均时延  : {avg_lat:.2f} ms")
        print(f" • 心跳 RTT P50 中位值: {p50_lat:.2f} ms")
        print(f" • 心跳 RTT P95 统计线: {p95_lat:.2f} ms")
        print(f" • 心跳 RTT P99 统计线: {p99_lat:.2f} ms")
        print("=" * 70)


def main():
    parser = argparse.ArgumentParser(description="MCPE 异步高并发 Minecraft 1.12.2 协议发压器")
    parser.add_argument("--host", type=str, default="127.0.0.1", help="目标服务器 IP 或域名 (默认: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=25566, help="目标服务器端口 (MCPE默认: 25566, Cuberite/Paper默认: 25565)")
    parser.add_argument("--bots", type=int, default=100, help="目标并发 Bot 数量 (默认: 100)")
    parser.add_argument("--rate", type=float, default=20.0, help="Bots 梯级接入速率 (个/秒, 默认: 20)")
    parser.add_argument("--mode", type=str, choices=["limbo", "crowd", "storm"], default="limbo", help="测试模式: limbo(纯心跳)/crowd(同屏跑动)/storm(快速重连)")
    parser.add_argument("--move-hz", type=float, default=10.0, help="crowd 模式下移动发包频率 (Hz, 默认: 10)")
    parser.add_argument("--stay", type=float, default=3.0, help="storm 模式下连接驻留秒数 (默认: 3.0)")
    parser.add_argument("--duration", type=int, default=30, help="压测维持运行秒数 (默认: 30)")

    args = parser.parse_args()

    orchestrator = SwarmOrchestrator(args)
    try:
        asyncio.run(orchestrator.run())
    except KeyboardInterrupt:
        print("\n[!] 用户中断压测任务。")


if __name__ == "__main__":
    main()
