"""验证固件 CAN-TX 信号量修复后 #ONLINE / #GET_DCE 不再"中毒"。"""
import httpx, time, json

BASE = "http://localhost:8000"

def call(cmd, timeout=4.0):
    t0 = time.monotonic()
    try:
        r = httpx.post(f"{BASE}/api/raw", json={"command": cmd, "timeout": timeout},
                       timeout=timeout + 5.0)
        dt = (time.monotonic()-t0)*1000
        ok = r.status_code == 200
        sym = "PASS" if ok else "FAIL"
        print(f"  [{sym}] {dt:6.0f}ms  {cmd:18s} → {r.text[:160]}")
        return ok
    except Exception as e:
        print(f"  [EXC]         {cmd:18s} → {e}")
        return False

print("=== 0) reconnect to start fresh ===")
httpx.post(f"{BASE}/api/disconnect", timeout=5)
time.sleep(1.5)
print(httpx.post(f"{BASE}/api/connect", json={"port":"COM4","baud":115200}, timeout=10).json())
time.sleep(0.5)

print("\n=== 1) baseline sanity ===")
call("#GETJPOS")

print("\n=== 2) #ONLINE — should return fast, NOT poison subsequent ===")
t0 = time.monotonic()
r = httpx.get(f"{BASE}/api/diag/online", timeout=8.0)
print(f"  online took {(time.monotonic()-t0)*1000:.0f}ms → {r.status_code}  {r.text[:300]}")

print("\n=== 3) immediately after #ONLINE — RGB and JPOS must work ===")
call("!RGB_MODE 0")
call("#GETJPOS")
call("!RGB_OFF")

print("\n=== 4) #GET_DCE — should return fast, NOT poison subsequent ===")
t0 = time.monotonic()
r = httpx.get(f"{BASE}/api/dce", timeout=10.0)
print(f"  dce took {(time.monotonic()-t0)*1000:.0f}ms → {r.status_code}  {r.text[:400]}")

print("\n=== 5) immediately after #GET_DCE — must work ===")
call("#GETJPOS")
call("#GETLPOS")
call("!RGB_MODE 0")

print("\n=== 6) leave RGB in auto mode (mode 11) ===")
call("!RGB_MODE 11")
call("!RGB_ON")
print("  → RGB 已回到 mode 11 (自动模式) + ON")
