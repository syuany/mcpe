#!/usr/bin/env python3
"""
scripts/monitor_collector.py - 跨平台轻量级进程级资源采样器与监控器

功能定位：
1. 监控指定被测服务端（MCPE / Cuberite / Paper）进程的物理硬件资源消耗；
2. 采样指标：CPU 占用率 (%)、物理驻留内存 (RSS, MB)、虚拟内存 (VMS, MB)、线程数、网络长连接数；
3. 输出格式：终端实时大盘显示 + 自动导出 CSV 详细时间序列数据，便于生成性能对照图表；
4. 零外部依赖降级：Linux 下若未安装 psutil，自动无缝降级读取 /proc/[pid]/stat 原生系统接口。
"""

import argparse
import csv
import os
import sys
import time

if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    except Exception:
        pass

try:
    import psutil
    HAS_PSUTIL = True
except ImportError:
    HAS_PSUTIL = False


class ProcessMonitor:
    def __init__(self, pid: int | None = None, name_pattern: str | None = None, output_csv: str | None = None, interval: float = 1.0):
        self.interval = interval
        self.output_csv = output_csv
        self.pid = pid
        self.name_pattern = name_pattern
        self.process = None

        # 采样聚合统计
        self.samples = []
        self.peak_rss_mb = 0.0
        self.peak_cpu_pct = 0.0
        self.sum_cpu = 0.0

        self._find_target_process()

    def _find_target_process(self):
        if self.pid is not None:
            if HAS_PSUTIL:
                try:
                    self.process = psutil.Process(self.pid)
                    return
                except psutil.NoSuchProcess:
                    print(f"[!] 错误: 未找到 PID={self.pid} 的进程。")
                    sys.exit(1)
            else:
                if not os.path.exists(f"/proc/{self.pid}"):
                    print(f"[!] 错误: /proc/{self.pid} 不存在。")
                    sys.exit(1)
                return

        if not self.name_pattern:
            print("[!] 错误: 必须指定 --pid 或 --name 其中之一。")
            sys.exit(1)

        if HAS_PSUTIL:
            for p in psutil.process_iter(["pid", "name", "cmdline"]):
                try:
                    name = p.info["name"] or ""
                    cmd = " ".join(p.info["cmdline"] or [])
                    if self.name_pattern.lower() in name.lower() or self.name_pattern.lower() in cmd.lower():
                        self.pid = p.pid
                        self.process = p
                        print(f"[+] 成功匹配目标进程: '{name}' (PID: {self.pid})")
                        return
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    continue
        else:
            # Linux 原生 /proc 扫描
            for entry in os.listdir("/proc"):
                if entry.isdigit():
                    pid = int(entry)
                    cmd_path = f"/proc/{pid}/cmdline"
                    if os.path.exists(cmd_path):
                        try:
                            with open(cmd_path, "rb") as f:
                                cmd = f.read().replace(b"\x00", b" ").decode("utf-8", errors="ignore")
                                if self.name_pattern.lower() in cmd.lower():
                                    self.pid = pid
                                    print(f"[+] 成功匹配目标进程: '{cmd.strip()[:40]}' (PID: {self.pid})")
                                    return
                        except Exception:
                            continue

        print(f"[!] 错误: 未找到包含匹配词 '{self.name_pattern}' 的运行中进程。")
        sys.exit(1)

    def _sample_psutil(self) -> dict:
        try:
            with self.process.oneshot():
                cpu = self.process.cpu_percent(interval=None)
                mem = self.process.memory_info()
                rss_mb = mem.rss / (1024 * 1024)
                vms_mb = mem.vms / (1024 * 1024)
                threads = self.process.num_threads()
                try:
                    conns = len(self.process.connections(kind="tcp"))
                except Exception:
                    conns = 0
            return {
                "cpu": cpu,
                "rss_mb": rss_mb,
                "vms_mb": vms_mb,
                "threads": threads,
                "conns": conns,
            }
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            return None

    def _sample_proc_linux(self) -> dict:
        status_path = f"/proc/{self.pid}/status"
        if not os.path.exists(status_path):
            return None
        rss_kb = 0
        vms_kb = 0
        threads = 1
        try:
            with open(status_path, "r") as f:
                for line in f:
                    if line.startswith("VmRSS:"):
                        rss_kb = int(line.split()[1])
                    elif line.startswith("VmSize:"):
                        vms_kb = int(line.split()[1])
                    elif line.startswith("Threads:"):
                        threads = int(line.split()[1])
        except Exception:
            return None

        # 连接数统计
        conns = 0
        fd_path = f"/proc/{self.pid}/fd"
        if os.path.exists(fd_path):
            try:
                conns = len(os.listdir(fd_path))
            except Exception:
                pass

        return {
            "cpu": 0.0,  # 简化采样
            "rss_mb": rss_kb / 1024.0,
            "vms_mb": vms_kb / 1024.0,
            "threads": threads,
            "conns": conns,
        }

    def sample_once(self) -> dict:
        if HAS_PSUTIL and self.process:
            return self._sample_psutil()
        else:
            return self._sample_proc_linux()

    def run(self, duration: float = 0.0):
        print("=" * 65)
        print("  MCPE 进程硬件资源实时采样监视器 (Process Monitor)")
        print("=" * 65)
        print(f" • 目标进程 PID  : {self.pid}")
        if self.output_csv:
            print(f" • 输出数据文件  : {self.output_csv}")
        print(f" • 采样间隔周期  : {self.interval} s")
        print("=" * 65)

        csv_file = None
        csv_writer = None
        if self.output_csv:
            os.makedirs(os.path.dirname(os.path.abspath(self.output_csv)), exist_ok=True)
            csv_file = open(self.output_csv, "w", newline="", encoding="utf-8")
            csv_writer = csv.writer(csv_file)
            csv_writer.writerow(["timestamp", "elapsed_s", "cpu_percent", "rss_mb", "vms_mb", "threads", "connections"])

        start_time = time.time()
        try:
            # 首次触发 cpu 计算预热
            if HAS_PSUTIL and self.process:
                self.process.cpu_percent(interval=None)

            while True:
                time.sleep(self.interval)
                now = time.time()
                elapsed = now - start_time

                metric = self.sample_once()
                if metric is None:
                    print(f"\n[!] 目标进程 (PID: {self.pid}) 已退出或终止。")
                    break

                cpu = metric["cpu"]
                rss = metric["rss_mb"]
                vms = metric["vms_mb"]
                threads = metric["threads"]
                conns = metric["conns"]

                self.peak_rss_mb = max(self.peak_rss_mb, rss)
                self.peak_cpu_pct = max(self.peak_cpu_pct, cpu)
                self.sum_cpu += cpu
                self.samples.append(metric)

                if csv_writer:
                    csv_writer.writerow([f"{now:.2f}", f"{elapsed:.1f}", f"{cpu:.1f}", f"{rss:.2f}", f"{vms:.2f}", threads, conns])
                    csv_file.flush()

                sys.stdout.write(
                    f"\r[{elapsed:04.0f}s] "
                    f"PID {self.pid} | "
                    f"CPU: {cpu:5.1f}% | "
                    f"物理内存(RSS): {rss:6.2f} MB (峰值: {self.peak_rss_mb:6.2f}MB) | "
                    f"线程: {threads:2d} | "
                    f"连接数: {conns:4d}"
                )
                sys.stdout.flush()

                if duration > 0 and elapsed >= duration:
                    break

        except KeyboardInterrupt:
            print("\n[!] 用户中断监控采样。")
        finally:
            if csv_file:
                csv_file.close()
            self._print_summary()

    def _print_summary(self):
        count = len(self.samples)
        avg_cpu = (self.sum_cpu / count) if count > 0 else 0.0
        print("\n" + "=" * 65)
        print("                 资源开销汇总指标")
        print("=" * 65)
        print(f" • 物理内存峰值 (Peak RSS) : {self.peak_rss_mb:.2f} MB")
        print(f" • CPU 瞬时最高峰值       : {self.peak_cpu_pct:.1f} %")
        print(f" • CPU 平均占用率         : {avg_cpu:.1f} %")
        print(f" • 采样点总数             : {count}")
        if self.output_csv:
            print(f" • 详细时间序列已保存至   : {self.output_csv}")
        print("=" * 65)


def main():
    parser = argparse.ArgumentParser(description="MCPE 进程级 CPU/内存/连接数采样监控器")
    parser.add_argument("--pid", type=int, default=None, help="目标进程 PID")
    parser.add_argument("--name", type=str, default=None, help="目标进程命令行匹配关键字 (例如 server / cuberite / java)")
    parser.add_argument("--interval", type=float, default=1.0, help="采样间隔 (秒, 默认: 1.0)")
    parser.add_argument("--duration", type=float, default=0.0, help="监控持续总秒数 (0 为持续运行直至按 Ctrl+C)")
    parser.add_argument("--output", type=str, default=None, help="保存结果数据 CSV 路径 (可选)")

    args = parser.parse_args()

    monitor = ProcessMonitor(pid=args.pid, name_pattern=args.name, output_csv=args.output, interval=args.interval)
    monitor.run(duration=args.duration)


if __name__ == "__main__":
    main()
