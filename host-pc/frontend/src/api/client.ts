import axios, { AxiosError } from 'axios'

// All requests go through Vite's dev proxy -> Python backend on :8000.
// In production, serve the built frontend behind the same host or set VITE_API_BASE.
const http = axios.create({
  baseURL: import.meta.env.VITE_API_BASE || '',
  timeout: 60_000,
})

http.interceptors.response.use(
  (r) => r,
  (err: AxiosError<{ detail?: string }>) => {
    const detail = err.response?.data?.detail || err.message
    return Promise.reject(new Error(detail))
  },
)

// ---------- types ----------

export interface PortInfo {
  device: string
  description: string
  hwid: string
  is_likely_robot: boolean
}

export interface ConnectionState {
  connected: boolean
  port: string | null
  baud: number | null
}

export interface JointPos {
  j1: number; j2: number; j3: number
  j4: number; j5: number; j6: number
  hand: number
}

export interface CartPos {
  x: number; y: number; z: number
  a: number; b: number; c: number
  hand: number
}

export interface OnlineResult {
  summary: string | null            // e.g. "all_online" | "has_offline" | "node=1 OL"
  nodes: Record<string, boolean>    // "1".."7" -> online (key is string due to JSON)
  raw: string[]
}

export type DceKind = 'kp' | 'kv' | 'ki' | 'kd'
export type MotionKind = 'acc' | 'speed' | 'i_limit'

export interface DceNodeGains {
  online: boolean
  kp?: number
  kv?: number
  ki?: number
  kd?: number
}

export interface DceResult {
  nodes: Record<string, DceNodeGains>  // "1".."7" -> gains
  raw: string[]
}

// ---------- connection ----------

export const api = {
  listPorts: () => http.get<{ ports: PortInfo[] }>('/api/ports').then((r) => r.data.ports),
  getConnection: () => http.get<ConnectionState>('/api/connection').then((r) => r.data),
  getDefaults: () => http.get<{ port: string; baud: number }>('/api/connection/defaults').then((r) => r.data),
  connect: (port: string, baud: number) =>
    http.post<ConnectionState>('/api/connect', { port, baud }).then((r) => r.data),
  disconnect: () => http.post<ConnectionState>('/api/disconnect').then((r) => r.data),

  // ---------- robot lifecycle ----------
  start: () => http.post('/api/start').then((r) => r.data),
  close: () => http.post('/api/close').then((r) => r.data),
  home: () => http.post('/api/home').then((r) => r.data),
  reset: () => http.post('/api/reset').then((r) => r.data),
  estop: () => http.post('/api/estop').then((r) => r.data),
  pause: () => http.post('/api/pause').then((r) => r.data),
  resume: () => http.post('/api/resume').then((r) => r.data),

  // ---------- motion ----------
  // speed === undefined  → omit (firmware uses its current speed; 6-arg wire form)
  // speed === number     → override speed for this move (7-arg wire form)
  moveJoint: (joints: number[], speed?: number) =>
    http.post('/api/move/joint', speed == null ? { joints } : { joints, speed })
      .then((r) => r.data),
  moveCart: (pose: number[], speed?: number) =>
    http.post('/api/move/cart', speed == null ? { pose } : { pose, speed })
      .then((r) => r.data),

  // ---------- queries ----------
  getJpos: () => http.get<JointPos>('/api/state/joint').then((r) => r.data),
  getLpos: () => http.get<CartPos>('/api/state/cart').then((r) => r.data),
  getSpeed: () => http.get<{ speed: number }>('/api/state/speed').then((r) => r.data.speed),

  // ---------- hand ----------
  handOpen: () => http.post('/api/hand/open').then((r) => r.data),
  handClose: () => http.post('/api/hand/close').then((r) => r.data),
  handEnable: (enable: boolean) => http.post('/api/hand/enable', { enable }).then((r) => r.data),
  handPos: (percent: number) => http.post('/api/hand/pos', { percent }).then((r) => r.data),
  handZero: () =>
    http.post<{ ok: boolean; closed: number; opened: number }>('/api/hand/zero')
      .then((r) => r.data),
  handCurrent: (amps: number) => http.post('/api/hand/current', { amps }).then((r) => r.data),

  // ---------- tuning ----------
  setDceGain: (node: number, kind: DceKind, value: number) =>
    http.post<{ ok: boolean; tag: string; rest: string; raw: string }>(
      '/api/dce/gain', { node, kind, value },
    ).then((r) => r.data),
  getDce: (node?: number, param?: DceKind) => {
    const params: Record<string, string | number> = {}
    if (node != null) params.node = node
    if (param) params.param = param
    return http.get<DceResult>('/api/dce', { params }).then((r) => r.data)
  },
  setMotionLimit: (node: number, kind: MotionKind, value: number) =>
    http.post<{ ok: boolean; tag: string; rest: string; raw: string }>(
      '/api/motion/limit', { node, kind, value },
    ).then((r) => r.data),

  // ---------- rgb ----------
  rgbOn: () => http.post('/api/rgb/on').then((r) => r.data),
  rgbOff: () => http.post('/api/rgb/off').then((r) => r.data),
  rgbMode: (mode: number) => http.post('/api/rgb/mode', { mode }).then((r) => r.data),
  rgbCustom: (r: number, g: number, b: number) =>
    http.post('/api/rgb/custom', { r, g, b }).then((res) => res.data),

  // ---------- diagnostics ----------
  checkOnline: (node?: number) =>
    http.get<OnlineResult>('/api/diag/online', { params: node != null ? { node } : {} })
      .then((r) => r.data),
  reboot: (node?: number | null) =>
    http.post<{ ok: boolean; tag: string; rest: string; raw: string }>(
      '/api/reboot', { node: node ?? null },
    ).then((r) => r.data),

  // ---------- maintenance ----------
  calibrate: () => http.post('/api/calibrate').then((r) => r.data),
  offsetJ: (target: number | 'all') =>
    http.post<{ ok: boolean; tag: string; rest: string; raw: string }>(
      '/api/offset_j', { target },
    ).then((r) => r.data),
  cmdMode: (mode: number) =>
    http.post<{ ok: boolean; tag: string; rest: string; raw: string }>(
      '/api/cmdmode', { mode },
    ).then((r) => r.data),

  // ---------- raw ----------
  raw: (command: string, timeout?: number) =>
    http.post<{ ok: boolean; tag: string; rest: string; error: string; raw: string }>(
      '/api/raw', { command, timeout },
    ).then((r) => r.data),
}

// ---------- WebSocket ----------

export type WsMessage =
  | { type: 'state'; connected: boolean; port?: string; baud?: number
      jpos: JointPos | null; lpos: CartPos | null; speed: number | null; message?: string }
  | { type: 'log'; line: string; noise?: boolean }
  | { type: 'response'; tag: string; ok: boolean; rest: string; error: string }
  | { type: 'pong' }
  | { type: 'error'; reason: string }

export type WsHandler = (msg: WsMessage) => void

export class RobotWs {
  private ws: WebSocket | null = null
  private handlers = new Set<WsHandler>()
  private reconnectTimer: number | null = null
  private url: string

  constructor(path = '/ws') {
    const proto = location.protocol === 'https:' ? 'wss' : 'ws'
    this.url = `${proto}://${location.host}${path}`
  }

  connect() {
    if (this.ws && this.ws.readyState !== WebSocket.CLOSED) return
    this.ws = new WebSocket(this.url)
    this.ws.onmessage = (ev) => {
      try {
        const msg = JSON.parse(ev.data) as WsMessage
        this.handlers.forEach((h) => h(msg))
      } catch {
        /* ignore malformed */
      }
    }
    this.ws.onclose = () => {
      this.ws = null
      // Auto-reconnect after 1.5s
      if (this.reconnectTimer == null) {
        this.reconnectTimer = window.setTimeout(() => {
          this.reconnectTimer = null
          this.connect()
        }, 1500)
      }
    }
  }

  send(obj: unknown) {
    if (this.ws?.readyState === WebSocket.OPEN) this.ws.send(JSON.stringify(obj))
  }

  on(h: WsHandler) { this.handlers.add(h); return () => this.handlers.delete(h) }

  close() {
    if (this.reconnectTimer != null) {
      clearTimeout(this.reconnectTimer)
      this.reconnectTimer = null
    }
    this.ws?.close()
    this.ws = null
  }
}
