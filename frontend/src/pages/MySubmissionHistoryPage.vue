<script setup lang="ts">
import EmptyState from '../components/EmptyState.vue';
import PageHeader from '../components/PageHeader.vue';
</script>

<template>
  <div class="page-stack">
    <PageHeader title="My submission history" description="Review your accepted, failed, and pending attempts." />
    <EmptyState title="No submission history" message="Your submissions will appear here after you send code for judging." />
  </div>
</template>
