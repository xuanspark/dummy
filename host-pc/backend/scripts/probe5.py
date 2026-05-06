"""Verify dispatcher-resilience fix: poison with #GET_DCE, then check
diag and recovery without disconnect/reconnect."""
import httpx, time, json

BASE = "http://localhost:8000"

def diag(label):
    r = httpx.get(f"{BASE}/api/diag/state", timeout=5.0)
    print(f"\n--- diag [{label}] ---")
    print(json.dumps(r.json(), indent=2))

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

# Make sure we're connected
state = httpx.get(f"{BASE}/api/connection", timeout=5.0).json()
if not state.get("connected"):
    httpx.post(f"{BASE}/api/connect", json={"port":"COM4","baud":115200}, timeout=10.0)
    time.sleep(0.5)

diag("baseline")

print("\nStep 1: warm-up sanity")
call("#GETJPOS")

diag("after-warmup")

print("\nStep 2: poison with #GET_DCE (expect 4s timeout)")
call("#GET_DCE")

diag("post-poison")

print("\nStep 3: retry #GETJPOS (key test — should now PASS thanks to dispatcher fix)")
call("#GETJPOS")

print("\nStep 4: try RGB_MODE 0")
call("!RGB_MODE 0")

print("\nStep 5: try RGB_OFF")
call("!RGB_OFF")

diag("final")
