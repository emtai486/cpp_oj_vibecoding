<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { RouterLink } from 'vue-router';
import { apiGet, apiPost, apiPut } from '../../api/client';
import EmptyState from '../../components/EmptyState.vue';
import PageHeader from '../../components/PageHeader.vue';
import type { PageResponse, ProblemDetail, ProblemDifficulty, ProblemStatus, ProblemSummary } from '../../types/problem';

interface ProblemForm {
  id: number | null;
  title: string;
  description: string;
  difficulty: ProblemDifficulty;
  tagsText: string;
  time_limit_ms: number;
  memory_limit_mb: number;
  default_code_template: string;
  source: string;
  status: ProblemStatus;
}

const defaultTemplate = `#include <bits/stdc++.h>
using namespace std;

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    return 0;
}
`;

const blankForm: ProblemForm = {
  id: null,
  title: '',
  description: '',
  difficulty: 'easy',
  tagsText: '',
  time_limit_ms: 2000,
  memory_limit_mb: 256,
  default_code_template: defaultTemplate,
  source: '',
  status: 'draft'
};

const problems = ref<ProblemSummary[]>([]);
const loading = ref(true);
const saving = ref(false);
const errorMessage = ref<string | null>(null);
const notice = ref<string | null>(null);
const form = ref<ProblemForm>({ ...blankForm });

const formTitle = computed(() => (form.value.id ? `编辑 #${form.value.id}` : '新建题目'));

function difficultyLabel(difficulty: ProblemDifficulty) {
  return {
    easy: '简单',
    medium: '中等',
    hard: '困难'
  }[difficulty];
}

function statusLabel(status: ProblemStatus) {
  return {
    draft: '草稿',
    published: '已发布',
    archived: '已归档'
  }[status];
}

function resetForm() {
  form.value = { ...blankForm };
  notice.value = null;
}

function payloadFromForm() {
  return {
    title: form.value.title,
    description: form.value.description,
    difficulty: form.value.difficulty,
    tags: form.value.tagsText
      .split(',')
      .map((tag) => tag.trim())
      .filter(Boolean),
    time_limit_ms: Number(form.value.time_limit_ms),
    memory_limit_mb: Number(form.value.memory_limit_mb),
    default_code_template: form.value.default_code_template,
    source: form.value.source,
    status: form.value.status
  };
}

async function loadProblems() {
  loading.value = true;
  errorMessage.value = null;
  try {
    const response = await apiGet<PageResponse<ProblemSummary>>('/api/admin/problems?page=1&page_size=100');
    problems.value = response.data.items;
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : '无法加载题目';
  } finally {
    loading.value = false;
  }
}

async function editProblem(problem: ProblemSummary) {
  errorMessage.value = null;
  notice.value = null;
  try {
    const response = await apiGet<ProblemDetail>(`/api/admin/problems/${problem.id}`);
    form.value = {
      id: response.data.id,
      title: response.data.title,
      description: response.data.description,
      difficulty: response.data.difficulty,
      tagsText: response.data.tags.join(', '),
      time_limit_ms: response.data.time_limit_ms,
      memory_limit_mb: response.data.memory_limit_mb,
      default_code_template: response.data.default_code_template,
      source: response.data.source,
      status: response.data.status
    };
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : '无法加载题目';
  }
}

async function saveProblem() {
  saving.value = true;
  errorMessage.value = null;
  notice.value = null;
  try {
    if (form.value.id) {
      await apiPut<ProblemDetail>(`/api/admin/problems/${form.value.id}`, payloadFromForm());
      notice.value = '题目已更新。';
    } else {
      const response = await apiPost<ProblemDetail>('/api/admin/problems', payloadFromForm());
      form.value.id = response.data.id;
      notice.value = '题目已创建。';
    }
    await loadProblems();
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : '无法保存题目';
  } finally {
    saving.value = false;
  }
}

onMounted(loadProblems);
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
      <PageHeader title="题目管理" description="创建、编辑、发布、归档并查看题目。" />

      <form class="form-panel admin-form" @submit.prevent="saveProblem">
        <div class="section-title-row">
          <h2>{{ formTitle }}</h2>
          <button class="ghost-button" type="button" @click="resetForm">新建</button>
        </div>

        <label>
          标题
          <input v-model.trim="form.title" type="text" required />
        </label>

        <label>
          题面
          <textarea v-model="form.description" class="text-area tall" required />
        </label>

        <div class="form-grid">
          <label>
            难度
            <select v-model="form.difficulty">
              <option value="easy">简单</option>
              <option value="medium">中等</option>
              <option value="hard">困难</option>
            </select>
          </label>
          <label>
            状态
            <select v-model="form.status">
              <option value="draft">草稿</option>
              <option value="published">已发布</option>
              <option value="archived">已归档</option>
            </select>
          </label>
          <label>
            时间限制 ms
            <input v-model.number="form.time_limit_ms" type="number" min="100" max="60000" />
          </label>
          <label>
            内存限制 MB
            <input v-model.number="form.memory_limit_mb" type="number" min="16" max="4096" />
          </label>
        </div>

        <label>
          标签
          <input v-model="form.tagsText" type="text" placeholder="数组, 动态规划" />
        </label>

        <label>
          来源
          <input v-model="form.source" type="text" placeholder="原创, LeetCode 风格, 内部题库" />
        </label>

        <label>
          默认 C++17 模板
          <textarea v-model="form.default_code_template" class="text-area code-area" spellcheck="false" />
        </label>

        <p v-if="notice" class="form-success">{{ notice }}</p>
        <p v-if="errorMessage" class="form-error">{{ errorMessage }}</p>
        <button class="solid-button" type="submit" :disabled="saving">{{ saving ? '正在保存' : '保存题目' }}</button>
      </form>

      <section class="table-panel">
        <table v-if="problems.length > 0">
          <thead>
            <tr>
              <th>ID</th>
              <th>标题</th>
              <th>难度</th>
              <th>状态</th>
              <th>标签</th>
              <th>操作</th>
            </tr>
          </thead>
          <tbody>
            <tr v-for="problem in problems" :key="problem.id">
              <td>#{{ problem.id }}</td>
              <td>{{ problem.title }}</td>
              <td><span class="difficulty-pill" :class="problem.difficulty">{{ difficultyLabel(problem.difficulty) }}</span></td>
              <td><span class="status-pill">{{ statusLabel(problem.status) }}</span></td>
              <td>
                <span v-for="tag in problem.tags" :key="tag" class="tag-pill">{{ tag }}</span>
                <span v-if="problem.tags.length === 0" class="muted-text">暂无标签</span>
              </td>
              <td><button class="ghost-button" type="button" @click="editProblem(problem)">编辑</button></td>
            </tr>
          </tbody>
        </table>
        <EmptyState v-if="loading" title="正在加载题目" message="正在获取管理端题目记录。" />
        <EmptyState v-if="!loading && problems.length === 0" title="暂无题目" message="请先在上方创建第一道题目。" />
      </section>
    </section>
  </div>
</template>
