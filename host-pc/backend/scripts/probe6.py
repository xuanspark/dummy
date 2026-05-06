"""Prove that '#GET_DCE poison' is just firmware-busy, not host-pc state corruption.

Strategy: hit #GET_DCE with a generous 60s timeout, wait for it to FULLY finish
emitting, then immediately try #GETJPOS without any disconnect. If it works,
the dispatcher / transport are perfectly healthy."""
import httpx, time, json

BASE = "http://localhost:8000"

def diag(label):
    r = httpx.get(f"{BASE}/api/diag/state", timeout=5.0)
    print(f"\n--- diag [{label}] ---")
    d = r.json()
    print(f"  dispatcher_alive={d['dispatcher_alive']}  "
          f"sub_q={d['sub_queue_size']}  pending_q={d['pending_queue_size']}  "
          f"log_tail_len={d['log_tail_len']}")
    print(f"  log_tail (last 30):")
    for ln in d.get("log_tail", []):
        print(f"    | {ln}")

def call(cmd, timeout=4.0):
    t0 = time.monotonic()
    try:
        r = httpx.post(f"{BASE}/api/raw",
                       json={"command": cmd, "timeout": timeout},
                       timeout=timeout + 5.0)
        dt = (time.monotonic()-t0)*1000
        print(f"  {dt:6.0f}ms  {cmd:18s} → {r.status_code}  {r.text[:140]}")
        return r.status_code == 200
    except Exception as e:
        print(f"          {cmd:18s} → EXC {e}")
        return False

# Connect if needed
if not httpx.get(f"{BASE}/api/connection").json().get("connected"):
    httpx.post(f"{BASE}/api/connect", json={"port":"COM4","baud":115200}, timeout=10.0)
    time.sleep(0.5)

print("\nStep 1: send #GET_DCE with 60s timeout (let firmware finish slowly)")
call("#GET_DCE", timeout=60.0)

diag("after-GET_DCE-completion")

print("\nStep 2: immediate #GETJPOS — proves Robot is healthy")
call("#GETJPOS")

print("\nStep 3: !RGB_MODE 0")
call("!RGB_MODE 0")

print("\nStep 4: !RGB_OFF")
call("!RGB_OFF")

print("\nStep 5: #GETLPOS")
call("#GETLPOS")

print("\nStep 6: #GETSPEED")
call("#GETSPEED")

diag("final")
