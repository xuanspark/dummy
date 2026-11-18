import { computed, defineComponent, ref } from 'vue'
import { ElMessage } from 'element-plus'
import { api } from '@/api/client'
import { useRobotStore } from '@/stores/robot'

export default defineComponent({
  setup() {
    const store = useRobotStore()
    const percent = ref(0)
    const current = ref(0.7)
    const busy = ref(false)
    const available = computed(() => (store.isConnected || store.dryRun) && !busy.value)
    async function action(label: string, command: string, operation: () => Promise<unknown>) {
      if (!available.value) return
      busy.value = true
      try {
        store.pushLog(`→ ${command}`, 'tx')
        if (store.dryRun) await store.sendRaw(command)
        else {
          const result = await operation() as { warning?: string } | undefined
          if (result?.warning) store.pushLog(`限制提醒：${result.warning}`, 'info')
        }
        store.pushLog(`${label}完成`, 'ok')
      } catch (error) {
        ElMessage.error((error as Error).message)
        store.pushLog((error as Error).message, 'err')
      } finally { busy.value = false }
    }
    const open = () => action('张开', '!HD_O', api.handOpen)
    const close = () => action('闭合', '!HD_C', api.handClose)
    const enable = () => action('启动', '!START', api.start)
    const shutdown = () => action('回零并关闭', '!CLOSE', api.close)
    const position = () => action('调整开度', `!HD_POS ${percent.value}`, () => api.handPos(percent.value))
    async function setCurrent() {
      if (!Number.isFinite(current.value) || current.value <= 0 || current.value > 2) {
        ElMessage.warning('电流必须大于 0 且不超过 2 A')
        return
      }
      await action('设置电流', `!HD_I ${current.value}`, () => api.handCurrent(current.value))
    }
    return { percent, current, available, open, close, enable, shutdown, position, setCurrent }
  },
})
