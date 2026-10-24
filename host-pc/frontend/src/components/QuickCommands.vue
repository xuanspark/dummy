<script setup lang="ts">
import { computed, ref } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { useRobotStore } from '@/stores/robot'

const store = useRobotStore()
const busy = ref<string | null>(null)   // which button is mid-flight

const canOperate = computed(() => store.isConnected || store.dryRun)

// state → badge colour + label
const badge = computed(() => {
  switch (store.robotState) {
    case 'normal':  return { label: 'NORMAL',  color: '#22c55e' }
    case 'estop':   return { label: 'E-STOP',  color: '#ef4444' }
    case 'paused':  return { label: 'PAUSED',  color: '#f59e0b' }
    case 'stopped': return { label: 'STOPPED', color: '#64748b' }
    default:        return { label: 'UNKNOWN', color: '#6b7280' }
  }
})

async function run(key: string, cmd: string, opts: { confirm?: string } = {}) {
  if (!canOperate.value) {
    ElMessage.warning('Not connected (or enable Dry-run in the header)')
    return
  }
  if (opts.confirm) {
    try {
      await ElMessageBox.confirm(opts.confirm, 'Confirm', {
        confirmButtonText: 'Yes', cancelButtonText: 'Cancel', type: 'warning',
      })
    } catch { return /* user cancelled */ }
  }
  busy.value = key
  try {
    await store.sendRaw(cmd)
  } catch {
    // already logged by store
  } finally {
    busy.value = null
  }
}

// E-STOP / PAUSE / RESUME live in the global header (reachable from every tab).
// QuickCommands focuses on lifecycle actions for the Home page.
const buttons: Array<{ key: string; label: string; cmd: string; type: string; confirm?: string }> = [
  { key: 'start', label: 'START', cmd: '!START', type: 'success' },
  { key: 'home',  label: 'HOME',  cmd: '!HOME',  type: 'primary' },
  { key: 'reset', label: 'RESET', cmd: '!RESET', type: 'info' },
  { key: 'close', label: 'CLOSE', cmd: '!CLOSE', type: 'info',
    confirm: 'Disable motors and release the arm. Continue?' },
]

function isDisabled(key: string): boolean {
  if (!canOperate.value) return true
  if (busy.value && busy.value !== key) return true
  // HOME / RESET shouldn't run while emergency-stopped (firmware will reject
  // anyway but disabling gives clearer feedback).
  const s = store.robotState
  if ((key === 'home' || key === 'reset') && s === 'estop') return true
  return false
}
</script>

<template>
  <section class="card quick">
    <div class="status">
      <span class="status-dot" :style="{ background: badge.color, boxShadow: `0 0 8px ${badge.color}` }"></span>
      <span class="status-label" :style="{ color: badge.color }">{{ badge.label }}</span>
      <span class="conn" v-if="store.connection.connected">
        {{ store.connection.port }} @ {{ store.connection.baud }}
      </span>
      <span class="conn dim" v-else-if="store.dryRun">dry-run</span>
      <span class="conn dim" v-else>no connection</span>
    </div>

    <div class="buttons">
      <el-button
        v-for="b in buttons" :key="b.key"
        :type="b.type"
        :loading="busy === b.key"
        :disabled="isDisabled(b.key)"
        class="btn-large"
        @click="run(b.key, b.cmd, { confirm: b.confirm })"
      >
        {{ b.label }}
      </el-button>
    </div>
  </section>
</template>

<style scoped>
.quick {
  display: flex;
  flex-direction: column;
  gap: 0.8em;
}
.status {
  display: flex;
  align-items: center;
  gap: 0.6em;
  font-weight: 600;
}
.status-dot {
  width: 10px;
  height: 10px;
  border-radius: 50%;
}
.status-label {
  font-size: 1rem;
  letter-spacing: 0.06em;
}
.conn {
  margin-left: auto;
  font-size: 0.85rem;
  color: var(--fg);
  font-weight: 400;
}
.conn.dim { color: var(--fg-dim); }
.buttons {
  display: flex;
  gap: 0.4em;
  flex-wrap: wrap;
}
.btn-large {
  flex: 1 1 auto;
  min-width: 90px;
  font-weight: 600;
  letter-spacing: 0.04em;
}
</style>
