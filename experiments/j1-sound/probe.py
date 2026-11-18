"""默认只读检查；显式指定 --play 才执行一次 J1 短音，不自动使能或归位。"""
import argparse
from datetime import datetime
import json
from pathlib import Path
import time

import serial


def main():
    parser = argparse.ArgumentParser(description="J1 短音和音量实验")
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--play", action="store_true", help="执行一次短音")
    parser.add_argument("--j1-only", action="store_true", help="仅 J1 接线，使用独立位置保持状态")
    parser.add_argument("--prepare", action="store_true", help="显式使能 J1 并保持当前位置，不执行整臂 HOME")
    parser.add_argument("--frequency", type=int, default=1000)
    parser.add_argument("--duration", type=int, default=200)
    parser.add_argument("--amplitude", type=int, default=50)
    parser.add_argument("--stop-after", type=int, help="短音受理后指定毫秒发送 STOP")
    args = parser.parse_args()
    if args.prepare and not args.j1_only:
        parser.error("--prepare 须同时指定 --j1-only")
    if not 400 <= args.frequency <= 2000 or not 50 <= args.duration <= 500 or not 25 <= args.amplitude <= 250:
        parser.error("频率须为 400～2000 Hz，时长 50～500 ms，励磁峰值 25～250 mA")
    if args.stop_after is not None and (not args.play or not 0 <= args.stop_after < args.duration):
        parser.error("中断测试须指定 --play，停止时间应小于短音时长")
    records = []
    link = serial.Serial(port=None, baudrate=115200, timeout=0.02, write_timeout=0.5)
    link.dtr = False
    link.rts = False
    link.port = args.port
    sequence = 0

    def send(command, terminal, timeout=2.0, stop_after=None):
        nonlocal sequence
        sequence += 1
        prefix = f"R:{sequence} "
        wire = f"T:{sequence} {command}"
        print(f">>> {command}", flush=True)
        records.append({"time": time.time(), "tx": wire})
        link.write((wire + "\n").encode("ascii"))
        deadline = time.monotonic() + timeout
        lines = []
        pending = bytearray()
        stop_at = None
        while time.monotonic() < deadline:
            if stop_at is not None and time.monotonic() >= stop_at:
                link.write(b"!STOP\n")
                records.append({"time": time.time(), "tx": "!STOP"})
                print(">>> !STOP", flush=True)
                stop_at = None
            pending.extend(link.read(max(link.in_waiting, 1)))
            while b"\n" in pending:
                raw, _, tail = pending.partition(b"\n")
                pending[:] = tail
                line = raw.decode("utf-8", errors="replace").strip()
                records.append({"time": time.time(), "rx": line})
                print(line, flush=True)
                if not line.startswith(prefix):
                    continue
                content = line[len(prefix):]
                lines.append(content)
                if stop_after is not None and content.startswith("J1Sound Accepted"):
                    stop_at = time.monotonic() + stop_after / 1000
                if terminal(content) or " Error:" in content:
                    return lines
        # 发声事务超时时尝试 STOP，子板还另有独立的本地时长限制。
        if command.startswith("!J1_SOUND"):
            link.write(b"!STOP\n")
            records.append({"time": time.time(), "tx": "!STOP"})
        raise RuntimeError(f"响应超时：{command}")

    try:
        link.open()
        status = send("#STATUS", lambda line: line == "Status End")
        if not any("FW:1.2.1-exp1 " in line or "FW:1.3.1-exp2 " in line for line in status):
            raise RuntimeError("REF 不是本次实验版本，请先核对烧录文件")
        sound = send("#J1_SOUND", lambda line: line.startswith("J1Sound OK"))
        if not any("EXP:1 " in line or "EXP:2 " in line for line in sound):
            raise RuntimeError("未收到 J1 实验子板确认，请核对 42 固件")
        if args.j1_only and not any("FW:1.3.1-exp2 " in line for line in status):
            raise RuntimeError("单 J1 模式须烧录新版 REF 实验固件")
        if args.j1_only and not any("EXP:2 " in line for line in sound):
            raise RuntimeError("单 J1 模式须烧录新版 J1 子板实验固件")
        if args.prepare:
            if not any("Status OK STATE:NORMAL" in line for line in status) or not any(
                    "ENABLED:0 HOMED:0 ACTIVE:0 QUEUE:0" in line for line in status):
                raise RuntimeError("准备单 J1 模式前，整臂须未使能、空闲且处于正常状态")
            result = send("!J1_TEST_START", lambda line: line.startswith("J1TestStart OK"), timeout=3.0)
            if not any("MODE:J1_ONLY HOLD:1 READY:1" in line for line in result):
                raise RuntimeError("单 J1 位置保持未就绪，请检查日志")
            status = send("#STATUS", lambda line: line == "Status End")
            sound = send("#J1_SOUND", lambda line: line.startswith("J1Sound OK"))
        send("#GETJPOS", lambda line: line.startswith("GetJPos OK"))
        if args.play:
            normal = "Status OK STATE:NORMAL" in "\n".join(status)
            if args.j1_only:
                ready = any("ENABLED:0 HOMED:0 ACTIVE:0 QUEUE:0" in line for line in status) and any(
                    "MODE:J1_ONLY HOLD:1 READY:1" in line for line in sound)
            else:
                ready = any("ENABLED:1 HOMED:1 ACTIVE:0 QUEUE:0" in line for line in status)
            if not normal or not ready:
                raise RuntimeError("测试位置保持未就绪或当前忙碌；脚本不会自动执行 START/HOME")
            if any("SOUND:1 " in line for line in sound):
                raise RuntimeError("已有短音正在播放")
            result = send(f"!J1_SOUND {args.frequency} {args.duration} {args.amplitude}",
                          lambda line: line.startswith("J1Sound OK"), stop_after=args.stop_after)
            if not any("J1Sound OK" in line or "cancelled" in line for line in result):
                raise RuntimeError("短音未正常完成，请检查日志")
            send("#J1_SOUND", lambda line: line.startswith("J1Sound OK"))
            send("#GETJPOS", lambda line: line.startswith("GetJPos OK"))
            send("#STATUS", lambda line: line == "Status End")
            print("本轮结束，请记录现场听感和机械臂表现；未测量分贝值。")
    except KeyboardInterrupt:
        if link.is_open:
            link.write(b"!STOP\n")
            records.append({"time": time.time(), "tx": "!STOP", "reason": "用户中断"})
        raise
    finally:
        link.close()
        destination = Path(__file__).resolve().parent / "results"
        destination.mkdir(exist_ok=True)
        path = destination / f"probe-{datetime.now():%Y%m%d-%H%M%S-%f}.json"
        path.write_text(json.dumps({"settings": vars(args), "records": records},
                                   ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"记录已保存：{path}")


if __name__ == "__main__":
    main()
