"""Test if firmware recovers from #GET_DCE timeout, or stays stuck."""
import httpx, time

BASE = "http://localhost:8000"

def call(cmd, timeout=4.0):
    t0 = time.monotonic()
    try:
        r = httpx.post(f"{BASE}/api/raw", json={"command": cmd, "timeout": timeout},
                       timeout=timeout + 5.0)
        dt = (time.monotonic()-t0)*1000
        print(f"  {dt:6.0f}ms  {cmd:18s} → {r.status_code} {r.text[:140]}")
        return r.status_code == 200
    except Exception as e:
        print(f"          {cmd:18s} → EXC {e}")
        return False

print("Step 1: warm-up (sanity)")
call("#GETJPOS")

print("\nStep 2: poison with #GET_DCE")
call("#GET_DCE")

print("\nStep 3: immediate retry (expected to fail)")
call("#GETJPOS")

print("\nStep 4: wait 5s then retry")
time.sleep(5.0)
call("#GETJPOS")

print("\nStep 5: another retry")
call("#GETJPOS")

print("\nStep 6: wait 10s more then retry")
time.sleep(10.0)
call("#GETJPOS")
