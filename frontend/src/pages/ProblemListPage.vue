<script setup lang="ts">
import { onMounted, ref } from 'vue';
import { RouterLink } from 'vue-router';
import { apiGet } from '../api/client';
import EmptyState from '../components/EmptyState.vue';
import PageHeader from '../components/PageHeader.vue';
import type { PageResponse, ProblemSummary } from '../types/problem';

const problems = ref<ProblemSummary[]>([]);
const loading = ref(true);
const errorMessage = ref<string | null>(null);

function formatAcceptance(problem: ProblemSummary) {
  return `${Number(problem.acceptance_rate).toFixed(1)}%`;
}

function difficultyLabel(difficulty: ProblemSummary['difficulty']) {
  return {
    easy: '简单',
    medium: '中等',
    hard: '困难'
  }[difficulty];
}

onMounted(async () => {
  try {
    const response = await apiGet<PageResponse<ProblemSummary>>('/api/v1/problems?page=1&page_size=50');
    problems.value = response.data.items;
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : '无法加载题目';
  } finally {
    loading.value = false;
  }
});
</script>

<template>
  <div class="page-stack">
    <PageHeader title="题库" description="浏览已发布的编程题目。" />

    <section class="table-panel">
      <table v-if="problems.length > 0">
        <thead>
          <tr>
            <th>标题</th>
            <th>难度</th>
            <th>通过率</th>
            <th>标签</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="problem in problems" :key="problem.id">
            <td>
              <RouterLink class="table-link" :to="`/problems/${problem.id}`">
                {{ problem.title }}
              </RouterLink>
            </td>
            <td>
              <span class="difficulty-pill" :class="problem.difficulty">{{ difficultyLabel(problem.difficulty) }}</span>
            </td>
            <td>{{ formatAcceptance(problem) }}</td>
            <td>
              <span v-if="problem.tags.length === 0" class="muted-text">暂无标签</span>
              <span v-for="tag in problem.tags" v-else :key="tag" class="tag-pill">{{ tag }}</span>
            </td>
          </tr>
        </tbody>
      </table>
      <EmptyState
        v-if="!loading && !errorMessage && problems.length === 0"
        title="暂无已发布题目"
        message="已发布题目会显示在这里。"
      />
      <EmptyState v-if="loading" title="正在加载题目" message="正在获取已发布题库。" />
      <EmptyState v-if="errorMessage" title="无法加载题目" :message="errorMessage" />
    </section>
  </div>
</template>
