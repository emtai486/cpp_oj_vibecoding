<script setup lang="ts">
import { onMounted, ref } from 'vue';
import { RouterLink } from 'vue-router';
import { apiGet } from '../../api/client';
import EmptyState from '../../components/EmptyState.vue';
import PageHeader from '../../components/PageHeader.vue';
import type { PageResponse, SubmissionSummary } from '../../types/problem';

const submissions = ref<SubmissionSummary[]>([]);
const loading = ref(true);
const errorMessage = ref<string | null>(null);

async function loadSubmissions() {
  loading.value = true;
  errorMessage.value = null;
  try {
    const response = await apiGet<PageResponse<SubmissionSummary>>('/api/admin/submissions?page=1&page_size=100');
    submissions.value = response.data.items;
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : '无法加载提交记录';
  } finally {
    loading.value = false;
  }
}

onMounted(loadSubmissions);
</script>

<template>
  <div class="admin-layout">
    <aside class="admin-nav">
      <RouterLink to="/admin/problems">题目管理</RouterLink>
      <RouterLink to="/admin/tags">标签管理</RouterLink>
      <RouterLink to="/admin/difficulties">难度管理</RouterLink>
      <RouterLink to="/admin/testdata">测试数据</RouterLink>
      <RouterLink to="/admin/submissions">提交记录</RouterLink>
      <RouterLink to="/admin/workers">Worker 状态</RouterLink>
      <RouterLink to="/admin/ai-logs">AI 日志</RouterLink>
    </aside>

    <section class="page-stack">
      <PageHeader title="提交记录" description="查看所有用户和题目的判题结果。" />

      <section class="table-panel">
        <table v-if="submissions.length > 0">
          <thead>
            <tr>
              <th>ID</th>
              <th>用户</th>
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
              <td>#{{ submission.id }}</td>
              <td>{{ submission.username }}</td>
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
        <EmptyState v-if="loading" title="正在加载提交记录" message="正在获取全站提交列表。" />
        <EmptyState v-if="errorMessage" title="无法加载提交记录" :message="errorMessage" />
        <EmptyState
          v-if="!loading && !errorMessage && submissions.length === 0"
          title="暂无提交"
          message="用户提交代码后，记录会显示在这里。"
        />
      </section>
    </section>
  </div>
</template>
