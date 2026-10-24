"""Probe individual ASCII commands via /api/raw to see actual firmware replies."""
import httpx, sys, time

BASE = "http://localhost:8000"

CMDS = [
    "!RGB_ON",
    "!RGB_OFF",
    "!RGB_MODE 0",
    "!RGB_CUSTOM 32 0 64",
    "#ONLINE",
    "#GET_DCE",
    "#GET_DCE 1",
    "#GET_DCE 1 kp",
]

for c in CMDS:
    t0 = time.monotonic()
    try:
        r = httpx.post(f"{BASE}/api/raw",
                       json={"command": c, "timeout": 8.0},
                       timeout=10.0)
        dt = (time.monotonic()-t0)*1000
        print(f"{dt:6.0f}ms  {c:25s} → {r.status_code}  {r.text[:200]}")
    except Exception as e:
        print(f"        {c:25s} → EXC {e}")
    time.sleep(0.3)
