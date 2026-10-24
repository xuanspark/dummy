"""ASCII protocol layer (pure functions, IO-free).

Firmware reference:
  - dummy-ref-core-fw/UserApp/protocols/ascii_protocol.cpp

Response conventions:
  - Success: "<Cmd> OK [...]"     e.g. "Move OK", "GetJPos OK 0.0 -75.0 ..."
  - Failure: "<Cmd> Error: <reason>"
  - Asynchronous events (no command initiated): "Estop OK", "Resume OK",
    "Estop Cleared OK", etc.

Error vocabulary (T12 — canonical reasons emitted by firmware):
  - bad_args / use ...    →  ErrorCategory.BAD_ARGS       (参数错误)
  - not_homed             →  ErrorCategory.NOT_READY      (未归零)
  - not_enabled           →  ErrorCategory.NOT_READY      (未使能)
  - state=...             →  ErrorCategory.STATE_BLOCKED  (状态不允许)
  - bad_state             →  ErrorCategory.STATE_BLOCKED
  - queue_full / too_long →  ErrorCategory.QUEUE_FULL     (队列满 / 超长)
  - limit J* Max/Min      →  ErrorCategory.LIMIT_EXCEEDED (软限位)
  - timeout               →  ErrorCategory.COMM_TIMEOUT   (通信超时)
"""
from __future__ import annotations

import re
from dataclasses import dataclass, field
from enum import Enum
from typing import Optional


# ---------- T12: error categories ----------

class ErrorCategory(str, Enum):
    BAD_ARGS = "bad_args"               # 参数错误 (use ..., bad node, sscanf 失败)
    NOT_READY = "not_ready"             # 设备未就绪 (not_homed, not_enabled)
    STATE_BLOCKED = "state_blocked"     # 状态机不允许 (state=PAUSED 等)
    QUEUE_FULL = "queue_full"           # 命令队列满或单条超长
    LIMIT_EXCEEDED = "limit_exceeded"   # 软限位 (limit J* Max/Min)
    COMM_TIMEOUT = "comm_timeout"       # host-pc → 固件的超时
    UNKNOWN = "unknown"                 # 未匹配上以上任意类别


# 按优先级排序的匹配规则（先匹配更具体的）
_ERROR_PATTERNS: tuple[tuple[re.Pattern[str], ErrorCategory], ...] = (
    # NOT_READY 类
    (re.compile(r"\bnot_homed\b", re.I),                  ErrorCategory.NOT_READY),
    (re.compile(r"\bnot_enabled\b", re.I),                ErrorCategory.NOT_READY),
    # QUEUE_FULL 类
    (re.compile(r"\bqueue_full\b", re.I),                 ErrorCategory.QUEUE_FULL),
    (re.compile(r"\bqueue too long\b", re.I),             ErrorCategory.QUEUE_FULL),
    (re.compile(r"\btoo_long\b", re.I),                   ErrorCategory.QUEUE_FULL),
    # STATE_BLOCKED 类
    (re.compile(r"\bbad_state\b", re.I),                  ErrorCategory.STATE_BLOCKED),
    (re.compile(r"\bstate\s*=", re.I),                    ErrorCategory.STATE_BLOCKED),
    # LIMIT_EXCEEDED 类
    (re.compile(r"\blimit\s+J\d+\s+(Max|Min)\b", re.I),   ErrorCategory.LIMIT_EXCEEDED),
    (re.compile(r"\blimit_error\b", re.I),                ErrorCategory.LIMIT_EXCEEDED),
    # COMM_TIMEOUT 类（host-pc 端注入，固件一般不直接吐）
    (re.compile(r"\btimeout\b", re.I),                    ErrorCategory.COMM_TIMEOUT),
    # BAD_ARGS 类（放在最后，触发条件最宽）
    (re.compile(r"\buse\s+#?!?\w", re.I),                 ErrorCategory.BAD_ARGS),
    (re.compile(r"\bbad\s+(node|args|target)\b", re.I),   ErrorCategory.BAD_ARGS),
    (re.compile(r"\bmust\s+be\b", re.I),                  ErrorCategory.BAD_ARGS),
)


def classify_error(reason: str) -> ErrorCategory:
    """Map a firmware error reason string to a canonical :class:`ErrorCategory`.

    Returns :attr:`ErrorCategory.UNKNOWN` if no pattern matches — this is
    intentional, so we can keep growing the vocabulary without breaking callers
    that only inspect the category enum.
    """
    if not reason:
        return ErrorCategory.UNKNOWN
    for pattern, category in _ERROR_PATTERNS:
        if pattern.search(reason):
            return category
    return ErrorCategory.UNKNOWN


# ---------- response classification ----------

_RX_OK = re.compile(r"^(?P<tag>[A-Za-z][A-Za-z0-9_]*)\s+OK(?:\s+(?P<rest>.*))?$")
_RX_ERR = re.compile(r"^(?P<tag>[A-Za-z][A-Za-z0-9_]*)\s+Error:\s*(?P<reason>.*)$")


@dataclass(frozen=True)
class Response:
    raw: str
    tag: str            # e.g. "Move", "GetJPos", "HD_O"
    ok: bool
    rest: str = ""      # everything after "OK " (may be empty)
    error: str = ""     # populated when ok=False
    # T12: 分类后的错误码（ok=True 时为 UNKNOWN，无意义）
    error_category: ErrorCategory = field(default=ErrorCategory.UNKNOWN)


def parse_response(line: str) -> Optional[Response]:
    """Classify one RX line. Returns None for unparseable / log lines."""
    line = line.strip()
    if not line:
        return None
    m = _RX_OK.match(line)
    if m:
        return Response(raw=line, tag=m["tag"], ok=True, rest=(m["rest"] or "").strip())
    m = _RX_ERR.match(line)
    if m:
        reason = m["reason"].strip()
        return Response(
            raw=line, tag=m["tag"], ok=False, error=reason,
            error_category=classify_error(reason),
        )
    return None  # unstructured log / banner


# ---------- joint / pose payload parsing ----------

@dataclass(frozen=True)
class JointPos:
    j1: float
    j2: float
    j3: float
    j4: float
    j5: float
    j6: float
    hand: int = -1            # -1 if absent

    def as_list(self) -> list[float]:
        return [self.j1, self.j2, self.j3, self.j4, self.j5, self.j6]


@dataclass(frozen=True)
class CartPos:
    x: float
    y: float
    z: float
    a: float
    b: float
    c: float
    hand: int = -1


def _parse_six_floats_with_hand(rest: str) -> Optional[tuple[list[float], int]]:
    """Parse "v1 v2 v3 v4 v5 v6 [H:n]" (whitespace-tolerant)."""
    parts = rest.replace(",", " ").split()
    if len(parts) < 6:
        return None
    try:
        vals = [float(p) for p in parts[:6]]
    except ValueError:
        return None
    hand = -1
    for tok in parts[6:]:
        if tok.upper().startswith("H:"):
            try:
                hand = int(tok[2:])
            except ValueError:
                pass
            break
    return vals, hand


def parse_jpos(rest: str) -> Optional[JointPos]:
    parsed = _parse_six_floats_with_hand(rest)
    if parsed is None:
        return None
    v, h = parsed
    return JointPos(*v, hand=h)


def parse_lpos(rest: str) -> Optional[CartPos]:
    parsed = _parse_six_floats_with_hand(rest)
    if parsed is None:
        return None
    v, h = parsed
    return CartPos(*v, hand=h)


# ---------- command builders ----------

def cmd_move_j(j: list[float], speed: Optional[float] = None) -> str:
    """Build a MoveJ wire command.

    Firmware ``ParseCommand`` (dummy_robot.cpp:782-802) accepts both:
      - 6 args:  ``&j1,j2,j3,j4,j5,j6``           → use current joint speed
      - 7 args:  ``&j1,j2,j3,j4,j5,j6,speed``     → call ``SetJointSpeed(speed)`` first

    Pass ``speed=None`` to emit the 6-arg variant.
    """
    if len(j) != 6:
        raise ValueError("move_j requires 6 joint values")
    body = "&" + ",".join(f"{x:.3f}" for x in j)
    return body if speed is None else f"{body},{speed:g}"


def cmd_move_l(pose: list[float], speed: Optional[float] = None) -> str:
    """Build a MoveL wire command. Like :func:`cmd_move_j`, ``speed`` is optional."""
    if len(pose) != 6:
        raise ValueError("move_l requires 6 pose values")
    body = "@" + ",".join(f"{x:.3f}" for x in pose)
    return body if speed is None else f"{body},{speed:g}"


def cmd_hand_pos(percent: int) -> str:
    if not 0 <= percent <= 100:
        raise ValueError("hand_pos percent must be 0..100")
    return f"!HD_POS {percent}"


def cmd_rgb_mode(mode: int) -> str:
    if not 0 <= mode <= 11:
        raise ValueError("rgb_mode must be 0..11")
    return f"!RGB_MODE {mode}"


def cmd_rgb_custom(r: int, g: int, b: int) -> str:
    for v in (r, g, b):
        if not 0 <= v <= 255:
            raise ValueError("rgb component must be 0..255")
    return f"!RGB_CUSTOM {r} {g} {b}"


# Tag-to-command-prefix mapping. Used by Robot to know what response tag to
# wait for after sending a command.
COMMAND_TAGS: dict[str, str] = {
    "!START":         "Started",
    "!STOP":          "Stop",
    "!CLOSE":         "Close",
    "!HOME":          "Home",
    "!RESET":         "Reset",
    "!PAUSE":         "Pause",
    "!RESUME":        "Resume",
    "!CALIBRATION":   "Calibration",
    "!HD_O":          "HD_O",
    "!HD_C":          "HD_C",
    "!HD_EN":         "HD_EN",
    "!HD_DIS":        "HD_DIS",
    "!HD_ZERO":       "HD_ZERO",
    "!HD_POS":        "HD_POS",
    "!HD_I":          "HD_I",
    "!RGB_ON":        "RGB_ON",
    "!RGB_OFF":       "RGB_OFF",
    "!RGB_MODE":      "RGB_MODE",
    "!RGB_CUSTOM":    "RGB_CUSTOM",
    "#GETJPOS":       "GetJPos",
    "#GETLPOS":       "GetLPos",
    "#GETSPEED":      "GetSpeed",
    "#ONLINE":        "Online",
    "#GET_DCE":       "GetDce",
    "#STATUS":        "Status",      # T10 — diagnostic summary (multi-line)
    "#SET_DCE_KP":    "SetDceKp",
    "#SET_DCE_KV":    "SetDceKv",
    "#SET_DCE_KI":    "SetDceKi",
    "#SET_DCE_KD":    "SetDceKd",
    "#ACC_J":         "AccJ",
    "#SPEED_J":       "SpeedJ",
    "#I_LIMIT_J":     "ILimitJ",
    "#REBOOT":        "Reboot",
    "#OFFSET_J":      "OffsetJ",
    "#CMDMODE":       "CmdMode",
    "&":              "Move",       # MoveJ
    "@":              "Move",       # MoveL
    ">":              "Move",       # MoveJ alias — firmware treats `>` and `&` identically
                                    # (dummy_robot.cpp ParseCommand). Kept here only so that
                                    # /api/raw passthrough does not KeyError; UI layer uses `&`.
}


def expected_tag(cmd: str) -> str:
    """Return the response tag the firmware will use for a given outgoing command."""
    if not cmd:
        raise ValueError("empty command")
    head = cmd[0]
    if head in ("&", "@", ">"):
        return COMMAND_TAGS[head]
    # Token before space (e.g. "!HD_POS 50" -> "!HD_POS")
    token = cmd.split(maxsplit=1)[0]
    if token in COMMAND_TAGS:
        return COMMAND_TAGS[token]
    raise KeyError(f"unknown command: {cmd!r}")
