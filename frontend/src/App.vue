<script setup lang="ts">
import { computed } from 'vue';
import { RouterLink, RouterView, useRouter } from 'vue-router';
import { clearSession, session } from './stores/session';

const router = useRouter();

const primaryLinks = computed(() => [
  { to: '/', label: '首页' },
  { to: '/problems', label: '题库' },
  ...(session.role === 'guest'
    ? []
    : [
        { to: '/submissions', label: '提交记录' },
        { to: '/me/submissions', label: '我的提交' }
      ]),
  ...(session.role === 'admin' ? [{ to: '/admin/problems', label: '管理后台' }] : [])
]);

function logout() {
  clearSession();
  router.push('/login');
}
</script>

<template>
  <div class="app-shell">
    <header class="topbar">
      <RouterLink class="brand" to="/">AI NATIVE OJ</RouterLink>

      <nav class="primary-nav" aria-label="主导航">
        <RouterLink v-for="link in primaryLinks" :key="link.to" :to="link.to">
          {{ link.label }}
        </RouterLink>
      </nav>

      <div class="auth-actions">
        <template v-if="session.role === 'guest'">
          <RouterLink class="ghost-button" to="/login">登录</RouterLink>
          <RouterLink class="solid-button" to="/register">注册</RouterLink>
        </template>
        <template v-else>
          <span class="session-name">{{ session.username }}</span>
          <button class="ghost-button" type="button" @click="logout">退出</button>
        </template>
      </div>
    </header>

    <main class="main-content">
      <RouterView />
    </main>
  </div>
</template>
