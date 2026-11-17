import { computed, defineComponent, ref } from 'vue'
import { useRobotStore } from '@/stores/robot'

export default defineComponent({
  setup() {
    const store = useRobotStore()
    const mode = ref(11)
    const color = ref('#646464')
    const available = computed(() => store.isConnected || store.dryRun)
    const modes = [
      '关闭', '红色常亮', '橙色常亮', '黄色常亮', '绿色常亮', '青色常亮',
      '蓝色常亮', '紫色常亮', '自定义 RGB 颜色常亮', '彩虹流水', '白色呼吸灯', '自动模式',
    ]
    async function send(command: string) {
      if (!available.value) return
      try { await store.sendRaw(command) } catch { /* 错误已写入控制台。 */ }
    }
    async function custom() {
      const rgb = [1, 3, 5].map(index => parseInt(color.value.slice(index, index + 2), 16))
      await send(`!RGB_CUSTOM ${rgb.join(' ')}`)
      mode.value = 8
    }
    return { mode, color, available, modes, send, custom }
  },
})
