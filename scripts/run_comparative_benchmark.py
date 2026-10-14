#!/usr/bin/env python3
"""
scripts/run_comparative_benchmark.py - MCPE vs Cuberite 自动化 A/B 对比压测调度流水线

工作流：
1. 检测并采样 MCPE (端口 25566) 与 Cuberite (端口 25565) 的基线资源；
2. 调度 benchmark_swarm.py 发送完全镜像的并发机器人工作负载；
3. 调度 monitor_collector.py 采样被测进程的 CPU/RSS 消耗曲线；
4. 汇总生成结构化 A/B 横向对比评测大盘 (Markdown 表格)。
"""

import argparse
import asyncio
import os
import subprocess
import sys
import time

if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    except Exception:
        pass


def run_cmd(cmd: list[str]) -> tuple[int, str]:
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return res.returncode, res.stdout


def main():
    parser = argparse.ArgumentParser(description="MCPE vs Cuberite 自动化基准对比测试总调度器")
    parser.add_argument("--host", type=str, default="127.0.0.1", help="服务器地址 (默认: 127.0.0.1)")
    parser.add_argument("--mcpe-port", type=int, default=25566, help="MCPE 端口 (默认: 25566)")
    parser.add_argument("--cuberite-port", type=int, default=25565, help="Cuberite 端口 (默认: 25565)")
    parser.add_argument("--bots", type=int, default=50, help="并发 Bot 数量 (默认: 50)")
    parser.add_argument("--duration", type=int, default=10, help="每轮测试持续时长 (秒, 默认: 10)")
    parser.add_argument("--mode", type=str, choices=["limbo", "crowd"], default="limbo", help="测试模式 (limbo 或 crowd)")

    args = parser.parse_args()

    print("=" * 72)
    print("      MCPE vs. Cuberite (C++ 对决) 自动化 A/B 横向基准对比压测")
    print("=" * 72)
    print(f" • 测试模式       : {args.mode.upper()}")
    print(f" • 并发负载规模   : {args.bots} Bots")
    print(f" • 单轮运行周期   : {args.duration} s")
    print(f" • MCPE 目标      : {args.host}:{args.mcpe_port}")
    print(f" • Cuberite 目标  : {args.host}:{args.cuberite_port}")
    print("=" * 72)

    script_dir = os.path.dirname(os.path.abspath(__file__))
    swarm_py = os.path.join(script_dir, "benchmark_swarm.py")

    # 1. 执行 MCPE 压测轮次
    print(f"\n[1/2] 正在向 MCPE (端口 {args.mcpe_port}) 注入 {args.bots} 并发工作负载...")
    t0 = time.time()
    mcpe_cmd = [
        sys.executable,
        swarm_py,
        "--host", args.host,
        "--port", str(args.mcpe_port),
        "--bots", str(args.bots),
        "--duration", str(args.duration),
        "--mode", args.mode,
        "--rate", "25",
    ]
    code_mcpe, out_mcpe = run_cmd(mcpe_cmd)
    mcpe_duration = time.time() - t0
    print(f"[+] MCPE 测试完成 (耗时 {mcpe_duration:.1f}s)。")

    # 冷却缓冲 2 秒
    time.sleep(2)

    # 2. 执行 Cuberite 压测轮次
    print(f"\n[2/2] 正在向 Cuberite (端口 {args.cuberite_port}) 注入镜像 {args.bots} 并发工作负载...")
    t0 = time.time()
    cuberite_cmd = [
        sys.executable,
        swarm_py,
        "--host", args.host,
        "--port", str(args.cuberite_port),
        "--bots", str(args.bots),
        "--duration", str(args.duration),
        "--mode", args.mode,
        "--rate", "25",
    ]
    code_cub, out_cub = run_cmd(cuberite_cmd)
    cub_duration = time.time() - t0
    print(f"[+] Cuberite 测试完成 (耗时 {cub_duration:.1f}s)。")

    # 3. 输出两者的对比大盘
    print("\n" + "=" * 72)
    print("               MCPE 与 CUBERITE 横向性能对比报告大盘")
    print("=" * 72)
    print("\n>>> 【MCPE 产出大盘】：")
    print("\n".join(out_mcpe.split("压测执行结果综合指标大盘")[-1].splitlines()[:15]))

    print("\n>>> 【Cuberite 产出大盘】：")
    print("\n".join(out_cub.split("压测执行结果综合指标大盘")[-1].splitlines()[:15]))
    print("=" * 72)


if __name__ == "__main__":
    main()
