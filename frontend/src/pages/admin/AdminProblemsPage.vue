<script setup lang="ts">
import { RouterLink } from 'vue-router';
import PageHeader from '../../components/PageHeader.vue';
</script>

<template>
  <div class="admin-layout">
    <aside class="admin-nav">
      <RouterLink to="/admin/problems">Problems</RouterLink>
      <RouterLink to="/admin/tags">Tags</RouterLink>
      <RouterLink to="/admin/difficulties">Difficulties</RouterLink>
      <RouterLink to="/admin/testdata">Test data</RouterLink>
      <RouterLink to="/admin/submissions">Submissions</RouterLink>
      <RouterLink to="/admin/workers">Workers</RouterLink>
      <RouterLink to="/admin/ai-logs">AI logs</RouterLink>
    </aside>

    <section class="page-stack">
      <PageHeader title="Problem management" description="Create, edit, publish, archive, and review problems." />
      <div class="content-panel">
        <button class="solid-button" type="button">New problem</button>
      </div>
    </section>
  </div>
</template>
