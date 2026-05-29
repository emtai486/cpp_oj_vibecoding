<script setup lang="ts">
import PageHeader from '../components/PageHeader.vue';
</script>

<template>
  <div class="narrow-page">
    <PageHeader title="Register" description="Create an account for submissions and history." />

    <form class="form-panel">
      <label>
        Username
        <input type="text" autocomplete="username" placeholder="solver42" />
      </label>
      <label>
        Email
        <input type="email" autocomplete="email" placeholder="name@example.com" />
      </label>
      <label>
        Password
        <input type="password" autocomplete="new-password" placeholder="Password" />
      </label>
      <button class="solid-button" type="button">Create account</button>
    </form>
  </div>
</template>
