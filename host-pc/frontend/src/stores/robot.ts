import { defineStore } from 'pinia'
import { computed, ref } from 'vue'
import { api, RobotWs, type CartPos, type ConnectionState, type JointPos, type MotionLimits, type PortInfo, type WsMessage } from '@/api/client'

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
  const motionLimits = ref<MotionLimits>({})
  const limitsReady = ref(false)
  const limitWarning = ref('')
  let logId = 0

  function stateLabel(state: string): string {
    switch (state) {
      case 'normal': return '正常'
      case 'estop': return '急停'
      case 'paused': return '暂停'
      case 'stopped': return '已关闭'
      default: return '未知'
    }
  }

  function localizeFirmwareLine(line: string): string {
    return line
      .replace(/^Start OK/, '启动完成')
      .replace(/^Started OK/, '启动完成')
      .replace(/^Home OK/, '归位完成')
      .replace(/^Homed OK/, '归位完成')
      .replace(/^Reset OK/, '复位完成')
      .replace(/^Resume OK/, '恢复完成')
      .replace(/^Resumed/, '已恢复')
      .replace(/^Close OK/, '关闭完成')
      .replace(/^Stop OK/, '急停完成')
      .replace(/^Pause OK/, '暂停完成')
      .replace(/STATE:EMERGENCY_STOP/g, '状态：急停')
      .replace(/STATE:PAUSED/g, '状态：暂停')
      .replace(/STATE:NORMAL/g, '状态：正常')
  }

  function applyMotionState(state: string) {
    if (state === 'EMERGENCY_STOP') robotState.value = 'estop'
    else if (state === 'PAUSED') robotState.value = 'paused'
    else if (state === 'NORMAL') robotState.value = 'normal'
  }

  function setDryRun(v: boolean) {
    dryRun.value = v
    localStorage.setItem(DRY_RUN_KEY, v ? '1' : '0')
  }

  // ---- WebSocket ----
  const ws = new RobotWs()
  ws.on((msg: WsMessage) => {
    if (msg.type === 'state') {
      if (msg.motion_state) applyMotionState(msg.motion_state)
      if (msg.jpos !== undefined) jpos.value = msg.jpos
      if (msg.lpos !== undefined) lpos.value = msg.lpos
      if (msg.speed !== undefined) speed.value = msg.speed
      if (msg.motion_limits !== undefined) motionLimits.value = msg.motion_limits
      if (msg.limits_ready !== undefined) limitsReady.value = msg.limits_ready
      if (msg.limit_warning !== undefined) limitWarning.value = msg.limit_warning
      if (msg.connected !== undefined) {
        connection.value = {
          connected: msg.connected,
          port: msg.port ?? connection.value.port,
          baud: msg.baud ?? connection.value.baud,
        }
      }
      if (msg.message) pushLog(localizeFirmwareLine(msg.message), 'info')
    } else if (msg.type === 'log') {
      pushLog(localizeFirmwareLine(msg.line), 'rx', msg.noise === true)
      // Derive lifecycle state from specific log prefixes
      const l = msg.line.replace(/^R:\d+\s+/, '')
      if (/^(Start|Started) OK/.test(l)) robotState.value = 'normal'
      else if (/^(Home|Homed) OK/.test(l)) robotState.value = 'normal'
      else if (/^Reset OK/.test(l)) robotState.value = 'normal'
      else if (/^Resume OK/.test(l) || /^Resumed/.test(l)) robotState.value = 'normal'
      else if (/^Close OK/.test(l)) robotState.value = 'stopped'
      else if (/^Stop OK/.test(l) || /^Estop/.test(l) || /^ESTOP\|/.test(l)) robotState.value = 'estop'
      else if (/^Pause OK/.test(l) || /^PAUSED\|/.test(l)) robotState.value = 'paused'
      const state = l.match(/\bSTATE[:=](NORMAL|PAUSED|EMERGENCY_STOP)\b/i)
      if (state && !/^Close OK/.test(l)) applyMotionState(state[1].toUpperCase())
    } else if (msg.type === 'response') {
      // parsed response already in log line; nothing to do unless we want colour
      if (msg.warning) {
        limitWarning.value = msg.warning
        pushLog(`限制提醒：${msg.warning}（实际发送：${msg.applied_command || '未知'}）`, 'info')
      }
    } else if (msg.type === 'command_result') {
      if (msg.warning) {
        limitWarning.value = msg.warning
        pushLog(`限制提醒：${msg.warning}（实际发送：${msg.applied_command || '未知'}）`, 'info')
      }
    } else if (msg.type === 'error') {
      pushLog(`通信错误：${msg.reason}`, 'err')
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
    pushLog(`正在连接 ${port} @ ${baud}…`, 'info')
    try {
      const result = await api.connect(port, baud)
      connection.value = result
      motionLimits.value = result.motion_limits || {}
      limitsReady.value = result.limits_ready
      limitWarning.value = result.limit_warning || ''
      pushLog('连接成功', 'ok')
      if (result.limits_ready) {
        pushLog('已读取设备运动限制，后续动作会自动提醒并限幅', 'info')
      } else {
        pushLog(result.limit_warning || '未读取到设备运动限制，动作仍由固件最终限幅', 'info')
      }
      // Reconnect WS so it subscribes to the newly opened transport
      ws.close()
      ws.connect()
    } catch (e) {
      pushLog(`连接失败：${(e as Error).message}`, 'err')
      throw e
    }
  }
  async function disconnect() {
    connection.value = await api.disconnect()
    motionLimits.value = {}
    limitsReady.value = false
    limitWarning.value = ''
    pushLog('已断开连接', 'info')
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
      const token = cmd.trim().toUpperCase()
      if (robotState.value === 'estop' && token === '!PAUSE') {
        pushLog('暂停指令：当前处于急停状态', 'rx')
        return { ok: true, tag: 'Pause', rest: 'STATE:EMERGENCY_STOP', error: '', raw: 'Pause OK STATE:EMERGENCY_STOP' }
      }
      const query = /^#(GET|STATUS|ONLINE)/.test(token) || token.startsWith('GETPID')
      if (['estop', 'paused'].includes(robotState.value) && !query && !['!STOP', '!RESUME', '!PAUSE'].includes(token)) {
        pushLog(`拒绝指令：当前状态为${stateLabel(robotState.value)}`, 'err')
        throw new Error('当前处于急停或暂停状态，请先恢复')
      }
      pushLog(`→ ${cmd}    [模拟运行]`, 'tx')
      const ack = simulateAck(cmd)
      if (ack) {
        // Slight delay so the order in the console mirrors a real device.
        setTimeout(() => {
          pushLog(localizeFirmwareLine(ack), 'rx')
          // Mirror the same robotState transitions the WS path would do.
          if (/^(Start|Home|Reset|Resume) OK$/.test(ack)) robotState.value = 'normal'
          else if (ack === 'Stop OK') robotState.value = 'estop'
          else if (ack === 'Pause OK') robotState.value = 'paused'
          else if (ack === 'Close OK') robotState.value = 'stopped'
        }, 30)
      }
      return { ok: true, tag: 'DryRun', rest: '', error: '', raw: `${cmd}（模拟运行）` }
    }
    pushLog(`→ ${cmd}`, 'tx')
    try {
      const r = await api.raw(cmd)
      if (r.warning) pushLog(`限制提醒：${r.warning}`, 'info')
      pushLog(`← ${localizeFirmwareLine(r.raw)}`, r.ok ? 'ok' : 'err')
      return r
    } catch (e) {
      pushLog(`← 错误：${(e as Error).message}`, 'err')
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
      pushLog(`→ ${cmd}    [模拟运行]`, 'tx')
      return
    }
    pushLog(`→ ${cmd}`, 'tx')
    try {
      const result = await api.moveJoint(joints, speedDeg)
      if (result.warning) pushLog(`限制提醒：${result.warning}`, 'info')
    } catch (e) {
      pushLog(`关节运动失败：${(e as Error).message}`, 'err')
      throw e
    }
  }

  // ---- getters ----
  const isConnected = computed(() => connection.value.connected)

  return {
    ports, connection, defaults, jpos, lpos, speed, logs, dryRun, robotState,
    motionLimits, limitsReady, limitWarning,
    isConnected,
    refreshPorts, refreshConnection, loadDefaults,
    connect, disconnect, sendRaw, sendMoveJoint,
    setDryRun, pushLog, applyMotionState,
  }
})
