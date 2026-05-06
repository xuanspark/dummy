"""Verify whether #GET_DCE timeout poisons subsequent commands."""
import httpx, time

BASE = "http://localhost:8000"

# Reverse smoke_real order: do #GET_DCE FIRST, then RGB.
SEQ = [
    ("/api/raw",  {"command": "#GET_DCE", "timeout": 4.0}),  # short timeout
    ("/api/raw",  {"command": "!RGB_MODE 0", "timeout": 4.0}),
    ("/api/raw",  {"command": "!RGB_OFF",    "timeout": 4.0}),
    ("/api/raw",  {"command": "#GETJPOS",    "timeout": 4.0}),
]

for path, body in SEQ:
    t0 = time.monotonic()
    try:
        r = httpx.post(BASE + path, json=body, timeout=12.0)
        dt = (time.monotonic()-t0)*1000
        print(f"{dt:6.0f}ms  {body['command']:20s} → {r.status_code}  {r.text[:160]}")
    except Exception as e:
        print(f"        {body['command']:20s} → EXC {e}")
    time.sleep(0.5)
