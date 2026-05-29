<script setup lang="ts">
import PageHeader from '../components/PageHeader.vue';
</script>

<template>
  <div class="narrow-page">
    <PageHeader title="Log in" description="Access your judge workspace." />

    <form class="form-panel">
      <label>
        Email or username
        <input type="text" autocomplete="username" placeholder="name@example.com" />
      </label>
      <label>
        Password
        <input type="password" autocomplete="current-password" placeholder="Password" />
      </label>
      <button class="solid-button" type="button">Log in</button>
    </form>
  </div>
</template>
