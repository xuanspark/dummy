"""WebSocket endpoint.

Server -> client messages (JSON, one per frame):
  {"type": "log",      "line": "<raw line>"}                      # every RX line
  {"type": "response", "tag": "GetJPos", "ok": true, "rest": "..."}# parsed
  {"type": "state",    "jpos": {...}|null, "lpos": {...}|null,
                       "speed": <float>|null}                      # cached state

Client -> server messages (JSON):
  {"type": "raw",   "command": "!HOME"}     # send raw command (response goes
                                            # back via the broadcast stream)
  {"type": "ping"}                          # -> {"type": "pong"}
"""
from __future__ import annotations

import asyncio
import json
import logging
from typing import Any

from fastapi import APIRouter, WebSocket, WebSocketDisconnect

from app.core import protocol as P
from app.core.connection import ConnectionManager
from app.core.noise_filter import is_boot_noise

log = logging.getLogger(__name__)
router = APIRouter()


@router.websocket("/ws")
async def ws_endpoint(websocket: WebSocket) -> None:
    """Streams logs/responses/state and accepts raw commands.

    The client may connect even when the backend has no active serial link;
    in that case we still accept the socket and wait for ``connect`` on the
    REST side. Once a connection is established the client should reconnect
    the WS to subscribe to the new transport's RX stream.
    """
    await websocket.accept()
    mgr: ConnectionManager = websocket.app.state.manager
    robot = mgr.robot

    if robot is None:
        await websocket.send_text(json.dumps({
            "type": "state", "connected": False,
            "message": "not connected; POST /api/connect first, then reconnect this WS",
        }))
        # Keep the socket open so the client can re-handshake without polling.
        try:
            while True:
                await websocket.receive_text()
                await websocket.send_text(json.dumps({"type": "error", "reason": "not_connected"}))
        except WebSocketDisconnect:
            return

    transport = robot._t  # subscribe to raw lines
    sub = transport.subscribe()

    async def push_initial_state() -> None:
        s = robot.state
        await websocket.send_text(json.dumps({
            "type": "state",
            "connected": True,
            "port": mgr.port,
            "baud": mgr.baud,
            "jpos": s.last_jpos.__dict__ if s.last_jpos else None,
            "lpos": s.last_lpos.__dict__ if s.last_lpos else None,
            "speed": s.last_speed,
        }))

    async def rx_pump() -> None:
        while True:
            line = await sub.queue.get()
            await websocket.send_text(json.dumps({
                "type": "log", "line": line, "noise": is_boot_noise(line),
            }))
            resp = P.parse_response(line)
            if resp is not None:
                await websocket.send_text(json.dumps({
                    "type": "response", "tag": resp.tag,
                    "ok": resp.ok, "rest": resp.rest, "error": resp.error,
                }))

    async def cmd_pump() -> None:
        while True:
            try:
                msg_text = await websocket.receive_text()
            except WebSocketDisconnect:
                raise
            try:
                msg: dict[str, Any] = json.loads(msg_text)
            except json.JSONDecodeError:
                await websocket.send_text(json.dumps({"type": "error", "reason": "invalid_json"}))
                continue
            kind = msg.get("type")
            if kind == "ping":
                await websocket.send_text(json.dumps({"type": "pong"}))
            elif kind == "raw":
                cmd = msg.get("command", "")
                if not isinstance(cmd, str) or not cmd:
                    await websocket.send_text(json.dumps({"type": "error", "reason": "missing_command"}))
                    continue
                try:
                    transport.write_line(cmd)
                except Exception as e:  # noqa: BLE001
                    await websocket.send_text(json.dumps({"type": "error", "reason": str(e)}))
            else:
                await websocket.send_text(json.dumps({"type": "error", "reason": f"unknown_type:{kind}"}))

    try:
        await push_initial_state()
        await asyncio.gather(rx_pump(), cmd_pump())
    except WebSocketDisconnect:
        pass
    except Exception as exc:  # noqa: BLE001
        log.exception("ws error: %s", exc)
    finally:
        transport.unsubscribe(sub)
        try:
            await websocket.close()
        except Exception:  # noqa: BLE001
            pass
