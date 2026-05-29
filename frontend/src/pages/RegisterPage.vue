<script setup lang="ts">
import { ref } from 'vue';
import { useRouter } from 'vue-router';
import { apiPost } from '../api/client';
import PageHeader from '../components/PageHeader.vue';
import { setSession } from '../stores/session';
import type { AuthResponse } from '../types/problem';

const router = useRouter();

const username = ref('');
const email = ref('');
const password = ref('');
const loading = ref(false);
const errorMessage = ref<string | null>(null);

async function submit() {
  errorMessage.value = null;
  loading.value = true;
  try {
    const response = await apiPost<AuthResponse>('/api/auth/register', {
      username: username.value,
      email: email.value,
      password: password.value
    });
    setSession(response.data.token, response.data.user);
    await router.push('/problems');
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : '注册失败';
  } finally {
    loading.value = false;
  }
}
</script>

<template>
  <div class="narrow-page">
    <PageHeader title="注册" description="创建账号以提交代码并查看历史记录。" />

    <form class="form-panel" @submit.prevent="submit">
      <label>
        用户名
        <input v-model.trim="username" type="text" autocomplete="username" placeholder="solver42" required />
      </label>
      <label>
        邮箱
        <input v-model.trim="email" type="email" autocomplete="email" placeholder="name@example.com" required />
      </label>
      <label>
        密码
        <input v-model="password" type="password" autocomplete="new-password" placeholder="至少 8 位密码" required minlength="8" />
      </label>
      <p v-if="errorMessage" class="form-error">{{ errorMessage }}</p>
      <button class="solid-button" type="submit" :disabled="loading">
        {{ loading ? '正在创建' : '创建账号' }}
      </button>
    </form>
  </div>
</template>
