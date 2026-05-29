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
    healthError.value = error instanceof Error ? error.message : 'Unable to load service health';
  }
});
</script>

<template>
  <div class="page-stack">
    <PageHeader
      eyebrow="Dashboard"
      title="Online judge workspace"
      description="Track platform status, problem activity, and recent judging work."
    />

    <section class="dashboard-grid" aria-label="Platform status">
      <article class="metric-card">
        <span class="metric-label">API</span>
        <strong>{{ health?.status ?? (healthError ? 'Offline' : 'Loading') }}</strong>
        <p>{{ health?.service ?? healthError ?? 'Checking service health.' }}</p>
      </article>
      <article class="metric-card">
        <span class="metric-label">PostgreSQL</span>
        <strong>{{ health?.database_configured ? 'Configured' : 'Pending' }}</strong>
        <p>Schema version is managed through migrations.</p>
      </article>
      <article class="metric-card">
        <span class="metric-label">Redis</span>
        <strong>{{ health?.redis_configured ? 'Configured' : 'Pending' }}</strong>
        <p>Judge queue storage is available when Redis is configured.</p>
      </article>
    </section>
  </div>
</template>
