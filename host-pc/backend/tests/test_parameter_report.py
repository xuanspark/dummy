"""验证中文分包、真实限制字段和异常反馈，避免测试报告误判。"""

import pytest

from scripts.check_parameters import MODE_NAMES, parse_limits, parse_mode, query


def limit_lines():
    return ["GetLimit Begin STATE:EMERGENCY_STOP", *[
        f"LIMIT NODE:{node} {kind}:{configured} APPLIED:{runtime}"
        for node in range(1, 8)
        for kind, configured, runtime in (("ACC", 15, 10), ("SPEED", 30, 1), ("CURRENT", 2, 1))
    ], "GetLimit End"]


@pytest.mark.parametrize("mode,name", MODE_NAMES.items())
def test_utf8_mode_reply_split_inside_chinese_character(mode, name):
    wire = f"R:1 GetCmdMode OK MODE:{mode} NAME:{name} RATIO:1.00 STATE:EMERGENCY_STOP\r\n".encode("utf-8")

    class Connection:
        # 每次只返回一个字节，覆盖 USB 分包切开中文字符的情形。
        in_waiting = 1

        def __init__(self):
            self.received = bytearray(wire)
            self.sent = b""

        def read(self, size):
            return bytes([self.received.pop(0)]) if self.received else b""

        def write(self, data):
            self.sent += data

    connection = Connection()
    result = query(connection, "#GETCMDMODE", 1, lambda line: line.startswith("GetCmdMode OK"))
    assert parse_mode(result["lines"])["name"] == name
    assert bytes.fromhex(result["raw_hex"][0]) + b"\n" == wire
    assert connection.sent == b"T:1 #GETCMDMODE\n"


def test_runtime_speed_below_configured_limit_is_valid():
    result = parse_limits(limit_lines())
    assert result[2]["speed"] == {"configured_limit": 30, "runtime_limit": 1, "unit": "r/s"}


@pytest.mark.parametrize("bad_line", [
    "LIMIT NODE:1 SPEED:30 APPLIED:31",
    "LIMIT NODE:1 SPEED:30 APPLIED:nan",
    "LIMIT NODE:1 SPEED:30 APPLIED:-1",
    "LIMIT NODE:1 SPEED:NL",
])
def test_bad_limit_is_rejected(bad_line):
    lines = limit_lines()
    lines[2] = bad_line
    with pytest.raises(ValueError):
        parse_limits(lines)


def test_missing_node_is_not_filled_from_previous_result():
    parse_limits(limit_lines())
    lines = [line for line in limit_lines() if "NODE:7 " not in line]
    with pytest.raises(ValueError, match="不完整"):
        parse_limits(lines)
