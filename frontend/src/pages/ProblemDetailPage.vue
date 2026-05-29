<script setup lang="ts">
import { computed } from 'vue';
import { useRoute } from 'vue-router';
import PageHeader from '../components/PageHeader.vue';

const route = useRoute();
const problemId = computed(() => String(route.params.id ?? ''));
</script>

<template>
  <div class="problem-layout">
    <section class="problem-statement">
      <PageHeader
        :title="`Problem #${problemId}`"
        description="Read the statement, run samples, and submit C++17 code."
      />

      <div class="content-panel">
        <h2>Statement unavailable</h2>
        <p>This problem is not published yet.</p>
      </div>
    </section>

    <aside class="editor-panel">
      <div class="editor-toolbar">
        <span>C++17</span>
        <button type="button">Run</button>
        <button class="solid-button" type="button">Submit</button>
      </div>
      <pre><code>#include &lt;bits/stdc++.h&gt;
using namespace std;

int main() {
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    return 0;
}</code></pre>
    </aside>
  </div>
</template>
