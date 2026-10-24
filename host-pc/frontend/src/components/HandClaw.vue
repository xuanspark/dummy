<script setup lang="ts">
import { ref } from 'vue'
import { ElMessage } from 'element-plus'
import { api } from '@/api/client'
import { useRobotStore } from '@/stores/robot'

const store = useRobotStore()

// Local UI state
const handOpen = ref<boolean>(false)
const posPct = ref<number>(0)
const current = ref<string>('1.0')
const zeroResult = ref<{ closed: number; opened: number } | null>(null)

function gateConnected(): boolean {
  if (!store.isConnected && !store.dryRun) {
    ElMessage.warning('Not connected')
    return false
  }
  return true
}

// Helper: log TX/RX symmetrically with the existing console formatting
async function withLog<T>(label: string, fn: () => Promise<T>): Promise<T | null> {
  if (!gateConnected()) return null
  if (store.dryRun) {
    store.pushLog(`→ ${label}    [dry-run]`, 'tx')
    return null
  }
  store.pushLog(`→ ${label}`, 'tx')
  try {
    const r = await fn()
    store.pushLog(`← ${label} OK`, 'ok')
    return r
  } catch (e) {
    store.pushLog(`← ${label} ERROR: ${(e as Error).message}`, 'err')
    ElMessage.error((e as Error).message)
    return null
  }
}

async function onOpen()  { await withLog('!HD_O',  () => api.handOpen());  handOpen.value = true  }
async function onClose() { await withLog('!HD_C',  () => api.handClose()); handOpen.value = false }
async function onEn()    { await withLog('!HD_EN', () => api.handEnable(true))  }
async function onDis()   { await withLog('!HD_DIS',() => api.handEnable(false)) }

async function onZero() {
  if (!gateConnected()) return
  if (store.dryRun) {
    store.pushLog('→ !HD_ZERO    [dry-run]', 'tx')
    return
  }
  store.pushLog('→ !HD_ZERO', 'tx')
  try {
    const res = await api.handZero()
    zeroResult.value = { closed: res.closed, opened: res.opened }
    store.pushLog(
      `← HD_ZERO OK MIN:${res.closed.toFixed(2)} MAX:${res.opened.toFixed(2)}`,
      'ok',
    )
    ElMessage.success(`Calibrated: closed=${res.closed.toFixed(2)}°, opened=${res.opened.toFixed(2)}°`)
  } catch (e) {
    store.pushLog(`← HD_ZERO ERROR: ${(e as Error).message}`, 'err')
    ElMessage.error((e as Error).message)
  }
}

// Pos% slider — commit on release to avoid flooding
async function onPosCommit() {
  const pct = Math.round(posPct.value)
  await withLog(`!HD_POS ${pct}`, () => api.handPos(pct))
}

async function onSetCurrent() {
  const v = parseFloat(current.value)
  if (!isFinite(v) || v <= 0 || v > 2) {
    ElMessage.warning('Current must be in (0, 2]')
    return
  }
  await withLog(`!HD_I ${v}`, () => api.handCurrent(v))
}
</script>

<template>
  <section class="card hand">
    <div class="header">
      <h2>Hand Claw</h2>
    </div>

    <!-- Row 1: Close / Open only -->
    <div class="row">
      <el-button
        :type="handOpen ? 'default' : 'success'"
        :plain="handOpen"
        size="small"
        class="grow"
        :disabled="!store.isConnected && !store.dryRun"
        @click="onClose"
      >Close</el-button>
      <el-button
        :type="handOpen ? 'success' : 'default'"
        :plain="!handOpen"
        size="small"
        class="grow"
        :disabled="!store.isConnected && !store.dryRun"
        @click="onOpen"
      >Open</el-button>
    </div>

    <!-- Row 2: EN / DIS / Zero + Current input + Set -->
    <div class="row row-even">
      <el-button size="small" type="primary" plain class="grow"
        :disabled="!store.isConnected && !store.dryRun" @click="onEn">EN</el-button>
      <el-button size="small" type="info" plain class="grow"
        :disabled="!store.isConnected && !store.dryRun" @click="onDis">DIS</el-button>
      <el-button size="small" class="grow"
        :disabled="!store.isConnected && !store.dryRun" @click="onZero">Zero</el-button>
      <el-input v-model="current" size="small" class="grow-input">
        <template #suffix>A</template>
      </el-input>
      <el-button size="small" type="primary" class="grow"
        :disabled="!store.isConnected && !store.dryRun" @click="onSetCurrent">Set</el-button>
    </div>

    <!-- Row 3: Pos% slider -->
    <div class="row">
      <label class="label">Pos %</label>
      <el-slider
        v-model="posPct"
        :min="0"
        :max="100"
        :step="1"
        :disabled="!store.isConnected && !store.dryRun"
        style="flex: 1; margin: 0 10px"
        @change="onPosCommit"
      />
      <span class="val">{{ Math.round(posPct) }}%</span>
    </div>
  </section>
</template>

<style scoped>
.hand {
  display: flex;
  flex-direction: column;
  gap: 0.6em;
  height: 100%;
  justify-content: space-evenly;    /* distribute header + 3 rows evenly across the card */
}
.hand .header { flex: 0 0 auto; }
.hand .row { flex: 0 0 auto; }
.header { display: flex; align-items: center; }
.header h2 { margin: 0; font-size: 1rem; }
.row {
  display: flex;
  gap: 0.4em;
  align-items: center;
  flex-wrap: wrap;
}
.grow { flex: 1 1 0; min-width: 0; }
.grow-input { flex: 1 1 0; min-width: 0; }
.row-even { flex-wrap: nowrap; }
.row :deep(.grow.el-button + .grow.el-button) { margin-left: 0; }
.label { color: var(--fg-dim); font-size: 0.85rem; min-width: 3.8em; }
.val {
  font-family: Consolas, monospace;
  font-size: 12px;
  min-width: 3em;
  text-align: right;
  color: #93c5fd;
}
</style>
