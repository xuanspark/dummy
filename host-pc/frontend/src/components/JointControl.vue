<script setup lang="ts">
import { computed, onBeforeUnmount, ref, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { api } from '@/api/client'
import { useRobotStore } from '@/stores/robot'

const store = useRobotStore()

// ---- joint config ----
// HOME (working) pose used as the slider's logical zero anchor.
// Source: firmware DummyRobot::Homing() => MoveJ(0,0,90,0,0,0).
const HOME = [0, 0, 90, 0, 0, 0]

// Hardware absolute limits (from firmware ascii_protocol joint range checks).
const LIMITS: Array<[number, number]> = [
  [-170, 170],
  [-75, 90],
  [35, 180],
  [-180, 180],
  [-100, 100],
  [-180, 180],
]

const NAMES = ['J1', 'J2', 'J3', 'J4', 'J5', 'J6']

// ---- state ----
const targets = ref<number[]>([...HOME])     // absolute target angles (deg)
const speed = ref<number>(10)                // deg/s (only used when overrideSpeed)
const overrideSpeed = ref<boolean>(true)     // false → omit speed (firmware uses current)
const autoQuery = ref<boolean>(true)
const sending = ref<boolean>(false)

// Throttle: while dragging a slider, fire a move command at most every N ms.
const THROTTLE_MS = 100
let lastSent = 0
let pending: ReturnType<typeof setTimeout> | null = null
let resendRequested = false
let disposed = false

watch(() => store.robotState, (state) => {
  if (state !== 'paused' && state !== 'estop') return
  resendRequested = false
  if (pending) clearTimeout(pending)
  pending = null
})

// Auto-query: poll joint position from the backend.
const POLL_MS = 250
let pollTimer: ReturnType<typeof setInterval> | null = null
let polling = false  // 单飞保护：上一轮轮询未结束不发下一轮

watch(autoQuery, (on) => {
  if (on) startPolling()
  else stopPolling()
}, { immediate: true })

watch(() => store.isConnected, (c) => {
  if (c) {
    autoQuery.value = true   // auto-enable Auto poll on every successful connect
    startPolling()
  } else {
    stopPolling()
  }
})

onBeforeUnmount(() => {
  disposed = true
  stopPolling()
  if (pending) clearTimeout(pending)
})

function startPolling() {
  stopPolling()
  if (!store.isConnected) return
  pollTimer = setInterval(async () => {
    if (!store.isConnected || sending.value) return
    if (store.dryRun) return                       // skip polling in dry-run
    if (polling) return                            // 上一轮未结束，跳过本轮
    polling = true
    try {
      // Poll joint + cartesian in parallel; each uses its own serialized
      // transaction on the backend so they don't interfere.
      const [jp, lp] = await Promise.allSettled([api.getJpos(), api.getLpos()])
      if (jp.status === 'fulfilled') store.$patch({ jpos: jp.value })
      if (lp.status === 'fulfilled') store.$patch({ lpos: lp.value })
    } finally {
      polling = false
    }
  }, POLL_MS)
}
function stopPolling() {
  if (pollTimer) {
    clearInterval(pollTimer)
    pollTimer = null
  }
}

// ---- slider handlers ----
// Track last emitted value per joint so we can interpolate gaps when the
// el-slider fires a single input that jumped multiple steps (fast drag).
// Use a plain (non-reactive) array to avoid triggering renders inside the
// hot interpolation loop.
const lastEmitted: number[] = [...HOME]

function fmtVal(v: number): string {
  return Number.isInteger(v) ? v.toString() : parseFloat(v.toFixed(3)).toString()
}

function onSliderInput(idx: number, v: number) {
  const target = Math.round(v)
  const prev = lastEmitted[idx]
  if (target === prev) return

  const dir = target > prev ? 1 : -1
  // Snapshot current targets — we mutate this local copy in the loop and
  // only commit it back to the reactive ref ONCE at the end, so the slider
  // and number input each render exactly once per drag tick (not 170 times).
  const next = [...targets.value]

  if (store.dryRun) {
    // Synchronous batch: push all log entries in this tick. Vue's reactivity
    // batches them into a single LogConsole re-render at the next microtask.
    const speedSuffix = overrideSpeed.value ? `,${speed.value}` : ''
    for (let cur = prev + dir; (dir > 0 ? cur <= target : cur >= target); cur += dir) {
      next[idx] = cur
      const cmd = '&' + next.map(fmtVal).join(',') + speedSuffix
      store.pushLog(`→ ${cmd}    [dry-run]`, 'tx')
    }
  } else {
    // Real mode: only the final target matters; throttle will collapse anyway.
    next[idx] = target
  }

  // Single reactive write at the end → one slider render, one input render.
  targets.value = next
  lastEmitted[idx] = target

  if (!store.dryRun) scheduleSend()
}

function scheduleSend() {
  if (disposed || store.robotState === 'paused' || store.robotState === 'estop') return
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

async function sendMove() {
  if (disposed || store.robotState === 'paused' || store.robotState === 'estop') return
  if (sending.value) {
    resendRequested = true
    return
  }
  sending.value = true
  resendRequested = false
  let succeeded = false
  try {
    await store.sendMoveJoint([...targets.value], overrideSpeed.value ? speed.value : undefined)
    succeeded = true
  } catch {
    // already logged by store
  } finally {
    sending.value = false
    // 当前运动结束后补发最后目标；停止或离开页面时清除尚未受理的本地目标。
    if (succeeded && resendRequested && store.isConnected) scheduleSend()
  }
}

async function onReset() {
  targets.value = [...HOME]
  for (let i = 0; i < 6; i++) lastEmitted[i] = HOME[i]
  try {
    await store.sendMoveJoint([...HOME], overrideSpeed.value ? speed.value : undefined)
    ElMessage.success(store.dryRun ? '模拟归位完成' : '已到达工作姿态')
  } catch (e) {
    ElMessage.error((e as Error).message)
  }
}

// ---- live readout ----
const live = computed(() => {
  const j = store.jpos
  if (!j) return null
  return [j.j1, j.j2, j.j3, j.j4, j.j5, j.j6]
})
const lposDisplay = computed(() => {
  const p = store.lpos
  if (!p) return null
  return [
    { k: 'X', v: p.x, u: 'mm' },
    { k: 'Y', v: p.y, u: 'mm' },
    { k: 'Z', v: p.z, u: 'mm' },
    { k: 'A', v: p.a, u: '°'  },
    { k: 'B', v: p.b, u: '°'  },
    { k: 'C', v: p.c, u: '°'  },
  ]
})
function fmt(v: number | undefined): string {
  return v == null ? '—' : v.toFixed(1) + '°'
}
function fmtLp(v: number, u: string): string {
  return v.toFixed(1) + u
}
</script>

<template>
  <section class="card">
    <div class="header">
      <h2>关节控制</h2>
      <div class="header-right">
        <el-checkbox v-model="overrideSpeed"
          title="关闭后使用设备当前速度">
          指定速度
        </el-checkbox>
        <span class="label">速度</span>
        <el-input-number
          v-model="speed"
          :min="1"
          :max="100"
          :step="1"
          :disabled="!overrideSpeed"
          size="small"
          controls-position="right"
          style="width: 100px"
        />
        <el-checkbox v-model="autoQuery" :disabled="!store.isConnected || store.dryRun">自动查询</el-checkbox>
        <el-button size="small" @click="onReset" :disabled="!store.isConnected && !store.dryRun">工作归位</el-button>
      </div>
    </div>

    <!-- Live cartesian pose strip (firmware #GETLPOS) -->
    <div class="lpos-strip" :class="{ stale: !lposDisplay }">
      <span class="lpos-title">末端位置：</span>
      <template v-if="lposDisplay">
        <span v-for="it in lposDisplay" :key="it.k" class="lpos-item">
          <span class="lpos-k">{{ it.k }}</span>
          <b>{{ fmtLp(it.v, it.u) }}</b>
        </span>
      </template>
      <span v-else class="lpos-empty">等待数据…</span>
    </div>

    <div v-for="(name, i) in NAMES" :key="name" class="joint-row">
      <span class="joint-name">{{ name }}</span>
      <el-slider
        :model-value="targets[i]"
        @input="(v: number | number[]) => onSliderInput(i, Array.isArray(v) ? v[0] : v)"
        :min="LIMITS[i][0]"
        :max="LIMITS[i][1]"
        :step="1"
        :disabled="!store.isConnected && !store.dryRun"
        show-input
        :show-input-controls="false"
        input-size="small"
        style="flex:1; margin: 0 12px"
      />
      <span class="joint-live" :class="{ stale: !live }">
        实际：<b>{{ fmt(live?.[i]) }}</b>
      </span>
    </div>
  </section>
</template>

<style scoped>
/* Compact the whole card so Console gets more vertical room. */
:deep(.card), section.card {
  padding: 0.45em 1.1em;   /* trimmed ~15px off block-axis vs 0.9em */
  display: flex;
  flex-direction: column;
}

.header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 0.6em;
  flex-wrap: wrap;
  gap: 0.5em;
}
.header h2 { margin: 0; }
.header-right {
  display: flex;
  align-items: center;
  gap: 0.6em;
  flex-wrap: wrap;
}
.label { font-size: 0.85rem; color: var(--fg-dim); }
.joint-row {
  display: flex;
  align-items: center;
  margin: 4px 0;
  min-height: 34px;
  flex: 1 1 0;       /* equally fill remaining card height */
}
/* let the slider naturally fill the row vertically, with a thicker rail */
.joint-row :deep(.el-slider__runway) { height: 8px; }
.joint-row :deep(.el-slider__button) { width: 16px; height: 16px; }
.joint-row :deep(.el-input__wrapper) { padding: 4px 8px; }
.joint-row :deep(.el-input) { font-size: 13px; }
.joint-name {
  width: 32px;
  font-weight: 600;
  color: var(--fg);
  font-size: 0.95rem;
}
.joint-live {
  width: 110px;
  text-align: right;
  font-family: Consolas, monospace;
  font-size: 13px;
  color: var(--fg-dim);
}
.joint-live.stale b { color: #475569; }
.joint-live b { color: #93c5fd; font-weight: 600; }

.lpos-strip {
  display: flex; flex-wrap: wrap; gap: 0.8em;
  padding: 4px 8px; margin-bottom: 4px;
  border-radius: 3px;
  background: rgba(30, 41, 59, 0.35);
  font-family: Consolas, monospace; font-size: 12px;
}
.lpos-title { color: var(--fg-dim); font-weight: 600; }
.lpos-item { color: var(--fg-dim); }
.lpos-k { color: #64748b; margin-right: 3px; }
.lpos-item b { color: #93c5fd; font-weight: 600; }
.lpos-strip.stale .lpos-item b { color: #475569; }
.lpos-empty { color: #64748b; font-style: italic; }
</style>
