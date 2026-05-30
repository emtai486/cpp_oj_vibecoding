<script setup lang="ts">
import { onMounted, ref } from 'vue';
import { RouterLink } from 'vue-router';
import { apiGet } from '../api/client';
import EmptyState from '../components/EmptyState.vue';
import PageHeader from '../components/PageHeader.vue';
import type { PageResponse, SubmissionSummary } from '../types/problem';

const submissions = ref<SubmissionSummary[]>([]);
const loading = ref(true);
const errorMessage = ref<string | null>(null);

async function loadSubmissions() {
  loading.value = true;
  errorMessage.value = null;
  try {
    const response = await apiGet<PageResponse<SubmissionSummary>>('/api/me/submissions?page=1&page_size=50');
    submissions.value = response.data.items;
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : '无法加载提交历史';
  } finally {
    loading.value = false;
  }
}

onMounted(loadSubmissions);
</script>

<template>
  <div class="page-stack">
    <PageHeader title="我的提交历史" description="查看你的提交状态、测试点通过数、耗时和内存。 " />

    <section class="table-panel">
      <table v-if="submissions.length > 0">
        <thead>
          <tr>
            <th>ID</th>
            <th>题目</th>
            <th>状态</th>
            <th>测试点</th>
            <th>耗时</th>
            <th>内存</th>
            <th>提交时间</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="submission in submissions" :key="submission.id">
            <td>
              <RouterLink class="table-link" :to="`/submissions?id=${submission.id}`">
                #{{ submission.id }}
              </RouterLink>
            </td>
            <td>
              <RouterLink class="table-link" :to="`/problems/${submission.problem_id}`">
                {{ submission.problem_title }}
              </RouterLink>
            </td>
            <td><span class="status-pill">{{ submission.status }}</span></td>
            <td>{{ submission.testcase_passed_count }}/{{ submission.testcase_total_count }}</td>
            <td>{{ submission.total_time_ms ?? '-' }} ms</td>
            <td>{{ submission.peak_memory_mb ?? '-' }} MB</td>
            <td>{{ submission.created_at }}</td>
          </tr>
        </tbody>
      </table>
      <EmptyState v-if="loading" title="正在加载提交历史" message="正在获取你的提交记录。" />
      <EmptyState v-if="errorMessage" title="无法加载提交历史" :message="errorMessage" />
      <EmptyState
        v-if="!loading && !errorMessage && submissions.length === 0"
        title="暂无提交历史"
        message="提交代码后，记录会显示在这里。"
      />
    </section>
  </div>
</template>
