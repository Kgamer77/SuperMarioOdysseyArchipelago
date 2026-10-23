<script setup lang="ts">
import { computed, ref, watch } from 'vue';
import {
  captureIsStageLocked,
  captureTokenForPlacement,
  categoryForPlacement,
  enemyEntityByName,
  enemySwapPlacements,
  friendlyNameForPlacement,
  placementIsSwapEligible,
  reliableCaptureTokens,
  safeCaptureTokens,
  swappableCaptureTokens
} from '../data/enemySwapData';
import { parseRequirementExpression } from '../lib/requirements';
import { ROUTE_DIFFICULTY_LEVELS } from '../lib/routeDifficulty';
import type {
  CaptureGate,
  EnemySwapPlacement,
  LogicWorkspace,
  MoonAccess,
  RouteEdge,
  ValidationIssue
} from '../lib/types';

const props = defineProps<{
  workspace: LogicWorkspace;
  kingdom: string;
  stage: string;
  issues: ValidationIssue[];
}>();
const emit = defineEmits<{ mutated: [] }>();

const selectedEdgeId = ref('');
const selectedSourceToken = ref('');
const selectedGateId = ref('');
const placementSearch = ref('');

type MoonGateTarget = {
  edgeId: string;
  moon: MoonAccess;
  kind: 'state' | 'local';
  label: string;
  tokens: string[];
};

const captureTokenSet = computed(() => new Set(
  props.workspace.logicTokens
    .filter((token) => token.token_type === 'capture')
    .map((token) => token.token)
));

function captureTokensForRequirement(requirement: string, stage: string): string[] {
  return [...new Set(
    parseRequirementExpression(
      requirement || '',
      props.workspace.logicTokens.map((token) => token.token)
    )
      .alternatives.flat()
      .filter((token) =>
        captureTokenSet.value.has(token)
        && swappableCaptureTokens.has(token)
        && !captureIsStageLocked(stage, token)
      )
  )].sort();
}

function captureTokens(edge: RouteEdge): string[] {
  return captureTokensForRequirement(edge.requirement_expr, edgeStage(edge));
}

function edgeStage(edge: RouteEdge): string {
  return props.workspace.logicNodes.find((node) => node.node_id === edge.from_node_id)?.stage
    || edge.from_node_id.split('::', 1)[0]
    || '';
}

const routes = computed(() => props.workspace.routeEdges
  .filter((edge) =>
    edge.kingdom === props.kingdom
    && edgeStage(edge) === props.stage
    && captureTokens(edge).length > 0
  )
  .sort((left, right) => (left.edge_id || '').localeCompare(right.edge_id || '')));

const selectedEdge = computed(() =>
  routes.value.find((edge) => edge.edge_id === selectedEdgeId.value)
);
const moonGateTargets = computed<MoonGateTarget[]>(() => {
  const targets: MoonGateTarget[] = [];
  for (const moon of props.workspace.moonAccess) {
    if (moon.kingdom !== props.kingdom || moon.stage !== props.stage) {
      continue;
    }
    const addTarget = (
      kind: 'state' | 'local',
      requirement: string,
      suffix: string
    ) => {
      const tokens = captureTokensForRequirement(requirement, moon.stage);
      if (tokens.length === 0) {
        return;
      }
      targets.push({
        edgeId: `moon_${kind}_${moon.uid}`,
        moon,
        kind,
        label: `${moon.moon_name || moon.moon_id} (${suffix})`,
        tokens
      });
    };
    addTarget('state', moon.state_requirement_expr, 'state requirement');
    addTarget('local', moon.requirement_expr, 'moon requirement');
  }
  return targets.sort((left, right) => left.label.localeCompare(right.label));
});
const selectedMoonTarget = computed(() =>
  moonGateTargets.value.find((target) => target.edgeId === selectedEdgeId.value)
);
const selectedTokens = computed(() =>
  selectedEdge.value
    ? captureTokens(selectedEdge.value)
    : selectedMoonTarget.value?.tokens || []
);
const selectedGate = computed(() =>
  props.workspace.captureGates.find((gate) => gate.gate_id === selectedGateId.value)
);
const gatesForSource = computed(() => props.workspace.captureGates
  .filter((gate) =>
    gate.edge_id === selectedEdgeId.value
    && gate.source_capture_token === selectedSourceToken.value
  )
  .sort((left, right) => (left.gate_id || '').localeCompare(right.gate_id || '')));

const stagePlacements = computed(() => enemySwapPlacements
  .filter((placement) => placement.logic_stage === props.stage)
  .filter((placement) => {
    const category = (categoryForPlacement(placement) || '').trim().toLowerCase();
    return category === 'capture' || category === 'enemy' || category === 'enemy2d'
      || category === 'npc';
  })
  .sort((left, right) =>
    (left.stage || '').localeCompare(right.stage || '')
    || (left.object_id || '').localeCompare(right.object_id || '')
  ));
const visiblePlacements = computed(() => {
  const search = placementSearch.value.trim().toLowerCase();
  if (!search) {
    return stagePlacements.value;
  }
  return stagePlacements.value.filter((placement) =>
    placement.stage.toLowerCase().includes(search)
    || placement.object_id.toLowerCase().includes(search)
    || placement.unit_config_name.toLowerCase().includes(search)
    || friendlyNameForPlacement(placement).toLowerCase().includes(search)
    || categoryForPlacement(placement).toLowerCase().includes(search)
  );
});

// Removed watch on props.kingdom/props.stage to prevent spurious deselection.

watch(selectedTokens, (tokens) => {
  if (!tokens.includes(selectedSourceToken.value)) {
    selectedSourceToken.value = tokens[0] || '';
  }
});

watch(gatesForSource, (gates) => {
  if (!gates.some((gate) => gate.gate_id === selectedGateId.value)) {
    selectedGateId.value = gates[0]?.gate_id || '';
  }
});

function selectEdge(edge: RouteEdge): void {
  selectedEdgeId.value = edge.edge_id;
  selectedSourceToken.value = captureTokens(edge)[0] || '';
}

function selectMoonTarget(target: MoonGateTarget): void {
  selectedEdgeId.value = target.edgeId;
  selectedSourceToken.value = target.tokens[0] || '';
}

function sourceComplete(edge: RouteEdge, token: string): boolean {
  return props.workspace.captureGates.some((gate) =>
    gate.edge_id === edge.edge_id && gate.source_capture_token === token
  );
}

function edgeComplete(edge: RouteEdge): boolean {
  return captureTokens(edge).every((token) => sourceComplete(edge, token));
}

function moonTargetComplete(target: MoonGateTarget): boolean {
  return target.tokens.every((token) => props.workspace.captureGates.some((gate) =>
    gate.edge_id === target.edgeId && gate.source_capture_token === token
  ));
}

function edgeHasError(edge: RouteEdge): boolean {
  return targetHasError(edge.edge_id);
}

function targetHasError(edgeId: string): boolean {
  const gateIds = new Set(
    props.workspace.captureGates
      .filter((gate) => gate.edge_id === edgeId)
      .map((gate) => gate.gate_id)
  );
  return props.issues.some((issue) =>
    issue.severity === 'error'
    && (
      issue.rowId === edgeId
      || gateIds.has(issue.rowId)
      || issue.rowId === 'capture-gate-solver'
    )
  );
}

function nodeLabel(nodeId: string): string {
  return props.workspace.logicNodes.find((node) => node.node_id === nodeId)?.display_name
    || nodeId;
}

function nextGateId(edgeId: string, sourceToken: string, replacementToken: string): string {
  const base = `cg_${edgeId}_${sourceToken}_${replacementToken}`
    .replace(/[^A-Za-z0-9]+/g, '_')
    .replace(/^_+|_+$/g, '');
  const ids = new Set(props.workspace.captureGates.map((gate) => gate.gate_id));
  let candidate = base;
  let suffix = 2;
  while (ids.has(candidate)) {
    candidate = `${base}_${suffix}`;
    suffix += 1;
  }
  return candidate;
}

function addGate(): void {
  const edge = selectedEdge.value;
  const moonTarget = selectedMoonTarget.value;
  const replacement = safeCaptureTokens[0]?.token || '';
  const targetId = edge?.edge_id || moonTarget?.edgeId || '';
  const targetKingdom = edge?.kingdom || moonTarget?.moon.kingdom || '';
  const targetStage = edge ? edgeStage(edge) : moonTarget?.moon.stage || '';
  if (!targetId || !selectedSourceToken.value || !replacement) {
    return;
  }
  const gate: CaptureGate = {
    gate_id: nextGateId(targetId, selectedSourceToken.value, replacement),
    kingdom: targetKingdom,
    logic_stage: targetStage,
    edge_id: targetId,
    source_capture_token: selectedSourceToken.value,
    replacement_capture_token: replacement,
    difficulty: 'casual',
    match_mode: 'any',
    minimum_count: '',
    notes: ''
  };
  props.workspace.captureGates.push(gate);
  selectedGateId.value = gate.gate_id;
  emit('mutated');
}

function deleteGate(): void {
  const gate = selectedGate.value;
  if (!gate) {
    return;
  }
  props.workspace.captureGates = props.workspace.captureGates
    .filter((row) => row.gate_id !== gate.gate_id);
  props.workspace.captureGateObjects = props.workspace.captureGateObjects
    .filter((row) => row.gate_id !== gate.gate_id);
  selectedGateId.value = '';
  emit('mutated');
}

function memberKey(placement: EnemySwapPlacement): string {
  return `${placement.stage}::${placement.object_id}`;
}

const selectedMemberKeys = computed(() => new Set(
  props.workspace.captureGateObjects
    .filter((member) => member.gate_id === selectedGateId.value)
    .map((member) => `${member.placement_stage}::${member.object_id}`)
));

function canSelectPlacement(placement: EnemySwapPlacement): boolean {
  return placementIsSwapEligible(placement)
    || captureTokenForPlacement(placement)
      === selectedGate.value?.replacement_capture_token;
}

function placementSafetyReason(placement: EnemySwapPlacement): string {
  if (placementIsSwapEligible(placement)) {
    return '';
  }
  if (captureTokenForPlacement(placement)
      === selectedGate.value?.replacement_capture_token) {
    return 'vanilla producer only';
  }
  if (categoryForPlacement(placement) === 'npc') {
    return 'depends on NPC randomization being enabled';
  }
  const entity = enemyEntityByName.get(placement.unit_config_name);
  if (!entity) {
    return 'unknown actor: not present in the curated entity catalog';
  }
  if (!['capture', 'enemy', 'prop', 'npc'].includes(entity.category)) {
    return `${entity.category || 'other'} actors are not replacement-capable`;
  }
  return 'source replacement is disabled by the curated safety catalog';
}

function placementCaptureStatus(placement: EnemySwapPlacement): string {
  const token = captureTokenForPlacement(placement);
  if (!token) {
    return 'none';
  }
  if (swappableCaptureTokens.has(token)) {
    return 'swappable';
  }
  if (reliableCaptureTokens.has(token)) {
    return 'reliable';
  }
  return 'other';
}

function togglePlacement(placement: EnemySwapPlacement, checked: boolean): void {
  const gate = selectedGate.value;
  if (!gate || !canSelectPlacement(placement)) {
    return;
  }
  const key = memberKey(placement);
  props.workspace.captureGateObjects = props.workspace.captureGateObjects
    .filter((member) =>
      member.gate_id !== gate.gate_id
      || `${member.placement_stage}::${member.object_id}` !== key
    );
  if (checked) {
    props.workspace.captureGateObjects.push({
      gate_id: gate.gate_id,
      placement_stage: placement.stage,
      object_id: placement.object_id
    });
  }
  emit('mutated');
}

function handlePlacementToggle(placement: EnemySwapPlacement, event: Event): void {
  togglePlacement(placement, (event.target as HTMLInputElement).checked);
}

function replaceMembers(placements: EnemySwapPlacement[]): void {
  const gate = selectedGate.value;
  if (!gate) {
    return;
  }
  props.workspace.captureGateObjects = [
    ...props.workspace.captureGateObjects.filter((member) => member.gate_id !== gate.gate_id),
    ...placements.filter(canSelectPlacement).map((placement) => ({
      gate_id: gate.gate_id,
      placement_stage: placement.stage,
      object_id: placement.object_id
    }))
  ];
  emit('mutated');
}

function selectVanillaProducers(): void {
  replaceMembers(stagePlacements.value.filter((placement) =>
    captureTokenForPlacement(placement)
      === selectedGate.value?.replacement_capture_token
  ));
}

function updateGate(): void {
  const gate = selectedGate.value;
  if (gate && gate.match_mode !== 'at_least' && gate.match_mode !== 'at_most') {
    gate.minimum_count = '';
  }
  emit('mutated');
}
</script>

<template>
  <section class="capture-editor">
    <aside class="capture-routes">
      <header>
        <h2>Capture connections</h2>
        <p>{{ routes.length }} directional routes and {{ moonGateTargets.length }} moon requirements in {{ stage }}</p>
      </header>
      <button
        v-for="edge in routes"
        :key="edge.edge_id"
        type="button"
        class="route-row"
        :class="{ selected: edge.edge_id === selectedEdgeId }"
        @click="selectEdge(edge)"
      >
        <span
          class="route-status"
          :class="{ complete: edgeComplete(edge) && !edgeHasError(edge), error: edgeHasError(edge) }"
        >
          {{ edgeHasError(edge) ? 'Error' : edgeComplete(edge) ? 'Complete' : 'Missing' }}
        </span>
        <strong>{{ nodeLabel(edge.from_node_id) }} → {{ nodeLabel(edge.to_node_id) }}</strong>
        <small>{{ captureTokens(edge).join(', ') }} · {{ edge.edge_id }}</small>
      </button>
      <p v-if="routes.length === 0" class="empty-state">
        This map has no route requirements containing capture tokens.
      </p>
      <template v-if="moonGateTargets.length > 0">
        <p class="target-section-label">Individual moon requirements</p>
        <button
          v-for="target in moonGateTargets"
          :key="target.edgeId"
          type="button"
          class="route-row"
          :class="{ selected: target.edgeId === selectedEdgeId }"
          @click="selectMoonTarget(target)"
        >
          <span
            class="route-status"
            :class="{
              complete: moonTargetComplete(target) && !targetHasError(target.edgeId),
              error: targetHasError(target.edgeId)
            }"
          >
            {{ targetHasError(target.edgeId) ? 'Error' : moonTargetComplete(target) ? 'Complete' : 'Missing' }}
          </span>
          <strong>{{ target.label }}</strong>
          <small>{{ target.tokens.join(', ') }} · {{ target.edgeId }}</small>
        </button>
      </template>
    </aside>

    <main class="capture-detail">
      <div v-if="selectedEdge || selectedMoonTarget" class="detail-inner">
        <header class="detail-header">
          <div>
            <h2 v-if="selectedEdge">{{ nodeLabel(selectedEdge.from_node_id) }} → {{ nodeLabel(selectedEdge.to_node_id) }}</h2>
            <h2 v-else>{{ selectedMoonTarget?.label }}</h2>
            <p>{{ selectedEdge?.edge_id || selectedMoonTarget?.edgeId }}</p>
          </div>
          <label>
            Source capture
            <select v-model="selectedSourceToken">
              <option v-for="token in selectedTokens" :key="token" :value="token">
                {{ token }}
              </option>
            </select>
          </label>
        </header>

        <div class="gate-tabs">
          <button
            v-for="gate in gatesForSource"
            :key="gate.gate_id"
            type="button"
            :class="{ active: gate.gate_id === selectedGateId }"
            @click="selectedGateId = gate.gate_id"
          >
            {{ gate.replacement_capture_token }} ({{ gate.difficulty || 'casual' }})
          </button>
          <button type="button" class="add-gate" @click="addGate">+ Alternative</button>
        </div>

        <div v-if="selectedGate" class="gate-form">
          <div class="form-grid">
            <label>
              Replacement capture
              <select v-model="selectedGate.replacement_capture_token" @change="updateGate">
                <option v-for="capture in safeCaptureTokens" :key="capture.token" :value="capture.token">
                  {{ capture.friendlyName }} ({{ capture.token }})
                </option>
              </select>
            </label>
            <label>
              Difficulty
              <select v-model="selectedGate.difficulty" @change="updateGate">
                <option
                  v-for="difficulty in ROUTE_DIFFICULTY_LEVELS"
                  :key="difficulty.value"
                  :value="difficulty.value"
                >
                  {{ difficulty.label }}
                </option>
              </select>
            </label>
            <label>
              Count rule
              <select v-model="selectedGate.match_mode" @change="updateGate">
                <option value="all">All selected objects</option>
                <option value="any">Any selected object</option>
                <option value="at_least">At least X objects</option>
                <option value="at_most">At most X objects</option>
              </select>
            </label>
            <label v-if="selectedGate.match_mode === 'at_least'">
              Minimum count
              <input
                v-model="selectedGate.minimum_count"
                type="number"
                min="1"
                :max="selectedMemberKeys.size || 1"
                @change="updateGate"
              />
            </label>
            <label v-if="selectedGate.match_mode === 'at_most'">
              Maximum count
              <input
                v-model="selectedGate.minimum_count"
                type="number"
                min="1"
                :max="selectedMemberKeys.size || 1"
                @change="updateGate"
              />
            </label>
            <label class="notes-field">
              Notes
              <input v-model="selectedGate.notes" type="text" @change="updateGate" />
            </label>
            <button type="button" class="delete-gate" @click="deleteGate">Delete alternative</button>
          </div>
        </div>
        <p v-else class="empty-state">
          No alternative selected. Browse this map's objects below to see what
          each currently is, then add an alternative to choose members.
        </p>

        <section class="object-inventory">
          <header class="inventory-header">
            <div>
              <h3>Objects in {{ stage }}</h3>
              <p>{{ visiblePlacements.length }} of {{ stagePlacements.length }} shown · what each object currently captures as</p>
            </div>
          </header>
          <div class="placement-toolbar">
            <input v-model="placementSearch" type="search" placeholder="Search object ID, capture, actor, category, or stage" />
            <template v-if="selectedGate">
              <button type="button" @click="replaceMembers(visiblePlacements)">Select visible</button>
              <button type="button" @click="replaceMembers([])">Clear</button>
              <button type="button" @click="selectVanillaProducers">Vanilla producers</button>
              <span>{{ selectedMemberKeys.size }} selected</span>
            </template>
            <span v-else>Select or add an alternative to choose members</span>
          </div>

          <div class="placement-table">
            <div class="placement-row placement-heading">
              <span></span>
              <span>Object</span>
              <span>Current capture</span>
              <span>Actor</span>
              <span>Category / scenarios</span>
              <span>Position</span>
            </div>
            <label
              v-for="placement in visiblePlacements"
              :key="memberKey(placement)"
              class="placement-row"
              :class="{ disabled: !selectedGate || !canSelectPlacement(placement) }"
            >
              <input
                type="checkbox"
                :checked="selectedMemberKeys.has(memberKey(placement))"
                :disabled="!selectedGate || !canSelectPlacement(placement)"
                @change="handlePlacementToggle(placement, $event)"
              />
              <span>
                <strong>{{ placement.object_id }}</strong>
                <small>{{ placement.stage }}</small>
              </span>
              <span>
                <strong v-if="captureTokenForPlacement(placement)">
                  {{ captureTokenForPlacement(placement) }}
                </strong>
                <strong v-else class="no-capture">— none</strong>
                <small class="capture-status" :class="placementCaptureStatus(placement)">
                  {{ placementCaptureStatus(placement) }}
                </small>
              </span>
              <span>
                <strong>{{ friendlyNameForPlacement(placement) }}</strong>
                <small>{{ placement.unit_config_name || '(unknown)' }}</small>
              </span>
              <span>
                <strong>{{ categoryForPlacement(placement) }}</strong>
                <small>{{ placement.layer || 'no layer' }} · {{ placement.scenarios || 'all' }}</small>
                <small v-if="placementSafetyReason(placement)" class="safety-reason">
                  {{ placementSafetyReason(placement) }}
                </small>
              </span>
              <span>
                <small>{{ placement.x || '?' }}, {{ placement.y || '?' }}, {{ placement.z || '?' }}</small>
              </span>
            </label>
            <p v-if="visiblePlacements.length === 0" class="empty-state">
              No extracted objects match this map or search.
            </p>
          </div>
        </section>
      </div>
      <p v-else class="empty-state centered">
        Select a capture-dependent connection or moon requirement.
      </p>
    </main>
  </section>
</template>

<style scoped>
.capture-editor {
  display: grid;
  grid-template-columns: minmax(260px, 340px) minmax(0, 1fr);
  min-height: 0;
  height: 100%;
  background: #f5f8fa;
}

.capture-routes {
  overflow: auto;
  border-right: 1px solid #cdd5dc;
  background: #fff;
}

.capture-routes header,
.detail-header,
.gate-form {
  padding: 16px;
}

h2,
p {
  margin: 0;
}

.capture-routes header p,
.detail-header p,
small {
  color: #60717f;
  font-size: 12px;
}

.target-section-label {
  margin: 0;
  padding: 12px 16px 6px;
  border-top: 1px solid #e4eaee;
  color: #526570;
  font-size: 11px;
  font-weight: 700;
  text-transform: uppercase;
}

.route-row {
  display: flex;
  width: 100%;
  flex-direction: column;
  gap: 5px;
  padding: 13px 16px;
  border: 0;
  border-top: 1px solid #e4eaee;
  background: #fff;
  text-align: left;
  cursor: pointer;
}

.route-row:hover,
.route-row.selected {
  background: #eaf4f3;
}

.route-status {
  align-self: flex-start;
  padding: 2px 6px;
  border-radius: 999px;
  background: #fff0ee;
  color: #7a2019;
  font-size: 10px;
  text-transform: uppercase;
}

.route-status.complete {
  background: #eff8f1;
  color: #245334;
}

.route-status.error {
  background: #fee2e2;
  color: #991b1b;
}

.safety-reason {
  color: #a33;
}

.capture-detail {
  min-width: 0;
  overflow: auto;
}

.detail-header {
  display: flex;
  align-items: end;
  justify-content: space-between;
  gap: 20px;
  border-bottom: 1px solid #dce3e8;
  background: #fff;
}

.detail-header label,
.form-grid label {
  display: flex;
  flex-direction: column;
  gap: 5px;
  color: #435563;
  font-size: 12px;
}

select,
input {
  min-height: 36px;
  padding: 6px 9px;
  border: 1px solid #bdc9d1;
  border-radius: 6px;
  background: #fff;
}

.gate-tabs,
.placement-toolbar {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 10px 16px;
  border-bottom: 1px solid #dce3e8;
  background: #f8fbfc;
}

.gate-tabs button,
.placement-toolbar button,
.delete-gate {
  min-height: 34px;
  padding: 6px 10px;
  border: 1px solid #bdc9d1;
  border-radius: 6px;
  background: #fff;
  cursor: pointer;
}

.gate-tabs button.active {
  border-color: #5d9395;
  background: #dff0ee;
}

.gate-tabs .add-gate {
  margin-left: auto;
}

.form-grid {
  display: grid;
  grid-template-columns: repeat(3, minmax(160px, 1fr));
  gap: 12px;
  margin-bottom: 16px;
}

.notes-field {
  grid-column: span 2;
}

.delete-gate {
  align-self: end;
  border-color: #dfaaa4;
  color: #7a2019;
}

.placement-toolbar {
  padding: 0 0 10px;
  border: 0;
}

.placement-toolbar input {
  min-width: 260px;
  flex: 1;
}

.placement-toolbar span {
  color: #60717f;
  font-size: 12px;
}

.object-inventory {
  padding: 4px 16px 16px;
}

.inventory-header {
  padding: 12px 0 6px;
}

.inventory-header h3 {
  margin: 0;
  font-size: 14px;
  color: #2b3a44;
}

.inventory-header p {
  margin: 2px 0 0;
}

.placement-table {
  overflow: auto;
  max-height: calc(100vh - 340px);
  border: 1px solid #d5dde2;
  border-radius: 7px;
  background: #fff;
}

.placement-row {
  display: grid;
  grid-template-columns: 28px minmax(130px, 0.75fr) minmax(120px, 0.65fr) minmax(150px, 0.9fr) minmax(160px, 0.9fr) minmax(120px, 0.55fr);
  align-items: center;
  gap: 10px;
  min-height: 48px;
  padding: 6px 10px;
  border-bottom: 1px solid #e7ecef;
}

.capture-status {
  align-self: flex-start;
  margin-top: 2px;
  padding: 1px 6px;
  border-radius: 999px;
  font-size: 10px;
  text-transform: uppercase;
}

.capture-status.swappable {
  background: #eaf1fb;
  color: #274b7a;
}

.capture-status.reliable {
  background: #eef8f1;
  color: #245334;
}

.capture-status.none,
.capture-status.other {
  background: #eef0f2;
  color: #6a747c;
}

.no-capture {
  color: #9aa3ab;
  font-weight: 500;
}

.placement-row > span {
  display: flex;
  min-width: 0;
  flex-direction: column;
}

.placement-row.disabled {
  opacity: 0.48;
}

.placement-heading {
  position: sticky;
  top: 0;
  z-index: 1;
  min-height: 34px;
  background: #eef3f5;
  color: #526570;
  font-size: 11px;
  font-weight: 700;
  text-transform: uppercase;
}

.empty-state {
  padding: 20px 16px;
  color: #60717f;
}

.empty-state.centered {
  display: grid;
  height: 100%;
  place-items: center;
}

</style>

<style>
/* Dark theme overrides (Global to match html.dark) */
html.dark .capture-editor {
  background: #111518;
}

html.dark .capture-routes,
html.dark .detail-header,
html.dark .route-row,
html.dark .capture-editor select,
html.dark .capture-editor input,
html.dark .gate-tabs button,
html.dark .placement-toolbar button,
html.dark .delete-gate,
html.dark .placement-table {
  background: #141e23;
  border-color: #262f36;
  color: #e2e8f0;
}

html.dark .gate-tabs,
html.dark .placement-toolbar {
  background: #1b262d;
  border-color: #262f36;
}

html.dark .route-row {
  border-top-color: #262f36;
  background: #141e23;
}

html.dark .target-section-label {
  border-top-color: #262f36;
  color: #7e8c99;
}

html.dark .route-row:hover,
html.dark .route-row.selected {
  background: #1b262d;
}

html.dark .inventory-header h3,
html.dark .detail-header h2,
html.dark .detail-header label,
html.dark .form-grid label {
  color: #e2e8f0;
}

html.dark .capture-routes header p,
html.dark .detail-header p,
html.dark .capture-editor small,
html.dark .placement-toolbar span {
  color: #7e8c99;
}

html.dark .placement-row.placement-heading {
  background: #1b262d;
  color: #7e8c99;
}

html.dark .placement-row {
  border-bottom-color: #262f36;
}

html.dark .gate-tabs button.active {
  border-color: #5d9395;
  background: rgba(108,182,255,0.15);
  color: #9ed0ff;
}

html.dark .capture-status.none,
html.dark .capture-status.other {
  background: #1b262d;
  color: #7e8c99;
}

html.dark .no-capture {
  color: #7e8c99;
}
</style>
