<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue';
import { RouterLink } from 'vue-router';
import { apiGet, apiPost } from '../../api/client';
import EmptyState from '../../components/EmptyState.vue';
import PageHeader from '../../components/PageHeader.vue';
import type { PageResponse, ProblemSummary, TestCaseSummary } from '../../types/problem';

const problems = ref<ProblemSummary[]>([]);
const selectedProblemId = ref<number | null>(null);
const testCases = ref<TestCaseSummary[]>([]);
const kind = ref<'sample' | 'public' | 'hidden'>('sample');
const input = ref('');
const output = ref('');
const displayFullContent = ref(true);
const sortOrder = ref(0);
const loading = ref(true);
const saving = ref(false);
const errorMessage = ref<string | null>(null);
const notice = ref<string | null>(null);

const selectedProblem = computed(() =>
  problems.value.find((problem) => problem.id === selectedProblemId.value)
);

function testCaseKindLabel(value: 'sample' | 'public' | 'hidden') {
  return {
    sample: '样例',
    public: '公开',
    hidden: '隐藏'
  }[value];
}

async function loadProblems() {
  loading.value = true;
  errorMessage.value = null;
  try {
    const response = await apiGet<PageResponse<ProblemSummary>>('/api/admin/problems?page=1&page_size=100');
    problems.value = response.data.items;
    selectedProblemId.value = problems.value[0]?.id ?? null;
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : '无法加载题目';
  } finally {
    loading.value = false;
  }
}

async function loadTestCases() {
  if (!selectedProblemId.value) {
    testCases.value = [];
    return;
  }
  try {
    const response = await apiGet<{ items: TestCaseSummary[] }>(
      `/api/admin/problems/${selectedProblemId.value}/testcases`
    );
    testCases.value = response.data.items;
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : '无法加载测试数据';
  }
}

async function saveTestCase() {
  if (!selectedProblemId.value) {
    return;
  }
  saving.value = true;
  errorMessage.value = null;
  notice.value = null;
  try {
    await apiPost<TestCaseSummary>(`/api/admin/problems/${selectedProblemId.value}/testcases`, {
      kind: kind.value,
      input: input.value,
      output: output.value,
      display_full_content: kind.value === 'hidden' ? false : displayFullContent.value,
      sort_order: Number(sortOrder.value)
    });
    input.value = '';
    output.value = '';
    sortOrder.value += 1;
    notice.value = '测试数据已保存。';
    await loadTestCases();
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : '无法保存测试数据';
  } finally {
    saving.value = false;
  }
}

watch(selectedProblemId, loadTestCases);
watch(kind, () => {
  displayFullContent.value = kind.value === 'sample';
});

onMounted(async () => {
  await loadProblems();
  await loadTestCases();
});
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
      <PageHeader title="测试数据管理" description="维护样例、公开和隐藏测试数据。" />

      <form class="form-panel admin-form" @submit.prevent="saveTestCase">
        <label>
          题目
          <select v-model.number="selectedProblemId" :disabled="problems.length === 0">
            <option v-for="problem in problems" :key="problem.id" :value="problem.id">
              #{{ problem.id }} {{ problem.title }}
            </option>
          </select>
        </label>

        <div class="form-grid">
          <label>
            类型
            <select v-model="kind">
              <option value="sample">样例</option>
              <option value="public">公开</option>
              <option value="hidden">隐藏</option>
            </select>
          </label>
          <label>
            排序
            <input v-model.number="sortOrder" type="number" min="0" />
          </label>
          <label class="checkbox-label">
            <input v-model="displayFullContent" type="checkbox" :disabled="kind === 'hidden'" />
            展示完整内容
          </label>
        </div>

        <label>
          输入
          <textarea v-model="input" class="text-area tall" required />
        </label>

        <label>
          期望输出
          <textarea v-model="output" class="text-area tall" required />
        </label>

        <p v-if="notice" class="form-success">{{ notice }}</p>
        <p v-if="errorMessage" class="form-error">{{ errorMessage }}</p>
        <button class="solid-button" type="submit" :disabled="saving || !selectedProblemId">
          {{ saving ? '正在保存' : '保存测试数据' }}
        </button>
      </form>

      <section class="table-panel">
        <table v-if="testCases.length > 0">
          <thead>
            <tr>
              <th>ID</th>
              <th>类型</th>
              <th>排序</th>
              <th>输入摘要</th>
              <th>输出摘要</th>
              <th>可见性</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="testCase in testCases" :key="testCase.id">
              <td>#{{ testCase.id }}</td>
              <td><span class="status-pill">{{ testCaseKindLabel(testCase.kind) }}</span></td>
              <td>{{ testCase.sort_order }}</td>
              <td>{{ testCase.input_summary }}</td>
              <td>{{ testCase.output_summary }}</td>
              <td>{{ testCase.display_full_content ? '完整内容' : '仅摘要' }}</td>
            </tr>
          </tbody>
        </table>
        <EmptyState v-if="loading" title="正在加载题目" message="正在获取题目记录。" />
        <EmptyState
          v-if="!loading && !selectedProblem"
          title="未选择题目"
          message="添加测试数据前，请先创建题目。"
        />
        <EmptyState
          v-if="!loading && selectedProblem && testCases.length === 0"
          title="暂无测试数据"
          message="请在上方添加样例、公开或隐藏数据。"
        />
      </section>
    </section>
  </div>
</template>
