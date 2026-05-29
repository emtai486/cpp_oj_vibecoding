<script setup lang="ts">
import { onMounted, ref } from 'vue';
import { apiGet } from '../../api/client';
import EmptyState from '../../components/EmptyState.vue';
import PageHeader from '../../components/PageHeader.vue';

interface DifficultyRecord {
  name: 'easy' | 'medium' | 'hard';
  problem_count: number;
}

const difficulties = ref<DifficultyRecord[]>([]);
const loading = ref(true);
const errorMessage = ref<string | null>(null);

function difficultyLabel(difficulty: DifficultyRecord['name']) {
  return {
    easy: '简单',
    medium: '中等',
    hard: '困难'
  }[difficulty];
}

onMounted(async () => {
  try {
    const response = await apiGet<{ items: DifficultyRecord[] }>('/api/admin/difficulties');
    difficulties.value = response.data.items;
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : '无法加载难度';
  } finally {
    loading.value = false;
  }
});
</script>

<template>
  <div class="page-stack">
    <PageHeader title="难度管理" description="查看内置难度标签和使用情况。" />

    <section class="table-panel">
      <table v-if="difficulties.length > 0">
        <thead>
          <tr>
            <th>名称</th>
            <th>题目数</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="difficulty in difficulties" :key="difficulty.name">
            <td><span class="difficulty-pill" :class="difficulty.name">{{ difficultyLabel(difficulty.name) }}</span></td>
            <td>{{ difficulty.problem_count }}</td>
          </tr>
        </tbody>
      </table>
      <EmptyState v-if="loading" title="正在加载难度" message="正在获取难度使用情况。" />
      <EmptyState v-if="errorMessage" title="无法加载难度" :message="errorMessage" />
    </section>
  </div>
</template>
