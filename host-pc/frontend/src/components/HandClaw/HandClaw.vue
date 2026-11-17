<script lang="ts" src="./HandClaw.ts"></script>

<template>
  <section class="card hand-panel">
    <h2>手爪控制</h2>
    <div class="row">
      <el-button type="success" :disabled="!available" @click="open">张开</el-button>
      <el-button type="primary" :disabled="!available" @click="close">闭合</el-button>
    </div>
    <div class="row">
      <el-button :disabled="!available" @click="enable">启动使能</el-button>
      <el-button :disabled="!available" @click="shutdown">回零后关闭</el-button>
    </div>
    <p>手爪随整机启动、关闭；尚未 HOME 时，动作前自动归位。</p>
    <label>张开比例：{{ percent }}%</label>
    <el-slider v-model="percent" :min="0" :max="100" :step="1" :disabled="!available" @change="position" />
    <div class="row">
      <label>夹持电流（A）</label>
      <el-input-number v-model="current" :min="0.01" :max="2" :step="0.1" :precision="2" />
      <el-button :disabled="!available" @click="setCurrent">设置</el-button>
    </div>
  </section>
</template>

<style scoped src="./HandClaw.css"></style>
