"""Real-hardware smoke test driver for host-pc.

Hits the running backend at ``http://localhost:8000`` to exercise every typed
API endpoint against a connected robot.  Designed to be safe-by-default:

  * **Phase A** — read-only / visual only (no motion).
  * **Phase B** — gripper-only motion (small, contained).
  * **Phase C** — body motion (``!START`` / ``!HOME`` / jog / ``!CLOSE``).
                  Requires ``--phase=C`` on the command line.

Each request gets a 10s timeout. Each step prints ``PASS / FAIL / SKIP`` plus
the raw response so a human watching alongside can spot misbehaviour fast.

Usage (PowerShell)::

    # Phase A only (default — totally safe)
    python scripts\\smoke_real.py --port COM4

    # A + B (gripper)
    python scripts\\smoke_real.py --port COM4 --phase B

    # A + B + C (full body — robot WILL move; clear the workspace first)
    python scripts\\smoke_real.py --port COM4 --phase C
"""
from __future__ import annotations

import argparse
import sys
import time
from typing import Any

import httpx

BASE = "http://localhost:8000"
TIMEOUT = 10.0

# ANSI colours; harmless on terminals that ignore them.
G = "\033[32m"; R = "\033[31m"; Y = "\033[33m"; D = "\033[2m"; X = "\033[0m"

results: list[tuple[str, str, str]] = []  # (label, status, detail)


def call(method: str, path: str, *, label: str, json: dict | None = None,
         skip_ok: bool = False) -> dict | None:
    """Hit one endpoint and pretty-print the result.

    Returns the parsed JSON on success (or None on HTTP failure / non-JSON).
    """
    url = BASE + path
    t0 = time.monotonic()
    try:
        if method == "GET":
            r = httpx.get(url, timeout=TIMEOUT)
        else:
            r = httpx.post(url, json=json, timeout=TIMEOUT)
    except Exception as e:
        dt = time.monotonic() - t0
        msg = f"{type(e).__name__}: {e}"
        print(f"  {R}FAIL{X}  {label:30s}  {D}{dt*1000:5.0f}ms{X}  {msg}")
        results.append((label, "FAIL", msg))
        return None

    dt = time.monotonic() - t0
    try:
        body: Any = r.json()
    except Exception:
        body = r.text

    detail = repr(body)[:120]
    if r.status_code == 200:
        if skip_ok:
            print(f"  {Y}SKIP{X}  {label:30s}  {D}{dt*1000:5.0f}ms{X}  {detail}")
            results.append((label, "SKIP", detail))
        else:
            print(f"  {G}PASS{X}  {label:30s}  {D}{dt*1000:5.0f}ms{X}  {detail}")
            results.append((label, "PASS", detail))
        return body if isinstance(body, dict) else None
    else:
        print(f"  {R}FAIL{X}  {label:30s}  {D}{dt*1000:5.0f}ms{X}  HTTP {r.status_code} {detail}")
        results.append((label, "FAIL", f"HTTP {r.status_code}: {detail}"))
        return None


def header(text: str) -> None:
    print(f"\n{D}{'─' * 72}{X}\n  {text}\n{D}{'─' * 72}{X}")


def ensure_connected(port: str, baud: int) -> bool:
    """Make sure the backend has an open serial connection on ``port``."""
    header(f"Connection setup → {port} @ {baud}")
    state = call("GET", "/api/connection", label="GET /api/connection")
    if state and state.get("connected"):
        if state.get("port") == port:
            print(f"  {Y}note{X}  already connected to {port}, reusing.")
            return True
        print(f"  {Y}note{X}  reconnecting from {state.get('port')} → {port}")
        call("POST", "/api/disconnect", label="POST /api/disconnect")
        time.sleep(0.5)
    body = call("POST", "/api/connect",
                label=f"POST /api/connect {port}",
                json={"port": port, "baud": baud})
    return bool(body) and bool(body.get("connected"))


def phase_a() -> None:
    header("Phase A — read-only / visual (no body motion)")
    call("GET", "/api/state/joint",  label="GET  state/joint  (#GETJPOS)")
    call("GET", "/api/state/cart",   label="GET  state/cart   (#GETLPOS)")
    call("GET", "/api/state/speed",  label="GET  state/speed  (#GETSPEED)")
    # NOTE: #ONLINE quickly returns "Online OK all_online" but kicks off a
    # slow background CAN enumeration that wedges the firmware for ~10 s
    # afterwards (every subsequent command times out). Skipping on REF 1.0.
    print(f"  {Y}SKIP{X}  GET  diag/online  (#ONLINE)            "
          f"{D}firmware bug — CAN poll wedges device{X}")
    results.append(("GET  diag/online  (#ONLINE)", "SKIP",
                    "firmware bug on REF 1.0 — async CAN poll blocks subsequent cmds"))
    # NOTE: #GET_DCE is intentionally skipped on REF 1.0 firmware. It walks
    # the CAN bus one node at a time at ~7-10 s/node and the firmware never
    # emits a final "GetDce OK" terminator — so it locks up for >60 s and
    # then permanently wedges, ignoring all further commands until the
    # serial port is closed and re-opened. See probe6/7.py for evidence.
    print(f"  {Y}SKIP{X}  GET  dce          (#GET_DCE)            "
          f"{D}firmware bug — wedges device for 60+s{X}")
    results.append(("GET  dce          (#GET_DCE)", "SKIP",
                    "firmware bug on REF 1.0 — locks up; needs reconnect"))
    # RGB is purely cosmetic — totally safe.
    call("POST", "/api/rgb/mode",    label="POST rgb/mode 0   (rainbow)", json={"mode": 0})
    time.sleep(1.0)
    call("POST", "/api/rgb/mode",    label="POST rgb/mode 11  (off-ish)", json={"mode": 11})
    call("POST", "/api/rgb/custom",  label="POST rgb/custom 32,0,64",
         json={"r": 32, "g": 0, "b": 64})
    call("POST", "/api/rgb/off",     label="POST rgb/off")


def phase_b() -> None:
    header("Phase B — gripper only (small, contained motion)")
    call("POST", "/api/hand/enable", label="POST hand/enable on", json={"enable": True})
    time.sleep(0.5)
    for pos in (0, 50, 100, 50, 0):
        call("POST", "/api/hand/pos", label=f"POST hand/pos {pos}", json={"percent": pos})
        time.sleep(0.6)
    call("POST", "/api/hand/open",   label="POST hand/open")
    time.sleep(0.5)
    call("POST", "/api/hand/close",  label="POST hand/close")
    time.sleep(0.5)
    call("POST", "/api/hand/enable", label="POST hand/enable off", json={"enable": False})


def phase_c() -> None:
    header("Phase C — body motion (!START / !HOME / small jog / !CLOSE)")
    print(f"  {Y}!! Robot WILL move. Make sure workspace is clear.{X}")
    print(f"  {Y}!! Sleeping 5s — Ctrl+C now if not ready.{X}")
    try:
        time.sleep(5)
    except KeyboardInterrupt:
        print("aborted.")
        return

    if call("POST", "/api/start", label="POST start (!START)") is None:
        print(f"  {R}!START failed — skipping motion tests.{X}")
        return
    time.sleep(2.0)

    call("POST", "/api/home", label="POST home (!HOME)")
    time.sleep(3.0)

    # Confirm we're at home, then issue a *tiny* relative jog (J6 +5°).
    jp = call("GET", "/api/state/joint", label="GET state/joint (post-HOME)")
    if jp and isinstance(jp.get("j1"), (int, float)):
        joints = [jp["j1"], jp["j2"], jp["j3"], jp["j4"], jp["j5"], jp["j6"] + 5]
        # No speed override — let firmware use its current speed.
        call("POST", "/api/move/joint",
             label="POST move/joint (J6 +5°, no speed)",
             json={"joints": joints})
        time.sleep(2.5)
        # Move back.
        joints[5] -= 5
        call("POST", "/api/move/joint",
             label="POST move/joint (J6 back, speed=10)",
             json={"joints": joints, "speed": 10})
        time.sleep(2.5)

    call("POST", "/api/reset",  label="POST reset (!RESET)")
    time.sleep(2.0)
    call("POST", "/api/close",  label="POST close (!CLOSE)")


def summary() -> int:
    header("Summary")
    n_pass = sum(1 for _, s, _ in results if s == "PASS")
    n_skip = sum(1 for _, s, _ in results if s == "SKIP")
    n_fail = sum(1 for _, s, _ in results if s == "FAIL")
    for label, status, detail in results:
        col = G if status == "PASS" else (Y if status == "SKIP" else R)
        print(f"  {col}{status:4s}{X}  {label:30s}  {D}{detail[:80]}{X}")
    print()
    print(f"  Total: {len(results)}   "
          f"{G}PASS={n_pass}{X}   "
          f"{Y}SKIP={n_skip}{X}   "
          f"{R}FAIL={n_fail}{X}")
    return 0 if n_fail == 0 else 1


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--port", required=True, help="serial device, e.g. COM4")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--phase", choices=["A", "B", "C"], default="A",
                   help="A=read-only (default); B=A+gripper; C=A+B+full body motion")
    p.add_argument("--keep-connected", action="store_true",
                   help="don't /api/disconnect at the end")
    args = p.parse_args()

    if not ensure_connected(args.port, args.baud):
        print(f"{R}Could not connect to {args.port} — abort.{X}")
        return 2

    try:
        phase_a()
        if args.phase in ("B", "C"):
            phase_b()
        if args.phase == "C":
            phase_c()
    finally:
        if not args.keep_connected:
            header("Disconnect")
            call("POST", "/api/disconnect", label="POST /api/disconnect")

    return summary()


if __name__ == "__main__":
    sys.exit(main())
