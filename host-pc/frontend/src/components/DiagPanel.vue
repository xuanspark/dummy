<script setup lang="ts">
import { computed, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { api, type OnlineResult } from '@/api/client'
import { useRobotStore } from '@/stores/robot'

const store = useRobotStore()

// ---- online check state ----
const onlineBusy = ref(false)
const lastResult = ref<OnlineResult | null>(null)

// Node labels: 1..6 = J1..J6, 7 = HAND
const NODES: Array<{ id: number; label: string }> = [
  { id: 1, label: 'J1' },
  { id: 2, label: 'J2' },
  { id: 3, label: 'J3' },
  { id: 4, label: 'J4' },
  { id: 5, label: 'J5' },
  { id: 6, label: 'J6' },
  { id: 7, label: 'HAND' },
]

function statusOf(id: number): 'online' | 'offline' | 'unknown' {
  const r = lastResult.value
  if (!r) return 'unknown'
  const v = r.nodes[String(id)]
  if (v === true) return 'online'
  if (v === false) return 'offline'
  return 'unknown'
}

const summaryColor = computed(() => {
  const s = lastResult.value?.summary ?? ''
  if (/all_online/.test(s)) return '#22c55e'
  if (/has_offline|NL/.test(s)) return '#ef4444'
  return '#64748b'
})

async function runOnlineAll() {
  if (!store.isConnected) {
    ElMessage.warning('Not connected')
    return
  }
  onlineBusy.value = true
  try {
    store.pushLog('→ #ONLINE', 'tx')
    const res = await api.checkOnline()
    lastResult.value = res
    store.pushLog(`← ${res.raw.join(' | ')}`, 'ok')
  } catch (e) {
    store.pushLog(`← ERROR: ${(e as Error).message}`, 'err')
    ElMessage.error((e as Error).message)
  } finally {
    onlineBusy.value = false
  }
}

async function runOnlineNode(id: number) {
  if (!store.isConnected) return
  onlineBusy.value = true
  try {
    store.pushLog(`→ #ONLINE ${id}`, 'tx')
    const res = await api.checkOnline(id)
    // Merge into lastResult so the badge for that single node updates
    const prev = lastResult.value ?? { summary: null, nodes: {}, raw: [] }
    lastResult.value = {
      summary: res.summary ?? prev.summary,
      nodes: { ...prev.nodes, ...res.nodes },
      raw: res.raw,
    }
    store.pushLog(`← ${res.raw.join(' | ')}`, 'ok')
  } catch (e) {
    store.pushLog(`← ERROR: ${(e as Error).message}`, 'err')
  } finally {
    onlineBusy.value = false
  }
}

// ---- reboot ----
// Option values map directly to `#REBOOT <n>` (null == no-arg = all+system-reset).
const REBOOT_OPTIONS: Array<{ value: number | null; label: string; danger?: boolean }> = [
  { value: null, label: '全部（所有电机 + 系统复位）', danger: true },
  { value: 0,    label: 'REF 主控板（系统复位）',      danger: true },
  { value: 1,    label: 'J1 关节电机' },
  { value: 2,    label: 'J2 关节电机' },
  { value: 3,    label: 'J3 关节电机' },
  { value: 4,    label: 'J4 关节电机' },
  { value: 5,    label: 'J5 关节电机' },
  { value: 6,    label: 'J6 关节电机' },
  { value: 7,    label: 'HAND 手爪' },
]

const rebootTarget = ref<number | null>(1)
const rebootBusy = ref(false)

async function runReboot() {
  if (!store.isConnected) {
    ElMessage.warning('Not connected')
    return
  }
  const opt = REBOOT_OPTIONS.find((o) => o.value === rebootTarget.value)
  const label = opt?.label ?? `node=${rebootTarget.value}`
  try {
    await ElMessageBox.confirm(
      `Really reboot: ${label}?` + (opt?.danger ? '\n\n⚠ 会触发主控系统复位，当前串口会断开。' : ''),
      'Confirm Reboot',
      { confirmButtonText: 'Reboot', cancelButtonText: 'Cancel', type: 'warning' },
    )
  } catch { return }

  rebootBusy.value = true
  try {
    const cmdStr = rebootTarget.value == null ? '#REBOOT' : `#REBOOT ${rebootTarget.value}`
    store.pushLog(`→ ${cmdStr}`, 'tx')
    const res = await api.reboot(rebootTarget.value)
    store.pushLog(`← ${res.raw}`, res.ok ? 'ok' : 'err')
    if (res.ok) ElMessage.success('Reboot command sent')
  } catch (e) {
    store.pushLog(`← ERROR: ${(e as Error).message}`, 'err')
    ElMessage.error((e as Error).message)
  } finally {
    rebootBusy.value = false
  }
}

// ---- maintenance (P1) ----
const calibBusy = ref(false)
const offsetBusy = ref(false)
const cmdModeBusy = ref(false)

// Firmware commandMode numeric values (see DummyRobot::CommandMode / main.cpp OLED)
const CMD_MODES: Array<{ value: number; label: string }> = [
  { value: 1, label: '1 — SEQ  (Target Point Sequential)' },
  { value: 2, label: '2 — INT  (Target Point Interruptable)' },
  { value: 3, label: '3 — TRJ  (Continuous Trajectory)' },
  { value: 4, label: '4 — TUN  (Motor Tuning)' },
]
const cmdMode = ref<number>(2)

// OFFSET_J target: 1..6 joint, 7 hand, 'all'
const OFFSET_TARGETS: Array<{ value: number | 'all'; label: string; danger?: boolean }> = [
  { value: 1,     label: 'J1' },
  { value: 2,     label: 'J2' },
  { value: 3,     label: 'J3' },
  { value: 4,     label: 'J4' },
  { value: 5,     label: 'J5' },
  { value: 6,     label: 'J6' },
  { value: 7,     label: 'HAND' },
  { value: 'all', label: 'ALL (全部 + 系统复位)', danger: true },
]
const offsetTarget = ref<number | 'all'>(1)

async function runCalibrate() {
  if (!store.isConnected) { ElMessage.warning('Not connected'); return }
  try {
    await ElMessageBox.confirm(
      '执行零点校准 (!CALIBRATION)？\n\n此操作将由固件驱动全部关节进行小幅标定动作；过程最多约 30 秒。',
      'Confirm Calibration',
      { confirmButtonText: 'Run', cancelButtonText: 'Cancel', type: 'warning' },
    )
  } catch { return }
  calibBusy.value = true
  try {
    store.pushLog('→ !CALIBRATION', 'tx')
    await api.calibrate()
    store.pushLog('← Calibration OK', 'ok')
    ElMessage.success('Calibration done')
  } catch (e) {
    store.pushLog(`← ERROR: ${(e as Error).message}`, 'err')
    ElMessage.error((e as Error).message)
  } finally {
    calibBusy.value = false
  }
}

async function runOffsetJ() {
  if (!store.isConnected) { ElMessage.warning('Not connected'); return }
  const opt = OFFSET_TARGETS.find((o) => o.value === offsetTarget.value)
  const label = opt?.label ?? String(offsetTarget.value)
  try {
    await ElMessageBox.confirm(
      `⚠ 危险操作：#OFFSET_J ${offsetTarget.value}\n\n` +
      '将当前关节位置写入 NVS 作为新的零点。写入是持久化的，重启后生效。\n' +
      (opt?.danger
        ? '\n"ALL" 变体会在写入后立即触发 HAL_NVIC_SystemReset()，串口会断开。'
        : '\n单节点写入不会触发系统复位。') +
      `\n\n确定对 ${label} 执行？`,
      'Confirm NVS Write',
      { confirmButtonText: 'Write NVS', cancelButtonText: 'Cancel', type: 'error' },
    )
  } catch { return }
  offsetBusy.value = true
  try {
    const cmdStr = `#OFFSET_J ${offsetTarget.value}`
    store.pushLog(`→ ${cmdStr}`, 'tx')
    const res = await api.offsetJ(offsetTarget.value)
    store.pushLog(`← ${res.raw}`, res.ok ? 'ok' : 'err')
    if (res.ok) ElMessage.success('Offset written')
  } catch (e) {
    store.pushLog(`← ERROR: ${(e as Error).message}`, 'err')
    ElMessage.error((e as Error).message)
  } finally {
    offsetBusy.value = false
  }
}

async function runCmdMode() {
  if (!store.isConnected) { ElMessage.warning('Not connected'); return }
  cmdModeBusy.value = true
  try {
    store.pushLog(`→ #CMDMODE ${cmdMode.value}`, 'tx')
    const res = await api.cmdMode(cmdMode.value)
    store.pushLog(`← ${res.raw}`, res.ok ? 'ok' : 'err')
    if (res.ok) ElMessage.success(`Mode set: ${cmdMode.value}`)
  } catch (e) {
    store.pushLog(`← ERROR: ${(e as Error).message}`, 'err')
    ElMessage.error((e as Error).message)
  } finally {
    cmdModeBusy.value = false
  }
}
</script>

<template>
  <section class="card">
    <div class="header">
      <h2>Diagnostics</h2>
      <span class="summary" :style="{ color: summaryColor }">
        {{ lastResult?.summary ?? '— run a check —' }}
      </span>
    </div>

    <!-- Online check -->
    <div class="block">
      <div class="block-title">Node Online Check (<code>#ONLINE</code>)</div>
      <div class="nodes-grid">
        <button
          v-for="n in NODES" :key="n.id"
          class="node-cell" :class="statusOf(n.id)"
          :disabled="!store.isConnected || onlineBusy"
          :title="`Click to check ${n.label} only`"
          @click="runOnlineNode(n.id)"
        >
          <div class="node-label">{{ n.label }}</div>
          <div class="node-status">
            <template v-if="statusOf(n.id) === 'online'">OL</template>
            <template v-else-if="statusOf(n.id) === 'offline'">NL</template>
            <template v-else>?</template>
          </div>
        </button>
      </div>
      <div class="actions">
        <el-button
          type="primary" size="small"
          :loading="onlineBusy" :disabled="!store.isConnected"
          @click="runOnlineAll"
        >Check All</el-button>
      </div>
    </div>

    <!-- Reboot -->
    <div class="block">
      <div class="block-title">Reboot Node (<code>#REBOOT</code>)</div>
      <div class="reboot-row">
        <el-select v-model="rebootTarget" size="small" style="flex:1">
          <el-option
            v-for="opt in REBOOT_OPTIONS" :key="String(opt.value)"
            :value="opt.value as any" :label="opt.label"
          />
        </el-select>
        <el-button
          type="danger" size="small"
          :loading="rebootBusy" :disabled="!store.isConnected"
          @click="runReboot"
        >Reboot</el-button>
      </div>
    </div>

    <!-- ===== Maintenance / danger zone ===== -->
    <div class="block danger-zone">
      <div class="block-title">⚠ Maintenance</div>

      <!-- Calibration -->
      <div class="maint-row">
        <div class="maint-label">
          <div class="maint-name">Home Calibration</div>
          <div class="maint-hint">Runs <code>!CALIBRATION</code>. Drives all joints through a short motion (~30s).</div>
        </div>
        <el-button
          type="warning" size="small"
          :loading="calibBusy" :disabled="!store.isConnected"
          @click="runCalibrate"
        >Calibrate</el-button>
      </div>

      <!-- Command mode -->
      <div class="maint-row">
        <div class="maint-label">
          <div class="maint-name">Command Mode</div>
          <div class="maint-hint"><code>#CMDMODE</code> — SEQ/INT/TRJ/TUN. TUN disables normal motion and enables motor tuning loop.</div>
        </div>
        <el-select v-model="cmdMode" size="small" style="width: 240px">
          <el-option
            v-for="m in CMD_MODES" :key="m.value"
            :value="m.value" :label="m.label"
          />
        </el-select>
        <el-button
          size="small"
          :loading="cmdModeBusy" :disabled="!store.isConnected"
          @click="runCmdMode"
        >Apply</el-button>
      </div>

      <!-- Offset J (danger) -->
      <div class="maint-row danger-row">
        <div class="maint-label">
          <div class="maint-name">Home Offset → NVS (<code>#OFFSET_J</code>)</div>
          <div class="maint-hint">⚠ 写入 NVS，持久化。先手动把关节摆到精确零位再写入。"ALL" 会触发系统复位。</div>
        </div>
        <el-select v-model="offsetTarget" size="small" style="width: 200px">
          <el-option
            v-for="opt in OFFSET_TARGETS" :key="String(opt.value)"
            :value="opt.value as any" :label="opt.label"
          />
        </el-select>
        <el-button
          type="danger" size="small"
          :loading="offsetBusy" :disabled="!store.isConnected"
          @click="runOffsetJ"
        >Write NVS</el-button>
      </div>
    </div>
  </section>
</template>

<style scoped>
.card { padding: 0.7em 1.1em; display: flex; flex-direction: column; gap: 0.8em; }
.header {
  display: flex; justify-content: space-between; align-items: center;
  margin-bottom: 0.2em;
}
.header h2 { margin: 0; }
.summary {
  font-family: Consolas, monospace;
  font-size: 0.82rem; font-weight: 600;
}

.block {
  display: flex; flex-direction: column; gap: 0.5em;
  padding: 0.5em 0.7em; border-radius: 4px;
  border: 1px solid #1f2937; background: rgba(30, 41, 59, 0.3);
}
.block-title { font-size: 0.8rem; color: var(--fg-dim); }
.block-title code { color: #93c5fd; background: rgba(59, 130, 246, 0.12); padding: 0 4px; border-radius: 2px; }

.nodes-grid {
  display: grid; grid-template-columns: repeat(7, 1fr); gap: 4px;
}
.node-cell {
  border: 1px solid #334155; border-radius: 4px;
  background: rgba(15, 23, 42, 0.5); color: var(--fg-dim);
  padding: 6px 2px; cursor: pointer; font-family: Consolas, monospace;
  display: flex; flex-direction: column; align-items: center; gap: 2px;
  transition: background 0.12s, border-color 0.12s;
}
.node-cell:hover:not(:disabled) { background: rgba(59, 130, 246, 0.15); }
.node-cell:disabled { cursor: not-allowed; opacity: 0.5; }
.node-cell.online  { border-color: #22c55e; color: #4ade80; background: rgba(34, 197, 94, 0.10); }
.node-cell.offline { border-color: #ef4444; color: #f87171; background: rgba(239, 68, 68, 0.10); }
.node-label { font-size: 0.72rem; }
.node-status { font-size: 0.9rem; font-weight: 700; }

.actions { display: flex; justify-content: flex-end; }
.reboot-row { display: flex; gap: 0.5em; align-items: center; }

.danger-zone {
  border-color: rgba(239, 68, 68, 0.35);
  background: rgba(239, 68, 68, 0.05);
}
.maint-row {
  display: flex; align-items: center; gap: 0.6em;
  padding: 6px 0;
  border-top: 1px dashed rgba(100, 116, 139, 0.25);
}
.maint-row:first-of-type { border-top: none; }
.maint-label { flex: 1; min-width: 0; }
.maint-name { font-size: 0.88rem; color: var(--fg); font-weight: 500; }
.maint-hint {
  font-size: 0.75rem; color: var(--fg-dim);
  line-height: 1.4; margin-top: 2px;
}
.maint-hint code {
  color: #93c5fd; background: rgba(59, 130, 246, 0.12);
  padding: 0 4px; border-radius: 2px;
}
.maint-row.danger-row .maint-name { color: #fca5a5; }
</style>
