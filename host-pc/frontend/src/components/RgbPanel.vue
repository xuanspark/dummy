<script setup lang="ts">
import { ref, computed } from 'vue'
import { ElMessage } from 'element-plus'
import { useRobotStore } from '@/stores/robot'

const store = useRobotStore()

// ---- state ----
const mode = ref<number>(11)                 // 0..11
const hex = ref<string>('#64ff64')

// 0..11 effect modes
const MODES: { value: number; label: string }[] = [
  { value: 0,  label: '0  — 关闭'      },
  { value: 1,  label: '1  — 红'        },
  { value: 2,  label: '2  — 橙'        },
  { value: 3,  label: '3  — 黄'        },
  { value: 4,  label: '4  — 绿'        },
  { value: 5,  label: '5  — 青'        },
  { value: 6,  label: '6  — 蓝'        },
  { value: 7,  label: '7  — 紫'        },
  { value: 8,  label: '8  — 自定义'    },
  { value: 9,  label: '9  — 彩虹'      },
  { value: 10, label: '10 — 呼吸灯'    },
  { value: 11, label: '11 — 自动'      },
]

async function send(cmd: string) {
  if (!store.isConnected && !store.dryRun) {
    ElMessage.warning('Not connected')
    return
  }
  try { await store.sendRaw(cmd) } catch { /* already logged */ }
}

function parseHex(s: string): [number, number, number] | null {
  const m = s.trim().replace(/^#/, '')
  if (!/^[0-9a-fA-F]{6}$/.test(m)) return null
  return [
    parseInt(m.slice(0, 2), 16),
    parseInt(m.slice(2, 4), 16),
    parseInt(m.slice(4, 6), 16),
  ]
}

const previewColor = computed(() => (parseHex(hex.value) ? hex.value : '#000'))
const currentModeLabel = computed(() => MODES.find(m => m.value === mode.value)?.label.split('— ')[1] ?? '')

async function onOn()     { await send('!RGB_ON')  }
async function onOff()    { await send('!RGB_OFF') }
async function onApply()  { await send(`!RGB_MODE ${mode.value}`) }
async function onSetColor() {
  const rgb = parseHex(hex.value)
  if (!rgb) { ElMessage.warning('Hex must be like #64ff64'); return }
  const [r, g, b] = rgb
  await send(`!RGB_CUSTOM ${r} ${g} ${b}`)
}
</script>

<template>
  <section class="card rgb">
    <div class="header">
      <h2>RGB Effect / 灯效</h2>
      <span class="mode-pill" :class="{ off: mode === 0 }">{{ currentModeLabel }}</span>
      <span class="flex-spacer" />
      <div class="header-btns">
        <el-button size="small" type="success" plain
          :disabled="!store.isConnected && !store.dryRun" @click="onOn">On</el-button>
        <el-button size="small" type="info" plain
          :disabled="!store.isConnected && !store.dryRun" @click="onOff">Off</el-button>
      </div>
    </div>

    <!-- Effect mode + Apply -->
    <div class="row">
      <el-select v-model="mode" size="small" class="grow-input"
        :disabled="!store.isConnected && !store.dryRun">
        <el-option v-for="m in MODES" :key="m.value" :value="m.value" :label="m.label" />
      </el-select>
      <el-button size="small" type="primary"
        :disabled="!store.isConnected && !store.dryRun" @click="onApply">Apply</el-button>
    </div>

    <!-- Color picker + hex + Set -->
    <div class="row">
      <label class="swatch" :style="{ background: previewColor }" :title="'点击选色'">
        <input type="color" v-model="hex" class="swatch-input"
          :disabled="!store.isConnected && !store.dryRun" />
      </label>
      <el-input v-model="hex" size="small" placeholder="#rrggbb" class="hex-input">
        <template #prefix><span class="hex-hash">#</span></template>
      </el-input>
      <el-button size="small" type="primary"
        :disabled="!store.isConnected && !store.dryRun" @click="onSetColor">Set</el-button>
    </div>
  </section>
</template>

<style scoped>
.rgb {
  display: flex;
  flex-direction: column;
  gap: 0.7em;
}
.header {
  display: flex;
  align-items: center;
  gap: 0.5em;
  padding-bottom: 0.5em;
  border-bottom: 1px solid #1e293b;
}
.header h2 { margin: 0; font-size: 1rem; }
.flex-spacer { flex: 1; }
.header-btns { display: flex; gap: 0.3em; }
.mode-pill {
  font-size: 0.75rem;
  padding: 2px 8px;
  border-radius: 999px;
  background: rgba(59, 130, 246, 0.15);
  color: #93c5fd;
  border: 1px solid rgba(59, 130, 246, 0.35);
  white-space: nowrap;
}
.mode-pill.off {
  background: transparent;
  color: var(--fg-dim);
  border-color: #334155;
}
.row {
  display: flex;
  gap: 0.4em;
  align-items: center;
  flex-wrap: nowrap;
}
.grow-input { flex: 1 1 0; min-width: 0; }
.hex-input {
  flex: 1 1 0;
  min-width: 0;
  font-family: Consolas, monospace;
}
.hex-hash { color: var(--fg-dim); font-family: Consolas, monospace; }
.hex-input :deep(.el-input__prefix) { margin-right: -2px; }
.swatch {
  width: 36px;
  height: 28px;
  border-radius: 6px;
  border: 1px solid #334155;
  flex: 0 0 auto;
  cursor: pointer;
  position: relative;
  overflow: hidden;
  transition: box-shadow 0.15s ease, transform 0.1s ease;
}
.swatch:hover { box-shadow: 0 0 0 2px rgba(59, 130, 246, 0.35); }
.swatch:active { transform: scale(0.95); }
.swatch-input {
  position: absolute;
  inset: 0;
  opacity: 0;
  cursor: pointer;
  border: 0;
  padding: 0;
}
</style>
