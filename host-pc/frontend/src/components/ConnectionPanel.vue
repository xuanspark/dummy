<script setup lang="ts">
import { computed, onMounted, ref } from 'vue'
import { ElMessage } from 'element-plus'
import { useRobotStore } from '@/stores/robot'

const store = useRobotStore()
const selectedPort = ref<string>('')
const baud = ref<number>(115200)
const busy = ref(false)

onMounted(async () => {
  await Promise.all([
    store.loadDefaults().catch(() => {}),
    store.refreshConnection().catch(() => {}),
    refresh(),
  ])
  if (!selectedPort.value) {
    selectedPort.value = store.defaults.port || preferredPort()
    baud.value = store.defaults.baud || 115200
  }
})

function preferredPort(): string {
  const likely = store.ports.find((p) => p.is_likely_robot)
  return likely?.device || store.ports[0]?.device || ''
}

async function refresh() {
  busy.value = true
  try {
    await store.refreshPorts()
    if (!selectedPort.value) selectedPort.value = preferredPort()
  } catch (e) {
    ElMessage.error((e as Error).message)
  } finally {
    busy.value = false
  }
}

async function onConnect() {
  if (!selectedPort.value) {
    ElMessage.warning('Pick a port first')
    return
  }
  busy.value = true
  try {
    await store.connect(selectedPort.value, baud.value)
    ElMessage.success(`Connected to ${selectedPort.value}`)
  } catch (e) {
    ElMessage.error((e as Error).message)
  } finally {
    busy.value = false
  }
}

async function onDisconnect() {
  busy.value = true
  try {
    await store.disconnect()
    ElMessage.success('Disconnected')
  } catch (e) {
    ElMessage.error((e as Error).message)
  } finally {
    busy.value = false
  }
}

const statusLabel = computed(() =>
  store.isConnected ? `Connected: ${store.connection.port} @ ${store.connection.baud}` : 'Disconnected',
)
</script>

<template>
  <section class="card">
    <div class="title-row">
      <h2>Connection</h2>
      <span class="dot" :class="{ on: store.isConnected }"></span>
      <span class="status-text">{{ statusLabel }}</span>
    </div>

    <div class="inline-row">
      <el-select
        v-model="selectedPort"
        placeholder="Port"
        :disabled="store.isConnected || busy"
        style="flex: 1 1 auto; min-width: 110px;"
      >
        <el-option
          v-for="p in store.ports"
          :key="p.device"
          :label="p.device"
          :value="p.device"
        >
          <span>{{ p.device }}</span>
          <span class="port-tag" v-if="p.is_likely_robot">likely</span>
          <span class="port-desc">{{ p.description }}</span>
        </el-option>
      </el-select>

      <el-input-number
        v-model="baud"
        :min="1200"
        :step="9600"
        :disabled="store.isConnected || busy"
        controls-position="right"
        style="width: 110px; flex: 0 0 auto;"
      />

      <el-button :loading="busy" @click="refresh" :disabled="store.isConnected">
        Refresh
      </el-button>
      <el-button
        type="primary"
        :loading="busy"
        :disabled="store.isConnected || !selectedPort"
        @click="onConnect"
      >
        Connect
      </el-button>
      <el-button
        type="danger"
        :loading="busy"
        :disabled="!store.isConnected"
        @click="onDisconnect"
      >
        Disconnect
      </el-button>
    </div>
  </section>
</template>

<style scoped>
.title-row {
  display: flex;
  align-items: center;
  gap: 0.6em;
  margin-bottom: 0.5em;
}
.title-row h2 { margin: 0; }
.status-text {
  font-size: 0.9rem;
  color: var(--fg-dim);
  white-space: nowrap;
}
.inline-row {
  display: flex;
  flex-wrap: nowrap;          /* port + baud + 3 buttons on one row */
  gap: 0.5em;
  align-items: center;
  margin: 0;
}
.inline-row :deep(.el-button) {
  flex: 0 0 auto;
  margin: 0;
  min-width: 78px;
}
.row {
  display: flex;
  gap: 0.5em;
  margin: 0.6em 0;
  align-items: center;
  flex-wrap: wrap;
}
.dot {
  width: 10px;
  height: 10px;
  border-radius: 50%;
  background: var(--err);
  box-shadow: 0 0 8px var(--err);
}
.dot.on {
  background: var(--ok);
  box-shadow: 0 0 8px var(--ok);
}
.port-tag {
  margin-left: 0.6em;
  padding: 0 6px;
  border-radius: 4px;
  font-size: 11px;
  background: var(--accent);
  color: white;
}
.port-desc {
  margin-left: 0.6em;
  color: var(--fg-dim);
  font-size: 12px;
}
</style>
