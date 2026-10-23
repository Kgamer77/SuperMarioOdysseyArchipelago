<script setup lang="ts">
import { computed, nextTick, onMounted, reactive, ref, watch } from 'vue';
import { AlertTriangle, Cable, CheckCircle2, Eye, EyeOff, LayoutGrid, Map, RotateCcw, Search, ShieldCheck, X, Moon, Sun } from '@lucide/vue';
import GraphCanvas from './components/GraphCanvas.vue';
import CaptureGateEditor from './components/CaptureGateEditor.vue';
import InspectorPanel from './components/InspectorPanel.vue';
import Sidebar from './components/Sidebar.vue';
import ValidationDrawer from './components/ValidationDrawer.vue';
import { createDefaultWorkspace } from './data/defaultData';
import {
  connectMoonToNode,
  createCustomNode,
  createReverseEdge,
  createRouteEdge,
  arrangeLayoutForStage,
  arrangeMapLayoutForStage,
  deleteCustomNode,
  deleteRouteConnection,
  findEdge,
  findMoon,
  findNode,
  findRouteConnection,
  getGraphForStage,
  getRoom,
  listKingdoms,
  listRoomsForKingdom,
  moonNodeId,
  moonRowKey,
  resetStageFromTemplate,
  routeConnectionId,
  mergeKingdomWorkspace,
  syncWorkspaceWithTemplate
} from './lib/workspace';
import {
  downloadEditableCsvs,
  downloadProject,
  loadKingdomSnapshot,
  loadAutosave,
  parseProject,
  saveAutosave,
  saveKingdomSnapshot
} from './lib/persistence';
import { computeKingdomReachableNodes, issueCounts, validateWorkspace, verifyStageMap } from './lib/validation';
import type { GraphFilters, IssueCountSummary, Position, Selection, ValidationIssue } from './lib/types';

const workspace = ref(createDefaultWorkspace());
const selectedKingdom = ref('');
const selectedStage = ref('');
const selection = ref<Selection>({ kind: 'none' });
const graphCanvas = ref<InstanceType<typeof GraphCanvas> | null>(null);
const editorMode = ref<'logic' | 'capture'>('logic');
// Transient "flash this node" target for the canvas; nonce retriggers the pulse
// even when the same node is clicked again.
const focusTarget = ref<{ id: string; nonce: number } | null>(null);
let focusNonce = 0;
const connectSource = ref('');
const sidebarWidth = ref(284);
const showValidation = ref(false);
const stageVerificationIssues = ref<ValidationIssue[] | null>(null);
const toast = ref('');
const filters = reactive<GraphFilters>({
  showExcluded: false,
  showMoons: true,
  showEvents: true,
  showRawAnchors: false,
  search: ''
});

const isDark = ref(localStorage.getItem('theme') === 'dark');

watch(isDark, (dark) => {
  if (dark) {
    document.documentElement.classList.add('dark');
    localStorage.setItem('theme', 'dark');
  } else {
    document.documentElement.classList.remove('dark');
    localStorage.setItem('theme', 'light');
  }
}, { immediate: true });

function toggleDarkTheme() {
  isDark.value = !isDark.value;
}

const graph = computed(() => getGraphForStage(workspace.value, selectedStage.value, filters));

// validateWorkspace() and the per-room verifyStageMap() loop below both walk the
// *entire* game's worth of nodes/edges/moons (every kingdom, every room), not just
// what's on screen. As plain computed()s they used to re-run synchronously on every
// single keystroke/click anywhere in the app (any nested workspace mutation is a
// tracked dependency), which is what made small edits like toggling a token chip
// feel like a multi-second freeze. They're now plain refs that only get recomputed
// by a debounced scheduler (see scheduleValidation below), so rapid edits coalesce
// into one recompute shortly after you stop interacting instead of one per click.
const issues = ref<ValidationIssue[]>([]);
const stageVerificationCounts = ref<Record<string, IssueCountSummary>>({});

function recomputeValidation() {
  issues.value = validateWorkspace(workspace.value);

  const result: Record<string, IssueCountSummary> = {};
  // Compute kingdom reachability once and share it across every map so the badge
  // counts match the Map Verification panel (both run verifyStageMap).
  const kingdomReachable = computeKingdomReachableNodes(workspace.value);
  for (const room of workspace.value.rooms) {
    result[room.stage] = issueCounts(
      verifyStageMap(workspace.value, room.stage, { ...verificationOptions(), kingdomReachable })
    );
  }
  stageVerificationCounts.value = result;
}

let validationTimer: number | undefined;
function scheduleValidation() {
  window.clearTimeout(validationTimer);
  validationTimer = window.setTimeout(recomputeValidation, 400);
}

const displayedIssues = computed(() => stageVerificationIssues.value ?? issues.value);
const validationTitle = computed(() =>
  stageVerificationIssues.value ? `Map Verification: ${selectedStage.value}` : 'Validation'
);
const counts = computed(() => issueCounts(displayedIssues.value));
const selectedId = computed(() => {
  if (selection.value.kind === 'none') {
    return '';
  }
  if (selection.value.kind === 'moon') {
    return moonNodeId(selection.value.id);
  }
  return selection.value.id;
});

let autosaveTimer: number | undefined;

function scheduleAutosave() {
  window.clearTimeout(autosaveTimer);
  autosaveTimer = window.setTimeout(() => saveAutosave(workspace.value), 450);
}

// The previous fix (debouncing the *heavy computed recalculation*) still left a
// `watch(workspace, ..., { deep: true })` in place. A deep watch on the whole
// workspace has to walk every field of every node/edge/moon in the *entire game*
// on every single edit just to notice something changed — that full-tree walk is
// what was still eating ~2s per click, even with the recompute itself debounced.
//
// Almost every edit happens in one of two ways:
//  1. Structural changes (add/delete/connect/move/reset/import/load) — these all
//     already go through named functions below, so they can just call
//     onWorkspaceMutated() directly. No watcher needed for these at all.
//  2. In-place field edits (name, notes, requirement, difficulty, effort, ...) via
//     v-model in the Inspector/RequirementEditor — these mutate whatever row is
//     currently selected. Instead of deep-watching the whole game, we only need to
//     deep-watch the one (or two, for a route pair) row objects currently open in
//     the Inspector. That's a handful of fields, not the whole dataset, so it's
//     effectively instant regardless of how large the workspace is.
function onWorkspaceMutated() {
  const keepStageVerificationOpen = stageVerificationIssues.value !== null && Boolean(selectedStage.value);
  stageVerificationIssues.value = keepStageVerificationOpen ? verifySelectedStage() : null;
  scheduleAutosave();
  scheduleValidation();
}

const editableRows = computed(() => {
  if (selection.value.kind === 'node') {
    const node = findNode(workspace.value, selection.value.id);
    return node ? [node] : [];
  }
  if (selection.value.kind === 'edge') {
    const connection = findRouteConnection(workspace.value, selection.value.id);
    if (!connection) {
      return [];
    }
    return connection.reverse ? [connection.forward, connection.reverse] : [connection.forward];
  }
  if (selection.value.kind === 'moon') {
    const moon = findMoon(workspace.value, selection.value.id);
    return moon ? [moon] : [];
  }
  return [];
});

watch(editableRows, onWorkspaceMutated, { deep: true });

onMounted(() => {
  try {
    const autosave = loadAutosave();
    if (autosave) {
      workspace.value = syncWorkspaceWithTemplate(autosave.workspace, createDefaultWorkspace());
      notify('Autosave restored and synced with latest templates.');
    }
  } catch {
    notify('Autosave was ignored because it could not be read.');
  }

  chooseInitialArea();
  recomputeValidation();
});

watch(selectedKingdom, () => {
  const firstRoom = listRoomsForKingdom(workspace.value, selectedKingdom.value)[0];
  if (firstRoom) {
    selectedStage.value = firstRoom.stage;
  }
  selection.value = { kind: 'none' };
});

watch(selectedStage, () => {
  const keepStageVerificationOpen = stageVerificationIssues.value !== null && Boolean(selectedStage.value);
  selection.value = { kind: 'none' };
  stageVerificationIssues.value = keepStageVerificationOpen
    ? verifySelectedStage()
    : null;
});

watch(
  () => filters.showRawAnchors,
  () => {
    if (stageVerificationIssues.value !== null && selectedStage.value) {
      stageVerificationIssues.value = verifySelectedStage();
    }
  }
);

function chooseInitialArea() {
  const kingdoms = listKingdoms(workspace.value);
  selectedKingdom.value = selectedKingdom.value && kingdoms.includes(selectedKingdom.value)
    ? selectedKingdom.value
    : kingdoms[0] ?? '';

  const rooms = listRoomsForKingdom(workspace.value, selectedKingdom.value);
  selectedStage.value = selectedStage.value && rooms.some((room) => room.stage === selectedStage.value)
    ? selectedStage.value
    : rooms[0]?.stage ?? '';
}

function notify(message: string) {
  toast.value = message;
  window.setTimeout(() => {
    if (toast.value === message) {
      toast.value = '';
    }
  }, 2800);
}

function verificationOptions() {
  return { includeRawAnchors: filters.showRawAnchors };
}

function verifySelectedStage(): ValidationIssue[] {
  return verifyStageMap(workspace.value, selectedStage.value, verificationOptions());
}

function selectGraphItem(kind: 'node' | 'edge' | 'moon', id: string) {
  if (kind === 'edge' && id.startsWith('moon_edge:')) {
    selection.value = { kind: 'moon', id: id.replace('moon_edge:', '') };
    return;
  }

  if (kind === 'moon') {
    selection.value = { kind: 'moon', id: id.replace(/^moon:/, '') };
    return;
  }

  selection.value = { kind, id };
}

// Clicking a validation issue jumps to and flashes the related graph element:
// switches kingdom/map if needed, selects it, and pulses the node on the canvas.
async function focusIssue(issue: ValidationIssue) {
  let targetStage = '';
  let targetKingdom = '';
  let sel: Selection = { kind: 'none' };
  let flashNodeId = '';

  if (issue.table === 'moonAccess') {
    const moon = findMoon(workspace.value, issue.rowId);
    if (moon) {
      targetStage = moon.stage;
      targetKingdom = moon.kingdom;
      sel = { kind: 'moon', id: moonRowKey(moon) };
      flashNodeId = moonNodeId(moonRowKey(moon));
      filters.showMoons = true;
    }
  } else if (issue.table === 'routeEdges') {
    const edge = findEdge(workspace.value, issue.rowId);
    const endpoint = edge && (findNode(workspace.value, edge.from_node_id) ?? findNode(workspace.value, edge.to_node_id));
    if (edge && endpoint) {
      targetStage = endpoint.stage;
      targetKingdom = endpoint.kingdom;
      sel = { kind: 'edge', id: routeConnectionId(edge.from_node_id, edge.to_node_id) };
      flashNodeId = endpoint.node_id;
    }
  } else if (issue.table === 'captureGates' || issue.table === 'captureGateObjects') {
    const gate = workspace.value.captureGates.find((row) =>
      row.gate_id === issue.rowId || row.edge_id === issue.rowId
    );
    if (gate) {
      targetStage = gate.logic_stage;
      targetKingdom = gate.kingdom;
      editorMode.value = 'capture';
    } else {
      const edge = findEdge(workspace.value, issue.rowId);
      const endpoint = edge && findNode(workspace.value, edge.from_node_id);
      if (edge && endpoint) {
        targetStage = endpoint.stage;
        targetKingdom = edge.kingdom;
        editorMode.value = 'capture';
      }
    }
  } else {
    const node = findNode(workspace.value, issue.rowId);
    if (node) {
      targetStage = node.stage;
      targetKingdom = node.kingdom;
      sel = { kind: 'node', id: node.node_id };
      flashNodeId = node.node_id;
      if (isExcludedNodeRow(node)) {
        filters.showExcluded = true;
      }
    } else {
      // Stage-level issue (rowId is a stage): just navigate to that map.
      const room = getRoom(workspace.value, issue.rowId);
      if (room) {
        targetStage = room.stage;
        targetKingdom = room.kingdom;
      }
    }
  }

  if (!targetStage) {
    return;
  }

  if (targetKingdom && selectedKingdom.value !== targetKingdom) {
    selectedKingdom.value = targetKingdom;
    await nextTick();
  }
  if (selectedStage.value !== targetStage) {
    selectedStage.value = targetStage;
    await nextTick();
  }

  selection.value = sel;
  if (flashNodeId) {
    await nextTick();
    focusTarget.value = { id: flashNodeId, nonce: ++focusNonce };
  }
}

function isExcludedNodeRow(node: { node_id: string; status?: string }): boolean {
  return node.status === 'excluded';
}

function activateNode(id: string) {
  if (!connectSource.value) {
    return;
  }

  const room = getRoom(workspace.value, selectedStage.value);
  if (!room) {
    return;
  }

  if (id === connectSource.value) {
    notify('Pick a different target node.');
    return;
  }

  if (id.startsWith('moon:')) {
    const moon = connectMoonToNode(workspace.value, id, connectSource.value);
    connectSource.value = '';
    if (moon) {
      selection.value = { kind: 'moon', id: moonRowKey(moon) };
      notify('Moon access connected.');
      onWorkspaceMutated();
    }
    return;
  }

  const edge = createRouteEdge(workspace.value, connectSource.value, id, room);
  connectSource.value = '';
  selection.value = { kind: 'edge', id: routeConnectionId(edge.from_node_id, edge.to_node_id) };
  notify('Connection created.');
  onWorkspaceMutated();
}

function updateNodePosition(id: string, position: Position) {
  workspace.value.nodePositions[id] = position;
  scheduleAutosave();
}

function addPlace() {
  const room = getRoom(workspace.value, selectedStage.value);
  if (!room) {
    return;
  }

  const node = createCustomNode(workspace.value, room);
  filters.search = '';
  selection.value = { kind: 'node', id: node.node_id };
  notify('Place added.');
  onWorkspaceMutated();
}

function startConnect(nodeId: string) {
  connectSource.value = nodeId;
  selection.value = { kind: 'node', id: nodeId };
  notify('Choose a target node or moon.');
}

function createReverse(connectionId: string) {
  const connection = findRouteConnection(workspace.value, connectionId);
  if (!connection || connection.reverse) {
    return;
  }

  const reverse = createReverseEdge(workspace.value, connection.forward);
  selection.value = { kind: 'edge', id: routeConnectionId(reverse.from_node_id, reverse.to_node_id) };
  notify('Reverse route created with its own requirements.');
  onWorkspaceMutated();
}

function removeEdge(connectionId: string) {
  if (!window.confirm('Delete this connection and all direction rows?')) {
    return;
  }

  if (deleteRouteConnection(workspace.value, connectionId)) {
    selection.value = { kind: 'none' };
    notify('Connection deleted.');
    onWorkspaceMutated();
  }
}

function removeNode(nodeId: string) {
  if (!window.confirm('Delete this place and its routes?')) {
    return;
  }

  if (deleteCustomNode(workspace.value, nodeId)) {
    selection.value = { kind: 'none' };
    notify('Place deleted.');
    onWorkspaceMutated();
  }
}

function verifyCurrentStage() {
  stageVerificationIssues.value = verifySelectedStage();
  showValidation.value = true;

  const stageCounts = issueCounts(stageVerificationIssues.value);
  if (stageCounts.error === 0 && stageCounts.warning === 0) {
    notify('Map verification passed.');
    return;
  }

  notify(`Map verification found ${stageCounts.error} errors and ${stageCounts.warning} warnings.`);
}

function toggleWorkspaceValidation() {
  stageVerificationIssues.value = null;
  showValidation.value = !showValidation.value;
}

async function arrangeCurrentStage() {
  arrangeLayoutForStage(workspace.value, selectedStage.value);
  scheduleAutosave();
  await nextTick();
  graphCanvas.value?.fitLayout();
  notify('Area arranged into related clusters.');
}

async function arrangeCurrentStageAsMap() {
  arrangeMapLayoutForStage(workspace.value, selectedStage.value);
  scheduleAutosave();
  await nextTick();
  graphCanvas.value?.fitLayout();
  notify('Area arranged using extracted coordinates.');
}

async function saveSnapshot() {
  try {
    const fileName = await saveKingdomSnapshot(workspace.value, selectedKingdom.value);
    notify(`Saved ${selectedKingdom.value} to ${fileName}.`);
  } catch (error) {
    notify((error as Error).message);
  }
}

async function loadSnapshot() {
  try {
    const project = await loadKingdomSnapshot(selectedKingdom.value);
    const loadedWorkspace = syncWorkspaceWithTemplate(project.workspace, createDefaultWorkspace());
    workspace.value = mergeKingdomWorkspace(workspace.value, loadedWorkspace, selectedKingdom.value);
    chooseInitialArea();
    recomputeValidation();
    scheduleAutosave();
    notify(`Loaded ${selectedKingdom.value} and synced with latest templates.`);
  } catch (error) {
    notify((error as Error).message);
  }
}

async function importProject(file: File) {
  try {
    const project = parseProject(await file.text());
    workspace.value = syncWorkspaceWithTemplate(project.workspace, createDefaultWorkspace());
    chooseInitialArea();
    recomputeValidation();
    scheduleAutosave();
    notify(`Imported ${file.name} and synced with latest templates.`);
  } catch (error) {
    notify((error as Error).message);
  }
}

function startResize(event: MouseEvent) {
  event.preventDefault();
  const startX = event.clientX;
  const startWidth = sidebarWidth.value;

  function onMove(e: MouseEvent) {
    const next = startWidth + (e.clientX - startX);
    sidebarWidth.value = Math.max(200, Math.min(next, 640));
  }

  function onUp() {
    document.removeEventListener('mousemove', onMove);
    document.removeEventListener('mouseup', onUp);
  }

  document.addEventListener('mousemove', onMove);
  document.addEventListener('mouseup', onUp);
}

function resetCurrentMap() {
  if (!selectedStage.value) {
    return;
  }

  if (!window.confirm(`Reset only ${selectedStage.value} to the generated template data?`)) {
    return;
  }

  const template = createDefaultWorkspace();
  if (!resetStageFromTemplate(workspace.value, template, selectedStage.value)) {
    notify('No template data found for this map.');
    return;
  }

  selection.value = { kind: 'none' };
  connectSource.value = '';
  filters.search = '';
  notify('Current map restored from template data.');
  onWorkspaceMutated();
}

</script>

<template>
  <div class="app-shell" :style="{ '--sidebar-width': `${sidebarWidth}px` }">
    <Sidebar
      :workspace="workspace"
      :selected-kingdom="selectedKingdom"
      :selected-stage="selectedStage"
      :verification-counts="stageVerificationCounts"
      @select-kingdom="selectedKingdom = $event"
      @select-stage="selectedStage = $event"
      @add-node="addPlace"
      @save-snapshot="saveSnapshot"
      @load-snapshot="loadSnapshot"
      @export-project="downloadProject(workspace)"
      @export-csv="downloadEditableCsvs(workspace)"
      @import-project="importProject"
    />

    <div class="sidebar-resize" @mousedown="startResize"></div>

    <main class="main-workspace">
      <header class="toolbar">
        <div class="toolbar-title">
          <strong>{{ selectedKingdom }}</strong>
          <span>{{ selectedStage }}</span>
        </div>

        <button
          type="button"
          class="toolbar-button"
          :class="{ active: editorMode === 'logic' }"
          @click="editorMode = 'logic'"
        >
          Logic Graph
        </button>
        <button
          type="button"
          class="toolbar-button"
          :class="{ active: editorMode === 'capture' }"
          @click="editorMode = 'capture'"
        >
          Capture Gates
        </button>

        <label class="search-box">
          <Search :size="17" />
          <input v-model="filters.search" type="search" placeholder="Find nodes, moons, routes" />
        </label>

        <button
          type="button"
          class="toolbar-button"
          :class="{ active: filters.showMoons }"
          title="Toggle moon nodes"
          @click="filters.showMoons = !filters.showMoons"
        >
          <Eye v-if="filters.showMoons" :size="17" />
          <EyeOff v-else :size="17" />
          Moons
        </button>
        <button
          type="button"
          class="toolbar-button"
          :class="{ active: filters.showExcluded }"
          title="Toggle excluded/commented-out entrances"
          @click="filters.showExcluded = !filters.showExcluded"
        >
          <Eye :size="17" />
          Excluded
        </button>
        <button
          type="button"
          class="toolbar-button"
          :class="{ active: filters.showRawAnchors }"
          title="Toggle raw StageData transition anchors"
          @click="filters.showRawAnchors = !filters.showRawAnchors"
        >
          <Eye v-if="filters.showRawAnchors" :size="17" />
          <EyeOff v-else :size="17" />
          Raw
        </button>
        <button
          type="button"
          class="toolbar-button"
          :class="{ active: Boolean(connectSource) }"
          title="Create a directed route from the selected source"
          @click="connectSource = selection.kind === 'node' ? selectedId : connectSource"
        >
          <Cable :size="17" />
          Connect
        </button>
        <button type="button" class="toolbar-button" title="Reset current map to template data" @click="resetCurrentMap">
          <RotateCcw :size="17" />
          Reset Map
        </button>
        <button type="button" class="toolbar-button" title="Cluster related nodes" @click="arrangeCurrentStage">
          <LayoutGrid :size="17" />
          Arrange
        </button>
        <button
          type="button"
          class="toolbar-button"
          title="Arrange using extracted in-game coordinates"
          @click="arrangeCurrentStageAsMap"
        >
          <Map :size="17" />
          Map
        </button>
        <button type="button" class="toolbar-button" title="Verify the selected map logic" @click="verifyCurrentStage">
          <ShieldCheck :size="17" />
          Verify
        </button>
        <button
          type="button"
          class="validation-pill"
          :class="{ error: counts.error > 0, warning: counts.error === 0 && counts.warning > 0 }"
          @click="toggleWorkspaceValidation"
        >
          <CheckCircle2 v-if="counts.error === 0 && counts.warning === 0" :size="17" />
          <AlertTriangle v-else :size="17" />
          {{ counts.error }} / {{ counts.warning }} / {{ counts.info }}
        </button>

        <button type="button" class="toolbar-button" title="Toggle dark theme" @click="toggleDarkTheme">
          <Moon v-if="!isDark" :size="17" />
          <Sun v-else :size="17" />
        </button>
      </header>

      <div v-if="connectSource" class="connect-banner">
        <Cable :size="17" />
        <span>Source: {{ connectSource }}</span>
        <button type="button" title="Cancel connection" @click="connectSource = ''">
          <X :size="16" />
        </button>
      </div>

      <section v-if="editorMode === 'logic'" class="work-area">
        <GraphCanvas
          ref="graphCanvas"
          :nodes="graph.nodes"
          :edges="graph.edges"
          :selected-id="selectedId"
          :connect-source="connectSource"
          :focus="focusTarget"
          @select="selectGraphItem"
          @activate-node="activateNode"
          @update-position="updateNodePosition"
        />
        <InspectorPanel
          :workspace="workspace"
          :selection="selection"
          :stage="selectedStage"
          :visible-nodes="graph.nodes"
          @create-reverse="createReverse"
          @delete-edge="removeEdge"
          @delete-node="removeNode"
          @start-connect="startConnect"
        />
        <ValidationDrawer v-if="showValidation" :issues="displayedIssues" :title="validationTitle" @focus="focusIssue" />
      </section>
      <section v-else class="capture-work-area">
        <CaptureGateEditor
          :workspace="workspace"
          :kingdom="selectedKingdom"
          :stage="selectedStage"
          :issues="issues"
          @mutated="onWorkspaceMutated"
        />
        <ValidationDrawer v-if="showValidation" :issues="displayedIssues" :title="validationTitle" @focus="focusIssue" />
      </section>
    </main>

    <div v-if="toast" class="toast">{{ toast }}</div>
  </div>
</template>
