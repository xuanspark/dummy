"""REST endpoints. Each route maps 1:1 to a Robot coroutine."""
from __future__ import annotations

from typing import Optional

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel, Field

from app.api.deps import get_robot
from app.core.robot import Robot, RobotError

router = APIRouter(prefix="/api", tags=["robot"])


# ---------- schemas ----------

class JointMove(BaseModel):
    joints: list[float] = Field(..., min_length=6, max_length=6)
    # Optional: when None, emits 6-arg wire form (firmware uses its current joint speed).
    speed: Optional[float] = None


class CartMove(BaseModel):
    pose: list[float] = Field(..., min_length=6, max_length=6)
    speed: Optional[float] = None


class HandPos(BaseModel):
    percent: int = Field(..., ge=0, le=100)


class HandEnable(BaseModel):
    enable: bool


class RgbMode(BaseModel):
    mode: int = Field(..., ge=0, le=11)


class RgbCustom(BaseModel):
    r: int = Field(..., ge=0, le=255)
    g: int = Field(..., ge=0, le=255)
    b: int = Field(..., ge=0, le=255)


class RawCmd(BaseModel):
    command: str
    timeout: Optional[float] = None


class RebootBody(BaseModel):
    # Firmware mapping (ascii_protocol.cpp #REBOOT):
    #   None/omitted -> all motors + ref board system-reset
    #   0            -> ref MCU system-reset only
    #   1..6         -> single joint motor reboot
    #   7            -> hand (gripper) reboot
    node: Optional[int] = Field(None, ge=0, le=7)


class OffsetJBody(BaseModel):
    # ``target`` accepts 1..6 (joint), 7 (hand), or the literal "all".
    # "all" writes NVS for every axis and triggers system reset.
    target: object = Field(..., description='int 1..7 or string "all"')


class CmdModeBody(BaseModel):
    # 1=SEQ, 2=INT, 3=TRJ, 4=TUN (see DummyRobot::CommandMode).
    mode: int = Field(..., ge=1, le=4)


class HandCurrentBody(BaseModel):
    # Firmware constraint: 0 < amps <= 2.0
    amps: float = Field(..., gt=0.0, le=2.0)


class DceGainBody(BaseModel):
    node: int = Field(..., ge=1, le=6)
    kind: str = Field(..., pattern="^(kp|kv|ki|kd|KP|KV|KI|KD)$")
    value: int = Field(..., ge=0)


class MotionLimitBody(BaseModel):
    node: int = Field(..., ge=1, le=7)
    kind: str = Field(..., pattern="^(acc|speed|i_limit|ACC|SPEED|I_LIMIT)$")
    value: float = Field(..., ge=0.0)


# ---------- helper ----------

# T12: 错误类别 → HTTP 状态码映射
#   BAD_ARGS / NOT_READY / STATE_BLOCKED / QUEUE_FULL / LIMIT_EXCEEDED → 422 (业务校验)
#   COMM_TIMEOUT                                                       → 504 (网关超时)
#   UNKNOWN                                                            → 400 (旧行为兼容)
_CATEGORY_HTTP_STATUS = {
    "bad_args":        422,
    "not_ready":       422,
    "state_blocked":   422,
    "queue_full":      503,
    "limit_exceeded":  422,
    "comm_timeout":    504,
    "unknown":         400,
}


async def _run(coro):
    try:
        return await coro
    except RobotError as e:
        cat = getattr(e, "category", None)
        cat_str = cat.value if cat is not None else "unknown"
        status = _CATEGORY_HTTP_STATUS.get(cat_str, 400)
        raise HTTPException(
            status_code=status,
            detail={"category": cat_str, "message": str(e)},
        ) from e


# ---------- lifecycle ----------

@router.post("/start")
async def start(r: Robot = Depends(get_robot)):
    await _run(r.start_robot())
    return {"ok": True}


@router.post("/close")
async def close(r: Robot = Depends(get_robot)):
    await _run(r.shutdown_robot())
    return {"ok": True}


@router.post("/home")
async def home(r: Robot = Depends(get_robot)):
    await _run(r.home())
    return {"ok": True}


@router.post("/reset")
async def reset(r: Robot = Depends(get_robot)):
    await _run(r.reset_pose())
    return {"ok": True}


@router.post("/estop")
async def estop(r: Robot = Depends(get_robot)):
    await _run(r.estop())
    return {"ok": True}


@router.post("/pause")
async def pause(r: Robot = Depends(get_robot)):
    await _run(r.pause())
    return {"ok": True}


@router.post("/resume")
async def resume(r: Robot = Depends(get_robot)):
    await _run(r.resume())
    return {"ok": True}


# ---------- motion ----------

@router.post("/move/joint")
async def move_joint(body: JointMove, r: Robot = Depends(get_robot)):
    await _run(r.move_j(body.joints, body.speed))
    return {"ok": True}


@router.post("/move/cart")
async def move_cart(body: CartMove, r: Robot = Depends(get_robot)):
    await _run(r.move_l(body.pose, body.speed))
    return {"ok": True}


# ---------- queries ----------

@router.get("/state/joint")
async def state_joint(r: Robot = Depends(get_robot)):
    jp = await _run(r.get_jpos())
    return {"j1": jp.j1, "j2": jp.j2, "j3": jp.j3, "j4": jp.j4, "j5": jp.j5, "j6": jp.j6, "hand": jp.hand}


@router.get("/state/cart")
async def state_cart(r: Robot = Depends(get_robot)):
    lp = await _run(r.get_lpos())
    return {"x": lp.x, "y": lp.y, "z": lp.z, "a": lp.a, "b": lp.b, "c": lp.c, "hand": lp.hand}


@router.get("/state/speed")
async def state_speed(r: Robot = Depends(get_robot)):
    return {"speed": await _run(r.get_speed())}


@router.get("/state")
async def state_full(r: Robot = Depends(get_robot)):
    s = r.state
    return {
        "connected": s.connected,
        "jpos": s.last_jpos.__dict__ if s.last_jpos else None,
        "lpos": s.last_lpos.__dict__ if s.last_lpos else None,
        "speed": s.last_speed,
    }


@router.get("/diag/state")
async def diag_state(r: Robot = Depends(get_robot)):
    """Internal health snapshot — used to diagnose 'all commands time out' wedges.

    Returns dispatcher task status, queue depths, and subscriber count. If
    ``dispatcher_alive`` is ``false`` after a transient failure, the Robot
    instance is wedged and a ``/api/disconnect`` + ``/api/connect`` cycle
    will rebuild it.
    """
    task = r._dispatcher_task
    sub = r._sub
    pending = r._pending
    transport = r._t
    return {
        "dispatcher_alive": bool(task and not task.done()),
        "dispatcher_done": bool(task and task.done()),
        "dispatcher_exception": (repr(task.exception())
                                  if task and task.done() and not task.cancelled()
                                  else None),
        "sub_queue_size": sub.queue.qsize() if sub else None,
        "pending_queue_size": pending.qsize() if pending else None,
        "pending_queue_full": pending.full() if pending else None,
        "transport_subs": len(getattr(transport, "_subs", [])),
        "transport_connected": getattr(transport, "connected", None),
        "log_tail_len": len(r.state.log_tail),
        "log_tail": r.state.log_tail[-30:],   # last 30 raw RX lines
    }


# ---------- hand ----------

@router.post("/hand/open")
async def hand_open(r: Robot = Depends(get_robot)):
    await _run(r.hand_open())
    return {"ok": True}


@router.post("/hand/close")
async def hand_close(r: Robot = Depends(get_robot)):
    await _run(r.hand_close())
    return {"ok": True}


@router.post("/hand/enable")
async def hand_enable(body: HandEnable, r: Robot = Depends(get_robot)):
    await _run(r.hand_enable(body.enable))
    return {"ok": True}


@router.post("/hand/pos")
async def hand_pos(body: HandPos, r: Robot = Depends(get_robot)):
    await _run(r.hand_pos(body.percent))
    return {"ok": True}


@router.post("/hand/zero")
async def hand_zero(r: Robot = Depends(get_robot)):
    """``!HD_ZERO`` — auto-calibrate gripper; returns the resolved range."""
    closed, opened = await _run(r.hand_zero())
    return {"ok": True, "closed": closed, "opened": opened}


@router.post("/hand/current")
async def hand_current(body: HandCurrentBody, r: Robot = Depends(get_robot)):
    """``!HD_I <amps>`` — set gripper current limit (0 < amps ≤ 2.0)."""
    await _run(r.hand_current(body.amps))
    return {"ok": True}


# ---------- rgb ----------

@router.post("/rgb/on")
async def rgb_on(r: Robot = Depends(get_robot)):
    await _run(r.rgb_on())
    return {"ok": True}


@router.post("/rgb/off")
async def rgb_off(r: Robot = Depends(get_robot)):
    await _run(r.rgb_off())
    return {"ok": True}


@router.post("/rgb/mode")
async def rgb_mode(body: RgbMode, r: Robot = Depends(get_robot)):
    await _run(r.rgb_mode(body.mode))
    return {"ok": True}


@router.post("/rgb/custom")
async def rgb_custom(body: RgbCustom, r: Robot = Depends(get_robot)):
    await _run(r.rgb_custom(body.r, body.g, body.b))
    return {"ok": True}


# ---------- diagnostics ----------

@router.get("/diag/status")
async def diag_status(r: Robot = Depends(get_robot)):
    """``#STATUS`` — query CAN health, robot state, and per-node online age.

    Response::

        {"state": "NORMAL", "can_rx": 12345, "can_drop": 0,
         "nodes": {1: {"online": true, "age_ms": 42}, ...}, "raw": [...]}
    """
    return await _run(r.get_status())


@router.get("/diag/online")
async def diag_online(node: Optional[int] = None, r: Robot = Depends(get_robot)):
    """``#ONLINE [node]`` — query node online status.

    Query param ``node`` (1-7) limits the check to one node; omit for full scan.
    Response:
      ``{"summary": "all_online"|"has_offline"|..., "nodes": {1: true, ...}, "raw": [...]}``
    """
    if node is not None and not (1 <= node <= 7):
        raise HTTPException(status_code=422, detail="node must be 1..7")
    return await _run(r.check_online(node))


@router.post("/reboot")
async def reboot(body: RebootBody, r: Robot = Depends(get_robot)):
    """``#REBOOT [node]`` — see :class:`RebootBody` for semantics."""
    resp = await _run(r.reboot(body.node))
    return {"ok": resp.ok, "tag": resp.tag, "rest": resp.rest, "raw": resp.raw}


# ---------- maintenance ----------

@router.post("/calibrate")
async def calibrate(r: Robot = Depends(get_robot)):
    """``!CALIBRATION`` — calibrate home offset (blocks up to 30 s)."""
    await _run(r.calibrate())
    return {"ok": True}


@router.post("/offset_j")
async def offset_j(body: OffsetJBody, r: Robot = Depends(get_robot)):
    """``#OFFSET_J <1-7|all>`` — persist current joint position as home offset.

    **DANGER**: writes NVS; the ``"all"`` variant triggers a full system reset.
    """
    target = body.target
    # Normalize: accept int-like or the literal "all" (case-insensitive)
    if isinstance(target, str):
        if target.strip().lower() != "all":
            raise HTTPException(status_code=422, detail='target must be 1..7 or "all"')
        norm = "all"
    else:
        try:
            norm = int(target)  # type: ignore[arg-type]
        except (TypeError, ValueError):
            raise HTTPException(status_code=422, detail='target must be 1..7 or "all"')
        if not (1 <= norm <= 7):
            raise HTTPException(status_code=422, detail="target int must be 1..7")
    resp = await _run(r.offset_joint(norm))
    return {"ok": resp.ok, "tag": resp.tag, "rest": resp.rest, "raw": resp.raw}


@router.post("/cmdmode")
async def cmdmode(body: CmdModeBody, r: Robot = Depends(get_robot)):
    """``#CMDMODE <1-4>`` — set firmware command dispatch mode."""
    resp = await _run(r.set_cmd_mode(body.mode))
    return {"ok": resp.ok, "tag": resp.tag, "rest": resp.rest, "raw": resp.raw}


# ---------- tuning ----------

@router.post("/dce/gain")
async def dce_gain(body: DceGainBody, r: Robot = Depends(get_robot)):
    """``#SET_DCE_K{P,V,I,D} <node> <value>`` — node 1..6, value uint32."""
    resp = await _run(r.set_dce_gain(body.node, body.kind, body.value))
    return {"ok": resp.ok, "tag": resp.tag, "rest": resp.rest, "raw": resp.raw}


@router.get("/dce")
async def dce_query(node: Optional[int] = None, param: Optional[str] = None,
                    r: Robot = Depends(get_robot)):
    """``#GET_DCE [node [param]]`` — returns parsed per-node gains."""
    if node is not None and not (1 <= node <= 7):
        raise HTTPException(status_code=422, detail="node must be 1..7")
    if param is not None and param.lower() not in {"kp", "kv", "ki", "kd"}:
        raise HTTPException(status_code=422, detail="param must be kp/kv/ki/kd")
    return await _run(r.get_dce(node, param))


@router.post("/motion/limit")
async def motion_limit(body: MotionLimitBody, r: Robot = Depends(get_robot)):
    """Set per-joint acceleration / velocity / current limit (1..7)."""
    resp = await _run(r.set_motion_limit(body.node, body.kind, body.value))
    return {"ok": resp.ok, "tag": resp.tag, "rest": resp.rest, "raw": resp.raw}


# ---------- escape hatch ----------

# T11: 危险命令前缀白名单 — raw 接口默认禁止这些命令，需显式传 maintenance=true
_DANGEROUS_PREFIXES = (
    "#OFFSET_J", "#SET_DCE_", "#CMDMODE", "#REBOOT",
    "#ACC_J", "#SPEED_J", "#I_LIMIT_J",
    "!CALIBRATION",
)


@router.post("/raw")
async def raw(body: RawCmd, r: Robot = Depends(get_robot)):
    cmd = body.command.strip()
    # T11: 拦截危险命令
    for prefix in _DANGEROUS_PREFIXES:
        if cmd.upper().startswith(prefix.upper()):
            raise HTTPException(
                status_code=403,
                detail=f"raw: '{prefix}' is a restricted command; use the dedicated API endpoint",
            )
    resp = await _run(r.raw(body.command, body.timeout))
    return {"ok": resp.ok, "tag": resp.tag, "rest": resp.rest, "error": resp.error, "raw": resp.raw}
