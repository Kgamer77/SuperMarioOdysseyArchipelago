<script setup lang="ts">
import { computed } from 'vue';
import { Download, FileInput, FolderOpen, Plus, Save, Upload } from '@lucide/vue';
import { listRoomsForKingdom, roomLabel } from '../lib/workspace';
import type { IssueCountSummary, LogicWorkspace, Room } from '../lib/types';

const props = defineProps<{
  workspace: LogicWorkspace;
  selectedKingdom: string;
  selectedStage: string;
  verificationCounts: Record<string, IssueCountSummary>;
}>();

const emit = defineEmits<{
  selectKingdom: [kingdom: string];
  selectStage: [stage: string];
  addNode: [];
  saveSnapshot: [];
  loadSnapshot: [];
  exportProject: [];
  exportCsv: [];
  importProject: [file: File];
}>();

const kingdoms = computed(() =>
  [...new Set(props.workspace.rooms.map((room) => room.kingdom).filter(Boolean))].sort()
);

const rooms = computed<Room[]>(() => listRoomsForKingdom(props.workspace, props.selectedKingdom));

function roomCounts(stage: string): IssueCountSummary {
  return props.verificationCounts[stage] ?? { error: 0, warning: 0, info: 0 };
}

// Memoized: one O(rooms) pass that fills a per-kingdom map, instead of the
// previous plain function which re-filtered + re-reduced the *entire* rooms
// list once per kingdom, every single render (O(kingdoms x rooms), and
// uncached since it was called directly from the template). This now only
// recomputes when workspace.rooms or verificationCounts actually change.
const kingdomCountsMap = computed(() => {
  const map = new Map<string, IssueCountSummary>();
  for (const room of props.workspace.rooms) {
    const summary = map.get(room.kingdom) ?? { error: 0, warning: 0, info: 0 };
    const counts = roomCounts(room.stage);
    summary.error += counts.error;
    summary.warning += counts.warning;
    summary.info += counts.info;
    map.set(room.kingdom, summary);
  }
  return map;
});

function kingdomCounts(kingdom: string): IssueCountSummary {
  return kingdomCountsMap.value.get(kingdom) ?? { error: 0, warning: 0, info: 0 };
}

function badgeText(counts: IssueCountSummary): string {
  if (counts.error > 0) {
    return `${counts.error}E`;
  }
  if (counts.warning > 0) {
    return `${counts.warning}W`;
  }
  if (counts.info > 0) {
    return `${counts.info}I`;
  }
  return 'OK';
}

function badgeClass(counts: IssueCountSummary): string {
  if (counts.error > 0) {
    return 'error';
  }
  if (counts.warning > 0) {
    return 'warning';
  }
  if (counts.info > 0) {
    return 'info';
  }
  return 'ok';
}

function badgeTitle(counts: IssueCountSummary): string {
  return `${counts.error} errors, ${counts.warning} warnings, ${counts.info} info`;
}

function handleProjectImport(event: Event) {
  const input = event.target as HTMLInputElement;
  const file = input.files?.[0];
  if (file) {
    emit('importProject', file);
  }
  input.value = '';
}
</script>

<template>
  <aside class="sidebar">
    <div class="brand">
      <div>
        <h1>Logic Graph</h1>
        <p>{{ workspace.logicNodes.length }} nodes - {{ workspace.routeEdges.length }} routes</p>
      </div>
    </div>

    <div class="sidebar-actions">
      <button type="button" title="Save the selected kingdom layout" @click="emit('saveSnapshot')">
        <Save :size="17" />
        Save
      </button>
      <button type="button" title="Load the selected kingdom layout" @click="emit('loadSnapshot')">
        <FolderOpen :size="17" />
        Load
      </button>
      <button type="button" title="Download project JSON" @click="emit('exportProject')">
        <Download :size="17" />
        JSON
      </button>
      <button type="button" title="Download edited CSV files" @click="emit('exportCsv')">
        <Upload :size="17" />
        CSV
      </button>
      <label class="file-button" title="Import project JSON">
        <FileInput :size="17" />
        Import
        <input type="file" accept="application/json,.json" @change="handleProjectImport" />
      </label>
    </div>

    <button type="button" class="add-node" @click="emit('addNode')">
      <Plus :size="17" />
      Add Place
    </button>

    <div class="section-title">Kingdoms</div>
    <nav class="kingdom-list">
      <button
        v-for="kingdom in kingdoms"
        :key="kingdom"
        type="button"
        :class="{ active: kingdom === selectedKingdom }"
        @click="emit('selectKingdom', kingdom)"
      >
        <span>{{ kingdom }}</span>
        <strong
          class="verify-badge"
          :class="badgeClass(kingdomCounts(kingdom))"
          :title="badgeTitle(kingdomCounts(kingdom))"
        >
          {{ badgeText(kingdomCounts(kingdom)) }}
        </strong>
      </button>
    </nav>

    <div class="section-title">Areas</div>
    <nav class="room-list">
      <button
        v-for="room in rooms"
        :key="room.stage"
        type="button"
        :class="{ active: room.stage === selectedStage }"
        :title="room.stage"
        @click="emit('selectStage', room.stage)"
      >
        <span class="room-name">{{ roomLabel(room) }}</span>
        <span class="room-meta">
          <small>{{ room.area_scope }}</small>
          <strong
            class="verify-badge"
            :class="badgeClass(roomCounts(room.stage))"
            :title="badgeTitle(roomCounts(room.stage))"
          >
            {{ badgeText(roomCounts(room.stage)) }}
          </strong>
        </span>
      </button>
    </nav>
  </aside>
</template>