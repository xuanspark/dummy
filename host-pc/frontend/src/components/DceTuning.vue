<script setup lang="ts">
import { ref } from 'vue'
import { ElMessage } from 'element-plus'
import { api, type DceKind, type MotionKind } from '@/api/client'
import { useRobotStore } from '@/stores/robot'

const store = useRobotStore()

// Selected joint node (firmware uses 1-6 for J1-J6; motion limits accept 1-7)
const node = ref<string>('1')

// Editable parameter values (strings to allow blank → "do nothing")
const vals = ref({
  kp: '',
  kv: '',
  ki: '',
  kd: '',
  acc: '',
  speed: '',
  iLimit: '',
})

// Last queried gains for the selected node (populated by Get DCE)
const liveGains = ref<{ kp?: number; kv?: number; ki?: number; kd?: number; online?: boolean } | null>(null)

// Field descriptors — grouped by function. ``family`` decides which typed
// backend endpoint to call: 'dce' → /api/dce/gain, 'motion' → /api/motion/limit.
type FieldKey = keyof typeof vals.value
type DceField    = { key: FieldKey; label: string; cmd: string; hint?: string; family: 'dce';    kind: DceKind }
type MotionField = { key: FieldKey; label: string; cmd: string; hint?: string; family: 'motion'; kind: MotionKind }
type Field = DceField | MotionField

const pidFields: DceField[] = [
  { key: 'kp', label: 'KP', cmd: '#SET_DCE_KP', hint: 'Position gain',    family: 'dce', kind: 'kp' },
  { key: 'kv', label: 'KV', cmd: '#SET_DCE_KV', hint: 'Velocity gain',    family: 'dce', kind: 'kv' },
  { key: 'ki', label: 'KI', cmd: '#SET_DCE_KI', hint: 'Integral gain',    family: 'dce', kind: 'ki' },
  { key: 'kd', label: 'KD', cmd: '#SET_DCE_KD', hint: 'Derivative gain',  family: 'dce', kind: 'kd' },
]
const motionFields: MotionField[] = [
  { key: 'acc',    label: 'ACC_J',   cmd: '#ACC_J',     hint: '°/s²',           family: 'motion', kind: 'acc'     },
  { key: 'speed',  label: 'SPEED_J', cmd: '#SPEED_J',   hint: '°/s',            family: 'motion', kind: 'speed'   },
  { key: 'iLimit', label: 'I_LIMIT', cmd: '#I_LIMIT_J', hint: 'Current clamp',  family: 'motion', kind: 'i_limit' },
]
const fields: Field[] = [...pidFields, ...motionFields]

function canSend(): boolean {
  if (!store.isConnected && !store.dryRun) {
    ElMessage.warning('Not connected (or enable Dry-run)')
    return false
  }
  return true
}

function logTx(line: string) {
  store.pushLog(`→ ${line}${store.dryRun ? '    [dry-run]' : ''}`, 'tx')
}

async function dispatchField(f: Field, v: string): Promise<void> {
  const n = parseInt(node.value, 10)
  // Echo the wire command (matches firmware grammar) for the console
  logTx(`${f.cmd} ${n} ${v}`)
  if (store.dryRun) return
  try {
    if (f.family === 'dce') {
      const res = await api.setDceGain(n, f.kind, parseInt(v, 10))
      store.pushLog(`← ${res.raw}`, res.ok ? 'ok' : 'err')
    } else {
      const res = await api.setMotionLimit(n, f.kind, parseFloat(v))
      store.pushLog(`← ${res.raw}`, res.ok ? 'ok' : 'err')
    }
  } catch (e) {
    store.pushLog(`← ERROR: ${(e as Error).message}`, 'err')
    throw e
  }
}

async function sendOne(f: Field) {
  const v = vals.value[f.key].trim()
  if (v === '') { ElMessage.info('Enter a value first'); return }
  if (!canSend()) return
  if (f.family === 'dce' && !/^\d+$/.test(v)) {
    ElMessage.warning(`${f.label} must be a non-negative integer`)
    return
  }
  if (f.family === 'motion' && !/^-?\d+(\.\d+)?$/.test(v)) {
    ElMessage.warning(`${f.label} must be a number`)
    return
  }
  try { await dispatchField(f, v) } catch { /* already logged */ }
}

async function getDce() {
  if (!canSend()) return
  const n = parseInt(node.value, 10)
  logTx(`#GET_DCE ${n}`)
  if (store.dryRun) return
  try {
    const res = await api.getDce(n)
    const g = res.nodes[String(n)]
    if (g) {
      liveGains.value = g
      store.pushLog(
        `← J${n} ${g.online ? 'OL' : 'NL'}: KP:${g.kp ?? '?'}  KV:${g.kv ?? '?'}  KI:${g.ki ?? '?'}  KD:${g.kd ?? '?'}`,
        'ok',
      )
      // Auto-fill the input boxes so user can edit-then-Set without retyping
      if (g.kp != null) vals.value.kp = String(g.kp)
      if (g.kv != null) vals.value.kv = String(g.kv)
      if (g.ki != null) vals.value.ki = String(g.ki)
      if (g.kd != null) vals.value.kd = String(g.kd)
    } else {
      store.pushLog(`← (no data for node ${n})`, 'err')
    }
  } catch (e) {
    store.pushLog(`← ERROR: ${(e as Error).message}`, 'err')
    ElMessage.error((e as Error).message)
  }
}

async function sendAll() {
  if (!canSend()) return
  // Send every non-empty field sequentially (validate per-field as we go)
  for (const f of fields) {
    const v = vals.value[f.key].trim()
    if (v === '') continue
    if (f.family === 'dce' && !/^\d+$/.test(v)) continue
    if (f.family === 'motion' && !/^-?\d+(\.\d+)?$/.test(v)) continue
    try { await dispatchField(f, v) } catch { /* logged */ }
  }
}

function clearAll() {
  for (const f of fields) vals.value[f.key] = ''
  liveGains.value = null
}
</script>

<template>
  <section class="card dce">
    <div class="title-row">
      <h2>DCE Tuning</h2>
      <span class="j-badge">J{{ node }}</span>
      <span class="hint">KP / KV / KI / KD · motion limits, per joint</span>
      <span class="spacer" />
      <label class="node-label">Node</label>
      <el-select v-model="node" style="width: 110px" size="small">
        <el-option v-for="i in 6" :key="i" :label="`${i} (J${i})`" :value="String(i)" />
      </el-select>
      <el-button size="small" :disabled="!store.isConnected && !store.dryRun" @click="getDce">
        Get DCE
      </el-button>
    </div>

    <!-- PID group -->
    <div class="group">
      <div class="group-title">PID 增益 <span class="group-sub">KP / KV / KI / KD</span></div>
      <div class="grid grid-4">
        <div v-for="f in pidFields" :key="f.key" class="field">
          <label class="label">{{ f.label }}</label>
          <el-input
            v-model="vals[f.key]"
            size="small"
            :placeholder="f.hint"
            @keyup.enter="sendOne(f)"
          />
          <el-button size="small" type="primary"
            :disabled="!store.isConnected && !store.dryRun"
            @click="sendOne(f)">Set</el-button>
        </div>
      </div>
    </div>

    <!-- Motion group -->
    <div class="group">
      <div class="group-title">运动限制 <span class="group-sub">ACC / SPEED / I_LIMIT</span></div>
      <div class="grid grid-3">
        <div v-for="f in motionFields" :key="f.key" class="field">
          <label class="label">{{ f.label }}</label>
          <el-input
            v-model="vals[f.key]"
            size="small"
            :placeholder="f.hint"
            @keyup.enter="sendOne(f)"
          />
          <el-button size="small" type="primary"
            :disabled="!store.isConnected && !store.dryRun"
            @click="sendOne(f)">Set</el-button>
        </div>
      </div>
    </div>

    <div class="actions">
      <el-button size="small" type="success"
        :disabled="!store.isConnected && !store.dryRun" @click="sendAll">
        Set All Filled
      </el-button>
      <el-button size="small" @click="clearAll">Clear Inputs</el-button>
      <span class="spacer" />
      <span class="warn">⚠ 数值写入关节板 RAM，需持久化请用固件命令</span>
    </div>
  </section>
</template>

<style scoped>
.dce { display: flex; flex-direction: column; gap: 0.7em; }
.title-row {
  display: flex;
  align-items: center;
  gap: 0.55em;
  flex-wrap: wrap;
  padding-bottom: 0.5em;
  border-bottom: 1px solid #1e293b;
}
.title-row h2 { margin: 0; font-size: 1rem; }
.j-badge {
  font-family: Consolas, monospace;
  font-size: 0.78rem;
  padding: 2px 8px;
  border-radius: 999px;
  background: rgba(59, 130, 246, 0.18);
  color: #93c5fd;
  border: 1px solid rgba(59, 130, 246, 0.4);
}
.hint { color: var(--fg-dim); font-size: 0.8rem; }
.spacer { flex: 1; }
.node-label { color: var(--fg-dim); font-size: 0.85rem; }

.group { display: flex; flex-direction: column; gap: 0.4em; }
.group-title {
  font-size: 0.8rem;
  color: var(--fg-dim);
  font-weight: 600;
  letter-spacing: 0.02em;
}
.group-sub {
  font-weight: 400;
  font-family: Consolas, monospace;
  font-size: 0.72rem;
  color: #64748b;
  margin-left: 0.4em;
}

.grid {
  display: grid;
  gap: 0.4em 0.8em;
}
.grid-4 { grid-template-columns: repeat(4, minmax(0, 1fr)); }
.grid-3 { grid-template-columns: repeat(3, minmax(0, 1fr)); }
@media (max-width: 1100px) {
  .grid-4 { grid-template-columns: repeat(2, minmax(0, 1fr)); }
  .grid-3 { grid-template-columns: repeat(2, minmax(0, 1fr)); }
}
@media (max-width: 700px) {
  .grid-4, .grid-3 { grid-template-columns: 1fr; }
}

.field {
  display: flex;
  align-items: center;
  gap: 0.4em;
  min-width: 0;
}
.label {
  min-width: 4.8em;
  color: var(--fg-dim);
  font-size: 0.85rem;
  font-family: Consolas, monospace;
  flex: 0 0 auto;
}
.field :deep(.el-input) { flex: 1 1 0; min-width: 0; }

.actions {
  display: flex;
  align-items: center;
  gap: 0.6em;
  flex-wrap: wrap;
  padding-top: 0.5em;
  border-top: 1px solid #1f2937;
  margin-top: 0.2em;
}
.warn {
  color: #fbbf24;
  font-size: 0.75rem;
  opacity: 0.85;
}
</style>
