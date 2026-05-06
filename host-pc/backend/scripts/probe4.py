"""Try to recover via disconnect/reconnect cycle."""
import httpx, time

BASE = "http://localhost:8000"

def post(p, body=None, t=10):
    try:
        r = httpx.post(BASE+p, json=body, timeout=t)
        return r.status_code, r.text[:200]
    except Exception as e:
        return -1, f"EXC {e}"

def get(p, t=10):
    try:
        r = httpx.get(BASE+p, timeout=t)
        return r.status_code, r.text[:200]
    except Exception as e:
        return -1, f"EXC {e}"

print(">>> connection state")
print(get("/api/connection"))

print("\n>>> try GETJPOS without doing anything")
print(post("/api/raw", {"command":"#GETJPOS","timeout":3.0}))

print("\n>>> disconnect")
print(post("/api/disconnect"))

time.sleep(1.5)

print("\n>>> reconnect COM4")
print(post("/api/connect", {"port":"COM4","baud":115200}))

time.sleep(0.5)

print("\n>>> GETJPOS after reconnect")
print(post("/api/raw", {"command":"#GETJPOS","timeout":3.0}))

print("\n>>> RGB_MODE 0 after reconnect")
print(post("/api/raw", {"command":"!RGB_MODE 0","timeout":3.0}))

print("\n>>> RGB_OFF after reconnect")
print(post("/api/raw", {"command":"!RGB_OFF","timeout":3.0}))
