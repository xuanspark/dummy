"""End-to-end simulation tests against a fake firmware.

Each test scripts how the firmware would respond to a given ASCII command
(based on the actual reply strings in
``dummy-ref-core-fw/UserApp/protocols/ascii_protocol.cpp``) and then drives
the typed ``Robot`` API to verify that:
  - the wire bytes written by host-pc match what the firmware grammar accepts;
  - multi-line responses (``#ONLINE``, ``#GET_DCE``) are aggregated correctly;
  - error responses propagate as ``RobotError`` with the firmware text.

These tests do NOT touch any serial port — a :class:`FakeTransport` stands in
for ``app.core.transport.Transport`` and runs entirely in-process.
"""
from __future__ import annotations

import asyncio
from typing import Callable, Iterable

import pytest

from app.core import protocol as P
from app.core.robot import Robot, RobotError


# ----------------------------- fake transport ------------------------------

class _Sub:
    """Mirror of transport._Subscriber — only ``.queue`` is required by Robot."""

    def __init__(self) -> None:
        self.queue: asyncio.Queue[str] = asyncio.Queue(maxsize=128)


class FakeTransport:
    """Duck-types :class:`app.core.transport.Transport`'s public surface.

    Tests register response handlers via :meth:`on`. When ``write_line`` is
    called, the matching handler runs synchronously and pushes its reply lines
    into every active subscriber's queue, exactly like the real RX dispatcher.
    """

    def __init__(self) -> None:
        self._subs: list[_Sub] = []
        self.tx_log: list[str] = []
        self._handlers: list[tuple[Callable[[str], bool], Callable[[str], Iterable[str]]]] = []

    # --- contract used by Robot ---
    def start(self, loop: asyncio.AbstractEventLoop) -> None:  # noqa: D401 — match signature
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
        for matcher, handler in self._handlers:
            if matcher(line):
                for reply in handler(line):
                    for s in self._subs:
                        try:
                            s.queue.put_nowait(reply)
                        except asyncio.QueueFull:
                            pass
                return
        # No handler → silence (Robot will time out, which is the correct
        # behaviour to assert in negative tests).

    # --- test API ---
    def on(self, prefix_or_predicate, handler: Callable[[str], Iterable[str]]) -> None:
        """Register ``handler(line)`` for commands matching ``prefix_or_predicate``.

        ``prefix_or_predicate`` may be:
          * a string  — matches ``line == prefix`` or ``line.startswith(prefix + ' ')``
                         (or, for single-char prefixes ``&@>``, any string starting with it);
          * a callable ``(line: str) -> bool``.
        """
        if isinstance(prefix_or_predicate, str):
            p = prefix_or_predicate
            if len(p) == 1 and p in "&@>":
                pred = lambda ln, p=p: ln.startswith(p)  # noqa: E731
            else:
                pred = lambda ln, p=p: ln == p or ln.startswith(p + " ")  # noqa: E731
        else:
            pred = prefix_or_predicate
        self._handlers.append((pred, handler))

    def emit_async_event(self, line: str) -> None:
        """Push an unsolicited firmware line (e.g. ``Estop OK`` from a button press)."""
        for s in self._subs:
            try:
                s.queue.put_nowait(line)
            except asyncio.QueueFull:
                pass


# ----------------------------- shared fixture ------------------------------


@pytest.fixture
async def robot_pair():
    """Yield ``(robot, transport)`` ready to receive commands."""
    t = FakeTransport()
    r = Robot(t, default_timeout=2.0)
    await r.start()
    try:
        yield r, t
    finally:
        await r.stop()


# ============================== lifecycle =================================


class TestLifecycle:
    async def test_start(self, robot_pair):
        r, t = robot_pair
        # Firmware: !START → "Started OK" via PostSysCmd. We model the synchronous reply.
        t.on("!START", lambda _l: ["Started OK"])
        await r.start_robot()
        assert t.tx_log == ["!START"]

    async def test_home(self, robot_pair):
        r, t = robot_pair
        t.on("!HOME", lambda _l: ["Home OK"])
        await r.home()
        assert t.tx_log[-1] == "!HOME"

    async def test_reset(self, robot_pair):
        r, t = robot_pair
        t.on("!RESET", lambda _l: ["Reset OK"])
        await r.reset_pose()
        assert "!RESET" in t.tx_log

    async def test_estop_pause_resume_close(self, robot_pair):
        r, t = robot_pair
        t.on("!STOP",   lambda _l: ["Stop OK", "STATE:Stoping QUEUE:0"])
        t.on("!PAUSE",  lambda _l: ["Pause OK", "STATE:Pauseing QUEUE:0"])
        t.on("!RESUME", lambda _l: ["Resume OK"])
        t.on("!CLOSE",  lambda _l: ["Close OK"])
        await r.estop()
        await r.pause()
        await r.resume()
        await r.shutdown_robot()
        assert t.tx_log == ["!STOP", "!PAUSE", "!RESUME", "!CLOSE"]

    async def test_calibrate(self, robot_pair):
        r, t = robot_pair
        t.on("!CALIBRATION", lambda _l: ["Calibration OK"])
        await r.calibrate()
        assert t.tx_log == ["!CALIBRATION"]

    async def test_home_not_enabled_error(self, robot_pair):
        r, t = robot_pair
        # Firmware refuses !HOME if not enabled.
        t.on("!HOME", lambda _l: ["Home Error: not_enabled"])
        with pytest.raises(RobotError, match="not_enabled"):
            await r.home()


# =============================== hand =====================================


class TestHand:
    async def test_open_close(self, robot_pair):
        r, t = robot_pair
        t.on("!HD_O", lambda _l: ["HD_O OK"])
        t.on("!HD_C", lambda _l: ["HD_C OK"])
        await r.hand_open()
        await r.hand_close()
        assert t.tx_log == ["!HD_O", "!HD_C"]

    async def test_enable_disable(self, robot_pair):
        r, t = robot_pair
        t.on("!HD_EN",  lambda _l: ["HD_EN OK enabled=1 angle=0.00"])
        t.on("!HD_DIS", lambda _l: ["HD_DIS OK enabled=0 angle=0.00"])
        await r.hand_enable(True)
        await r.hand_enable(False)
        assert t.tx_log == ["!HD_EN", "!HD_DIS"]

    async def test_pos(self, robot_pair):
        r, t = robot_pair
        t.on("!HD_POS",
             lambda l: [f"HD_POS OK pos={l.split()[-1]} angle=12.34"])
        await r.hand_pos(50)
        assert t.tx_log == ["!HD_POS 50"]

    async def test_zero_returns_min_max(self, robot_pair):
        r, t = robot_pair
        t.on("!HD_ZERO", lambda _l: ["HD_ZERO OK MIN:-1.20 MAX:34.50"])
        closed, opened = await r.hand_zero()
        assert closed == -1.20
        assert opened == 34.50

    async def test_current_validation_pre_send(self, robot_pair):
        r, t = robot_pair
        # Out-of-range value must NOT hit the wire.
        with pytest.raises(RobotError, match="amps must be in"):
            await r.hand_current(2.5)
        assert t.tx_log == []
        with pytest.raises(RobotError, match="amps must be in"):
            await r.hand_current(0.0)
        assert t.tx_log == []

    async def test_current_ok(self, robot_pair):
        r, t = robot_pair
        t.on("!HD_I", lambda l: [f"HD_I OK current={l.split()[-1]}"])
        await r.hand_current(0.5)
        # Trailing zero trimming → "0.5"
        assert t.tx_log == ["!HD_I 0.5"]


# =============================== rgb ======================================


class TestRgb:
    async def test_on_off(self, robot_pair):
        r, t = robot_pair
        t.on("!RGB_ON",  lambda _l: ["RGB_ON OK"])
        t.on("!RGB_OFF", lambda _l: ["RGB_OFF OK"])
        await r.rgb_on()
        await r.rgb_off()
        assert t.tx_log == ["!RGB_ON", "!RGB_OFF"]

    async def test_mode(self, robot_pair):
        r, t = robot_pair
        t.on("!RGB_MODE", lambda l: [f"RGB_MODE OK mode={l.split()[-1]}"])
        await r.rgb_mode(11)
        assert t.tx_log == ["!RGB_MODE 11"]

    async def test_custom(self, robot_pair):
        r, t = robot_pair
        t.on("!RGB_CUSTOM", lambda _l: ["RGB_CUSTOM OK r=255 g=128 b=0"])
        await r.rgb_custom(255, 128, 0)
        assert t.tx_log == ["!RGB_CUSTOM 255 128 0"]

    async def test_mode_validation_pre_send(self, robot_pair):
        r, t = robot_pair
        with pytest.raises(ValueError):
            await r.rgb_mode(12)
        assert t.tx_log == []


# ============================= state queries ==============================


class TestQueries:
    async def test_jpos(self, robot_pair):
        r, t = robot_pair
        t.on("#GETJPOS", lambda _l: ["GetJPos OK 0.0 -75.0 90.0 0.0 0.0 0.0 H:0"])
        jp = await r.get_jpos()
        assert (jp.j1, jp.j2, jp.j3, jp.j4, jp.j5, jp.j6) == (0.0, -75.0, 90.0, 0.0, 0.0, 0.0)
        assert jp.hand == 0

    async def test_lpos(self, robot_pair):
        r, t = robot_pair
        t.on("#GETLPOS", lambda _l: ["GetLPos OK 175.0 44.0 160.0 -135.0 -13.54 -14.6 H:1"])
        lp = await r.get_lpos()
        assert (lp.x, lp.y, lp.z) == (175.0, 44.0, 160.0)
        assert lp.hand == 1

    async def test_speed(self, robot_pair):
        r, t = robot_pair
        t.on("#GETSPEED", lambda _l: ["GetSpeed OK SPEED:25.50"])
        s = await r.get_speed()
        assert s == 25.50

    async def test_state_cache_population(self, robot_pair):
        """Successful query should also populate Robot.state.last_*."""
        r, t = robot_pair
        t.on("#GETJPOS", lambda _l: ["GetJPos OK 1 2 3 4 5 6 H:0"])
        t.on("#GETLPOS", lambda _l: ["GetLPos OK 10 20 30 40 50 60 H:0"])
        t.on("#GETSPEED", lambda _l: ["GetSpeed OK SPEED:42.0"])
        await r.get_jpos()
        await r.get_lpos()
        await r.get_speed()
        # Yield to dispatcher to ensure cache writes flushed
        await asyncio.sleep(0.05)
        assert r.state.last_jpos is not None and r.state.last_jpos.j1 == 1
        assert r.state.last_lpos is not None and r.state.last_lpos.x == 10
        assert r.state.last_speed == 42.0


# =============================== movement =================================


class TestMovement:
    async def test_move_j_with_speed_emits_7_args(self, robot_pair):
        """``move_j(joints, speed=10)`` → 7-arg wire form."""
        r, t = robot_pair
        t.on("&", lambda _l: ["Move OK"])
        await r.move_j([0, -75, 90, 0, 0, 0], speed=10)
        cmd = t.tx_log[-1]
        assert cmd.startswith("&") and len(cmd.split(",")) == 7
        assert cmd.endswith(",10")

    async def test_move_j_no_speed_emits_6_args(self, robot_pair):
        """User example: ``&0,0,134,35,66,0`` (no speed)."""
        r, t = robot_pair
        t.on("&", lambda _l: ["Move OK"])
        await r.move_j([0, 0, 134, 35, 66, 0])  # speed omitted
        cmd = t.tx_log[-1]
        assert len(cmd.split(",")) == 6
        assert cmd == "&0.000,0.000,134.000,35.000,66.000,0.000"

    async def test_move_l_with_speed(self, robot_pair):
        r, t = robot_pair
        t.on("@", lambda _l: ["Move OK"])
        await r.move_l([100, 50, 200, 0, 0, 0], speed=5)
        assert t.tx_log[-1].endswith(",5")

    async def test_move_l_no_speed(self, robot_pair):
        """User example: ``@175,44,160,-135,-13.54,-14.6`` (no speed)."""
        r, t = robot_pair
        t.on("@", lambda _l: ["Move OK"])
        await r.move_l([175, 44, 160, -135, -13.54, -14.6])
        cmd = t.tx_log[-1]
        assert cmd.startswith("@") and len(cmd.split(",")) == 6
        assert cmd == "@175.000,44.000,160.000,-135.000,-13.540,-14.600"

    async def test_move_error_propagates(self, robot_pair):
        r, t = robot_pair
        t.on("&", lambda _l: ["Move Error: limit J1 Max"])
        with pytest.raises(RobotError, match="limit J1 Max"):
            await r.move_j([200, 0, 90, 0, 0, 0], speed=10)

    async def test_move_not_enabled_error(self, robot_pair):
        r, t = robot_pair
        t.on("@", lambda _l: ["Move Error: not_enabled"])
        with pytest.raises(RobotError, match="not_enabled"):
            await r.move_l([0, 0, 0, 0, 0, 0])


# =========================== diagnostics: online ==========================


class TestOnline:
    async def test_full_scan_all_online(self, robot_pair):
        r, t = robot_pair
        # Firmware emits the summary first, then 7 follow-ups.
        t.on("#ONLINE",
             lambda _l: [
                 "Online OK all_online",
                 "J[1] OL", "J[2] OL", "J[3] OL",
                 "J[4] OL", "J[5] OL", "J[6] OL",
                 "HAND[7] OL",
             ])
        result = await r.check_online(quiet_ms=80, total_timeout=2.0)
        assert result["summary"] == "all_online"
        assert result["nodes"] == {1: True, 2: True, 3: True, 4: True, 5: True, 6: True, 7: True}

    async def test_full_scan_has_offline(self, robot_pair):
        r, t = robot_pair
        t.on("#ONLINE",
             lambda _l: [
                 "Online OK has_offline",
                 "J[1] OL", "J[2] NL", "J[3] OL",
                 "J[4] OL", "J[5] OL", "J[6] OL",
                 "HAND[7] NL",
             ])
        result = await r.check_online(quiet_ms=80, total_timeout=2.0)
        assert result["summary"] == "has_offline"
        assert result["nodes"][2] is False
        assert result["nodes"][7] is False
        assert result["nodes"][1] is True

    async def test_single_node(self, robot_pair):
        r, t = robot_pair
        t.on("#ONLINE 3", lambda _l: ["Online OK node=3 OL"])
        result = await r.check_online(node=3, quiet_ms=80, total_timeout=2.0)
        assert result["summary"] == "node=3 OL"
        assert result["nodes"] == {3: True}

    async def test_bad_node_rejected_pre_send(self, robot_pair):
        r, t = robot_pair
        with pytest.raises(RobotError, match="node must be 1..7"):
            await r.check_online(node=8)
        assert t.tx_log == []


# =============================== reboot ===================================


class TestReboot:
    async def test_motor(self, robot_pair):
        r, t = robot_pair
        t.on("#REBOOT 3", lambda _l: ["Reboot OK motor=3"])
        resp = await r.reboot(3)
        assert resp.ok and resp.tag == "Reboot"
        assert "motor=3" in resp.rest

    async def test_hand(self, robot_pair):
        r, t = robot_pair
        t.on("#REBOOT 7", lambda _l: ["Reboot OK hand"])
        resp = await r.reboot(7)
        assert resp.ok and "hand" in resp.rest

    async def test_ref_resets_system(self, robot_pair):
        r, t = robot_pair
        t.on("#REBOOT 0", lambda _l: ["Reboot OK ref (reset in 100ms)"])
        resp = await r.reboot(0)
        assert resp.ok and "ref" in resp.rest

    async def test_all(self, robot_pair):
        r, t = robot_pair
        t.on("#REBOOT", lambda _l: ["Reboot OK all (reset in 100ms)"])
        resp = await r.reboot()  # node=None
        assert resp.ok and "all" in resp.rest
        assert t.tx_log == ["#REBOOT"]

    async def test_bad_node_rejected_pre_send(self, robot_pair):
        r, t = robot_pair
        with pytest.raises(RobotError, match="node must be 0..7"):
            await r.reboot(99)
        assert t.tx_log == []


# ============================= maintenance ================================


class TestMaintenance:
    async def test_offset_single_node(self, robot_pair):
        r, t = robot_pair
        t.on("#OFFSET_J 2", lambda _l: ["OffsetJ OK node=2"])
        resp = await r.offset_joint(2)
        assert resp.ok and "node=2" in resp.rest

    async def test_offset_all(self, robot_pair):
        r, t = robot_pair
        # Firmware emits this then resets — we model only the line that arrives.
        t.on("#OFFSET_J all", lambda _l: ["OffsetJ OK all rebooting..."])
        resp = await r.offset_joint("all")
        assert resp.ok

    async def test_offset_bad_target_rejected(self, robot_pair):
        r, t = robot_pair
        with pytest.raises(RobotError, match="node must be 1..7"):
            await r.offset_joint(8)
        with pytest.raises(RobotError, match="must be 'all'"):
            await r.offset_joint("foo")
        assert t.tx_log == []

    async def test_cmdmode(self, robot_pair):
        r, t = robot_pair
        t.on("#CMDMODE 2", lambda _l: ["CmdMode OK mode=2"])
        resp = await r.set_cmd_mode(2)
        assert resp.ok and "mode=2" in resp.rest

    async def test_cmdmode_bad(self, robot_pair):
        r, t = robot_pair
        with pytest.raises(RobotError, match="mode must be 1..4"):
            await r.set_cmd_mode(5)
        assert t.tx_log == []


# ============================= tuning: set ================================


class TestTuningSet:
    @pytest.mark.parametrize("kind, wire", [
        ("kp", "#SET_DCE_KP"),
        ("kv", "#SET_DCE_KV"),
        ("ki", "#SET_DCE_KI"),
        ("kd", "#SET_DCE_KD"),
    ])
    async def test_set_dce_gain(self, robot_pair, kind, wire):
        r, t = robot_pair
        t.on(wire, lambda l, k=kind: [
            f"SetDce{k.capitalize()} OK node={l.split()[1]} val={l.split()[2]}"
        ])
        resp = await r.set_dce_gain(node=3, kind=kind, value=42)
        assert resp.ok
        assert t.tx_log[-1] == f"{wire} 3 42"

    async def test_set_dce_gain_bad_node(self, robot_pair):
        r, t = robot_pair
        with pytest.raises(RobotError, match="node must be 1..6"):
            await r.set_dce_gain(node=7, kind="kp", value=10)
        assert t.tx_log == []

    async def test_set_dce_gain_bad_kind(self, robot_pair):
        r, t = robot_pair
        with pytest.raises(RobotError, match="kind must be"):
            await r.set_dce_gain(node=1, kind="zz", value=10)

    @pytest.mark.parametrize("kind, wire", [
        ("acc",     "#ACC_J"),
        ("speed",   "#SPEED_J"),
        ("i_limit", "#I_LIMIT_J"),
    ])
    async def test_set_motion_limit(self, robot_pair, kind, wire):
        r, t = robot_pair
        t.on(wire, lambda l: [
            "AccJ OK node=1 val=12.500" if l.startswith("#ACC_J") else
            "SpeedJ OK node=1 val=12.500" if l.startswith("#SPEED_J") else
            "ILimitJ OK node=1 val=12.500"
        ])
        await r.set_motion_limit(node=1, kind=kind, value=12.5)
        assert t.tx_log[-1] == f"{wire} 1 12.5"

    async def test_set_motion_limit_node_7_allowed(self, robot_pair):
        """Per firmware: motion limits accept node 1..7 (gripper included)."""
        r, t = robot_pair
        t.on("#SPEED_J", lambda _l: ["SpeedJ OK node=7 val=20.000"])
        await r.set_motion_limit(node=7, kind="speed", value=20)


# ============================= tuning: query ==============================


class TestGetDce:
    async def test_all_nodes(self, robot_pair):
        r, t = robot_pair
        # Firmware emits 14 lines: 7 headers + 7 payloads
        replies: list[str] = []
        for n in range(1, 8):
            replies.append(f"J{n}  OL:")
            replies.append(f"KP:{n*10}  KV:{n*20}  KI:{n*30}  KD:{n*40}")
        t.on("#GET_DCE", lambda _l: replies)
        result = await r.get_dce(quiet_ms=80, total_timeout=2.0)
        assert set(result["nodes"].keys()) == set(range(1, 8))
        assert result["nodes"][3]["kp"] == 30 and result["nodes"][3]["kd"] == 120
        assert result["nodes"][7]["online"] is True

    async def test_single_node(self, robot_pair):
        r, t = robot_pair
        t.on("#GET_DCE 4",
             lambda _l: ["J4  OL:", "KP:100  KV:200  KI:300  KD:400"])
        result = await r.get_dce(node=4, quiet_ms=80, total_timeout=2.0)
        assert list(result["nodes"].keys()) == [4]
        g = result["nodes"][4]
        assert g["kp"] == 100 and g["kv"] == 200 and g["ki"] == 300 and g["kd"] == 400

    async def test_single_param(self, robot_pair):
        r, t = robot_pair
        t.on("#GET_DCE 2 kp",
             lambda _l: ["J2  OL:", "KP:55"])
        result = await r.get_dce(node=2, param="kp", quiet_ms=80, total_timeout=2.0)
        assert result["nodes"][2]["kp"] == 55
        # Other params should NOT be set
        for k in ("kv", "ki", "kd"):
            assert k not in result["nodes"][2]

    async def test_offline_node(self, robot_pair):
        r, t = robot_pair
        t.on("#GET_DCE 5",
             lambda _l: ["J5  NL:", "KP:0  KV:0  KI:0  KD:0"])
        result = await r.get_dce(node=5, quiet_ms=80, total_timeout=2.0)
        assert result["nodes"][5]["online"] is False

    async def test_error(self, robot_pair):
        r, t = robot_pair
        t.on("#GET_DCE 9",
             lambda _l: ["GetDce Error: use #GET_DCE [1-7 [kp|kv|ki|kd]]"])
        # 9 fails our pre-send guard (1..7) — use 7 + bad param to actually
        # exercise the firmware error path.
        # Re-bind for valid wire grammar but bad param semantics:
        t._handlers.clear()
        t.on("#GET_DCE 7 zz",
             lambda _l: ["GetDce Error: param must be kp/kv/ki/kd"])
        # Robot's own validation rejects zz first → simulate by forcing through raw():
        with pytest.raises(RobotError, match="param must be kp/kv/ki/kd"):
            await r.get_dce(node=7, param="zz", quiet_ms=80, total_timeout=2.0)


# ====================== async events / out-of-band lines ==================


class TestAsyncEvents:
    async def test_unsolicited_event_ignored_until_matching_tag(self, robot_pair):
        """Stray async lines (e.g. user-pressed Estop) must not satisfy ``_exec``."""
        r, t = robot_pair
        t.on("!HOME", lambda _l: ["Home OK"])
        # Push a stray event BEFORE issuing the command — Robot._exec drains
        # stale responses, so this should be discarded.
        t.emit_async_event("Estop OK")
        await r.home()
        assert t.tx_log == ["!HOME"]

    async def test_command_timeout_propagates(self, robot_pair):
        r, t = robot_pair
        # Register no handler → Robot times out.
        with pytest.raises(RobotError, match="timeout"):
            # default_timeout=2.0 from fixture, but use even shorter via raw with timeout
            await r.raw("!HOME", timeout=0.2)


# ============================ tag map sanity ==============================


class TestTagCoverage:
    """Make sure every command we now have a typed method for is in COMMAND_TAGS."""

    @pytest.mark.parametrize("cmd", [
        "!START", "!STOP", "!CLOSE", "!HOME", "!RESET", "!PAUSE", "!RESUME",
        "!CALIBRATION",
        "!HD_O", "!HD_C", "!HD_EN", "!HD_DIS", "!HD_ZERO", "!HD_POS 0", "!HD_I 1",
        "!RGB_ON", "!RGB_OFF", "!RGB_MODE 0", "!RGB_CUSTOM 1 2 3",
        "#GETJPOS", "#GETLPOS", "#GETSPEED",
        "#ONLINE", "#GET_DCE",
        "#SET_DCE_KP 1 1", "#SET_DCE_KV 1 1", "#SET_DCE_KI 1 1", "#SET_DCE_KD 1 1",
        "#ACC_J 1 1", "#SPEED_J 1 1", "#I_LIMIT_J 1 1",
        "#REBOOT", "#OFFSET_J 1", "#CMDMODE 1",
        "&0,0,0,0,0,0", "@0,0,0,0,0,0", ">0,0,0,0,0,0",
    ])
    def test_expected_tag_no_keyerror(self, cmd):
        # Not async — pure function check
        tag = P.expected_tag(cmd)
        assert isinstance(tag, str) and tag != ""
