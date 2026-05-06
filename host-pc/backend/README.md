# Dummy Robot Backend

Python backend for the Dummy 6-DOF arm. Speaks the firmware ASCII protocol
over **direct serial** (USB-CDC / USB-UART). Exposes:

- **REST API** — synchronous command endpoints (`POST /api/move/joint`, ...)
- **WebSocket** — `/ws` streams every RX line + parsed responses + cached state
- **OpenAPI docs** — visit `http://localhost:8000/docs` for a clickable test UI

The backend **never auto-connects**. After the server starts, you (or the
frontend) call `GET /api/ports` to enumerate serial devices, then
`POST /api/connect` to open one.

## Architecture

```
┌─────────────────────────────┐
│  REST + WS API (FastAPI)    │   app/api/
├─────────────────────────────┤
│  Robot high-level coroutines│   app/core/robot.py
├─────────────────────────────┤
│  ASCII protocol (pure)      │   app/core/protocol.py
├─────────────────────────────┤
│  Transport: serial | tcp    │   app/core/transport.py
└─────────────────────────────┘
```

Each layer is independently testable. The protocol layer has no IO at all.

## Install

```powershell
cd D:\Desktop\t3\host-pc\backend
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -e .[dev]
```

## Configure (optional)

`.env.example` → `.env` only customises the connect-dialog defaults and the
HTTP bind address. Can be skipped entirely.

```ini
ROBOT_DEFAULT_PORT=          # blank ⇒ no pre-fill
ROBOT_DEFAULT_BAUD=115200
ROBOT_HTTP_HOST=0.0.0.0
ROBOT_HTTP_PORT=8000
ROBOT_CMD_TIMEOUT=5.0
```

## Run

```powershell
python -m app.main
# or with auto-reload during development:
uvicorn app.main:app --reload --host 0.0.0.0 --port 8000
```

Open http://localhost:8000/docs for interactive REST testing.

## Quick start (in /docs)

1. `GET /api/ports` — see detected serial devices (likely-robot flagged first)
2. `POST /api/connect` with `{"port":"COM5","baud":115200}`
3. `POST /api/start` to enable the robot
4. `POST /api/home`, then any motion command
5. `POST /api/disconnect` when finished

## REST endpoints

### Connection
| Method | Path | Body |
|---|---|---|
| GET  | `/api/ports`              | — (auto-detect serial devices) |
| GET  | `/api/connection`         | — (current connection state) |
| GET  | `/api/connection/defaults`| — (defaults from .env) |
| POST | `/api/connect`            | `{port, baud}` |
| POST | `/api/disconnect`         | — |

### Robot (require an active connection)
| Method | Path | Body |
|---|---|---|
| POST | `/api/start` | — |
| POST | `/api/close` | — |
| POST | `/api/home` | — |
| POST | `/api/reset` | — |
| POST | `/api/estop` | — |
| POST | `/api/pause` / `/api/resume` | — |
| POST | `/api/move/joint` | `{joints:[6 floats], speed}` |
| POST | `/api/move/cart`  | `{pose:[6 floats], speed}` |
| GET  | `/api/state/joint` | — |
| GET  | `/api/state/cart`  | — |
| GET  | `/api/state/speed` | — |
| GET  | `/api/state` | — (cached) |
| POST | `/api/hand/{open,close}` | — |
| POST | `/api/hand/enable` | `{enable: bool}` |
| POST | `/api/hand/pos` | `{percent: 0..100}` |
| POST | `/api/rgb/{on,off}` | — |
| POST | `/api/rgb/mode` | `{mode: 0..11}` |
| POST | `/api/rgb/custom` | `{r,g,b}` |
| POST | `/api/raw` | `{command, timeout?}` |

Errors return HTTP 400 with `{detail: "<reason>"}` matching the firmware's
`Error: <reason>` text.

## WebSocket protocol

Connect to `ws://localhost:8000/ws`. JSON frames both directions.

Server → client:
```json
{"type":"log",      "line":"Move OK"}
{"type":"response", "tag":"Move", "ok":true, "rest":"", "error":""}
{"type":"state",    "jpos":{"j1":0,...,"hand":0}, "lpos":null, "speed":null}
```

Client → server:
```json
{"type":"raw","command":"!HOME"}
{"type":"ping"}
```

## Test

```powershell
pytest -q
```

Tests cover the IO-free protocol layer (parse / build / expected_tag).

## Notes / gotchas

- **One owner per serial port**: if the ESP32 web UI is also using the STM32
  USB port, prefer transport `tcp` and connect through ESP32 (4001).
- **`!START` / `!HOME`** can take >5 s. Default timeouts in `Robot` are
  already widened for those (15-30 s).
- **Async events** like `Estop OK` arrive without a matching command;
  `Robot._exec` ignores out-of-band tags while waiting for the expected one.
- **State cache** in `Robot.state` is updated whenever a `GetJPos` /
  `GetLPos` / `GetSpeed` response goes through, so multiple WS clients all
  see fresh values without re-querying.
