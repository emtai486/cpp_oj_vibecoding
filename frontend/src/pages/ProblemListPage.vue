<script setup lang="ts">
import EmptyState from '../components/EmptyState.vue';
import PageHeader from '../components/PageHeader.vue';
</script>

<template>
  <div class="page-stack">
    <PageHeader title="Problems" description="Browse published programming problems." />

    <section class="table-panel">
      <table>
        <thead>
          <tr>
            <th>Title</th>
            <th>Difficulty</th>
            <th>Acceptance</th>
            <th>Tags</th>
          </tr>
        </thead>
        <tbody></tbody>
      </table>
      <EmptyState title="No published problems" message="Published problems will appear here." />
    </section>
  </div>
</template>
