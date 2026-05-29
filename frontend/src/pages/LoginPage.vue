<script setup lang="ts">
import { ref } from 'vue';
import { useRoute, useRouter } from 'vue-router';
import { apiPost } from '../api/client';
import PageHeader from '../components/PageHeader.vue';
import { setSession } from '../stores/session';
import type { AuthResponse } from '../types/problem';

const router = useRouter();
const route = useRoute();

const login = ref('');
const password = ref('');
const loading = ref(false);
const errorMessage = ref<string | null>(null);

async function submit() {
  errorMessage.value = null;
  loading.value = true;
  try {
    const response = await apiPost<AuthResponse>('/api/auth/login', {
      login: login.value,
      password: password.value
    });
    setSession(response.data.token, response.data.user);
    const redirect = typeof route.query.redirect === 'string' ? route.query.redirect : '/problems';
    await router.push(redirect);
  } catch (error) {
    errorMessage.value = error instanceof Error ? error.message : '登录失败';
  } finally {
    loading.value = false;
  }
}
</script>

<template>
  <div class="narrow-page">
    <PageHeader title="登录" description="进入你的在线判题工作台。" />

    <form class="form-panel" @submit.prevent="submit">
      <label>
        邮箱或用户名
        <input v-model.trim="login" type="text" autocomplete="username" placeholder="name@example.com" required />
      </label>
      <label>
        密码
        <input v-model="password" type="password" autocomplete="current-password" placeholder="请输入密码" required />
      </label>
      <p v-if="errorMessage" class="form-error">{{ errorMessage }}</p>
      <button class="solid-button" type="submit" :disabled="loading">
        {{ loading ? '正在登录' : '登录' }}
      </button>
    </form>
  </div>
</template>
