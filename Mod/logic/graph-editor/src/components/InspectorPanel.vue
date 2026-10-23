<script setup lang="ts">
import { computed } from 'vue';
import { CopyPlus, Link, Trash2 } from '@lucide/vue';
import RequirementEditor from './RequirementEditor.vue';
import { EFFORT_LEVELS } from '../lib/effort';
import {
  availableScenariosForKingdom,
  findMoon,
  findNode,
  findRouteConnection,
  getRoom,
  isStoryMoon,
  moonNodeId,
  moonRowKey
} from '../lib/workspace';
import type { GraphNode, LogicNode, LogicWorkspace, RouteEdge, Selection } from '../lib/types';

const props = defineProps<{
  workspace: LogicWorkspace;
  selection: Selection;
  stage: string;
  visibleNodes: GraphNode[];
}>();

const emit = defineEmits<{
  createReverse: [edgeId: string];
  deleteEdge: [edgeId: string];
  deleteNode: [nodeId: string];
  startConnect: [nodeId: string];
}>();

const selectedNode = computed(() =>
  props.selection.kind === 'node' ? findNode(props.workspace, props.selection.id) : undefined
);
const selectedConnection = computed(() =>
  props.selection.kind === 'edge' ? findRouteConnection(props.workspace, props.selection.id) : undefined
);
const selectedConnectionFixed = computed(
  () => selectedConnection.value?.forward.source === 'fixed_transition'
);
const selectedMoon = computed(() =>
  props.selection.kind === 'moon' ? findMoon(props.workspace, props.selection.id) : undefined
);
const room = computed(() => getRoom(props.workspace, props.stage));
const activeKingdom = computed(
  () =>
    room.value?.kingdom ||
    selectedMoon.value?.kingdom ||
    selectedConnection.value?.forward.kingdom ||
    ''
);
const availableScenarios = computed(() =>
  availableScenariosForKingdom(props.workspace, activeKingdom.value)
);
const requirementTypes = ['ability', 'capture', 'kingdom'];
const scenarioRows = computed(() =>
  props.workspace.scenarioMetadata.filter((scenario) => scenario.stage === props.stage)
);
const scenarioMoveOptions = computed(() => {
  const values = new Set(
    scenarioRows.value.map((scenario) => scenario.scenario_no).filter((scenarioNo) => scenarioNo.trim())
  );
  if (selectedMoon.value?.scenario_move_to) {
    values.add(selectedMoon.value.scenario_move_to);
  }
  return [...values].sort(compareScenarioNo);
});
const logicNodeOptions = computed(() =>
  props.visibleNodes
    .filter((node) => node.kind === 'logic')
    .map((node) => ({ id: node.id, label: `${node.label} - ${node.type}` }))
);

// id -> label lookup built once per render instead of the old nodeLabel()
// doing a linear Array.find() over visibleNodes on every call. directionLabel
// is invoked directly from the template (uncached), so on the old version
// typing into any field here — which re-runs the whole render — re-scanned
// visibleNodes from scratch for every direction label shown.
const visibleNodeLabels = computed(() => {
  const map = new Map<string, string>();
  for (const node of props.visibleNodes) {
    map.set(node.id, node.label);
  }
  return map;
});

function nodeLabel(nodeId: string): string {
  return visibleNodeLabels.value.get(nodeId) || nodeId;
}

function directionLabel(edge: RouteEdge): string {
  return `${nodeLabel(edge.from_node_id)} to ${nodeLabel(edge.to_node_id)}`;
}

function nodeRoleLabel(node: LogicNode): string {
  if (node.node_type === 'fixed_transition') {
    return 'Fixed vanilla cross-stage transition';
  }
  if (node.node_type === 'stage_start') {
    return 'Default source';
  }
  if (node.node_type === 'checkpoint') {
    return 'Checkpoint source after activation';
  }
  if (node.node_type === 'stage_change') {
    return 'Transition trigger';
  }
  if (node.node_type === 'state_event') {
    return 'Progression event location';
  }
  if (node.side === 'home_in') {
    return 'Home-side transition trigger';
  }
  if (node.side === 'home_out') {
    return 'Home-side return/arrival';
  }
  if (node.side === 'sub_near') {
    return 'Subarea entry/arrival side';
  }
  if (node.side === 'sub_far') {
    return 'Subarea return/exit side';
  }
  return 'Map point';
}

function compareScenarioNo(left: string, right: string): number {
  const leftNumber = Number(left);
  const rightNumber = Number(right);
  if (Number.isFinite(leftNumber) && Number.isFinite(rightNumber)) {
    return leftNumber - rightNumber;
  }
  return left.localeCompare(right);
}
</script>

<template>
  <aside class="inspector">
    <template v-if="selectedNode">
      <div class="inspector-header">
        <div>
          <h2>Place</h2>
          <p>{{ selectedNode.node_type }}</p>
        </div>
        <button
          type="button"
          class="icon-button"
          title="Use this place as the source for a new directed connection"
          @click="emit('startConnect', selectedNode.node_id)"
        >
          <Link :size="18" />
        </button>
      </div>

      <label>
        Name
        <input v-model="selectedNode.display_name" type="text" :readonly="selectedNode.source === 'fixed_transition'" />
      </label>
      <label>
        Notes
        <textarea v-model="selectedNode.notes" />
      </label>
      <dl class="metadata-list">
        <dt>Node ID</dt>
        <dd>{{ selectedNode.node_id }}</dd>
        <dt>Runtime Anchor</dt>
        <dd>{{ selectedNode.source }} {{ selectedNode.entrance_id || selectedNode.obj_id }}</dd>
        <dt>Map Role</dt>
        <dd>{{ nodeRoleLabel(selectedNode) }}</dd>
        <dt>Entrance Side</dt>
        <dd>{{ selectedNode.side || 'none' }}</dd>
        <dt>Scenario Indices</dt>
        <dd>{{ selectedNode.scenario_indices || 'unknown' }}</dd>
      </dl>

      <button
        v-if="selectedNode.source === 'editor'"
        type="button"
        class="danger-button"
        @click="emit('deleteNode', selectedNode.node_id)"
      >
        <Trash2 :size="16" />
        Delete Place
      </button>
    </template>

    <template v-else-if="selectedConnection">
      <div class="inspector-header">
        <div>
          <h2>Connection</h2>
          <p>{{ nodeLabel(selectedConnection.from) }} to {{ nodeLabel(selectedConnection.to) }}</p>
        </div>
        <button
          v-if="!selectedConnection.reverse && !selectedConnectionFixed"
          type="button"
          class="icon-button"
          title="Add reverse direction"
          @click="emit('createReverse', selectedConnection.id)"
        >
          <CopyPlus :size="18" />
        </button>
      </div>

      <section class="direction-card">
        <div class="direction-head">
          <strong>{{ directionLabel(selectedConnection.forward) }}</strong>
          <span>{{ selectedConnection.forward.edge_type || 'route' }}</span>
        </div>
        <RequirementEditor
          v-if="!selectedConnectionFixed"
          :key="`${selectedConnection.forward.edge_id}:requirement`"
          v-model="selectedConnection.forward.requirement_expr"
          v-model:option-difficulties="selectedConnection.forward.difficulty"
          v-model:option-scenarios="selectedConnection.forward.option_scenarios"
          label="Movement Requirement"
          :tokens="workspace.logicTokens"
          :nodes="workspace.logicNodes"
          :allowed-types="requirementTypes"
          :available-scenarios="availableScenarios"
        />
        <p v-else class="raw-warning">
          This fixed vanilla transition is always available and is not part of entrance or painting randomization.
        </p>
        <label v-if="!selectedConnectionFixed">
          Notes
          <textarea v-model="selectedConnection.forward.notes" />
        </label>
        <dl class="metadata-list">
          <dt>Edge ID</dt>
          <dd>{{ selectedConnection.forward.edge_id }}</dd>
          <dt>Source</dt>
          <dd>{{ selectedConnection.forward.source }}</dd>
        </dl>
      </section>

      <section v-if="selectedConnection.reverse" class="direction-card">
        <div class="direction-head">
          <strong>{{ directionLabel(selectedConnection.reverse) }}</strong>
          <span>{{ selectedConnection.reverse.edge_type || 'route' }}</span>
        </div>
        <RequirementEditor
          :key="`${selectedConnection.reverse.edge_id}:requirement`"
          v-model="selectedConnection.reverse.requirement_expr"
          v-model:option-difficulties="selectedConnection.reverse.difficulty"
          v-model:option-scenarios="selectedConnection.reverse.option_scenarios"
          label="Movement Requirement"
          :tokens="workspace.logicTokens"
          :nodes="workspace.logicNodes"
          :allowed-types="requirementTypes"
          :available-scenarios="availableScenarios"
        />
        <label>
          Notes
          <textarea v-model="selectedConnection.reverse.notes" />
        </label>
        <dl class="metadata-list">
          <dt>Edge ID</dt>
          <dd>{{ selectedConnection.reverse.edge_id }}</dd>
          <dt>Source</dt>
          <dd>{{ selectedConnection.reverse.source }}</dd>
        </dl>
      </section>
      <button
        v-else-if="!selectedConnectionFixed"
        type="button"
        class="text-button"
        @click="emit('createReverse', selectedConnection.id)"
      >
        <CopyPlus :size="16" />
        Add Reverse Direction
      </button>

      <p v-if="selectedConnection.duplicateCount" class="raw-warning">
        {{ selectedConnection.duplicateCount }} duplicate route row{{ selectedConnection.duplicateCount === 1 ? '' : 's' }} share these endpoints.
      </p>

      <button
        v-if="!selectedConnectionFixed"
        type="button"
        class="danger-button"
        @click="emit('deleteEdge', selectedConnection.id)"
      >
        <Trash2 :size="16" />
        Delete Connection
      </button>
    </template>

    <template v-else-if="selectedMoon">
      <div class="inspector-header">
        <div>
          <h2>Moon</h2>
          <p>{{ selectedMoon.moon_name }}</p>
        </div>
      </div>

      <label>
        Reachable From
        <select v-model="selectedMoon.from_node_id">
          <option value="">Unconnected</option>
          <option v-for="node in logicNodeOptions" :key="node.id" :value="node.id">{{ node.label }}</option>
        </select>
      </label>
      <label>
        Effort Level
        <select v-model="selectedMoon.effort_level">
          <option v-for="level in EFFORT_LEVELS" :key="level.value" :value="level.value">
            {{ level.label }}
          </option>
        </select>
      </label>
      <label title="Scenario number to move to after this moon is collected. Leave blank for no authored transition.">
        Scenario After Collection
        <input v-model.trim="selectedMoon.scenario_move_to" type="text" list="scenario-move-options" />
        <datalist id="scenario-move-options">
          <option v-for="scenarioNo in scenarioMoveOptions" :key="scenarioNo" :value="scenarioNo" />
        </datalist>
      </label>
      <RequirementEditor
        :key="`${selectedMoon.__editor_id}:local-requirement`"
        v-model="selectedMoon.requirement_expr"
        v-model:option-difficulties="selectedMoon.difficulty"
        v-model:option-scenarios="selectedMoon.option_scenarios"
        label="Local Moon Requirement"
        :tokens="workspace.logicTokens"
        :nodes="workspace.logicNodes"
        :allowed-types="requirementTypes"
        :available-scenarios="availableScenarios"
      />
      <dl class="metadata-list">
        <dt>Moon Type</dt>
        <dd>{{ isStoryMoon(selectedMoon) ? 'story' : 'regular' }}</dd>
        <dt>Available Stage Maps</dt>
        <dd>{{ selectedMoon.placement_stage_map_indices || 'unknown' }}</dd>
        <dt>Available Scenarios</dt>
        <dd>{{ selectedMoon.shine_info_available_scenarios || 'unknown' }}</dd>
        <dt>Main Scenario</dt>
        <dd>{{ selectedMoon.shine_info_main_scenario_no || 'none' }}</dd>
        <dt>ShineInfo Object</dt>
        <dd>{{ selectedMoon.shine_info_object_name || 'unknown' }}</dd>
        <dt>Object ScenarioNo</dt>
        <dd>{{ selectedMoon.placement_scenario_no_values || 'none' }}</dd>
        <dt>Moon ID</dt>
        <dd>{{ selectedMoon.moon_id }}</dd>
        <dt>Graph Node</dt>
        <dd>{{ moonNodeId(moonRowKey(selectedMoon)) }}</dd>
        <dt>Flags</dt>
        <dd>{{ selectedMoon.flags || 'none' }}</dd>
        <dt>Placement</dt>
        <dd>{{ selectedMoon.placement_found === 'true' ? selectedMoon.placement_unit_config_names : 'not found' }}</dd>
        <dt>Layers</dt>
        <dd>{{ selectedMoon.placement_layer_config_names || 'unknown' }}</dd>
      </dl>
      <label>
        Effort Notes
        <textarea v-model="selectedMoon.effort_notes" />
      </label>
      <label>
        Notes
        <textarea v-model="selectedMoon.notes" />
      </label>
    </template>

    <template v-else>
      <div class="inspector-header">
        <div>
          <h2>{{ room?.stage_display_name || stage }}</h2>
          <p>{{ room?.stage || stage }}</p>
        </div>
      </div>
      <dl class="metadata-list">
        <dt>Area Scope</dt>
        <dd>{{ room?.area_scope || 'unknown' }}</dd>
        <dt>Stage Warp</dt>
        <dd>{{ room?.listed_in_stage_warp || 'unknown' }}</dd>
        <dt>Scenario Rows</dt>
        <dd>{{ scenarioRows.length }}</dd>
      </dl>
      <div class="scenario-list">
        <article v-for="scenario in scenarioRows" :key="`${scenario.stage}:${scenario.scenario_no}:${scenario.source_file}`">
          <strong>Scenario {{ scenario.scenario_no || 'raw' }}</strong>
          <span>{{ scenario.active_layers || 'layers unknown' }}</span>
        </article>
      </div>
    </template>
  </aside>
</template>