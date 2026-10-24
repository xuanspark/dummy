<script setup lang="ts">
import { computed, onBeforeUnmount, ref, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { api } from '@/api/client'
import { useRobotStore } from '@/stores/robot'

const store = useRobotStore()

// ---- axis config ----
// Names + soft-limit ranges. The real robot's reachable workspace is joint-
// limited and much smaller; these ranges are generous so the user can scrub
// anywhere the IK might be solvable and rely on firmware to reject invalid
// poses (Move Error will flow back through the log console).
const AXES = [
  { key: 'x', label: 'X', min: -400, max:  400, unit: 'mm'  },
  { key: 'y', label: 'Y', min: -400, max:  400, unit: 'mm'  },
  { key: 'z', label: 'Z', min:    0, max:  600, unit: 'mm'  },
  { key: 'a', label: 'A', min: -180, max:  180, unit: '°'   },
  { key: 'b', label: 'B', min: -180, max:  180, unit: '°'   },
  { key: 'c', label: 'C', min: -180, max:  180, unit: '°'   },
] as const

// Sensible starting pose (home-ish, above workspace origin). User will
// typically press "Sync" right after connecting so targets = live values.
const DEFAULT_POSE = [200, 0, 200, 0, 0, 0]

// ---- state ----
const targets = ref<number[]>([...DEFAULT_POSE])
const speed = ref<number>(10)              // only used when overrideSpeed is true
const overrideSpeed = ref<boolean>(true)   // false → 6-arg wire (@x,y,z,a,b,c)
const autoQuery = ref<boolean>(true)
const sending = ref<boolean>(false)

// Throttle identical to JointControl so fast drags don't flood the firmware queue.
const THROTTLE_MS = 120
let lastSent = 0
let pending: ReturnType<typeof setTimeout> | null = null

const POLL_MS = 300
let pollTimer: ReturnType<typeof setInterval> | null = null

watch(autoQuery, (on) => { on ? startPolling() : stopPolling() }, { immediate: true })
watch(() => store.isConnected, (c) => {
  if (c) { autoQuery.value = true; startPolling() }
  else   { stopPolling() }
})
onBeforeUnmount(() => {
  stopPolling()
  if (pending) clearTimeout(pending)
})

function startPolling() {
  stopPolling()
  if (!store.isConnected) return
  pollTimer = setInterval(async () => {
    if (!store.isConnected || sending.value || store.dryRun) return
    try {
      const lp = await api.getLpos()
      store.$patch({ lpos: lp })
    } catch { /* ignore transient */ }
  }, POLL_MS)
}
function stopPolling() {
  if (pollTimer) { clearInterval(pollTimer); pollTimer = null }
}

// ---- live readout ----
const live = computed(() => {
  const p = store.lpos
  if (!p) return null
  return [p.x, p.y, p.z, p.a, p.b, p.c]
})
function fmtLive(v: number | undefined, unit: string): string {
  return v == null ? '—' : v.toFixed(1) + unit
}

// ---- actions ----
function fmtVal(v: number): string {
  return Number.isInteger(v) ? v.toString() : parseFloat(v.toFixed(3)).toString()
}

async function sendMove() {
  if (sending.value) return
  const pose = [...targets.value]
  const body = '@' + pose.map(fmtVal).join(',')
  const cmd = overrideSpeed.value ? `${body},${speed.value}` : body
  if (store.dryRun) {
    store.pushLog(`→ ${cmd}    [dry-run]`, 'tx')
    return
  }
  sending.value = true
  store.pushLog(`→ ${cmd}`, 'tx')
  try {
    await api.moveCart(pose, overrideSpeed.value ? speed.value : undefined)
  } catch (e) {
    store.pushLog(`MoveL failed: ${(e as Error).message}`, 'err')
    ElMessage.error((e as Error).message)
  } finally {
    sending.value = false
  }
}

function scheduleSend() {
  if (!store.isConnected && !store.dryRun) return
  const now = Date.now()
  const elapsed = now - lastSent
  if (elapsed >= THROTTLE_MS) {
    lastSent = now
    void sendMove()
  } else if (!pending) {
    pending = setTimeout(() => {
      pending = null
      lastSent = Date.now()
      void sendMove()
    }, THROTTLE_MS - elapsed)
  }
}

function onSliderInput(idx: number, v: number) {
  const t = Math.round(v)
  if (targets.value[idx] === t) return
  const next = [...targets.value]
  next[idx] = t
  targets.value = next
  if (!store.dryRun) scheduleSend()
}

async function onSync() {
  // Copy current live pose into targets
  if (!store.lpos) {
    ElMessage.info('No live pose yet — wait for auto-poll')
    return
  }
  const p = store.lpos
  targets.value = [p.x, p.y, p.z, p.a, p.b, p.c].map((v) => Math.round(v))
  ElMessage.success('Targets synced to live pose')
}

async function onGoTarget() {
  if (!store.isConnected && !store.dryRun) {
    ElMessage.warning('Not connected (or enable Dry-run)')
    return
  }
  await sendMove()
}
</script>

<template>
  <section class="card">
    <div class="header">
      <h2>Cartesian Control (MoveL)</h2>
      <div class="header-right">
        <el-checkbox v-model="overrideSpeed"
          title="Off → 6-arg wire form (@x,y,z,a,b,c) — firmware uses current speed">
          Override
        </el-checkbox>
        <span class="label">Speed</span>
        <el-input-number
          v-model="speed" :min="1" :max="200" :step="1"
          :disabled="!overrideSpeed"
          size="small" controls-position="right" style="width: 100px"
        />
        <el-checkbox v-model="autoQuery" :disabled="!store.isConnected || store.dryRun">Auto poll</el-checkbox>
        <el-button size="small" @click="onSync" :disabled="!store.lpos">Sync</el-button>
        <el-button size="small" type="primary" @click="onGoTarget"
                   :disabled="!store.isConnected && !store.dryRun">Go</el-button>
      </div>
    </div>

    <div v-for="(ax, i) in AXES" :key="ax.key" class="axis-row">
      <span class="axis-name">{{ ax.label }}</span>
      <el-slider
        :model-value="targets[i]"
        @input="(v: number | number[]) => onSliderInput(i, Array.isArray(v) ? v[0] : v)"
        :min="ax.min" :max="ax.max" :step="1"
        :disabled="!store.isConnected && !store.dryRun"
        show-input :show-input-controls="false" input-size="small"
        style="flex:1; margin: 0 12px"
      />
      <span class="axis-live" :class="{ stale: !live }">
        live: <b>{{ fmtLive(live?.[i], ax.unit) }}</b>
      </span>
    </div>
  </section>
</template>

<style scoped>
.card { padding: 0.45em 1.1em; display: flex; flex-direction: column; }
.header {
  display: flex; justify-content: space-between; align-items: center;
  margin-bottom: 0.6em; flex-wrap: wrap; gap: 0.5em;
}
.header h2 { margin: 0; }
.header-right { display: flex; align-items: center; gap: 0.6em; flex-wrap: wrap; }
.label { font-size: 0.85rem; color: var(--fg-dim); }

.axis-row {
  display: flex; align-items: center;
  margin: 4px 0; min-height: 34px; flex: 1 1 0;
}
.axis-row :deep(.el-slider__runway) { height: 8px; }
.axis-row :deep(.el-slider__button) { width: 16px; height: 16px; }
.axis-row :deep(.el-input__wrapper) { padding: 4px 8px; }
.axis-row :deep(.el-input) { font-size: 13px; }

.axis-name {
  width: 32px; font-weight: 600; color: var(--fg); font-size: 0.95rem;
}
.axis-live {
  width: 120px; text-align: right;
  font-family: Consolas, monospace; font-size: 13px; color: var(--fg-dim);
}
.axis-live.stale b { color: #475569; }
.axis-live b { color: #93c5fd; font-weight: 600; }
</style>
