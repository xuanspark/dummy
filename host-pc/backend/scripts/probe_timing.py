"""精确测固件 #ONLINE / #GET_DCE 实际耗时（log_tail 增长速度）。"""
import httpx, time, threading

BASE = "http://localhost:8000"

print(">>> reconnect")
httpx.post(f"{BASE}/api/disconnect", timeout=5)
time.sleep(2.0)
httpx.post(f"{BASE}/api/connect", json={"port":"COM4","baud":115200}, timeout=10)
time.sleep(1.0)

def baseline_len():
    return httpx.get(f"{BASE}/api/diag/state", timeout=5).json()['log_tail_len']

# 测试函数：异步发命令，主线程每 100ms 采样 log_tail_len
def run_test(cmd):
    print(f"\n========= {cmd} =========")
    base = baseline_len()
    print(f"baseline log_tail_len = {base}")
    
    result = {'rc': None, 'raw': '', 'err': '', 't_ms': 0}
    def post():
        t0 = time.monotonic()
        try:
            r = httpx.post(f"{BASE}/api/raw", json={"command":cmd,"timeout":12},
                           timeout=15).json()
            result['rc'] = r.get('ok')
            result['raw'] = r.get('raw', '')
            result['err'] = r.get('detail', '')
            result['t_ms'] = (time.monotonic()-t0)*1000
        except Exception as e:
            result['err'] = str(e)
    
    th = threading.Thread(target=post)
    t0 = time.monotonic()
    th.start()
    
    samples = []
    while th.is_alive() and (time.monotonic()-t0) < 13:
        time.sleep(0.1)
        n = baseline_len() - base
        elapsed = (time.monotonic()-t0)*1000
        if not samples or samples[-1][1] != n:
            samples.append((elapsed, n))
            print(f"  t+{elapsed:6.0f}ms  +{n} new RX lines")
    th.join()
    print(f"FINAL: {result}")
    
    # 最后看一下 log_tail 末尾
    d = httpx.get(f"{BASE}/api/diag/state", timeout=5).json()
    print(f"last 10 RX lines:")
    for ln in d.get('log_tail', [])[-10:]:
        print(f"  | {ln}")

run_test("#GET_DCE 1 kp")
run_test("#ONLINE")
