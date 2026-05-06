import { defineStore } from 'pinia'
import { computed, ref } from 'vue'
import { api, RobotWs, type CartPos, type ConnectionState, type JointPos, type PortInfo, type WsMessage } from '@/api/client'

export interface LogEntry {
  id: number
  time: number
  line: string
  kind: 'rx' | 'tx' | 'info' | 'ok' | 'err'
  noise?: boolean   // ESP32 boot/system spam, hidden by default in Console
}

const MAX_LOG = 500
const DRY_RUN_KEY = 'dummy.dryRun'

export const useRobotStore = defineStore('robot', () => {
  // ---- state ----
  const ports = ref<PortInfo[]>([])
  const connection = ref<ConnectionState>({ connected: false, port: null, baud: null })
  const defaults = ref<{ port: string; baud: number }>({ port: '', baud: 115200 })
  const jpos = ref<JointPos | null>(null)
  const lpos = ref<CartPos | null>(null)
  const speed = ref<number | null>(null)
  const logs = ref<LogEntry[]>([])
  const dryRun = ref<boolean>(localStorage.getItem(DRY_RUN_KEY) === '1')
  const robotState = ref<'unknown' | 'normal' | 'estop' | 'paused' | 'stopped'>('unknown')
  let logId = 0

  function setDryRun(v: boolean) {
    dryRun.value = v
    localStorage.setItem(DRY_RUN_KEY, v ? '1' : '0')
  }

  // ---- WebSocket ----
  const ws = new RobotWs()
  ws.on((msg: WsMessage) => {
    if (msg.type === 'state') {
      if (msg.jpos !== undefined) jpos.value = msg.jpos
      if (msg.lpos !== undefined) lpos.value = msg.lpos
      if (msg.speed !== undefined) speed.value = msg.speed
      if (msg.connected !== undefined) {
        connection.value = {
          connected: msg.connected,
          port: msg.port ?? connection.value.port,
          baud: msg.baud ?? connection.value.baud,
        }
      }
      if (msg.message) pushLog(msg.message, 'info')
    } else if (msg.type === 'log') {
      pushLog(msg.line, 'rx', msg.noise === true)
      // Derive lifecycle state from specific log prefixes
      const l = msg.line
      if (/^(Start|Started) OK/.test(l)) robotState.value = 'normal'
      else if (/^(Home|Homed) OK/.test(l)) robotState.value = 'normal'
      else if (/^Reset OK/.test(l)) robotState.value = 'normal'
      else if (/^Resume OK/.test(l) || /^Resumed/.test(l)) robotState.value = 'normal'
      else if (/^Close OK/.test(l)) robotState.value = 'stopped'
      else if (/^Stop OK/.test(l) || /^Estop/.test(l) || /^ESTOP\|/.test(l)) robotState.value = 'estop'
      else if (/^Pause OK/.test(l) || /^PAUSED\|/.test(l)) robotState.value = 'paused'
    } else if (msg.type === 'response') {
      // parsed response already in log line; nothing to do unless we want colour
    } else if (msg.type === 'error') {
      pushLog(`[ws] ${msg.reason}`, 'err')
    }
  })
  ws.connect()

  function pushLog(line: string, kind: LogEntry['kind'] = 'rx', noise = false) {
    logs.value.push({ id: ++logId, time: Date.now(), line, kind, noise })
    if (logs.value.length > MAX_LOG) logs.value.splice(0, logs.value.length - MAX_LOG)
  }

  // ---- actions ----
  async function refreshPorts() {
    ports.value = await api.listPorts()
  }
  async function refreshConnection() {
    connection.value = await api.getConnection()
  }
  async function loadDefaults() {
    defaults.value = await api.getDefaults()
  }
  async function connect(port: string, baud: number) {
    pushLog(`connecting ${port} @ ${baud}...`, 'info')
    try {
      connection.value = await api.connect(port, baud)
      pushLog(`connected`, 'ok')
      // Reconnect WS so it subscribes to the newly opened transport
      ws.close()
      ws.connect()
    } catch (e) {
      pushLog(`connect failed: ${(e as Error).message}`, 'err')
      throw e
    }
  }
  async function disconnect() {
    connection.value = await api.disconnect()
    pushLog('disconnected', 'info')
  }
  // Dry-run: synthesise the firmware acknowledgement that a real device would
  // emit, so that downstream state derivation (robotState badge, gating, etc.)
  // remains exercisable without hardware. Keep the simulated text close to the
  // strings that the parser in the WS handler matches against.
  function simulateAck(cmd: string): string | null {
    const c = cmd.trim().toUpperCase()
    if (c === '!START')  return 'Start OK'
    if (c === '!HOME')   return 'Home OK'
    if (c === '!RESET')  return 'Reset OK'
    if (c === '!RESUME') return 'Resume OK'
    if (c === '!PAUSE')  return 'Pause OK'
    if (c === '!STOP')   return 'Stop OK'
    if (c === '!CLOSE')  return 'Close OK'
    return null
  }

  async function sendRaw(cmd: string) {
    if (dryRun.value) {
      pushLog(`→ ${cmd}    [dry-run]`, 'tx')
      const ack = simulateAck(cmd)
      if (ack) {
        // Slight delay so the order in the console mirrors a real device.
        setTimeout(() => {
          pushLog(ack, 'rx')
          // Mirror the same robotState transitions the WS path would do.
          if (/^(Start|Home|Reset|Resume) OK$/.test(ack)) robotState.value = 'normal'
          else if (ack === 'Stop OK') robotState.value = 'estop'
          else if (ack === 'Pause OK') robotState.value = 'paused'
          else if (ack === 'Close OK') robotState.value = 'stopped'
        }, 30)
      }
      return { ok: true, tag: 'DryRun', rest: '', error: '', raw: `${cmd} (dry-run)` }
    }
    pushLog(`→ ${cmd}`, 'tx')
    try {
      const r = await api.raw(cmd)
      pushLog(`← ${r.raw}`, r.ok ? 'ok' : 'err')
      return r
    } catch (e) {
      pushLog(`← ERROR: ${(e as Error).message}`, 'err')
      throw e
    }
  }

  /** Build a MoveJ command string and either send it or just log it.
   *
   * When ``speedDeg`` is ``undefined`` the 6-arg wire form is used (firmware
   * will fall back to its current joint speed). Pass a number to override
   * speed for this move (7-arg form).
   */
  async function sendMoveJoint(joints: number[], speedDeg?: number) {
    // Format: integers as is, fractions trimmed to ≤3 decimals with trailing zeros removed
    const fmt = (v: number) =>
      Number.isInteger(v) ? v.toString() : parseFloat(v.toFixed(3)).toString()
    const body = '&' + joints.map(fmt).join(',')
    const cmd = speedDeg == null ? body : `${body},${speedDeg}`
    if (dryRun.value) {
      pushLog(`→ ${cmd}    [dry-run]`, 'tx')
      return
    }
    pushLog(`→ ${cmd}`, 'tx')
    try {
      await api.moveJoint(joints, speedDeg)
    } catch (e) {
      pushLog(`MoveJ failed: ${(e as Error).message}`, 'err')
      throw e
    }
  }

  // ---- getters ----
  const isConnected = computed(() => connection.value.connected)

  return {
    ports, connection, defaults, jpos, lpos, speed, logs, dryRun, robotState,
    isConnected,
    refreshPorts, refreshConnection, loadDefaults,
    connect, disconnect, sendRaw, sendMoveJoint,
    setDryRun, pushLog,
  }
})
