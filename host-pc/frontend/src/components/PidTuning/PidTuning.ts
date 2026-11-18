import { computed, defineComponent, reactive, ref } from 'vue'
import { ElMessage } from 'element-plus'
import { api, type DceKind, type DceNodeGains, type MotionKind } from '@/api/client'
import { useRobotStore } from '@/stores/robot'

export default defineComponent({
  setup() {
    const store = useRobotStore()
    const node = ref(1)
    const queryNode = ref<number | 'all'>(1)
    const queryParam = ref<DceKind | 'all'>('all')
    const gains = reactive<Record<DceKind, string>>({ kp: '', ki: '', kd: '', kv: '' })
    const fields: Array<{ key: DceKind; label: string }> = [
      { key: 'kp', label: 'KP（比例）' },
      { key: 'ki', label: 'KI（积分）' },
      { key: 'kd', label: 'KD（微分）' },
      { key: 'kv', label: 'KV（速度）' },
    ]
    const limits = reactive<Record<MotionKind, string>>({ acc: '', speed: '', i_limit: '' })
    const limitFields: Array<{ key: MotionKind; label: string }> = [
      { key: 'acc', label: '加速度' },
      { key: 'speed', label: '速度限制' },
      { key: 'i_limit', label: '电流限制' },
    ]
    const results = ref<Record<string, DceNodeGains>>({})
    const busy = ref(false)
    const available = computed(() => (store.isConnected || store.dryRun) && !busy.value)

    async function execute(label: string, operation: () => Promise<unknown>, readOnly = false) {
      if (!available.value) return
      if (!readOnly && ['estop', 'paused'].includes(store.robotState)) {
        ElMessage.warning('当前处于急停或暂停状态，只允许查询和恢复')
        return
      }
      busy.value = true
      store.pushLog(`→ ${label}`, 'tx')
      try {
        if (!store.dryRun) await operation()
        store.pushLog(`${label}：${store.dryRun ? '模拟完成' : '完成'}`, 'ok')
      } catch (error) {
        ElMessage.error((error as Error).message)
        store.pushLog((error as Error).message, 'err')
      } finally {
        busy.value = false
      }
    }
    async function setPid(kind?: DceKind) {
      const keys = kind ? [kind] : fields.map(field => field.key)
      const values: Partial<Record<DceKind, number>> = {}
      for (const key of keys) {
        const value = gains[key].trim()
        if (!/^\d+$/.test(value) || Number(value) > 2147483647) {
          ElMessage.warning('请填写 0～2147483647 的整数；整组设置需要填写四项')
          return
        }
        values[key] = Number(value)
      }
      await execute(`设置节点 ${node.value} 的 ${kind?.toUpperCase() || '全部 PIDV'}`, () => api.setPid(node.value, values))
    }
    async function query() {
      await execute('查询 PIDV 参数', async () => {
        const response = await api.getPid(queryNode.value === 'all' ? undefined : queryNode.value,
          queryNode.value === 'all' || queryParam.value === 'all' ? undefined : queryParam.value)
        results.value = response.nodes
      }, true)
    }
    async function setLimit(kind: MotionKind) {
      const value = Number(limits[kind])
      if (!limits[kind].trim() || !Number.isFinite(value) || value <= 0) {
        ElMessage.warning('限制值必须为大于 0 的有限数')
        return
      }
      await execute(`设置节点 ${node.value} 的限制`, () => api.setMotionLimit(node.value, kind, value))
    }
    return { node, queryNode, queryParam, gains, fields, results, available, busy, limits, limitFields, setPid, query, setLimit }
  },
})
