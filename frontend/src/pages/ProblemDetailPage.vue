<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { RouterLink, useRoute, useRouter } from 'vue-router';
import { apiGet, apiPost } from '../api/client';
import PageHeader from '../components/PageHeader.vue';
import { session } from '../stores/session';
import type { ProblemDetail, SubmissionDetail, SubmissionStatus } from '../types/problem';

const route = useRoute();
const router = useRouter();
const problemId = computed(() => String(route.params.id ?? ''));
const problem = ref<ProblemDetail | null>(null);
const loading = ref(true);
const errorMessage = ref<string | null>(null);
const actionMessage = ref<string | null>(null);
const code = ref('');
const customInput = ref('');
const runMode = ref<'samples' | 'custom'>('samples');
const activeSubmission = ref<SubmissionDetail | null>(null);
const submitting = ref(false);
const pollingTimer = ref<number | null>(null);

const isFinalStatus = computed(() => {
  if (!activeSubmission.value) {
    return true;
  }
  return !['Pending', 'Judging'].includes(activeSubmission.value.status);
});

function difficultyLabel(difficulty: ProblemDetail['difficulty']) {
  return {
    easy: '简单',
    medium: '中等',
    hard: '困难'
  }[difficulty];
}

function statusLabel(status: SubmissionStatus) {
  return status;
}

function testCaseKindLabel(kind: 'sample' | 'public' | 'hidden' | 'custom') {
  return {
    sample: '样例',
    public: '公开',
    hidden: '隐藏',
    custom: '自定义'
  }[kind];
}

function clearPolling() {
  if (pollingTimer.value !== null) {
    window.clearInterval(pollingTimer.value);
    pollingTimer.value = null;
  }
}

async function refreshSubmission(id: number) {
  const response = await apiGet<SubmissionDetail>(`/api/submissions/${id}`);
  activeSubmission.value = response.data;
  if (!['Pending', 'Judging'].includes(response.data.status)) {
    clearPolling();
  }
}

function startPolling(id: number) {
  clearPolling();
  pollingTimer.value = window.setInterval(() => {
    refreshSubmission(id).catch((error) => {
      actionMessage.value = error instanceof Error ? error.message : '无法刷新提交结果';
      clearPolling();
    });
  }, 1600);
}

async function loadProblem() {
  loading.value = true;
  errorMessage.value = null;
  activeSubmission.value = null;
  clearPolling();
  try {
    const response = await apiGet<ProblemDetail>(`/api/v1/problems/${problemId.value}`);
    problem.value = response.data;
    code.value = response.data.default_code_template;
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : '无法加载题目';
  } finally {
    loading.value = false;
  }
}

function requireLogin() {
  if (session.role === 'guest') {
    router.push({ path: '/login', query: { redirect: route.fullPath } });
    return false;
  }
  return true;
}

async function createTask(path: '/api/run' | '/api/submissions') {
  if (!problem.value || !requireLogin()) {
    return;
  }
  submitting.value = true;
  actionMessage.value = null;
  try {
    const payload =
      path === '/api/run'
        ? {
            problem_id: problem.value.id,
            source_code: code.value,
            mode: runMode.value,
            custom_input: customInput.value
          }
        : {
            problem_id: problem.value.id,
            language: 'cpp17',
            source_code: code.value
          };
    const response = await apiPost<SubmissionDetail>(path, payload);
    activeSubmission.value = response.data;
    startPolling(response.data.id);
  } catch (error) {
    actionMessage.value = error instanceof Error ? error.message : '无法创建判题任务';
  } finally {
    submitting.value = false;
  }
}

onMounted(loadProblem);
onBeforeUnmount(clearPolling);
watch(problemId, loadProblem);
</script>

<template>
  <div class="problem-layout">
    <section class="problem-statement">
      <PageHeader
        :title="problem?.title ?? `题目 #${problemId}`"
        :description="
          problem
            ? `${difficultyLabel(problem.difficulty)} · ${problem.time_limit_ms} ms · ${problem.memory_limit_mb} MB`
            : '阅读题面、运行样例并提交 C++17 代码。'
        "
      />

      <div v-if="loading" class="content-panel">
        <h2>正在加载题面</h2>
        <p>正在获取题目详情。</p>
      </div>

      <div v-else-if="errorMessage" class="content-panel">
        <h2>题目不可用</h2>
        <p>{{ errorMessage }}</p>
      </div>

      <div v-else-if="problem" class="content-panel problem-content">
        <div class="statement-text">{{ problem.description }}</div>

        <section v-if="problem.test_cases.length > 0" class="sample-stack">
          <h2>样例与公开测试</h2>
          <article v-for="testCase in problem.test_cases" :key="testCase.id" class="sample-block">
            <div class="sample-heading">
              <strong>{{ testCaseKindLabel(testCase.kind) }}</strong>
              <span>#{{ testCase.sort_order }}</span>
            </div>
            <template v-if="testCase.input !== undefined && testCase.output !== undefined">
              <label>
                输入
                <pre class="sample-pre">{{ testCase.input }}</pre>
              </label>
              <label>
                输出
                <pre class="sample-pre">{{ testCase.output }}</pre>
              </label>
            </template>
            <template v-else>
              <p>{{ testCase.input_summary }}</p>
              <p>{{ testCase.output_summary }}</p>
            </template>
          </article>
        </section>
      </div>

      <section v-if="activeSubmission" class="content-panel result-panel">
        <div class="section-title-row">
          <h2>判题结果</h2>
          <RouterLink class="ghost-button" :to="`/submissions?id=${activeSubmission.id}`">
            查看详情
          </RouterLink>
        </div>
        <div class="result-summary">
          <span class="status-pill" :class="{ accepted: activeSubmission.status === 'Accepted' }">
            {{ statusLabel(activeSubmission.status) }}
          </span>
          <span>
            {{ activeSubmission.testcase_passed_count }}/{{ activeSubmission.testcase_total_count }} 测试点
          </span>
          <span v-if="activeSubmission.total_time_ms !== null">{{ activeSubmission.total_time_ms }} ms</span>
          <span v-if="activeSubmission.peak_memory_mb !== null">{{ activeSubmission.peak_memory_mb }} MB</span>
          <span v-if="!isFinalStatus" class="muted-text">正在轮询结果...</span>
        </div>
        <p v-if="activeSubmission.judge_message" class="muted-text">{{ activeSubmission.judge_message }}</p>
        <pre v-if="activeSubmission.compile_error" class="sample-pre">{{ activeSubmission.compile_error }}</pre>
        <pre v-else-if="activeSubmission.stdout" class="sample-pre">{{ activeSubmission.stdout }}</pre>
        <table v-if="activeSubmission.case_results.length > 0">
          <thead>
            <tr>
              <th>测试点</th>
              <th>状态</th>
              <th>耗时</th>
              <th>内存</th>
              <th>摘要</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="caseResult in activeSubmission.case_results" :key="caseResult.id">
              <td>{{ testCaseKindLabel(caseResult.kind) }} #{{ caseResult.sort_order }}</td>
              <td>{{ caseResult.status }}</td>
              <td>{{ caseResult.time_ms ?? '-' }} ms</td>
              <td>{{ caseResult.memory_mb ?? '-' }} MB</td>
              <td>{{ caseResult.error_summary || caseResult.output_summary || '-' }}</td>
            </tr>
          </tbody>
        </table>
      </section>
    </section>

    <aside class="editor-panel">
      <div class="editor-toolbar">
        <span>C++17</span>
        <div class="editor-actions">
          <button type="button" :disabled="submitting" @click="createTask('/api/run')">运行</button>
          <button class="solid-button" type="button" :disabled="submitting" @click="createTask('/api/submissions')">
            提交
          </button>
        </div>
      </div>
      <textarea v-model="code" class="code-editor" spellcheck="false" />
      <div class="run-options">
        <label>
          <input v-model="runMode" type="radio" value="samples" />
          运行样例
        </label>
        <label>
          <input v-model="runMode" type="radio" value="custom" />
          自定义输入
        </label>
        <textarea
          v-if="runMode === 'custom'"
          v-model="customInput"
          class="text-area"
          placeholder="输入自定义测试数据"
          spellcheck="false"
        />
        <p v-if="actionMessage" class="form-error">{{ actionMessage }}</p>
      </div>
    </aside>
  </div>
</template>
