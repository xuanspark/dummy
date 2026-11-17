<script lang="ts" src="./PidTuning.ts"></script>

<template>
  <section class="card pid-panel">
    <h2>PIDV 参数调试</h2>
    <div class="controls">
      <label>设置节点</label>
      <el-select v-model="node" class="selector">
        <el-option v-for="id in 7" :key="id" :value="id" :label="id === 7 ? '7：手爪' : `${id}：关节 ${id}`" />
      </el-select>
    </div>
    <div class="parameters">
      <div v-for="field in fields" :key="field.key" class="parameter">
        <label>{{ field.label }}</label>
        <el-input v-model="gains[field.key]" placeholder="非负整数" />
        <el-button :disabled="!available" @click="setPid(field.key)">设置单项</el-button>
      </div>
    </div>
    <el-button type="primary" :disabled="!available" @click="setPid()">设置本节点全部 PIDV</el-button>
    <p class="hint">写入后校验回读值及保存状态；急停、暂停时禁止修改参数。</p>
    <div class="controls">
      <label>查询范围</label>
      <el-select v-model="queryNode" class="selector">
        <el-option label="全部节点" value="all" />
        <el-option v-for="id in 7" :key="id" :value="id" :label="id === 7 ? '7：手爪' : `${id}：关节 ${id}`" />
      </el-select>
      <el-select v-model="queryParam" :disabled="queryNode === 'all'" class="selector">
        <el-option label="全部参数" value="all" />
        <el-option v-for="field in fields" :key="field.key" :value="field.key" :label="field.label" />
      </el-select>
      <el-button :disabled="!available" @click="query">查询</el-button>
    </div>
    <table v-if="Object.keys(results).length">
      <thead><tr><th>节点</th><th>回包完整</th><th v-for="field in fields" :key="field.key">{{ field.key.toUpperCase() }}</th></tr></thead>
      <tbody>
        <tr v-for="(result, id) in results" :key="id">
          <td>{{ id }}</td><td>{{ result.online ? '是' : '否' }}</td>
          <td v-for="field in fields" :key="field.key">{{ result[field.key] ?? '—' }}</td>
        </tr>
      </tbody>
    </table>
    <h3>节点运动限制</h3>
    <div v-for="field in limitFields" :key="field.key" class="controls">
      <label>{{ field.label }}</label>
      <el-input v-model="limits[field.key]" class="selector" placeholder="非负数" />
      <el-button :disabled="!available" @click="setLimit(field.key)">设置</el-button>
    </div>
  </section>
</template>

<style scoped src="./PidTuning.css"></style>
