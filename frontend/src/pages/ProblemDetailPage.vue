<script setup lang="ts">
import { computed, onMounted, ref, watch } from 'vue';
import { useRoute } from 'vue-router';
import { apiGet } from '../api/client';
import PageHeader from '../components/PageHeader.vue';
import type { ProblemDetail } from '../types/problem';

const route = useRoute();
const problemId = computed(() => String(route.params.id ?? ''));
const problem = ref<ProblemDetail | null>(null);
const loading = ref(true);
const errorMessage = ref<string | null>(null);
const code = ref('');

function difficultyLabel(difficulty: ProblemDetail['difficulty']) {
  return {
    easy: '简单',
    medium: '中等',
    hard: '困难'
  }[difficulty];
}

function testCaseKindLabel(kind: 'sample' | 'public' | 'hidden') {
  return {
    sample: '样例',
    public: '公开',
    hidden: '隐藏'
  }[kind];
}

async function loadProblem() {
  loading.value = true;
  errorMessage.value = null;
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

onMounted(loadProblem);
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
    </section>

    <aside class="editor-panel">
      <div class="editor-toolbar">
        <span>C++17</span>
        <button type="button">运行</button>
        <button class="solid-button" type="button">提交</button>
      </div>
      <textarea v-model="code" class="code-editor" spellcheck="false" />
    </aside>
  </div>
</template>
