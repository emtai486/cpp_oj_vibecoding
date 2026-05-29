<script setup lang="ts">
import EmptyState from '../components/EmptyState.vue';
import PageHeader from '../components/PageHeader.vue';
</script>

<template>
  <div class="page-stack">
    <PageHeader title="我的提交历史" description="查看已通过、未通过和等待中的提交。" />
    <EmptyState title="暂无提交历史" message="你提交代码后，记录会显示在这里。" />
  </div>
</template>
