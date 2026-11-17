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
  { id: 7, label: '手爪' },
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

function summaryLabel(summary: string | null | undefined): string {
  if (!summary) return '尚未检测'
  if (summary === 'all_online') return '全部在线'
  if (summary === 'has_offline') return '存在离线节点'
  const match = summary.match(/^node=(\d+)\s+(OL|NL)$/)
  if (match) return `节点 ${match[1]}：${match[2] === 'OL' ? '在线' : '离线'}`
  return summary
}

async function runOnlineAll() {
  if (!store.isConnected) {
    ElMessage.warning('设备未连接')
    return
  }
  onlineBusy.value = true
  try {
    store.pushLog('→ #ONLINE', 'tx')
    const res = await api.checkOnline()
    lastResult.value = res
    store.pushLog(`← ${res.raw.join(' | ')}`, 'ok')
  } catch (e) {
    store.pushLog(`← 错误：${(e as Error).message}`, 'err')
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
    store.pushLog(`← 错误：${(e as Error).message}`, 'err')
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
    ElMessage.warning('设备未连接')
    return
  }
  const opt = REBOOT_OPTIONS.find((o) => o.value === rebootTarget.value)
  const label = opt?.label ?? `节点 ${rebootTarget.value}`
  try {
    await ElMessageBox.confirm(
      `确认重启 ${label}？` + (opt?.danger ? '\n\n会触发主控系统复位，当前串口会断开。' : ''),
      '确认重启',
      { confirmButtonText: '重启', cancelButtonText: '取消', type: 'warning' },
    )
  } catch { return }

  rebootBusy.value = true
  try {
    const cmdStr = rebootTarget.value == null ? '#REBOOT' : `#REBOOT ${rebootTarget.value}`
    store.pushLog(`→ ${cmdStr}`, 'tx')
    const res = await api.reboot(rebootTarget.value)
    store.pushLog(`← ${res.raw}`, res.ok ? 'ok' : 'err')
    if (res.ok) ElMessage.success('已发送重启指令')
  } catch (e) {
    store.pushLog(`← 错误：${(e as Error).message}`, 'err')
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
  { value: 1, label: '1：顺序模式' },
  { value: 2, label: '2：默认队列模式' },
  { value: 3, label: '3：轨迹参数模式' },
  { value: 4, label: '4：电机调试模式' },
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
  { value: 7,     label: '手爪' },
  { value: 'all', label: '全部节点（写入后重启）', danger: true },
]
const offsetTarget = ref<number | 'all'>(1)

async function runCalibrate() {
  if (!store.isConnected) { ElMessage.warning('设备未连接'); return }
  try {
    await ElMessageBox.confirm(
      '请先将全部关节人工对齐到 HOME 工作姿态。标定会记录该姿态，再运动到零点姿态并写入偏移；全过程保持使能，支持急停和恢复。确认开始？',
      '确认标定',
      { confirmButtonText: '开始', cancelButtonText: '取消', type: 'warning' },
    )
  } catch { return }
  calibBusy.value = true
  try {
    store.pushLog('→ !CALIBRATION', 'tx')
    await api.calibrate()
    store.pushLog('← 标定完成', 'ok')
    ElMessage.success('标定完成')
  } catch (e) {
    store.pushLog(`← 错误：${(e as Error).message}`, 'err')
    ElMessage.error((e as Error).message)
  } finally {
    calibBusy.value = false
  }
}

async function runOffsetJ() {
  if (!store.isConnected) { ElMessage.warning('设备未连接'); return }
  const opt = OFFSET_TARGETS.find((o) => o.value === offsetTarget.value)
  const label = opt?.label ?? String(offsetTarget.value)
  try {
    await ElMessageBox.confirm(
      `⚠ 危险操作：#OFFSET_J ${offsetTarget.value}\n\n` +
      '将当前关节位置写入 NVS 作为新的零点。写入是持久化的，重启后生效。\n' +
      (opt?.danger
        ? '\n全部节点保存校验通过后自动重启，串口会断开；写入失败或急停时不会继续重启。'
        : '\n单节点写入不会触发系统复位。') +
      `\n\n确定对 ${label} 执行？`,
      '确认写入偏移',
      { confirmButtonText: '写入', cancelButtonText: '取消', type: 'error' },
    )
  } catch { return }
  offsetBusy.value = true
  try {
    const cmdStr = `#OFFSET_J ${offsetTarget.value}`
    store.pushLog(`→ ${cmdStr}`, 'tx')
    const res = await api.offsetJ(offsetTarget.value)
    store.pushLog(`← ${res.raw}`, res.ok ? 'ok' : 'err')
    if (res.ok) ElMessage.success('偏移写入完成')
  } catch (e) {
    store.pushLog(`← 错误：${(e as Error).message}`, 'err')
    ElMessage.error((e as Error).message)
  } finally {
    offsetBusy.value = false
  }
}

async function runCmdMode() {
  if (!store.isConnected) { ElMessage.warning('设备未连接'); return }
  cmdModeBusy.value = true
  try {
    store.pushLog(`→ #CMDMODE ${cmdMode.value}`, 'tx')
    const res = await api.cmdMode(cmdMode.value)
    store.pushLog(`← ${res.raw}`, res.ok ? 'ok' : 'err')
    if (res.ok) ElMessage.success(`已设置模式 ${cmdMode.value}`)
  } catch (e) {
    store.pushLog(`← 错误：${(e as Error).message}`, 'err')
    ElMessage.error((e as Error).message)
  } finally {
    cmdModeBusy.value = false
  }
}
</script>

<template>
  <section class="card">
    <div class="header">
      <h2>设备诊断</h2>
      <span class="summary" :style="{ color: summaryColor }">
        {{ summaryLabel(lastResult?.summary) }}
      </span>
    </div>

    <!-- Online check -->
    <div class="block">
      <div class="block-title">节点在线检测（<code>#ONLINE</code>）</div>
      <div class="nodes-grid">
        <button
          v-for="n in NODES" :key="n.id"
          class="node-cell" :class="statusOf(n.id)"
          :disabled="!store.isConnected || onlineBusy"
          :title="`检测 ${n.label}`"
          @click="runOnlineNode(n.id)"
        >
          <div class="node-label">{{ n.label }}</div>
          <div class="node-status">
            <template v-if="statusOf(n.id) === 'online'">在线</template>
            <template v-else-if="statusOf(n.id) === 'offline'">离线</template>
            <template v-else>?</template>
          </div>
        </button>
      </div>
      <div class="actions">
        <el-button
          type="primary" size="small"
          :loading="onlineBusy" :disabled="!store.isConnected"
          @click="runOnlineAll"
        >检测全部节点</el-button>
      </div>
    </div>

    <!-- Reboot -->
    <div class="block">
      <div class="block-title">重启节点（<code>#REBOOT</code>）</div>
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
        >重启</el-button>
      </div>
    </div>

    <!-- ===== Maintenance / danger zone ===== -->
    <div class="block danger-zone">
      <div class="block-title">参数维护</div>

      <!-- Calibration -->
      <div class="maint-row">
        <div class="maint-label">
          <div class="maint-name">零点标定</div>
          <div class="maint-hint">先人工对齐 HOME 工作姿态，再执行 <code>!CALIBRATION</code>；标定中会运动到零点姿态。</div>
        </div>
        <el-button
          type="warning" size="small"
          :loading="calibBusy" :disabled="!store.isConnected"
          @click="runCalibrate"
        >开始标定</el-button>
      </div>

      <!-- Command mode -->
      <div class="maint-row">
        <div class="maint-label">
          <div class="maint-name">命令模式</div>
          <div class="maint-hint"><code>#CMDMODE</code>：各模式均使用统一队列，支持急停、暂停和恢复。</div>
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
        >应用</el-button>
      </div>

      <!-- Offset J (danger) -->
      <div class="maint-row danger-row">
        <div class="maint-label">
          <div class="maint-name">写入零点偏移（<code>#OFFSET_J</code>）</div>
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
        >写入偏移</el-button>
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
