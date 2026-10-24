"""彻底新连接，只发一条 #ONLINE，量精确耗时。"""
import httpx, time, json

BASE = "http://localhost:8000"

print(">>> disconnect & reconnect for clean state")
httpx.post(f"{BASE}/api/disconnect", timeout=5)
time.sleep(2.0)
print(httpx.post(f"{BASE}/api/connect", json={"port":"COM4","baud":115200}, timeout=10).json())
time.sleep(1.0)

print("\n>>> sanity: #GETJPOS")
t0 = time.monotonic()
r = httpx.post(f"{BASE}/api/raw", json={"command":"#GETJPOS","timeout":3}, timeout=8).json()
print(f"  {(time.monotonic()-t0)*1000:.0f}ms  ok={r.get('ok')}")

print("\n>>> ONE shot #ONLINE (first time after reconnect)")
t0 = time.monotonic()
r = httpx.get(f"{BASE}/api/diag/online", timeout=15.0).json()
dt = (time.monotonic()-t0)*1000
print(f"  {dt:.0f}ms  →  {r}")

print("\n>>> immediately after, check log_tail to see all firmware output")
time.sleep(0.3)
d = httpx.get(f"{BASE}/api/diag/state", timeout=5).json()
print(f"  log_tail_len={d['log_tail_len']}")
for ln in d.get('log_tail', []):
    print(f"    | {ln}")

print("\n>>> NEXT command #GETJPOS — should be FAST if fix works")
t0 = time.monotonic()
r = httpx.post(f"{BASE}/api/raw", json={"command":"#GETJPOS","timeout":3}, timeout=8).json()
dt = (time.monotonic()-t0)*1000
print(f"  {dt:.0f}ms  ok={r.get('ok')}  detail={r.get('detail', r.get('raw',''))}")
