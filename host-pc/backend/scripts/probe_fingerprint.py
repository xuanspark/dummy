"""固件版本指纹：用 CAN 命令耗时判断烧的是新固件还是旧版。"""
import httpx, time

BASE = "http://localhost:8000"

print(">>> reconnect")
httpx.post(f"{BASE}/api/disconnect", timeout=5)
time.sleep(2.0)
print(httpx.post(f"{BASE}/api/connect", json={"port":"COM4","baud":115200}, timeout=10).json())
time.sleep(1.0)

def call(cmd, timeout=8):
    t0 = time.monotonic()
    try:
        r = httpx.post(f"{BASE}/api/raw", json={"command":cmd,"timeout":timeout},
                       timeout=timeout+5).json()
        dt = (time.monotonic()-t0)*1000
        print(f"  {dt:6.0f}ms  {cmd:20s}  ok={r.get('ok')}  raw={r.get('raw','')!r}  err={r.get('detail','')!r}")
        return dt
    except Exception as e:
        print(f"          {cmd:20s}  EXC: {e}")
        return -1

print("\n>>> 单节点单参 #GET_DCE 1 kp")
print("    旧固件 (osWaitForever): 会卡很久")
print("    新固件 (100ms 超时):     < 200ms 完成")
call("#GET_DCE 1 kp")

print("\n>>> 然后立刻 #GETJPOS（不应被前一条卡）")
call("#GETJPOS")

print("\n>>> log_tail 看到的所有 RX 行")
d = httpx.get(f"{BASE}/api/diag/state", timeout=5).json()
for ln in d.get('log_tail', []):
    print(f"    | {ln}")
