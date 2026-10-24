<script setup lang="ts">
import { computed, ref } from 'vue'
import { ElButton, ElSwitch, ElMessage } from 'element-plus'
import CartControl from '@/components/CartControl.vue'
import ConnectionPanel from '@/components/ConnectionPanel.vue'
import DceTuning from '@/components/DceTuning.vue'
import DiagPanel from '@/components/DiagPanel.vue'
import HandClaw from '@/components/HandClaw.vue'
import JointControl from '@/components/JointControl.vue'
import LogConsole from '@/components/LogConsole.vue'
import QuickCommands from '@/components/QuickCommands.vue'
import RgbPanel from '@/components/RgbPanel.vue'
import { useRobotStore } from '@/stores/robot'

const store = useRobotStore()
const dryRun = computed({
  get: () => store.dryRun,
  set: (v: boolean) => store.setDryRun(v),
})

// Always-available safety / recovery buttons in the header.
const canOperate = computed(() => store.isConnected || store.dryRun)
async function safetyCmd(cmd: string) {
  if (!canOperate.value) {
    ElMessage.warning('Not connected (or enable Dry-run)')
    return
  }
  try { await store.sendRaw(cmd) } catch { /* logged */ }
}

// Tab routing — persist last-active tab in localStorage
type Tab = 'home' | 'cart' | 'diag' | 'tuning'
const TAB_KEY = 'dummy.tab'
const activeTab = ref<Tab>(((localStorage.getItem(TAB_KEY) as Tab) || 'home'))
function setTab(t: Tab) {
  activeTab.value = t
  localStorage.setItem(TAB_KEY, t)
}

const tabs: Array<{ key: Tab; label: string }> = [
  { key: 'home',   label: 'Home' },
  { key: 'cart',   label: 'Cartesian' },
  { key: 'diag',   label: 'Diagnostics' },
  { key: 'tuning', label: 'DCE Tuning' },
]
</script>

<template>
  <div class="app-layout">
    <header class="app-header">
      <h1>Dummy Robot Controller</h1>
      <span class="tagline">Python backend · Vue frontend · serial direct</span>
      <span class="spacer" />

      <!-- Global safety controls — reachable from every tab -->
      <div class="safety-group">
        <ElButton
          type="danger" size="small"
          :disabled="!canOperate" @click="safetyCmd('!STOP')"
        >E-STOP</ElButton>
        <ElButton
          type="warning" size="small"
          :disabled="!canOperate" @click="safetyCmd('!PAUSE')"
        >PAUSE</ElButton>
        <ElButton
          type="success" size="small"
          :disabled="!canOperate" @click="safetyCmd('!RESUME')"
        >RESUME</ElButton>
      </div>

      <span class="dry-run-label" :class="{ on: dryRun }">Dry-run</span>
      <ElSwitch v-model="dryRun" inline-prompt active-text="ON" inactive-text="OFF" />
    </header>

    <nav class="tab-bar">
      <button
        v-for="t in tabs" :key="t.key"
        class="tab-btn" :class="{ active: activeTab === t.key }"
        @click="setTab(t.key)"
      >{{ t.label }}</button>
    </nav>

    <main class="app-main">
      <!-- Home tab: top row (Connection + QuickCommands) | mid row (Joint + Hand) -->
      <template v-if="activeTab === 'home'">
        <div class="top-row top-row-2col">
          <ConnectionPanel />
          <QuickCommands />
        </div>
        <div class="mid-row">
          <JointControl />
          <div class="right-stack">
            <HandClaw />
            <RgbPanel />
          </div>
        </div>
      </template>

      <!-- Cartesian (MoveL) tab -->
      <template v-else-if="activeTab === 'cart'">
        <CartControl class="span-2" />
      </template>

      <!-- Diagnostics tab -->
      <template v-else-if="activeTab === 'diag'">
        <DiagPanel class="span-2" />
      </template>

      <!-- Tuning tab -->
      <template v-else-if="activeTab === 'tuning'">
        <DceTuning class="span-2" />
      </template>

      <!-- Console: pinned at the bottom of every tab -->
      <LogConsole class="span-2" />
    </main>
  </div>
</template>

<style scoped>
.tagline {
  color: var(--fg-dim);
  font-size: 0.85rem;
}
.spacer { flex: 1; }
.safety-group {
  display: flex;
  gap: 0.4em;
  margin-right: 0.6em;
}
.safety-group :deep(.el-button) {
  font-weight: 600;
  letter-spacing: 0.04em;
}
.tab-bar {
  display: flex;
  gap: 0.4em;
  padding: 0 1.2em;
  border-bottom: 1px solid #1f2937;
  background: var(--bg-elevated, transparent);
}
.tab-btn {
  background: transparent;
  border: none;
  color: var(--fg-dim);
  font-size: 0.95rem;
  font-weight: 500;
  padding: 0.6em 1em;
  cursor: pointer;
  border-bottom: 2px solid transparent;
  transition: color 0.15s, border-color 0.15s;
}
.tab-btn:hover { color: var(--fg, #e5e7eb); }
.tab-btn.active {
  color: #3b82f6;
  border-bottom-color: #3b82f6;
}
.top-row {
  grid-column: span 2;
  display: grid;
  grid-template-columns: 1fr 1fr 1fr;
  gap: 1em;
}
.top-row-2col { grid-template-columns: 1fr 1fr; }
.mid-row {
  display: grid;
  grid-template-columns: 3fr 1fr;   /* Joint Control wide, right stack narrow */
  gap: 1em;
  align-items: stretch;            /* right column matches Joint Control height */
}
.right-stack {
  display: flex;
  flex-direction: column;
  gap: 1em;
  min-width: 0;
  min-height: 0;
}
.right-stack > .card {
  flex: 1 1 0;                     /* HandClaw + RgbPanel split height equally */
  min-height: 0;
}
@media (max-width: 1100px) {
  .top-row, .top-row-2col, .mid-row { grid-template-columns: 1fr; }
}
.dry-run-label {
  font-size: 0.85rem;
  color: var(--fg-dim);
  padding: 2px 8px;
  border-radius: 4px;
  border: 1px solid #334155;
}
.dry-run-label.on {
  color: #fbbf24;
  border-color: #fbbf24;
  background: rgba(251, 191, 36, 0.1);
}
</style>
