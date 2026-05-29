<script setup lang="ts">
import EmptyState from '../components/EmptyState.vue';
import PageHeader from '../components/PageHeader.vue';
</script>

<template>
  <div class="page-stack">
    <PageHeader title="提交记录" description="查看判题状态、运行时间、内存和结果摘要。" />
    <EmptyState title="暂无提交" message="提交记录会显示在这里。" />
  </div>
</template>
