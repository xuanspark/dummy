<script setup lang="ts">
import { computed, nextTick, ref, watch } from 'vue'
import { ElMessage } from 'element-plus'
import { Sort } from '@element-plus/icons-vue'
import { useRobotStore } from '@/stores/robot'

const store = useRobotStore()
const boxRef = ref<HTMLElement | null>(null)
const input = ref('')

// Reverse layout: input on top, newest log right under it.
const REV_KEY = 'console.reversed'
const reversed = ref<boolean>(localStorage.getItem(REV_KEY) === '1')
function toggleReverse() {
  reversed.value = !reversed.value
  localStorage.setItem(REV_KEY, reversed.value ? '1' : '0')
  void scrollToNewest()
}

// Verbose: include ESP32 boot/system noise lines (hidden by default).
const VERB_KEY = 'console.verbose'
const verbose = ref<boolean>(localStorage.getItem(VERB_KEY) === '1')
function toggleVerbose() {
  verbose.value = !verbose.value
  localStorage.setItem(VERB_KEY, verbose.value ? '1' : '0')
  void scrollToNewest()
}

async function scrollToNewest() {
  await nextTick()
  const box = boxRef.value
  if (!box) return
  // Newest is at the top in reversed mode, at the bottom otherwise.
  box.scrollTop = reversed.value ? 0 : box.scrollHeight
}

watch(() => store.logs.length, scrollToNewest)

// Hide ESP32 boot noise unless verbose is on.
const visibleLogs = computed(() =>
  verbose.value ? store.logs : store.logs.filter(l => !l.noise),
)
const hiddenNoiseCount = computed(() =>
  verbose.value ? 0 : store.logs.reduce((n, l) => n + (l.noise ? 1 : 0), 0),
)

// Render order: in reversed mode the newest log is first, so it visually
// sits right below the input (which is also on top in reversed layout).
const displayLogs = computed(() =>
  reversed.value ? visibleLogs.value.slice().reverse() : visibleLogs.value,
)

async function submit() {
  const cmd = input.value.trim()
  if (!cmd) return
  if (!store.isConnected && !store.dryRun) {
    ElMessage.warning('Not connected (or enable Dry-run in the header)')
    return
  }
  try {
    await store.sendRaw(cmd)
  } catch {
    // already logged
  }
  input.value = ''
}

function clear() {
  store.logs.splice(0, store.logs.length)
}

function fmt(t: number): string {
  const d = new Date(t)
  return `${d.getHours().toString().padStart(2, '0')}:${d.getMinutes().toString().padStart(2, '0')}:${d.getSeconds().toString().padStart(2, '0')}`
}
</script>

<template>
  <section class="card console" :class="{ reversed }">
    <div class="header">
      <h2>Console</h2>
      <span v-if="hiddenNoiseCount > 0" class="hidden-pill" :title="`已隐藏 ${hiddenNoiseCount} 条 ESP32 启动调试信息`">
        +{{ hiddenNoiseCount }} 隐藏
      </span>
      <span class="flex-spacer" />
      <el-tooltip :content="verbose ? '隐藏 ESP32 启动/驱动日志' : '显示全部日志（含 ESP32 启动调试信息）'" placement="top">
        <el-button size="small" :type="verbose ? 'warning' : 'default'" :plain="!verbose" @click="toggleVerbose">
          {{ verbose ? '详细' : '简洁' }}
        </el-button>
      </el-tooltip>
      <el-tooltip :content="reversed ? '切回正序（输入框在下）' : '反转布局（输入框在上，最新在上）'" placement="top">
        <el-button size="small" :type="reversed ? 'primary' : 'default'" :plain="!reversed" @click="toggleReverse">
          <el-icon><Sort /></el-icon>
          <span style="margin-left: 4px;">{{ reversed ? '倒序' : '正序' }}</span>
        </el-button>
      </el-tooltip>
      <el-button size="small" @click="clear">Clear</el-button>
    </div>

    <div ref="boxRef" class="log-box">
      <div v-for="l in displayLogs" :key="l.id" class="log-line" :class="[l.kind, { noise: l.noise }]">
        <span class="ts">{{ fmt(l.time) }}</span>
        <span class="msg">{{ l.line }}</span>
      </div>
    </div>

    <div class="input-row">
      <el-input
        v-model="input"
        placeholder="Type a raw command (e.g. !HOME, &0,0,90,0,0,0,10) and press Enter"
        :disabled="!store.isConnected && !store.dryRun"
        @keyup.enter="submit"
      />
      <el-button type="primary" :disabled="!store.isConnected && !store.dryRun" @click="submit">Send</el-button>
    </div>
  </section>
</template>

<style scoped>
.console { display: flex; flex-direction: column; }
.header {
  display: flex;
  align-items: center;
  gap: 0.4em;
  margin-bottom: 0.5em;
}
.header h2 { margin: 0; }
.flex-spacer { flex: 1; }
.log-line {
  display: flex;
  gap: 0.6em;
}
.log-line .ts {
  color: #64748b;
  flex: 0 0 auto;
}
.log-line .msg { flex: 1; }
.log-line.noise { opacity: 0.55; font-style: italic; }   /* visible only in verbose */
.hidden-pill {
  font-size: 0.72rem;
  padding: 1px 8px;
  border-radius: 999px;
  background: rgba(148, 163, 184, 0.15);
  color: #94a3b8;
  border: 1px solid #334155;
  cursor: help;
}
.input-row {
  display: flex;
  gap: 0.4em;
  margin-top: 0.6em;
}

/* Log box anchors content toward the input:
 *   - Normal mode (input at bottom): newest log sticks to the BOTTOM of box.
 *   - Reversed mode (input at top):  newest log sticks to the TOP of box.
 * Implemented with flex + auto margin on the first child. Scrolling still works. */
.log-box {
  display: flex !important;
  flex-direction: column;
}
.log-box > :first-child { margin-top: auto; }             /* 正序: push to bottom */
.console.reversed .log-box > :first-child { margin-top: 0; } /* 倒序: natural top flow */

/* --- Reversed layout: header → input → logs (newest first) --- */
.console.reversed .header   { order: 0; }
.console.reversed .input-row {
  order: 1;
  margin-top: 0;
  margin-bottom: 0.6em;
}
.console.reversed .log-box { order: 2; }
</style>
