<script setup lang="ts">
import EmptyState from '../components/EmptyState.vue';
import PageHeader from '../components/PageHeader.vue';
</script>

<template>
  <div class="page-stack">
    <PageHeader title="Submissions" description="Review judge status, runtime, memory, and result summaries." />
    <EmptyState title="No submissions" message="Submission records will appear here." />
  </div>
</template>
