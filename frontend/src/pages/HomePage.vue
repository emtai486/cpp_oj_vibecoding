<script setup lang="ts">
import { onMounted, ref } from 'vue';
import { apiGet } from '../api/client';
import PageHeader from '../components/PageHeader.vue';

interface HealthData {
  service: string;
  version: string;
  status: string;
  database_configured: boolean;
  redis_configured: boolean;
}

const health = ref<HealthData | null>(null);
const healthError = ref<string | null>(null);

onMounted(async () => {
  try {
    const response = await apiGet<HealthData>('/api/health');
    health.value = response.data;
  } catch (error) {
    healthError.value = error instanceof Error ? error.message : '无法加载服务状态';
  }
});
</script>

<template>
  <div class="page-stack">
    <PageHeader
      eyebrow="控制台"
      title="在线判题工作台"
      description="查看平台状态、题库活动和近期判题工作。"
    />

    <section class="dashboard-grid" aria-label="平台状态">
      <article class="metric-card">
        <span class="metric-label">API</span>
        <strong>{{ health?.status === 'ok' ? '正常' : healthError ? '离线' : '加载中' }}</strong>
        <p>{{ health?.service ?? healthError ?? '正在检查服务状态。' }}</p>
      </article>
      <article class="metric-card">
        <span class="metric-label">PostgreSQL</span>
        <strong>{{ health?.database_configured ? '已配置' : '待配置' }}</strong>
        <p>数据库结构版本通过迁移脚本管理。</p>
      </article>
      <article class="metric-card">
        <span class="metric-label">Redis</span>
        <strong>{{ health?.redis_configured ? '已配置' : '待配置' }}</strong>
        <p>Redis 配置完成后可用于判题队列存储。</p>
      </article>
    </section>
  </div>
</template>
