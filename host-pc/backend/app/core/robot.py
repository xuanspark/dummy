"""High-level async Robot API.

Each public coroutine sends one command and awaits the matching response,
returning a structured result or raising RobotError on failure / timeout.
"""
from __future__ import annotations

import asyncio
import logging
import re
import secrets
import time
from dataclasses import dataclass, field, replace
from typing import Optional

from app.core import protocol as P
from app.core.transport import Transport

log = logging.getLogger(__name__)


class RobotError(RuntimeError):
    """Generic robot/protocol error.

    Carries an optional :class:`protocol.ErrorCategory` so callers (UI, API)
    can map firmware-level reasons to user-facing classes (T12).
    """

    def __init__(self, message: str, category: "P.ErrorCategory | None" = None) -> None:
        super().__init__(message)
        self.category: "P.ErrorCategory" = category or P.ErrorCategory.UNKNOWN


@dataclass
class RobotState:
    connected: bool = False
    last_jpos: Optional[P.JointPos] = None
    last_lpos: Optional[P.CartPos] = None
    last_speed: Optional[float] = None
    motion_state: str = "UNKNOWN"
    log_tail: list[str] = field(default_factory=list)


class Robot:
    """Owns a Transport and serializes command/response transactions."""

    def __init__(self, transport: Transport, default_timeout: float = 5.0, correlated: bool = False) -> None:
        self._t = transport
        self._timeout = default_timeout
        self._cmd_lock = asyncio.Lock()
        self._sub = None  # set on start
        self.state = RobotState()
        self._dispatcher_task: Optional[asyncio.Task] = None
        self._pending: asyncio.Queue[P.Response] | None = None
        self._correlated = correlated
        self._request_id = secrets.randbelow(1_000_000_000) + 1
        self._suspended_total = 0.0
        self._suspended_since: float | None = None
        self._motion_limits: dict[int, dict[str, float]] = {}
        self._limits_task: Optional[asyncio.Task] = None
        self._limits_ready_event = asyncio.Event()
        self._limits_error: str | None = None
        self.limits_ready = False
        self.last_limit_warning = ""
        self.last_applied_command = ""

    # ---- lifecycle ----
    async def start(self) -> None:
        loop = asyncio.get_running_loop()
        self._t.start(loop)
        self._sub = self._t.subscribe()
        self._pending = asyncio.Queue(maxsize=64)
        self.state.connected = True
        self._dispatcher_task = asyncio.create_task(self._dispatch_loop(), name="robot-rx")
        if self._t.__class__.__name__ == "SerialTransport":
            self._limits_ready_event.clear()
            self._limits_task = asyncio.create_task(self._load_limits_on_connect(), name="robot-limits")
        else:
            self._limits_ready_event.set()

    async def _load_limits_on_connect(self) -> None:
        try:
            self._motion_limits.clear()
            await self.get_motion_limits()
            if not self._motion_limits:
                raise RobotError("连接后未读取到运动限制", P.ErrorCategory.COMM_TIMEOUT)
            self.limits_ready = True
            self._limits_error = None
            self.last_limit_warning = ""
        except asyncio.CancelledError:
            raise
        except Exception as error:  # noqa: BLE001
            self.limits_ready = False
            self._limits_error = str(error)
            self.last_limit_warning = "连接后读取运动限制失败，动作仍由固件最终限幅"
            log.warning("连接后读取运动限制失败: %s", error)
        finally:
            self._limits_ready_event.set()

    async def wait_limits(self, timeout: float = 10.0) -> None:
        """Wait until the connection-time limit query has completed."""
        await asyncio.wait_for(self._limits_ready_event.wait(), timeout=timeout)

    @property
    def limits_error(self) -> str | None:
        return self._limits_error

    async def stop(self) -> None:
        if self._limits_task:
            self._limits_task.cancel()
            try:
                await self._limits_task
            except (asyncio.CancelledError, Exception):
                pass
            self._limits_task = None
        if self._dispatcher_task:
            self._dispatcher_task.cancel()
            try:
                await self._dispatcher_task
            except (asyncio.CancelledError, Exception):
                pass
        if self._sub:
            self._t.unsubscribe(self._sub)
        self._t.stop()
        self.state.connected = False

    # ---- background: classify each RX line, route structured responses to
    #      whichever command is awaiting; cache state from spontaneous lines.
    async def _dispatch_loop(self) -> None:
        assert self._sub is not None and self._pending is not None
        while True:
            # Each iteration is independently fault-isolated. Without this
            # wrapper, a single malformed line could raise an unhandled
            # exception, kill the task, and leave every subsequent ``_exec``
            # silently timing out (the symptom is "post-#GET_DCE poison":
            # one bad command wedges the whole pipeline forever).
            try:
                line = await self._sub.queue.get()
            except asyncio.CancelledError:
                raise
            except Exception:  # noqa: BLE001
                log.exception("dispatcher: queue.get() raised; continuing")
                continue
            try:
                self._record_log(line)
                self._update_motion_state(line)
                resp = P.parse_response(line)
                if resp is None:
                    continue
                # Cache common state-bearing responses
                if resp.ok and resp.tag == "GetJPos":
                    jp = P.parse_jpos(resp.rest)
                    if jp:
                        self.state.last_jpos = jp
                elif resp.ok and resp.tag == "GetLPos":
                    lp = P.parse_lpos(resp.rest)
                    if lp:
                        self.state.last_lpos = lp
                elif resp.ok and resp.tag == "GetSpeed":
                    # Firmware: "GetSpeed OK SPEED:%.2f"
                    token = resp.rest.split()[0] if resp.rest else ""
                    if token.upper().startswith("SPEED:"):
                        token = token.split(":", 1)[1]
                    try:
                        self.state.last_speed = float(token)
                    except (ValueError, IndexError):
                        pass
                try:
                    self._pending.put_nowait(resp)
                except asyncio.QueueFull:
                    log.warning("pending queue full, dropping response: %s", resp.raw)
            except asyncio.CancelledError:
                raise
            except Exception:  # noqa: BLE001
                # Never let a malformed firmware line take the dispatcher
                # down — log full traceback so we can debug post-hoc.
                log.exception("dispatcher: error processing line %r; continuing", line)

    # ---- internal: process async/out-of-band events ----
    _SAFETY_TAGS = {"Stop", "Pause", "Resume", "Estop", "Emergency"}

    def _handle_async_event(self, resp: P.Response) -> None:
        """Process safety-critical or state-bearing out-of-band responses.

        Called when a response is drained before a new command or arrives
        with a tag that doesn't match the in-flight command.  Instead of
        silently dropping it, we update cached state so the upper layer
        can observe STOP / ERROR events.
        """
        if resp.tag in self._SAFETY_TAGS:
            log.warning("async safety event: %s (ok=%s rest=%s)", resp.tag, resp.ok, resp.rest)
        elif not resp.ok:
            log.warning("async error event: tag=%s error=%s", resp.tag, resp.error)

    def _record_log(self, line: str) -> None:
        tail = self.state.log_tail
        tail.append(line)
        if len(tail) > 200:
            del tail[: len(tail) - 200]

    def _update_motion_state(self, line: str) -> None:
        _, payload = P.unwrap_response(line)
        match = re.search(r"\bSTATE[:=](NORMAL|PAUSED|EMERGENCY_STOP)\b", payload, re.I)
        if match:
            state = match[1].upper()
            blocked = state in {"PAUSED", "EMERGENCY_STOP"}
            if blocked and self._suspended_since is None:
                self._suspended_since = time.monotonic()
            elif not blocked and self._suspended_since is not None:
                self._suspended_total += time.monotonic() - self._suspended_since
                self._suspended_since = None
            self.state.motion_state = state

    def _active_time(self, action: bool) -> float:
        now = time.monotonic()
        if not action:
            return now
        paused = self._suspended_total
        if self._suspended_since is not None:
            paused += now - self._suspended_since
        return now - paused

    def _next_request(self, command: str) -> tuple[int, str]:
        self._request_id = self._request_id % 2_000_000_000 + 1
        return self._request_id, f"T:{self._request_id} {command}"

    async def _exchange(self, command: str, timeout: float, collect: bool = False) -> list[str]:
        """按事务编号等待响应；动作等待期间仍可查询、急停和恢复。"""
        request, wire = self._next_request(command)
        tag = P.expected_tag(command)
        sub = self._t.subscribe()
        lines: list[str] = []
        action = command.startswith(("&", "@", ">", "!START", "!HOME", "!RESET", "!CLOSE", "!HD_O", "!HD_C", "!HD_POS"))
        remaining = timeout
        queued = False
        executing = False
        previous = self._active_time(action)
        try:
            self._t.write_line(wire)
            while remaining > 0:
                try:
                    raw = await asyncio.wait_for(sub.queue.get(), timeout=min(remaining, 0.2))
                except asyncio.TimeoutError:
                    raw = None
                if raw is not None:
                    self._update_motion_state(raw)
                now = self._active_time(action)
                if not queued:
                    remaining -= now - previous
                previous = now
                if raw is None:
                    if not getattr(self._t, "connected", self.state.connected):
                        raise RobotError("设备已断开", P.ErrorCategory.COMM_TIMEOUT)
                    continue
                response_id, line = P.unwrap_response(raw)
                if response_id != request:
                    continue
                if action and line.startswith(f"{tag} Accepted"):
                    queued = not executing
                    continue
                if action and line.startswith("Task Executing"):
                    queued = False
                    executing = True
                    previous = self._active_time(action)
                    continue
                response = P.parse_response(line)
                if response and response.tag == tag and not response.ok:
                    raise RobotError(f"{tag} failed: {response.error}", response.error_category)
                if collect:
                    lines.append(line)
                    if line == f"{tag} End" or (response and response.tag == tag and tag in {"GetDce", "GetPid"}):
                        return lines
                elif response and response.tag == tag:
                    return [line]
            raise RobotError(f"timeout waiting for {tag}", P.ErrorCategory.COMM_TIMEOUT)
        except RobotError:
            raise
        except Exception as error:
            raise RobotError(f"通信失败：{error}", P.ErrorCategory.COMM_TIMEOUT) from error
        finally:
            self._t.unsubscribe(sub)

    # ---- internal: priority send for safety commands (bypasses _cmd_lock) ----
    async def _send_priority(self, cmd: str, timeout: Optional[float] = None) -> P.Response:
        """Send a safety-critical command without waiting for _cmd_lock.

        STOP / PAUSE / RESUME must not be delayed by in-flight queries or
        long-running transactions.  The command is written directly to the
        transport; we then wait for the matching tag on a *dedicated*
        subscription so we don't steal tokens from ``_exec``.
        """
        try:
            tag = P.expected_tag(cmd)
        except (KeyError, ValueError) as error:
            raise RobotError("不支持的指令", P.ErrorCategory.BAD_ARGS) from error
        timeout = timeout if timeout is not None else 2.0
        if self._correlated:
            return P.parse_response((await self._exchange(cmd, timeout))[0])
        sub = self._t.subscribe()
        try:
            try:
                self._t.write_line(cmd)
            except Exception as e:  # noqa: BLE001
                raise RobotError(f"transport error: {e}", P.ErrorCategory.COMM_TIMEOUT) from e
            deadline = asyncio.get_event_loop().time() + timeout
            while True:
                remaining = deadline - asyncio.get_event_loop().time()
                if remaining <= 0:
                    raise RobotError(f"timeout waiting for {tag} response to {cmd!r}", P.ErrorCategory.COMM_TIMEOUT)
                try:
                    line = await asyncio.wait_for(sub.queue.get(), timeout=remaining)
                except asyncio.TimeoutError as e:
                    raise RobotError(f"timeout waiting for {tag} response to {cmd!r}", P.ErrorCategory.COMM_TIMEOUT) from e
                resp = P.parse_response(line)
                if resp is not None and resp.tag == tag:
                    if not resp.ok:
                        raise RobotError(f"{tag} failed: {resp.error}", resp.error_category)
                    return resp
        finally:
            self._t.unsubscribe(sub)

    # ---- internal: send + await matching response ----
    async def _exec(self, cmd: str, timeout: Optional[float] = None) -> P.Response:
        try:
            cmd = P.normalize_command(cmd)
        except ValueError as error:
            raise RobotError(str(error), P.ErrorCategory.BAD_ARGS) from error
        try:
            tag = P.expected_tag(cmd)
        except (KeyError, ValueError) as error:
            raise RobotError("不支持的指令", P.ErrorCategory.BAD_ARGS) from error
        timeout = timeout if timeout is not None else self._timeout
        if self._correlated:
            return P.parse_response((await self._exchange(cmd, timeout))[0])
        async with self._cmd_lock:
            assert self._pending is not None
            # Drain stale responses left over from spurious events
            while not self._pending.empty():
                try:
                    stale = self._pending.get_nowait()
                except asyncio.QueueEmpty:
                    break
                log.info("drain: discarding stale response: %s", stale.raw)
                self._handle_async_event(stale)
            # Send
            try:
                self._t.write_line(cmd)
            except Exception as e:  # noqa: BLE001
                raise RobotError(f"transport error: {e}", P.ErrorCategory.COMM_TIMEOUT) from e
            # Await matching tag
            deadline = asyncio.get_event_loop().time() + timeout
            while True:
                remaining = deadline - asyncio.get_event_loop().time()
                if remaining <= 0:
                    raise RobotError(f"timeout waiting for {tag} response to {cmd!r}", P.ErrorCategory.COMM_TIMEOUT)
                try:
                    resp = await asyncio.wait_for(self._pending.get(), timeout=remaining)
                except asyncio.TimeoutError as e:
                    raise RobotError(f"timeout waiting for {tag} response to {cmd!r}", P.ErrorCategory.COMM_TIMEOUT) from e
                if resp.tag == tag:
                    if not resp.ok:
                        raise RobotError(f"{tag} failed: {resp.error}", resp.error_category)
                    return resp
                # Different tag (likely async event like Estop) — log and process, keep waiting
                log.info("out-of-band event while waiting for %s: %s", tag, resp.raw)
                self._handle_async_event(resp)

    # ---- public commands ----
    async def start_robot(self) -> None:
        await self._exec("!START", timeout=300.0)

    async def shutdown_robot(self) -> None:
        await self._exec("!CLOSE", timeout=300.0)

    async def home(self) -> None:
        await self._exec("!HOME", timeout=300.0)

    async def reset_pose(self) -> None:
        await self._exec("!RESET", timeout=300.0)

    async def estop(self) -> None:
        await self._send_priority("!STOP", timeout=2.0)

    async def pause(self) -> None:
        await self._send_priority("!PAUSE", timeout=2.0)

    async def resume(self) -> None:
        await self._send_priority("!RESUME", timeout=2.0)

    def _clamp_speed(self, speed: Optional[float], limits: dict[int, dict[str, float]]) -> tuple[Optional[float], str]:
        if speed is None:
            return None, ""
        caps = [v["speed"] for v in limits.values()
                if "speed" in v and v["speed"] > 0]
        if not caps:
            raise RobotError("实时速度限制不可用", P.ErrorCategory.COMM_TIMEOUT)
        applied = min([speed, *caps])
        if applied < speed:
            return applied, f"速度 {speed:g} 超过已保存上限，已按 {applied:g} 执行"
        return speed, ""

    async def _prepare_limit_command(self, command: str) -> str:
        """Clamp manual motion/current commands using the connection snapshot."""
        self.last_limit_warning = ""
        self.last_applied_command = command
        if command[:1] in "&@>":
            parts = command.split(",")
            if len(parts) >= 7:
                try:
                    requested = float(parts[-1])
                except ValueError:
                    return command
                limits = await self.get_motion_limits()
                applied, warning = self._clamp_speed(requested, limits)
                if applied is not None and applied < requested:
                    parts[-1] = f"{applied:g}"
                    command = ",".join(parts)
                    self.last_limit_warning = warning
        elif command.upper().startswith("!HD_I "):
            try:
                requested = float(command.split(None, 1)[1])
            except (IndexError, ValueError):
                return command
            limits = await self.get_motion_limits(7)
            limit = limits.get(7, {}).get("i_limit")
            if limit is None:
                raise RobotError("实时手爪电流限制不可用", P.ErrorCategory.COMM_TIMEOUT)
            if requested > limit:
                command = f"!HD_I {limit:g}"
                self.last_limit_warning = f"手爪电流 {requested:g} 超过已保存上限，已按 {limit:g} 执行"
        self.last_applied_command = command
        return command

    async def move_j(self, j: list[float], speed: Optional[float] = None) -> dict:
        """``&j1..j6[,speed]`` — pass ``speed=None`` to use the firmware's current speed."""
        requested_speed = speed
        limits = await self.get_motion_limits() if speed is not None else {}
        speed, warning = self._clamp_speed(speed, limits)
        self.last_limit_warning = warning
        command = P.cmd_move_j(j, speed)
        self.last_applied_command = command
        await self._exec(command, timeout=300.0)
        return {"requested_speed": requested_speed, "applied_speed": speed, "warning": self.last_limit_warning}

    async def move_l(self, pose: list[float], speed: Optional[float] = None) -> dict:
        """``@x,y,z,a,b,c[,speed]`` — pass ``speed=None`` to use the firmware's current speed."""
        requested_speed = speed
        limits = await self.get_motion_limits() if speed is not None else {}
        speed, warning = self._clamp_speed(speed, limits)
        self.last_limit_warning = warning
        command = P.cmd_move_l(pose, speed)
        self.last_applied_command = command
        await self._exec(command, timeout=300.0)
        return {"requested_speed": requested_speed, "applied_speed": speed, "warning": self.last_limit_warning}

    async def hand_open(self) -> None:
        await self._exec("!HD_O", timeout=300.0)

    async def hand_close(self) -> None:
        await self._exec("!HD_C", timeout=300.0)

    async def hand_enable(self, enable: bool) -> None:
        await self._exec("!HD_EN" if enable else "!HD_DIS", timeout=10.0)

    async def hand_pos(self, percent: int) -> None:
        await self._exec(P.cmd_hand_pos(percent), timeout=300.0)

    async def rgb_on(self) -> None:
        await self._exec("!RGB_ON")

    async def rgb_off(self) -> None:
        await self._exec("!RGB_OFF")

    async def rgb_mode(self, mode: int) -> None:
        await self._exec(P.cmd_rgb_mode(mode))

    async def rgb_custom(self, r: int, g: int, b: int) -> None:
        await self._exec(P.cmd_rgb_custom(r, g, b))

    async def get_jpos(self) -> P.JointPos:
        resp = await self._exec("#GETJPOS")
        jp = P.parse_jpos(resp.rest)
        if jp is None:
            raise RobotError(f"malformed GetJPos payload: {resp.rest!r}")
        return jp

    async def get_lpos(self) -> P.CartPos:
        resp = await self._exec("#GETLPOS")
        lp = P.parse_lpos(resp.rest)
        if lp is None:
            raise RobotError(f"malformed GetLPos payload: {resp.rest!r}")
        return lp

    async def get_speed(self) -> float:
        resp = await self._exec("#GETSPEED")
        # Firmware: "GetSpeed OK SPEED:%.2f"  →  rest="SPEED:25.50"
        # Tolerate both "SPEED:N" and a bare number for forward-compatibility.
        token = resp.rest.split()[0] if resp.rest else ""
        if token.upper().startswith("SPEED:"):
            token = token.split(":", 1)[1]
        try:
            return float(token)
        except (ValueError, IndexError) as e:
            raise RobotError(f"malformed GetSpeed payload: {resp.rest!r}") from e

    async def raw(self, cmd: str, timeout: Optional[float] = None) -> P.Response:
        """Escape hatch for arbitrary commands."""
        try:
            cmd = P.normalize_command(cmd)
        except ValueError as error:
            raise RobotError(str(error), P.ErrorCategory.BAD_ARGS) from error
        cmd = await self._prepare_limit_command(cmd)
        warning = self.last_limit_warning
        applied_command = cmd
        if cmd in {"!STOP", "!PAUSE", "!RESUME"}:
            response = await self._send_priority(cmd, timeout)
            return replace(response, warning=warning, applied_command=applied_command)
        if cmd in {"!DISABLE"}:
            raise RobotError("请使用 START 或 CLOSE 管理使能状态", P.ErrorCategory.STATE_BLOCKED)
        response = await self._exec(cmd, timeout=timeout or (300 if cmd.startswith(("&", "@", ">", "!START", "!CLOSE", "!HOME", "!RESET", "!HD_")) else None))
        return replace(response, warning=warning, applied_command=applied_command)

    # ---- internal: multi-line command with quiet-window aggregation ----
    async def _collect_lines(self, cmd: str, start_predicate,
                             quiet_ms: int = 400,
                             total_timeout: float = 5.0) -> list[str]:
        """Send ``cmd`` and collect subsequent RX lines.

        Uses a fresh :class:`Transport` subscription so no lines are missed
        (the main dispatcher keeps its own subscription). Reads until
        ``quiet_ms`` elapses without a new line after ``start_predicate(line)``
        first matches; raises :class:`RobotError` if that predicate never
        matches within ``total_timeout``.

        This is the pattern needed by firmware replies that span multiple
        plain-text lines with no single "OK" tag to wait on (e.g. ``#ONLINE``
        full-scan, ``#GET_DCE``).
        """
        if self._correlated:
            return await self._exchange(cmd, total_timeout, collect=True)
        async with self._cmd_lock:
            sub = self._t.subscribe()
            try:
                try:
                    self._t.write_line(cmd)
                except Exception as e:  # noqa: BLE001
                    raise RobotError(f"transport error: {e}", P.ErrorCategory.COMM_TIMEOUT) from e

                loop = asyncio.get_event_loop()
                deadline = loop.time() + total_timeout
                quiet = quiet_ms / 1000.0
                lines: list[str] = []
                got_first = False
                while True:
                    remaining = deadline - loop.time()
                    if remaining <= 0:
                        break
                    wait = quiet if got_first else remaining
                    try:
                        line = await asyncio.wait_for(sub.queue.get(), timeout=wait)
                    except asyncio.TimeoutError:
                        if got_first:
                            break
                        raise RobotError(f"timeout waiting for {cmd!r} response", P.ErrorCategory.COMM_TIMEOUT)
                    line = line.strip()
                    if not got_first:
                        if start_predicate(line):
                            got_first = True
                            lines.append(line)
                        # else: stray line — ignore until the trigger arrives
                    else:
                        lines.append(line)
                return lines
            finally:
                self._t.unsubscribe(sub)

    # ---- diagnostics ----
    async def get_status(self, quiet_ms: int = 400,
                         total_timeout: float = 5.0) -> dict:
        """Query CAN health and node status via ``#STATUS``.

        Firmware emits ``Status OK STATE:.. CAN_RX:.. CAN_DROP:..`` followed
        by per-node ``J[n] OL|NL age=Nms`` / ``HD[7] OL|NL age=Nms`` lines.

        Returns::

            {"state": "NORMAL", "can_rx": 12345, "can_drop": 0,
             "nodes": {1: {"online": True, "age_ms": 42}, ...},
             "raw": [...]}
        """
        lines = await self._collect_lines(
            "#STATUS",
            start_predicate=lambda ln: "Status OK" in ln or "Status Error" in ln,
            quiet_ms=quiet_ms,
            total_timeout=total_timeout,
        )
        result: dict = {"state": None, "can_rx": 0, "can_drop": 0, "nodes": {}, "raw": lines}
        for line in lines:
            # Header: "Status OK STATE:NORMAL CAN_RX:123 CAN_DROP:0"
            m = re.search(r"STATE:(\S+)\s+CAN_RX:(\d+)\s+CAN_DROP:(\d+)", line)
            if m:
                result["state"] = m.group(1)
                result["can_rx"] = int(m.group(2))
                result["can_drop"] = int(m.group(3))
                continue
            # Node: "J[1] OL age=42ms" or "HD[7] NL age=4294967295ms"
            m = re.match(r"(?:J|HD)\[(\d+)\]\s+(OL|NL)\s+age=(\d+)ms", line)
            if m:
                nid = int(m.group(1))
                result["nodes"][nid] = {
                    "online": m.group(2) == "OL",
                    "age_ms": int(m.group(3)),
                }
        return result

    async def check_online(self, node: Optional[int] = None, quiet_ms: int = 400,
                           total_timeout: float = 5.0) -> dict:
        """Query node online status via ``#ONLINE`` and aggregate multi-line reply.

        Firmware emits one ``Online OK <summary>`` line first, optionally
        followed by ``J[n] OL|NL`` / ``HAND[7] OL|NL`` lines.

        Returns::

            {"summary": "all_online" | "has_offline" | "node=1 OL" | ...,
             "nodes":   {1: True, 2: False, ...},
             "raw":     ["Online OK all_online", "J[1] OL", ...]}
        """
        if node is not None and not (1 <= node <= 7):
            raise RobotError(f"online: node must be 1..7, got {node}", P.ErrorCategory.BAD_ARGS)
        cmd = f"#ONLINE {node}" if node is not None else "#ONLINE"

        lines = await self._collect_lines(
            cmd,
            start_predicate=lambda ln: "Online OK" in ln or "Online Error" in ln,
            quiet_ms=quiet_ms,
            total_timeout=total_timeout,
        )

        summary: Optional[str] = None
        nodes: dict[int, bool] = {}
        for line in lines:
            r = P.parse_response(line)
            if r and r.tag == "Online":
                if not r.ok:
                    raise RobotError(f"Online failed: {r.error}", r.error_category)
                summary = r.rest
                m = re.match(r"node=(\d+)\s+(OL|NL)", r.rest)
                if m:
                    nodes[int(m.group(1))] = m.group(2) == "OL"
                continue
            m = re.match(r"(?:J|HAND)\[(\d+)\]\s+(OL|NL)", line)
            if m:
                nodes[int(m.group(1))] = m.group(2) == "OL"
        return {"summary": summary, "nodes": nodes, "raw": lines}

    async def reboot(self, node: Optional[int] = None) -> P.Response:
        """``#REBOOT [node]`` where node: 0=ref MCU, 1-6=joint motor, 7=hand, None=all.

        Both ``#REBOOT`` (all) and ``#REBOOT 0`` trigger a ``HAL_NVIC_SystemReset()``
        after ~100 ms; the reply arrives before the reset, so a short timeout
        suffices. Per-motor reboots (1-7) do not reset the ref board.
        """
        if node is not None and not (0 <= node <= 7):
            raise RobotError(f"reboot: node must be 0..7, got {node}", P.ErrorCategory.BAD_ARGS)
        cmd = f"#REBOOT {node}" if node is not None else "#REBOOT"
        return await self._exec(cmd, timeout=3.0)

    # ---- maintenance ----
    async def calibrate(self) -> None:
        """``!CALIBRATION`` — run ``DummyRobot::CalibrateHomeOffset()``.

        Firmware responds with ``Calibration OK`` synchronously after the
        routine completes. Timeout is generous as calibration can drive all
        joints through a small motion.
        """
        await self._exec("!CALIBRATION", timeout=300.0)

    async def offset_joint(self, target) -> P.Response:
        """``#OFFSET_J <1-7|all>`` — persist current position as home offset (writes NVS).

        ``target`` accepts the ints 1..6 (joints), 7 (hand), or the string
        ``"all"`` / ``"ALL"``. The ``all`` variant triggers a
        ``HAL_NVIC_SystemReset()`` ~200 ms after responding; single-node
        variants return ``OffsetJ OK node=N`` and leave the system running.
        """
        if isinstance(target, str):
            key = target.strip().lower()
            if key != "all":
                raise RobotError(f"offset_joint: string target must be 'all', got {target!r}", P.ErrorCategory.BAD_ARGS)
            cmd = "#OFFSET_J all"
            timeout = 3.0  # reply comes before system reset
        else:
            node = int(target)
            if not (1 <= node <= 7):
                raise RobotError(f"offset_joint: node must be 1..7, got {node}", P.ErrorCategory.BAD_ARGS)
            cmd = f"#OFFSET_J {node}"
            timeout = 5.0
        return await self._exec(cmd, timeout=timeout)

    async def set_cmd_mode(self, mode: int) -> P.Response:
        """``#CMDMODE <mode>`` — set ``DummyRobot::commandMode``.

        Valid modes (from ``main.cpp`` OLED names ``cmdModeNames[commandMode-1]``)::

            1 = COMMAND_TARGET_POINT_SEQUENTIAL   ("SEQ")
            2 = COMMAND_TARGET_POINT_INTERRUPTABLE("INT")
            3 = COMMAND_CONTINUES_TRAJECTORY      ("TRJ")
            4 = COMMAND_MOTOR_TUNING              ("TUN")
        """
        if not (1 <= int(mode) <= 4):
            raise RobotError(f"set_cmd_mode: mode must be 1..4, got {mode}", P.ErrorCategory.BAD_ARGS)
        return await self._exec(f"#CMDMODE {int(mode)}", timeout=3.0)

    # ---- hand ----
    async def hand_zero(self) -> tuple[float, float]:
        """``!HD_ZERO`` — run gripper auto-calibration; returns (closed_angle, opened_angle)."""
        resp = await self._exec("!HD_ZERO", timeout=300.0)
        # Response rest: "MIN:X MAX:Y"
        m = re.search(r"MIN:\s*(-?\d+(?:\.\d+)?)\s+MAX:\s*(-?\d+(?:\.\d+)?)", resp.rest)
        if not m:
            raise RobotError(f"malformed HD_ZERO payload: {resp.rest!r}")
        return float(m.group(1)), float(m.group(2))

    async def hand_current(self, amps: float) -> dict:
        """``!HD_I <amps>`` — set gripper current limit (strict range 0 < amps ≤ 2)."""
        a = float(amps)
        if not (0.0 < a <= 2.0):
            raise RobotError(f"hand_current: amps must be in (0, 2.0], got {a}", P.ErrorCategory.BAD_ARGS)
        self.last_limit_warning = ""
        # Firmware uses %f parse; trim trailing zeros for cleaner wire output
        s = f"{a:.3f}".rstrip('0').rstrip('.')
        requested_amps = amps
        fresh_limits = await self.get_motion_limits(7)
        limit = fresh_limits.get(7, {}).get("i_limit")
        if limit is None:
            raise RobotError("实时手爪电流限制不可用", P.ErrorCategory.COMM_TIMEOUT)
        if limit is not None:
            amps = min(amps, limit)
            self.last_limit_warning = (
                f"手爪电流 {requested_amps:g} 超过已保存上限，已按 {amps:g} 执行"
                if amps < requested_amps else ""
            )
        s = f"{amps:.6f}".rstrip('0').rstrip('.') or "0"
        command = f"!HD_I {s}"
        self.last_applied_command = command
        await self._exec(command)
        return {"requested_amps": float(requested_amps), "applied_amps": amps,
                "warning": self.last_limit_warning}

    # ---- DCE tuning ----
    _DCE_KINDS = {"kp": "KP", "kv": "KV", "ki": "KI", "kd": "KD"}

    async def set_dce_gain(self, node: int, kind: str, value: int) -> P.Response:
        """``#SET_DCE_K{P,V,I,D} <node> <value>`` — node 1..6, value uint32."""
        if not (1 <= int(node) <= 7):
            raise RobotError(f"set_dce_gain: node must be 1..7, got {node}", P.ErrorCategory.BAD_ARGS)
        k = kind.strip().lower()
        if k not in self._DCE_KINDS:
            raise RobotError(f"set_dce_gain: kind must be kp/kv/ki/kd, got {kind!r}", P.ErrorCategory.BAD_ARGS)
        v = int(value)
        if not 0 <= v <= 2147483647:
            raise RobotError(f"set_dce_gain: value must be >= 0, got {value}", P.ErrorCategory.BAD_ARGS)
        cmd = f"#SET_DCE_{self._DCE_KINDS[k]} {int(node)} {v}"
        return await self._exec(cmd)

    async def set_pid(self, node: int, values: dict[str, int]) -> P.Response:
        """设置单节点的单个或全部 PIDV，参数顺序为 KP、KI、KD、KV。"""
        if not 1 <= node <= 7 or not values or any(k not in self._DCE_KINDS for k in values):
            raise RobotError("PIDV 节点或参数无效", P.ErrorCategory.BAD_ARGS)
        if any(type(v) is not int or not 0 <= v <= 2147483647 for v in values.values()):
            raise RobotError("PIDV 参数必须为非负的 32 位有符号整数", P.ErrorCategory.BAD_ARGS)
        if len(values) == 1:
            kind, value = next(iter(values.items()))
            cmd = f"#SETPID {node} {kind} {value}"
        elif set(values) == set(self._DCE_KINDS):
            cmd = f"#SETPID {node} " + " ".join(str(values[k]) for k in ("kp", "ki", "kd", "kv"))
        else:
            raise RobotError("请提供单个参数或全部四个参数", P.ErrorCategory.BAD_ARGS)
        return await self._exec(cmd, timeout=12)

    async def get_dce(self, node: Optional[int] = None,
                      param: Optional[str] = None,
                      quiet_ms: int = 400,
                      total_timeout: float = 6.0, pid: bool = False) -> dict:
        """``#GET_DCE [node [param]]`` — query PID gains.

        Firmware emits unstructured multi-line output without an "OK" tag
        (``J<n>  OL|NL:`` followed by ``KP:.. KV:.. KI:.. KD:..``), so we
        aggregate lines via the quiet-window pattern.

        Returns::

            {"nodes": {1: {"online": True, "kp": .., "kv": .., "ki": .., "kd": ..}, ...},
             "raw":   [...]}

        When a single ``param`` is queried only that key is populated.
        """
        if node is not None and not (1 <= node <= 7):
            raise RobotError(f"get_dce: node must be 1..7, got {node}", P.ErrorCategory.BAD_ARGS)
        if param is not None and param.strip().lower() not in self._DCE_KINDS:
            raise RobotError(f"get_dce: param must be kp/kv/ki/kd, got {param!r}", P.ErrorCategory.BAD_ARGS)

        if param is not None and node is None:
            raise RobotError("单参数查询需要指定节点", P.ErrorCategory.BAD_ARGS)
        cmd = "#GETPID" if pid else "#GET_DCE"
        if node is not None:
            cmd += f" {int(node)}"
            if param is not None:
                cmd += f" {param.strip().lower()}"

        # Start trigger: either an error line or the first "J<n>  OL|NL:" header
        def is_start(ln: str) -> bool:
            if ln.startswith(("GetDce Error", "GetPid Error")):
                return True
            return bool(re.match(r"J\d+\s+(OL|NL):", ln))

        lines = await self._collect_lines(cmd, is_start, quiet_ms=quiet_ms,
                                          total_timeout=total_timeout)

        # Error path
        for ln in lines:
            if ln.startswith(("GetDce Error", "GetPid Error")):
                raise RobotError(ln)

        # Parse interleaved pairs: "J<n>  OL|NL:" then a payload line
        nodes: dict[int, dict] = {}
        current_node: Optional[int] = None
        current_online: Optional[bool] = None
        for ln in lines:
            mh = re.match(r"J(\d+)\s+(OL|NL):", ln)
            if mh:
                current_node = int(mh.group(1))
                current_online = mh.group(2) == "OL"
                nodes.setdefault(current_node, {"online": current_online})
                nodes[current_node]["online"] = current_online
                continue
            if current_node is None:
                continue
            # Payload: "KP:V  KV:V  KI:V  KD:V" (full) or "KP:V" (single param)
            for pm in re.finditer(r"(KP|KV|KI|KD)\s*:\s*(-?\d+)", ln):
                nodes[current_node][pm.group(1).lower()] = int(pm.group(2))
        return {"nodes": nodes, "raw": lines}

    # ---- per-joint motion limits ----
    _MOTION_CMDS = {"acc": "#ACC_J", "speed": "#SPEED_J", "i_limit": "#I_LIMIT_J"}

    async def set_motion_limit(self, node: int, kind: str, value: float) -> P.Response:
        """Set one of ``#ACC_J`` / ``#SPEED_J`` / ``#I_LIMIT_J`` for node 1..7."""
        if not (1 <= int(node) <= 7):
            raise RobotError(f"set_motion_limit: node must be 1..7, got {node}")
        k = kind.strip().lower()
        if k not in self._MOTION_CMDS:
            raise RobotError(
                f"set_motion_limit: kind must be acc/speed/i_limit, got {kind!r}")
        v = float(value)
        if v < 0:
            raise RobotError(f"set_motion_limit: value must be >= 0, got {value}")
        if k == "acc" and v <= 0:
            raise RobotError("加速度必须大于 0")
        # Format float cleanly; firmware parses %f
        s = f"{v:.6f}".rstrip('0').rstrip('.') or "0"
        response = await self._exec(f"{self._MOTION_CMDS[k]} {int(node)} {s}")
        self._motion_limits.setdefault(int(node), {})[k] = v
        return response

    async def get_motion_limits(self, node: Optional[int] = None) -> dict[int, dict[str, float]]:
        """读取保存限制与当前有效值，并缓存保存限制供动作命令限幅。"""
        cmd = "#GETLIMIT all" if node is None else f"#GETLIMIT {int(node)}"
        fresh: dict[int, dict[str, float]] = {}
        lines = await self._collect_lines(cmd, lambda line: line.startswith("GetLimit Begin") or line.startswith("GetLimit Error"), quiet_ms=120, total_timeout=3.0)
        if any(line.startswith("GetLimit Error") for line in lines):
            self._motion_limits.clear()
            raise RobotError("读取运动限制失败", P.ErrorCategory.COMM_TIMEOUT)
        for line in lines:
            match = re.match(r"LIMIT NODE:(\d+) (ACC|SPEED|CURRENT):([0-9.+-eE]+)", line)
            if match:
                key = {"ACC": "acc", "SPEED": "speed", "CURRENT": "i_limit"}[match[2]]
                fresh.setdefault(int(match[1]), {})[key] = float(match[3])
        expected_nodes = [int(node)] if node is not None else list(range(1, 8))
        if any(set(fresh.get(item, {})) != {"acc", "speed", "i_limit"} for item in expected_nodes):
            self._motion_limits.clear()
            raise RobotError("实时运动限制参数不完整", P.ErrorCategory.COMM_TIMEOUT)
        self._motion_limits = fresh
        if node is None:
            return fresh
        return {int(node): dict(fresh[int(node)])}
