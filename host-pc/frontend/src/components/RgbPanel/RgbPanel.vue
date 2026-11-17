<script lang="ts" src="./RgbPanel.ts"></script>

<template>
  <section class="card rgb-panel">
    <h2>灯光模式</h2>
    <div class="row">
      <el-select v-model="mode" :disabled="!available" class="mode-select">
        <el-option v-for="(label, id) in modes" :key="id" :value="id" :label="`${id}：${label}`" />
      </el-select>
      <el-button :disabled="!available" @click="send(`!RGB_MODE ${mode}`)">应用</el-button>
    </div>
    <div class="row">
      <el-button :disabled="!available" @click="send('!RGB_ON')">开启灯光</el-button>
      <el-button :disabled="!available" @click="send('!RGB_OFF')">关闭灯光</el-button>
      <input v-model="color" type="color" :disabled="!available" aria-label="自定义灯光颜色" />
      <el-button :disabled="!available" @click="custom">设置颜色</el-button>
    </div>
    <p v-if="mode === 11">待机：白色呼吸；工作：绿色常亮；完成：青色慢闪，约 5 秒后待机；急停：红色渐变快闪；暂停：黄色流水。</p>
  </section>
</template>

<style scoped src="./RgbPanel.css"></style>
