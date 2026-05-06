"""Wait long enough for firmware to drain GET_DCE backlog, then test."""
import httpx, time, json

BASE = "http://localhost:8000"

def diag(label):
    r = httpx.get(f"{BASE}/api/diag/state", timeout=5.0).json()
    print(f"[{label}] dispatcher={r['dispatcher_alive']} log_len={r['log_tail_len']} "
          f"last={r['log_tail'][-1] if r['log_tail'] else '<empty>'!r}")

def call(cmd, timeout=3.0, label=None):
    t0 = time.monotonic()
    try:
        r = httpx.post(f"{BASE}/api/raw", json={"command": cmd, "timeout": timeout},
                       timeout=timeout + 5.0)
        dt = (time.monotonic()-t0)*1000
        ok = r.status_code == 200
        sym = "PASS" if ok else "FAIL"
        print(f"  [{sym}] {dt:6.0f}ms  {cmd:15s} → {r.text[:120]}")
        return ok
    except Exception as e:
        print(f"  [EXC]         {cmd:15s} → {e}")
        return False

print("Wait 30s for firmware to drain any leftover GET_DCE output...")
for i in range(6):
    time.sleep(5)
    diag(f"t+{(i+1)*5}s")

print("\nNow probe quickly:")
call("#GETJPOS")
call("!RGB_MODE 0")
call("!RGB_OFF")
call("#GETLPOS")
