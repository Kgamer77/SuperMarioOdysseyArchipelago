<script setup lang="ts">
import { computed, ref } from 'vue';
import { AlertCircle, Info, TriangleAlert, Copy, Check } from '@lucide/vue';
import type { ValidationIssue } from '../lib/types';

const props = defineProps<{
  issues: ValidationIssue[];
  title?: string;
}>();

const emit = defineEmits<{
  focus: [issue: ValidationIssue];
}>();

const visibleIssues = computed(() => props.issues.slice(0, 160));
const copied = ref(false);

async function copyIssues() {
  const text = props.issues
    .map((i) => `[${i.severity.toUpperCase()}] ${i.table} - ${i.field}: ${i.message} (Row: ${i.rowId})`)
    .join('\n');
  try {
    await navigator.clipboard.writeText(text);
    copied.value = true;
    setTimeout(() => {
      copied.value = false;
    }, 2000);
  } catch (err) {
    console.error('Failed to copy issues', err);
  }
}
</script>

<template>
  <section class="validation-drawer">
    <div class="validation-head">
      <div>
        <h2>{{ title || 'Validation' }}</h2>
        <span style="font-size: 13px; color: #5a6b7c;">{{ issues.length }} issues</span>
      </div>
      <button v-if="issues.length > 0" type="button" class="icon-button" @click="copyIssues" title="Copy all issues to clipboard">
        <Check v-if="copied" :size="18" />
        <Copy v-else :size="18" />
      </button>
    </div>
    <div class="validation-list">
      <p v-if="issues.length === 0" class="empty-state">No issues found.</p>
      <article
        v-for="issue in visibleIssues"
        :key="`${issue.table}:${issue.rowId}:${issue.field}:${issue.message}`"
        :class="[issue.severity, 'issue-clickable']"
        role="button"
        tabindex="0"
        title="Show this on the graph"
        @click="emit('focus', issue)"
        @keydown.enter.prevent="emit('focus', issue)"
      >
        <AlertCircle v-if="issue.severity === 'error'" :size="16" />
        <TriangleAlert v-else-if="issue.severity === 'warning'" :size="16" />
        <Info v-else :size="16" />
        <div>
          <strong>{{ issue.table }} - {{ issue.field }}</strong>
          <p>{{ issue.message }}</p>
          <small>{{ issue.rowId }}</small>
        </div>
      </article>
      <p v-if="issues.length > visibleIssues.length" class="issue-overflow">
        Showing first {{ visibleIssues.length }} issues.
      </p>
    </div>
  </section>
</template>
