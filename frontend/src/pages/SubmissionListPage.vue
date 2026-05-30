<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { RouterLink, useRoute } from 'vue-router';
import { apiGet } from '../api/client';
import EmptyState from '../components/EmptyState.vue';
import PageHeader from '../components/PageHeader.vue';
import type { PageResponse, SubmissionDetail, SubmissionSummary } from '../types/problem';

const route = useRoute();
const submissions = ref<SubmissionSummary[]>([]);
const selectedSubmission = ref<SubmissionDetail | null>(null);
const loading = ref(true);
const detailLoading = ref(false);
const errorMessage = ref<string | null>(null);
const detailError = ref<string | null>(null);
const pollingTimer = ref<number | null>(null);

const selectedId = computed(() => {
  const raw = route.query.id;
  return typeof raw === 'string' && raw ? Number(raw) : null;
});

function clearPolling() {
  if (pollingTimer.value !== null) {
    window.clearInterval(pollingTimer.value);
    pollingTimer.value = null;
  }
}

async function loadList() {
  loading.value = true;
  errorMessage.value = null;
  try {
    const response = await apiGet<PageResponse<SubmissionSummary>>('/api/me/submissions?page=1&page_size=50');
    submissions.value = response.data.items;
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : '无法加载提交记录';
  } finally {
    loading.value = false;
  }
}

async function loadDetail(id: number) {
  detailLoading.value = true;
  detailError.value = null;
  try {
    const response = await apiGet<SubmissionDetail>(`/api/submissions/${id}`);
    selectedSubmission.value = response.data;
    if (['Pending', 'Judging'].includes(response.data.status)) {
      clearPolling();
      pollingTimer.value = window.setInterval(() => {
        loadDetail(id).catch(() => clearPolling());
      }, 1600);
    } else {
      clearPolling();
    }
  } catch (error) {
    detailError.value = error instanceof Error ? error.message : '无法加载提交详情';
    selectedSubmission.value = null;
    clearPolling();
  } finally {
    detailLoading.value = false;
  }
}

watch(
  selectedId,
  (id) => {
    if (id) {
      loadDetail(id);
    } else {
      selectedSubmission.value = null;
      clearPolling();
    }
  },
  { immediate: true }
);

onMounted(loadList);
onBeforeUnmount(clearPolling);
</script>

<template>
  <div class="page-stack">
    <PageHeader title="提交记录" description="查看判题状态、运行时间、内存和测试点摘要。" />

    <section v-if="selectedSubmission || detailLoading || detailError" class="content-panel result-panel">
      <div class="section-title-row">
        <h2>提交详情</h2>
        <RouterLink class="ghost-button" to="/submissions">返回列表</RouterLink>
      </div>
      <p v-if="detailLoading" class="muted-text">正在加载提交详情...</p>
      <p v-if="detailError" class="form-error">{{ detailError }}</p>
      <template v-if="selectedSubmission">
        <div class="result-summary">
          <span class="status-pill">{{ selectedSubmission.status }}</span>
          <span>#{{ selectedSubmission.id }}</span>
          <RouterLink class="table-link" :to="`/problems/${selectedSubmission.problem_id}`">
            {{ selectedSubmission.problem_title }}
          </RouterLink>
          <span>{{ selectedSubmission.testcase_passed_count }}/{{ selectedSubmission.testcase_total_count }} 测试点</span>
          <span>{{ selectedSubmission.total_time_ms ?? '-' }} ms</span>
          <span>{{ selectedSubmission.peak_memory_mb ?? '-' }} MB</span>
        </div>
        <p v-if="selectedSubmission.judge_message" class="muted-text">{{ selectedSubmission.judge_message }}</p>
        <pre v-if="selectedSubmission.compile_error" class="sample-pre">{{ selectedSubmission.compile_error }}</pre>
        <pre v-if="selectedSubmission.stdout" class="sample-pre">{{ selectedSubmission.stdout }}</pre>
        <pre v-if="selectedSubmission.stderr" class="sample-pre">{{ selectedSubmission.stderr }}</pre>
        <table v-if="selectedSubmission.case_results.length > 0">
          <thead>
            <tr>
              <th>测试点</th>
              <th>状态</th>
              <th>耗时</th>
              <th>内存</th>
              <th>错误摘要</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="caseResult in selectedSubmission.case_results" :key="caseResult.id">
              <td>{{ caseResult.kind }} #{{ caseResult.sort_order }}</td>
              <td>{{ caseResult.status }}</td>
              <td>{{ caseResult.time_ms ?? '-' }} ms</td>
              <td>{{ caseResult.memory_mb ?? '-' }} MB</td>
              <td>{{ caseResult.error_summary || '-' }}</td>
            </tr>
          </tbody>
        </table>
      </template>
    </section>

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
      <EmptyState v-if="loading" title="正在加载提交记录" message="正在获取提交列表。" />
      <EmptyState v-if="errorMessage" title="无法加载提交记录" :message="errorMessage" />
      <EmptyState
        v-if="!loading && !errorMessage && submissions.length === 0"
        title="暂无提交"
        message="提交记录会显示在这里。"
      />
    </section>
  </div>
</template>
