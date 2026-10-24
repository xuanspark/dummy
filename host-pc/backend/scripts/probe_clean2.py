"""验证 ASCII thread 真的卡在 #ONLINE 里：等足够久看后续 J[n] 行有没有姗姗来迟。"""
import httpx, time, json

BASE = "http://localhost:8000"

print(">>> reconnect")
httpx.post(f"{BASE}/api/disconnect", timeout=5)
time.sleep(2.0)
httpx.post(f"{BASE}/api/connect", json={"port":"COM4","baud":115200}, timeout=10)
time.sleep(1.0)

# 用 raw 走 _exec 路径，host 等 GetJPos 的 OK tag, 不会被 quiet-window 提前结束
print("\n>>> #ONLINE via /api/raw (waits for 'Online' tag, no quiet-window)")
t0 = time.monotonic()
r = httpx.post(f"{BASE}/api/raw", json={"command":"#ONLINE","timeout":15}, timeout=20).json()
print(f"  {(time.monotonic()-t0)*1000:.0f}ms  ok={r.get('ok')}  raw={r.get('raw','')!r}")

# 然后等 1 秒再看 log_tail，看固件后续行是不是慢慢挤出来
for delay in [0.2, 1.0, 3.0, 5.0]:
    time.sleep(delay if delay > 0 else 0)
    d = httpx.get(f"{BASE}/api/diag/state", timeout=5).json()
    print(f"\n>>> after {delay}s sleep — log_tail_len={d['log_tail_len']}")
    for ln in d.get('log_tail', [])[-15:]:
        print(f"    | {ln}")
