<script setup lang="ts">
import { RouterLink, RouterView } from 'vue-router';

const primaryLinks = [
  { to: '/', label: 'Home' },
  { to: '/problems', label: 'Problems' },
  { to: '/submissions', label: 'Submissions' },
  { to: '/me/submissions', label: 'My History' },
  { to: '/admin/problems', label: 'Admin' }
];
</script>

<template>
  <div class="app-shell">
    <header class="topbar">
      <RouterLink class="brand" to="/">AI Native OJ</RouterLink>

      <nav class="primary-nav" aria-label="Primary navigation">
        <RouterLink v-for="link in primaryLinks" :key="link.to" :to="link.to">
          {{ link.label }}
        </RouterLink>
      </nav>

      <div class="auth-actions">
        <RouterLink class="ghost-button" to="/login">Log in</RouterLink>
        <RouterLink class="solid-button" to="/register">Register</RouterLink>
      </div>
    </header>

    <main class="main-content">
      <RouterView />
    </main>
  </div>
</template>

