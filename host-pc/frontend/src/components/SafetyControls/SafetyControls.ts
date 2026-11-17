import { computed, defineComponent } from 'vue'
import { api } from '@/api/client'
import { useRobotStore } from '@/stores/robot'

export default defineComponent({
  setup() {
    const store = useRobotStore()
    const available = computed(() => store.isConnected || store.dryRun)
    function stateLabel(state: string): string {
      switch (state) {
        case 'NORMAL': return '正常'
        case 'PAUSED': return '暂停'
        case 'EMERGENCY_STOP': return '急停'
        default: return state
      }
    }
    async function send(command: 'stop' | 'pause' | 'resume') {
      const wire = { stop: '!STOP', pause: '!PAUSE', resume: '!RESUME' }[command]
      if (!available.value) return
      if (store.dryRun) {
        await store.sendRaw(wire)
        return
      }
      store.pushLog(`→ ${wire}`, 'tx')
      try {
        const response = await { stop: api.estop, pause: api.pause, resume: api.resume }[command]()
        store.applyMotionState(response.state)
        store.pushLog(`← ${wire} 状态：${stateLabel(response.state)}`, 'ok')
      } catch (error) {
        store.pushLog(`指令失败：${(error as Error).message}`, 'err')
      }
    }
    return { available, send }
  },
})
