"""只读检查模式名称和实时限制，统一使用 UTF-8 输出及保存原始回包。"""

import argparse
from datetime import datetime, timedelta, timezone
import json
import math
from pathlib import Path
import re
import sys
import time

import serial


MODE_NAMES = {1: "顺序点位", 2: "可中断点位", 3: "连续轨迹", 4: "电机调参"}
LIMIT_FIELDS = {
    "ACC": ("acc", "加速度", "r/s²"),
    "SPEED": ("speed", "速度", "r/s"),
    "CURRENT": ("current", "电流", "A"),
}
VERSION = "0.1.1"


def configure_console():
    # Windows 管道的默认编码可能是 GBK，与工具读取输出时使用的 UTF-8 不一致。
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")


def query(connection, command, request, terminal, timeout=3.0):
    """按事务编号收齐回包；完整拼接一行后才严格解码，保留原始字节。"""
    connection.write(f"T:{request} {command}\n".encode("ascii"))
    deadline = time.monotonic() + timeout
    buffer = b""
    lines = []
    raw_lines = []
    prefix = f"R:{request} "
    while time.monotonic() < deadline:
        buffer += connection.read(connection.in_waiting or 1)
        while b"\n" in buffer:
            raw, _, buffer = buffer.partition(b"\n")
            line = raw.rstrip(b"\r").decode("utf-8")
            if not line.startswith(prefix):
                continue
            payload = line[len(prefix):]
            lines.append(payload)
            raw_lines.append(raw.hex())
            if " Error:" in payload:
                raise RuntimeError(payload)
            if terminal(payload):
                return {"command": command, "lines": lines, "raw_hex": raw_lines}
    raise TimeoutError(f"查询超时：{command}，已收到 {len(lines)} 行")


def parse_mode(lines):
    pattern = r"GetCmdMode OK MODE:([1-4]) NAME:(.+?) RATIO:([0-9.]+) STATE:(\S+)"
    matches = [re.fullmatch(pattern, line) for line in lines]
    matches = [match for match in matches if match]
    if len(matches) != 1:
        raise ValueError("命令模式回包不完整")
    match = matches[0]
    mode = int(match[1])
    if match[2] != MODE_NAMES[mode]:
        raise ValueError("命令模式名称与编号不符，请检查串口编码和固件")
    return {"mode": mode, "name": match[2], "ratio": float(match[3]), "state": match[4]}


def parse_limits(lines):
    """保存上限与当前指令运行限幅分别记录，运行限幅无需等于保存上限。"""
    if not lines or not lines[0].startswith("GetLimit Begin STATE:") or lines[-1] != "GetLimit End":
        raise ValueError("限制查询未完整结束")
    limits = {}
    for line in lines[1:-1]:
        match = re.fullmatch(r"LIMIT NODE:([1-7]) (ACC|SPEED|CURRENT):(\S+) APPLIED:(\S+)", line)
        if not match:
            raise ValueError(f"限制回包无效：{line}")
        node = int(match[1])
        key, _, unit = LIMIT_FIELDS[match[2]]
        configured, runtime = float(match[3]), float(match[4])
        if not all(math.isfinite(value) and value >= 0 for value in (configured, runtime)):
            raise ValueError(f"节点 {node} 的 {key} 不是有效的非负数")
        if runtime > configured + 0.0001:
            raise ValueError(f"节点 {node} 的 {key} 运行限幅超出保存上限")
        values = limits.setdefault(node, {})
        if key in values:
            raise ValueError(f"节点 {node} 的 {key} 重复返回")
        values[key] = {"configured_limit": configured, "runtime_limit": runtime, "unit": unit}
    if set(limits) != set(range(1, 8)) or any(set(values) != {"acc", "speed", "current"} for values in limits.values()):
        raise ValueError("节点 1～7 的实时限制参数不完整，禁止使用历史数据补齐")
    return limits


def main():
    configure_console()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM3", help="REF 串口")
    parser.add_argument("--output", type=Path, help="JSON 记录保存位置")
    args = parser.parse_args()
    now = datetime.now(timezone(timedelta(hours=8)))
    root = Path(__file__).resolve().parents[3]
    output = args.output or root / "test/results" / f"parameters-{now:%Y%m%d-%H%M%S-%f}.json"
    responses = []
    # 只发送三个查询，不改变使能、停止状态或任何运动参数。
    with serial.Serial(args.port, 115200, timeout=0.05, write_timeout=1) as connection:
        connection.reset_input_buffer()
        responses.append(query(connection, "#GETCMDMODE", 1, lambda line: line.startswith("GetCmdMode OK")))
        responses.append(query(connection, "#GETLIMIT all", 2, lambda line: line == "GetLimit End"))
        responses.append(query(connection, "#STATUS", 3, lambda line: line == "Status End"))
    mode = parse_mode(responses[0]["lines"])
    limits = parse_limits(responses[1]["lines"])
    report = {"tool_version": VERSION, "time": now.isoformat(), "port": args.port,
              "mode": mode, "limits": limits, "responses": responses}
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2)
    print(f"模式：{mode['mode']}（{mode['name']}）；状态：{mode['state']}")
    print("APPLIED 表示当前指令的运行限幅，低于保存上限属于正常情况；它不是实测转速。")
    for node, values in limits.items():
        for key, title, unit in LIMIT_FIELDS.values():
            value = values[key]
            print(f"节点 {node} {title}：保存上限={value['configured_limit']:g}，运行限幅={value['runtime_limit']:g} {unit}")
    print(f"检查通过：21 项实时回读完整，运行限幅均未超过保存上限。记录：{output}")


if __name__ == "__main__":
    try:
        main()
    except (serial.SerialException, UnicodeError, ValueError, RuntimeError, TimeoutError, OSError) as error:
        print(f"检查失败：{error}", file=sys.stderr)
        sys.exit(1)
