<script lang="ts" src="./SafetyControls.ts"></script>

<template>
  <div class="safety-controls">
    <el-button type="danger" size="small" :disabled="!available" @click="send('stop')">急停</el-button>
    <el-button type="warning" size="small" :disabled="!available" @click="send('pause')">暂停</el-button>
    <el-button type="success" size="small" :disabled="!available" @click="send('resume')">恢复</el-button>
  </div>
</template>

<style scoped src="./SafetyControls.css"></style>
