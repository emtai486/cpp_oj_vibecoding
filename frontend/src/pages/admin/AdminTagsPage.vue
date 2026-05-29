<script setup lang="ts">
import { onMounted, ref } from 'vue';
import { apiGet, apiPost } from '../../api/client';
import EmptyState from '../../components/EmptyState.vue';
import PageHeader from '../../components/PageHeader.vue';

interface TagRecord {
  id: number;
  name: string;
  created_at: string;
  problem_count: number;
}

const tags = ref<TagRecord[]>([]);
const name = ref('');
const loading = ref(true);
const saving = ref(false);
const errorMessage = ref<string | null>(null);

async function loadTags() {
  loading.value = true;
  errorMessage.value = null;
  try {
    const response = await apiGet<{ items: TagRecord[] }>('/api/admin/tags');
    tags.value = response.data.items;
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : '无法加载标签';
  } finally {
    loading.value = false;
  }
}

async function saveTag() {
  saving.value = true;
  errorMessage.value = null;
  try {
    await apiPost<TagRecord>('/api/admin/tags', { name: name.value });
    name.value = '';
    await loadTags();
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : '无法保存标签';
  } finally {
    saving.value = false;
  }
}

onMounted(loadTags);
</script>

<template>
  <div class="page-stack">
    <PageHeader title="标签管理" description="维护用于题目筛选的知识点标签。" />

    <form class="form-panel inline-form" @submit.prevent="saveTag">
      <label>
        标签名称
        <input v-model.trim="name" type="text" required />
      </label>
      <button class="solid-button" type="submit" :disabled="saving">{{ saving ? '正在保存' : '保存标签' }}</button>
      <p v-if="errorMessage" class="form-error">{{ errorMessage }}</p>
    </form>

    <section class="table-panel">
      <table v-if="tags.length > 0">
        <thead>
          <tr>
            <th>名称</th>
            <th>题目数</th>
            <th>创建时间</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="tag in tags" :key="tag.id">
            <td><span class="tag-pill">{{ tag.name }}</span></td>
            <td>{{ tag.problem_count }}</td>
            <td>{{ tag.created_at }}</td>
          </tr>
        </tbody>
      </table>
      <EmptyState v-if="loading" title="正在加载标签" message="正在获取标签记录。" />
      <EmptyState v-if="!loading && tags.length === 0" title="暂无标签" message="保存后的标签会显示在这里。" />
    </section>
  </div>
</template>
