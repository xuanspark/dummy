# Dummy Robot Frontend

Vue 3 + Vite + TypeScript + Element Plus + Pinia frontend for the Python
backend at `../backend`.

## Prerequisites

- Node.js 18+
- Python backend running on `http://localhost:8000` (see `../backend/README.md`)

## Install & run

```powershell
cd D:\Desktop\t3\host-pc\frontend
npm install
npm run dev
```

Open http://localhost:5173. The dev server proxies `/api` and `/ws` to the
backend, so everything works with relative URLs.

## Build

```powershell
npm run build
npm run preview
```

## Current features

- **Connection panel**: auto-detects serial ports via `GET /api/ports`,
  likely-robot devices flagged; manual Connect / Disconnect.
- **Live log console**: subscribes to `/ws`, shows every RX/TX line with
  colour-coded kinds. Input line sends raw commands via `POST /api/raw`.
- **Pinia store** (`@/stores/robot`): single source of truth for
  ports / connection state / jpos / lpos / speed / logs.
- **Axios client** (`@/api/client`): typed wrappers for every REST endpoint
  plus a reconnecting `RobotWs` class.

## Project layout

```
src/
├── main.ts                # app bootstrap: Pinia + Element Plus
├── App.vue                # top-level layout
├── styles.css             # design tokens (dark theme)
├── api/
│   └── client.ts          # axios wrapper + WS client + shared types
├── stores/
│   └── robot.ts           # Pinia store (state + actions)
└── components/
    ├── ConnectionPanel.vue
    └── LogConsole.vue
```

## Next components (planned)

- `JointControl.vue` — six sliders + speed + real-time state readout
- `CartesianControl.vue` — x/y/z/a/b/c inputs
- `HandClaw.vue` — open/close + enable + position slider
- `Teaching.vue` — record frames to JSON, play back with speed control
- `DceTuning.vue` — per-joint PID tuning form
