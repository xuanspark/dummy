"""T13 + T12: 关键链路最小回归测试脚本 + 错误码分类。

覆盖第三~第五轮补丁的验收点，每个测试都对应任务列表 `06-整改任务列表.md` 中一条具体规则。
所有用例都在内存里跑（FakeTransport），无需真机串口。

覆盖点：
  - T01: 非法 CAN 节点 ID — host-pc 侧通过 #STATUS 的 CAN_DROP 计数观察（间接）
  - T05: 长命令 / 队列满 — Push 返回 0xFB/0xFF 时的 host-pc 解析
  - T06: STOP / PAUSE 优先级 — 普通查询占锁时也能发出
  - T07: 异步事件不被静默吞掉 — drain 时记日志
  - T10: #STATUS 多行响应解析
  - T11: raw 接口危险命令前缀拒绝

运行方式：
  cd host-pc/backend && pytest tests/test_regression_t13.py -v
"""
from __future__ import annotations

import asyncio
import logging
from typing import Callable, Iterable

import pytest

from app.core import protocol as P
from app.core.robot import Robot, RobotError


# ----- 复用 test_simulation 中的 FakeTransport（不直接 import 避免耦合） -----

class _Sub:
    def __init__(self) -> None:
        self.queue: asyncio.Queue[str] = asyncio.Queue(maxsize=128)


class FakeTransport:
    def __init__(self) -> None:
        self._subs: list[_Sub] = []
        self.tx_log: list[str] = []
        self._handlers: list[tuple[Callable[[str], bool], Callable[[str], Iterable[str]]]] = []
        self._tx_hook: Callable[[str], None] | None = None

    def start(self, loop: asyncio.AbstractEventLoop) -> None:
        pass

    def stop(self) -> None:
        pass

    def subscribe(self) -> _Sub:
        s = _Sub()
        self._subs.append(s)
        return s

    def unsubscribe(self, s: _Sub) -> None:
        try:
            self._subs.remove(s)
        except ValueError:
            pass

    def write_line(self, line: str) -> None:
        self.tx_log.append(line)
        if self._tx_hook is not None:
            self._tx_hook(line)
        for matcher, handler in self._handlers:
            if matcher(line):
                for reply in handler(line):
                    for s in self._subs:
                        try:
                            s.queue.put_nowait(reply)
                        except asyncio.QueueFull:
                            pass
                return

    def on(self, prefix_or_predicate, handler):
        if isinstance(prefix_or_predicate, str):
            p = prefix_or_predicate
            if len(p) == 1 and p in "&@>":
                pred = lambda ln, p=p: ln.startswith(p)
            else:
                pred = lambda ln, p=p: ln == p or ln.startswith(p + " ")
        else:
            pred = prefix_or_predicate
        self._handlers.append((pred, handler))

    def emit_async_event(self, line: str) -> None:
        for s in self._subs:
            try:
                s.queue.put_nowait(line)
            except asyncio.QueueFull:
                pass


@pytest.fixture
async def robot_pair():
    t = FakeTransport()
    r = Robot(t, default_timeout=2.0)
    await r.start()
    try:
        yield r, t
    finally:
        await r.stop()


# ============================================================================
# T06: STOP / PAUSE / RESUME 优先级 — 不被普通查询阻塞
# ============================================================================


class TestT06Priority:
    async def test_estop_uses_priority_path(self, robot_pair):
        """estop() 应该走 _send_priority 而不占 _cmd_lock。"""
        r, t = robot_pair
        t.on("!STOP", lambda _l: ["Stop OK"])
        await r.estop()
        assert t.tx_log == ["!STOP"]

    async def test_estop_bypasses_cmd_lock(self, robot_pair):
        """普通查询持有 _cmd_lock 时，STOP 仍能立刻发出。"""
        r, t = robot_pair
        # 让 #GETJPOS 故意不回复，占住 _cmd_lock
        t.on("#GETJPOS", lambda _l: [])  # 无响应
        t.on("!STOP", lambda _l: ["Stop OK"])

        # 启动一个永远不会完成的 get_jpos
        slow_task = asyncio.create_task(r.get_jpos())
        await asyncio.sleep(0.05)  # 让它占住锁

        # 在锁被占住时发 STOP，应该立刻成功
        try:
            await asyncio.wait_for(r.estop(), timeout=1.0)
        except asyncio.TimeoutError:
            pytest.fail("estop() 被普通查询阻塞，未走优先级路径")
        finally:
            slow_task.cancel()
            try:
                await slow_task
            except (asyncio.CancelledError, RobotError):
                pass

        assert "!STOP" in t.tx_log

    async def test_pause_and_resume_priority(self, robot_pair):
        r, t = robot_pair
        t.on("!PAUSE", lambda _l: ["Pause OK"])
        t.on("!RESUME", lambda _l: ["Resume OK"])
        await r.pause()
        await r.resume()
        assert t.tx_log == ["!PAUSE", "!RESUME"]


# ============================================================================
# T07: 异步事件不被静默吞掉 — drain 时记日志、out-of-band tag 触发回调
# ============================================================================


class TestT07AsyncEvents:
    async def test_drained_stale_response_is_logged(self, robot_pair, caplog):
        """_exec drain 之前残留的响应时应该写 info 日志。"""
        r, t = robot_pair
        t.on("!HOME", lambda _l: ["Home OK"])
        # 先 push 一条残留响应
        t.emit_async_event("Estop OK")
        await asyncio.sleep(0.05)  # 让 dispatcher 收进 _pending

        with caplog.at_level(logging.INFO, logger="app.core.robot"):
            await r.home()

        # 必须能找到 "drain: discarding stale response" 日志
        msgs = [rec.message for rec in caplog.records]
        assert any("drain: discarding stale response" in m for m in msgs), \
            f"未找到 drain 日志，实际记录: {msgs}"

    async def test_out_of_band_tag_is_logged_during_wait(self, robot_pair, caplog):
        """等待匹配 tag 时收到其他 tag 应记日志而非静默丢弃。"""
        r, t = robot_pair
        # !HOME 先吐一条无关 Estop，再吐 Home OK
        t.on("!HOME", lambda _l: ["Estop OK", "Home OK"])

        with caplog.at_level(logging.INFO, logger="app.core.robot"):
            await r.home()

        msgs = [rec.message for rec in caplog.records]
        assert any("out-of-band event while waiting" in m for m in msgs), \
            f"未找到 out-of-band 日志，实际记录: {msgs}"

    async def test_safety_event_emits_warning(self, robot_pair, caplog):
        """Estop / Stop 等安全 tag 在 drain 时升级到 warning 日志。"""
        r, t = robot_pair
        t.on("!HOME", lambda _l: ["Home OK"])
        t.emit_async_event("Stop OK")  # 安全事件
        await asyncio.sleep(0.05)

        with caplog.at_level(logging.WARNING, logger="app.core.robot"):
            await r.home()

        warnings = [rec.message for rec in caplog.records if rec.levelno >= logging.WARNING]
        assert any("async safety event" in m and "Stop" in m for m in warnings), \
            f"未找到安全事件 warning，实际记录: {warnings}"


# ============================================================================
# T10: #STATUS 多行响应解析 — CAN 健康 + 节点在线 + age
# ============================================================================


class TestT10Status:
    async def test_status_parses_can_counters_and_nodes(self, robot_pair):
        r, t = robot_pair
        t.on("#STATUS", lambda _l: [
            "Status OK STATE:NORMAL CAN_RX:12345 CAN_DROP:7",
            "J[1] OL age=12ms",
            "J[2] OL age=15ms",
            "J[3] NL age=4294967295ms",  # 离线时 age 取 0xFFFFFFFF
            "J[4] OL age=20ms",
            "J[5] OL age=22ms",
            "J[6] OL age=18ms",
            "HD[7] OL age=30ms",
        ])
        result = await r.get_status(quiet_ms=80, total_timeout=2.0)
        assert result["state"] == "NORMAL"
        assert result["can_rx"] == 12345
        assert result["can_drop"] == 7
        assert result["nodes"][1] == {"online": True, "age_ms": 12}
        assert result["nodes"][3]["online"] is False
        assert result["nodes"][3]["age_ms"] == 4294967295
        assert result["nodes"][7] == {"online": True, "age_ms": 30}

    async def test_status_detects_can_drops_after_illegal_ids(self, robot_pair):
        """T01 + T10 联动：固件丢弃非法 CAN 帧时 CAN_DROP 计数会增加。"""
        r, t = robot_pair
        # 模拟两轮 #STATUS：第二轮 CAN_DROP 增加表示固件确实在丢非法帧
        responses = [
            ["Status OK STATE:NORMAL CAN_RX:100 CAN_DROP:0", "J[1] OL age=5ms"],
            ["Status OK STATE:NORMAL CAN_RX:200 CAN_DROP:3", "J[1] OL age=5ms"],
        ]
        call_idx = [0]

        def status_handler(_l):
            r = responses[call_idx[0]]
            call_idx[0] = min(call_idx[0] + 1, len(responses) - 1)
            return r

        t.on("#STATUS", status_handler)

        r1 = await r.get_status(quiet_ms=80, total_timeout=2.0)
        r2 = await r.get_status(quiet_ms=80, total_timeout=2.0)
        assert r2["can_drop"] > r1["can_drop"]
        assert r2["can_rx"] > r1["can_rx"]


# ============================================================================
# T05: 长命令 / 队列满 — Push 返回 0xFB/0xFF 解析
# ============================================================================


class TestT05QueueLimits:
    async def test_move_queue_too_long_propagates(self, robot_pair):
        """固件 Push() 返回 0xFB（too_long）时报 Move Error: too_long。

        实际固件输出参见 ascii_protocol.cpp:643 / 1240：
            Respond(_responseChannel, "Move Error: too_long");
        """
        r, t = robot_pair
        t.on(">", lambda _l: ["Move Error: too_long"])
        with pytest.raises(RobotError) as exc_info:
            await r.raw(">0,0,0,0,0,0,1", timeout=1.0)
        # 验证错误体里能找到 too_long 词，且被分类为 QUEUE_FULL
        assert "too_long" in str(exc_info.value)
        assert exc_info.value.category == P.ErrorCategory.QUEUE_FULL

    async def test_move_queue_full_propagates(self, robot_pair):
        r, t = robot_pair
        t.on("&", lambda _l: ["Move Error: queue_full"])
        with pytest.raises(RobotError, match="queue_full"):
            await r.move_j([0, 0, 0, 0, 0, 0], speed=10)


# ============================================================================
# T11: raw 接口危险命令拒绝 — 这是 API 层规则，直接在 router 上测
# ============================================================================


class TestT11RawWhitelist:
    """这些测试不需要 robot_pair，只验证 API 层 _DANGEROUS_PREFIXES 列表完整性。"""

    def test_dangerous_prefixes_cover_known_set(self):
        from app.api.robot import _DANGEROUS_PREFIXES
        # 第二/三/四轮已覆盖的所有 maintenance / tuning 命令都应在列表里
        must_have = [
            "#OFFSET_J", "#SET_DCE_", "#CMDMODE", "#REBOOT",
            "#ACC_J", "#SPEED_J", "#I_LIMIT_J", "!CALIBRATION",
        ]
        for cmd in must_have:
            assert cmd in _DANGEROUS_PREFIXES, f"缺失危险前缀: {cmd}"

    def test_dangerous_prefix_matches_case_insensitive(self):
        from app.api.robot import _DANGEROUS_PREFIXES
        # 抽样：API endpoint 用 cmd.upper().startswith(prefix.upper())
        sample = "#offset_j 1"
        assert any(sample.upper().startswith(p.upper()) for p in _DANGEROUS_PREFIXES)

    def test_safe_commands_not_blocked(self):
        from app.api.robot import _DANGEROUS_PREFIXES
        # 正常查询命令不能被误拦
        for safe in ["#GETJPOS", "#GETLPOS", "#STATUS", "!START", "&0,0,0,0,0,0"]:
            assert not any(safe.upper().startswith(p.upper()) for p in _DANGEROUS_PREFIXES), \
                f"安全命令被误拦: {safe}"


# ============================================================================
# T01 间接验证：node=7 / 非法 node 在 host-pc 侧已被拦截
# ============================================================================


class TestT01NodeBoundary:
    async def test_node_8_rejected_pre_send(self, robot_pair):
        r, t = robot_pair
        with pytest.raises(RobotError, match="node must be 1..7"):
            await r.check_online(node=8)
        assert t.tx_log == []

    async def test_node_0_rejected_pre_send(self, robot_pair):
        r, t = robot_pair
        with pytest.raises(RobotError, match="node must be 1..7"):
            await r.check_online(node=0)
        assert t.tx_log == []

    async def test_node_7_hand_allowed(self, robot_pair):
        """node=7 是手爪，host-pc 必须允许。"""
        r, t = robot_pair
        t.on("#ONLINE 7", lambda _l: ["Online OK node=7 OL"])
        result = await r.check_online(node=7, quiet_ms=80, total_timeout=2.0)
        assert result["nodes"] == {7: True}


# ============================================================================
# T12: 错误码分类 — protocol.classify_error 覆盖所有已知 firmware 错误词汇
# ============================================================================


class TestT12ErrorClassify:
    """直接测试 protocol.classify_error，确认每个固件错误词都映射到正确类别。"""

    @pytest.mark.parametrize("reason, expected", [
        # NOT_READY
        ("not_homed",                      P.ErrorCategory.NOT_READY),
        ("not_enabled",                    P.ErrorCategory.NOT_READY),
        # QUEUE_FULL
        ("queue_full",                     P.ErrorCategory.QUEUE_FULL),
        ("queue too long",                 P.ErrorCategory.QUEUE_FULL),
        ("too_long",                       P.ErrorCategory.QUEUE_FULL),
        # STATE_BLOCKED
        ("bad_state",                      P.ErrorCategory.STATE_BLOCKED),
        ("state=PAUSED",                   P.ErrorCategory.STATE_BLOCKED),
        ("state=EMERGENCY_STOP",           P.ErrorCategory.STATE_BLOCKED),
        # LIMIT_EXCEEDED
        ("limit J1 Max",                   P.ErrorCategory.LIMIT_EXCEEDED),
        ("limit J6 Min",                   P.ErrorCategory.LIMIT_EXCEEDED),
        ("limit_error",                    P.ErrorCategory.LIMIT_EXCEEDED),
        # COMM_TIMEOUT
        ("timeout waiting for Move",       P.ErrorCategory.COMM_TIMEOUT),
        # BAD_ARGS
        ("use #ONLINE [1-7]",              P.ErrorCategory.BAD_ARGS),
        ("bad node=99",                    P.ErrorCategory.BAD_ARGS),
        ("mode must be 1..4",              P.ErrorCategory.BAD_ARGS),
        # UNKNOWN
        ("",                               P.ErrorCategory.UNKNOWN),
        ("some completely novel error",    P.ErrorCategory.UNKNOWN),
    ])
    def test_classify(self, reason: str, expected: P.ErrorCategory):
        assert P.classify_error(reason) == expected

    async def test_response_carries_category(self, robot_pair):
        """parse_response 应自动填充 error_category。"""
        r, t = robot_pair
        t.on("!HOME", lambda _l: ["Home Error: not_enabled"])
        with pytest.raises(RobotError) as exc_info:
            await r.home()
        assert exc_info.value.category == P.ErrorCategory.NOT_READY

    async def test_move_limit_classified(self, robot_pair):
        r, t = robot_pair
        t.on("&", lambda _l: ["Move Error: limit J1 Max"])
        with pytest.raises(RobotError) as exc_info:
            await r.move_j([200, 0, 90, 0, 0, 0], speed=10)
        assert exc_info.value.category == P.ErrorCategory.LIMIT_EXCEEDED

    async def test_queue_full_classified(self, robot_pair):
        r, t = robot_pair
        t.on("&", lambda _l: ["Move Error: queue_full"])
        with pytest.raises(RobotError) as exc_info:
            await r.move_j([0, 0, 0, 0, 0, 0], speed=10)
        assert exc_info.value.category == P.ErrorCategory.QUEUE_FULL

    async def test_bad_args_pre_send_classified(self, robot_pair):
        """host-pc 端 pre-send 校验也应带 BAD_ARGS 类别。"""
        r, t = robot_pair
        with pytest.raises(RobotError) as exc_info:
            await r.check_online(node=99)
        assert exc_info.value.category == P.ErrorCategory.BAD_ARGS

    async def test_comm_timeout_classified(self, robot_pair):
        r, t = robot_pair
        # 注册无响应 handler → Robot 超时
        with pytest.raises(RobotError) as exc_info:
            await r.raw("!HOME", timeout=0.1)
        assert exc_info.value.category == P.ErrorCategory.COMM_TIMEOUT

    def test_api_status_code_mapping(self):
        """API 层错误码映射表覆盖所有 ErrorCategory。"""
        from app.api.robot import _CATEGORY_HTTP_STATUS
        for cat in P.ErrorCategory:
            assert cat.value in _CATEGORY_HTTP_STATUS, f"缺失映射: {cat.value}"
        # 关键断言：BAD_ARGS → 422, COMM_TIMEOUT → 504
        assert _CATEGORY_HTTP_STATUS["bad_args"] == 422
        assert _CATEGORY_HTTP_STATUS["comm_timeout"] == 504
