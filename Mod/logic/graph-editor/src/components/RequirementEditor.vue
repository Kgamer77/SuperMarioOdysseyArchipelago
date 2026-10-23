<script setup lang="ts">
import { computed, nextTick, ref, watch } from 'vue';
import { AlertTriangle, MapPin, Plus, X } from '@lucide/vue';
import {
  collectNodeSet,
  collectTokenSet,
  parseRequirementExpression,
  setRequirementMode,
  stringifyRequirement,
  toggleNodeInAlternative,
  toggleTokenInAlternative,
  tokenOptionsByType
} from '../lib/requirements';
import {
  normalizeRouteDifficulties,
  normalizeRouteDifficulty,
  ROUTE_DIFFICULTY_LEVELS,
  stringifyRouteDifficulties
} from '../lib/routeDifficulty';
import {
  scenariosForOption,
  stringifyOptionScenarios
} from '../lib/optionScenarios';
import type { LogicNode, LogicToken, RequirementModel } from '../lib/types';

const props = withDefaults(
  defineProps<{
    label: string;
    modelValue: string;
    tokens: LogicToken[];
    // Every authored node, not just this stage's: node access is allowed to
    // point across stages and kingdoms.
    nodes?: LogicNode[];
    allowedTypes?: string[];
    emptyLabel?: string;
    optionDifficulties?: string;
    optionScenarios?: string;
    availableScenarios?: string[];
  }>(),
  {
    allowedTypes: () => ['ability', 'capture'],
    nodes: () => [],
    emptyLabel: 'Todo',
    optionDifficulties: 'casual',
    optionScenarios: '',
    availableScenarios: () => []
  }
);

const emit = defineEmits<{
  'update:modelValue': [value: string];
  'update:optionDifficulties': [value: string];
  'update:optionScenarios': [value: string];
}>();

const search = ref('');
const customDraft = ref<string[][] | null>(null);
const customNodeDraft = ref<string[][] | null>(null);
const tokenSet = computed(() => collectTokenSet(props.tokens));
const nodeSet = computed(() => collectNodeSet(props.nodes));
const parsed = computed(() =>
  parseRequirementExpression(
    props.modelValue,
    tokenSet.value,
    props.nodes.length > 0 ? nodeSet.value : undefined
  )
);
const activeMode = computed<RequirementModel['mode']>(() =>
  customDraft.value ? 'custom' : parsed.value.mode
);
const tokenOptions = computed(() => tokenOptionsByType(props.tokens, props.allowedTypes));
const filteredTokens = computed(() => {
  const query = search.value.trim().toLowerCase();
  if (!query) {
    return tokenOptions.value;
  }

  return tokenOptions.value.filter((token) =>
    `${token.token} ${token.display_name} ${token.category}`.toLowerCase().includes(query)
  );
});

const groupedTokens = computed(() => {
  const groups = new Map<string, LogicToken[]>();
  for (const token of filteredTokens.value) {
    const key = token.category || token.token_type;
    groups.set(key, [...(groups.get(key) ?? []), token]);
  }
  return [...groups.entries()];
});

const customModel = computed<RequirementModel>(() => {
  if (customDraft.value) {
    return {
      mode: 'custom',
      alternatives: customDraft.value,
      nodeAlternatives: customNodeDraft.value ?? customDraft.value.map(() => []),
      raw: '',
      errors: []
    };
  }

  if (parsed.value.mode === 'custom') {
    return parsed.value;
  }

  return {
    mode: 'custom',
    alternatives: [[]],
    nodeAlternatives: [[]],
    raw: '',
    errors: []
  };
});

function optionNodes(alternativeIndex: number): string[] {
  return customModel.value.nodeAlternatives[alternativeIndex] ?? [];
}

function changeMode(mode: RequirementModel['mode']) {
  if (mode === 'custom') {
    customDraft.value =
      parsed.value.mode === 'custom' && parsed.value.alternatives.length > 0
        ? cloneAlternatives(parsed.value.alternatives)
        : [[]];
    customNodeDraft.value =
      parsed.value.mode === 'custom' && parsed.value.alternatives.length > 0
        ? cloneAlternatives(parsed.value.nodeAlternatives, customDraft.value.length)
        : [[]];
    emitCustomDraft();
    emitDifficultyList(currentDifficultyList(customDraft.value.length));
    return;
  }

  customDraft.value = null;
  customNodeDraft.value = null;
  emit('update:modelValue', setRequirementMode(props.modelValue, mode, tokenSet.value));
  emitDifficultyList(currentDifficultyList(1));
}

function toggleToken(alternativeIndex: number, token: string) {
  adoptParsedDraft();

  if (customDraft.value) {
    growDraft(alternativeIndex);

    const alternative = customDraft.value[alternativeIndex];
    const existingIndex = alternative.indexOf(token);
    if (existingIndex >= 0) {
      alternative.splice(existingIndex, 1);
    } else {
      alternative.push(token);
    }
    emitCustomDraft();
    return;
  }

  emit('update:modelValue', toggleTokenInAlternative(props.modelValue, alternativeIndex, token, tokenSet.value));
}

function toggleNode(alternativeIndex: number, nodeId: string) {
  adoptParsedDraft();

  if (customDraft.value && customNodeDraft.value) {
    growDraft(alternativeIndex);

    const nodes = customNodeDraft.value[alternativeIndex];
    const existingIndex = nodes.indexOf(nodeId);
    if (existingIndex >= 0) {
      nodes.splice(existingIndex, 1);
    } else {
      nodes.push(nodeId);
    }
    emitCustomDraft();
    return;
  }

  emit(
    'update:modelValue',
    toggleNodeInAlternative(props.modelValue, alternativeIndex, nodeId, tokenSet.value)
  );
}

function adoptParsedDraft() {
  if (!customDraft.value && parsed.value.mode === 'custom') {
    customDraft.value = cloneAlternatives(parsed.value.alternatives);
    customNodeDraft.value = cloneAlternatives(
      parsed.value.nodeAlternatives,
      customDraft.value.length
    );
  }
}

function growDraft(alternativeIndex: number) {
  if (!customDraft.value) {
    return;
  }
  while (customDraft.value.length <= alternativeIndex) {
    customDraft.value.push([]);
  }
  customNodeDraft.value = customNodeDraft.value ?? [];
  while (customNodeDraft.value.length < customDraft.value.length) {
    customNodeDraft.value.push([]);
  }
}

function addOrOption() {
  adoptParsedDraft();
  customDraft.value = customDraft.value ?? [[]];
  customNodeDraft.value = customNodeDraft.value ?? [[]];
  const difficulties = currentDifficultyList(customDraft.value.length);
  customDraft.value.push([]);
  customNodeDraft.value.push([]);
  emitCustomDraft();
  difficulties.push('casual');
  emitDifficultyList(difficulties);
}

function removeOrOption(index: number) {
  adoptParsedDraft();
  customDraft.value = customDraft.value ?? [[]];
  customNodeDraft.value = customNodeDraft.value ?? [[]];
  const difficulties = currentDifficultyList(customDraft.value.length);
  const scenarioPerOption = currentScenarioPerOption(customDraft.value.length);
  customDraft.value.splice(index, 1);
  customNodeDraft.value.splice(index, 1);
  difficulties.splice(index, 1);
  scenarioPerOption.splice(index, 1);
  if (customDraft.value.length === 0) {
    customDraft.value.push([]);
    customNodeDraft.value.push([]);
    difficulties.push('casual');
    scenarioPerOption.push([...props.availableScenarios]);
  }
  emitCustomDraft();
  emitDifficultyList(difficulties);
  emitScenarioList(scenarioPerOption);
}

function updateRaw(value: string) {
  customDraft.value = null;
  customNodeDraft.value = null;
  emit('update:modelValue', value);
}

function replaceFromModel(model: RequirementModel) {
  customDraft.value = null;
  customNodeDraft.value = null;
  emit('update:modelValue', stringifyRequirement(model));
}

function emitCustomDraft() {
  const alternatives = customDraft.value ?? [[]];
  const nextValue = stringifyRequirement({
    mode: 'custom',
    alternatives,
    nodeAlternatives: customNodeDraft.value ?? alternatives.map(() => []),
    raw: '',
    errors: []
  });
  emit('update:modelValue', nextValue);
}

// Memoized once per render: the old optionDifficulty() called
// normalizeRouteDifficulties() — which re-parses the whole difficulty
// string into a fresh array — separately for *every* alternative rendered
// in the v-for, turning an O(n) parse into O(n^2) work across n options.
// Now the string is parsed once and each alternative just indexes into it.
const difficultyList = computed(() => currentDifficultyList(customModel.value.alternatives.length));

function optionDifficulty(alternativeIndex: number): string {
  return difficultyList.value[alternativeIndex] ?? 'casual';
}

function updateOptionDifficulty(alternativeIndex: number, value: string) {
  const difficulties = currentDifficultyList(customModel.value.alternatives.length);
  difficulties[alternativeIndex] = normalizeRouteDifficulty(value);
  emitDifficultyList(difficulties);
}

function currentDifficultyList(optionCount: number): string[] {
  return normalizeRouteDifficulties(props.optionDifficulties, optionCount);
}

function emitDifficultyList(values: string[]) {
  emit('update:optionDifficulties', stringifyRouteDifficulties(values));
}

const hasScenarioChoices = computed(() => props.availableScenarios.length > 0);

// Same idea as difficultyList above: resolve each option's scenario set
// once per render (one parse per option) instead of re-parsing
// optionScenarios from scratch for every single checkbox cell rendered
// (options x scenarios parses -> options parses).
const scenarioSetsByOption = computed(() => {
  const optionCount = Math.max(customModel.value.alternatives.length, 1);
  return Array.from({ length: optionCount }, (_, index) =>
    new Set(scenariosForOption(props.optionScenarios, index, props.availableScenarios))
  );
});

function scenarioChecked(optionIndex: number, scenario: string): boolean {
  return scenarioSetsByOption.value[optionIndex]?.has(scenario) ?? false;
}

function currentScenarioPerOption(optionCount: number): string[][] {
  return Array.from({ length: Math.max(1, optionCount) }, (_, index) =>
    scenariosForOption(props.optionScenarios, index, props.availableScenarios)
  );
}

function emitScenarioList(perOption: string[][]) {
  emit('update:optionScenarios', stringifyOptionScenarios(perOption, props.availableScenarios));
}

function toggleScenario(optionIndex: number, scenario: string) {
  const perOption = currentScenarioPerOption(Math.max(customModel.value.alternatives.length, optionIndex + 1));
  const list = perOption[optionIndex] ?? [...props.availableScenarios];
  const existingIndex = list.indexOf(scenario);
  if (existingIndex >= 0) {
    list.splice(existingIndex, 1);
  } else {
    list.push(scenario);
  }
  perOption[optionIndex] = list;
  emitScenarioList(perOption);
}

// ── Node access picker ────────────────────────────────────────────────────
//
// Node ids are long and there are well over a thousand of them across every
// kingdom, so they get a searchable dialog rather than the flat chip list the
// tokens use. It stays open while the author adds several nodes to one option.
const nodePickerOption = ref<number | null>(null);
const nodeSearch = ref('');
const nodeSearchInput = ref<HTMLInputElement | null>(null);
const kMaxNodeResults = 200;

const nodeLabels = computed(() => {
  const labels = new Map<string, string>();
  for (const node of props.nodes) {
    labels.set(node.node_id, node.display_name || node.node_id);
  }
  return labels;
});

function nodeLabel(nodeId: string): string {
  return nodeLabels.value.get(nodeId) ?? nodeId;
}

const nodeResults = computed(() => {
  const query = nodeSearch.value.trim().toLowerCase();
  const selected = new Set(
    nodePickerOption.value === null ? [] : optionNodes(nodePickerOption.value)
  );
  const matches = props.nodes.filter((node) => {
    if (!query) {
      return true;
    }
    return `${node.node_id} ${node.display_name} ${node.stage_display_name} ${node.kingdom} ${node.node_type}`
      .toLowerCase()
      .includes(query);
  });
  // Already-chosen nodes stay visible at the top so they can be removed from
  // inside the dialog, even when the search text no longer matches them.
  const chosen = props.nodes.filter((node) => selected.has(node.node_id));
  const rest = matches.filter((node) => !selected.has(node.node_id));
  return {
    rows: [...chosen, ...rest.slice(0, kMaxNodeResults)],
    hiddenCount: Math.max(0, rest.length - kMaxNodeResults)
  };
});

function openNodePicker(alternativeIndex: number) {
  nodePickerOption.value = alternativeIndex;
  nodeSearch.value = '';
  void nextTick(() => nodeSearchInput.value?.focus());
}

function closeNodePicker() {
  nodePickerOption.value = null;
  nodeSearch.value = '';
}

function nodeChosen(nodeId: string): boolean {
  return nodePickerOption.value !== null
    && optionNodes(nodePickerOption.value).includes(nodeId);
}

watch(
  () => props.modelValue,
  () => {
    if (nodePickerOption.value !== null
      && nodePickerOption.value >= customModel.value.alternatives.length) {
      closeNodePicker();
    }
  }
);

function cloneAlternatives(alternatives: string[][], length?: number): string[][] {
  const cloned = alternatives.length > 0
    ? alternatives.map((alternative) => [...alternative])
    : [[]];
  // Node lists are index-parallel with the token lists, so a shorter one (an
  // option with no node access authored) is padded rather than left ragged.
  while (length !== undefined && cloned.length < length) {
    cloned.push([]);
  }
  return cloned;
}
</script>

<template>
  <section class="requirement-editor">
    <div class="field-label">{{ label }}</div>

    <div class="segmented" role="group" :aria-label="label">
      <button
        type="button"
        :class="{ active: activeMode === 'unset' }"
        title="Leave this blank for later"
        @click="changeMode('unset')"
      >
        {{ emptyLabel }}
      </button>
      <button
        type="button"
        :class="{ active: activeMode === 'free' }"
        title="No requirement"
        @click="changeMode('free')"
      >
        Free
      </button>
      <button
        type="button"
        :class="{ active: activeMode === 'impossible' }"
        title="Intentionally unreachable"
        @click="changeMode('impossible')"
      >
        Blocked
      </button>
      <button
        type="button"
        :class="{ active: activeMode === 'custom' }"
        title="Build OR and AND requirements from known tokens"
        @click="changeMode('custom')"
      >
        Custom
      </button>
    </div>

    <div v-if="activeMode === 'raw'" class="raw-warning">
      <AlertTriangle :size="16" />
      <span>{{ parsed.errors.join(' ') || 'This expression needs attention.' }}</span>
    </div>

    <div v-if="activeMode !== 'custom' && activeMode !== 'impossible'" class="single-option">
      <div class="single-option-row">
        <span>Option 1</span>
        <select
          class="option-difficulty"
          :value="optionDifficulty(0)"
          title="Logic difficulty for this movement option"
          @change="updateOptionDifficulty(0, ($event.target as HTMLSelectElement).value)"
        >
          <option v-for="level in ROUTE_DIFFICULTY_LEVELS" :key="level.value" :value="level.value">
            {{ level.label }}
          </option>
        </select>
      </div>
      <div v-if="hasScenarioChoices" class="scenario-toggles">
        <span class="scenario-toggles-label">Scenarios</span>
        <label
          v-for="scenario in availableScenarios"
          :key="`single:${scenario}`"
          class="scenario-chip"
          :class="{ checked: scenarioChecked(0, scenario) }"
        >
          <input
            type="checkbox"
            :checked="scenarioChecked(0, scenario)"
            @change="toggleScenario(0, scenario)"
          />
          {{ scenario }}
        </label>
      </div>
    </div>

    <textarea
      v-if="activeMode === 'raw'"
      class="raw-expression"
      :value="modelValue"
      spellcheck="false"
      @input="updateRaw(($event.target as HTMLTextAreaElement).value)"
    />

    <div v-if="activeMode === 'custom'" class="builder">
      <div
        v-for="(alternative, alternativeIndex) in customModel.alternatives"
        :key="alternativeIndex"
        class="alternative"
      >
        <div class="alternative-head">
          <span>Option {{ alternativeIndex + 1 }}</span>
          <select
            class="option-difficulty"
            :value="optionDifficulty(alternativeIndex)"
            title="Logic difficulty for this movement option"
            @change="updateOptionDifficulty(alternativeIndex, ($event.target as HTMLSelectElement).value)"
          >
            <option v-for="level in ROUTE_DIFFICULTY_LEVELS" :key="level.value" :value="level.value">
              {{ level.label }}
            </option>
          </select>
          <button
            v-if="customModel.alternatives.length > 1"
            type="button"
            class="icon-button small"
            title="Remove OR option"
            @click="removeOrOption(alternativeIndex)"
          >
            <X :size="15" />
          </button>
        </div>

        <div v-if="hasScenarioChoices" class="scenario-toggles">
          <span class="scenario-toggles-label">Scenarios</span>
          <label
            v-for="scenario in availableScenarios"
            :key="`${alternativeIndex}:scenario:${scenario}`"
            class="scenario-chip"
            :class="{ checked: scenarioChecked(alternativeIndex, scenario) }"
          >
            <input
              type="checkbox"
              :checked="scenarioChecked(alternativeIndex, scenario)"
              @change="toggleScenario(alternativeIndex, scenario)"
            />
            {{ scenario }}
          </label>
        </div>

        <div class="token-chips">
          <button
            v-for="token in alternative"
            :key="token"
            type="button"
            class="chip selected"
            title="Remove token"
            @click="toggleToken(alternativeIndex, token)"
          >
            {{ token }}
          </button>
          <span v-if="alternative.length === 0" class="empty-state">Select tokens below</span>
        </div>

        <div class="token-picker">
          <input v-model="search" type="search" placeholder="Filter tokens" />
          <div class="token-groups">
            <div v-for="[group, groupTokens] in groupedTokens" :key="group" class="token-group">
              <div class="token-group-title">{{ group }}</div>
              <button
                v-for="token in groupTokens"
                :key="`${alternativeIndex}:${token.token}`"
                type="button"
                class="chip"
                :class="{ selected: alternative.includes(token.token) }"
                :title="token.notes || token.token"
                @click="toggleToken(alternativeIndex, token.token)"
              >
                {{ token.display_name || token.token }}
              </button>
            </div>
          </div>
        </div>

        <div v-if="nodes.length > 0" class="node-access">
          <div class="node-access-head">
            <span class="field-label">Node Access</span>
            <button
              type="button"
              class="text-button"
              title="Require the player to be able to reach another node"
              @click="openNodePicker(alternativeIndex)"
            >
              <MapPin :size="15" />
              Add Node
            </button>
          </div>
          <div class="token-chips">
            <button
              v-for="nodeId in optionNodes(alternativeIndex)"
              :key="`${alternativeIndex}:node:${nodeId}`"
              type="button"
              class="chip selected node-chip"
              :title="`Remove ${nodeId}`"
              @click="toggleNode(alternativeIndex, nodeId)"
            >
              {{ nodeLabel(nodeId) }}
            </button>
            <span v-if="optionNodes(alternativeIndex).length === 0" class="empty-state">
              No node prerequisites
            </span>
          </div>
        </div>
      </div>

      <button type="button" class="text-button" @click="addOrOption">
        <Plus :size="16" />
        Add OR Option
      </button>
    </div>

    <div v-if="nodePickerOption !== null" class="node-dialog-backdrop" @click.self="closeNodePicker">
      <div class="node-dialog" role="dialog" aria-label="Select nodes this option requires">
        <div class="node-dialog-head">
          <span>Node access for Option {{ nodePickerOption + 1 }}</span>
          <button type="button" class="icon-button small" title="Close" @click="closeNodePicker">
            <X :size="15" />
          </button>
        </div>
        <input
          ref="nodeSearchInput"
          v-model="nodeSearch"
          type="search"
          placeholder="Search nodes by id, name, stage or kingdom"
        />
        <div class="node-dialog-list">
          <button
            v-for="node in nodeResults.rows"
            :key="`picker:${node.node_id}`"
            type="button"
            class="node-row"
            :class="{ selected: nodeChosen(node.node_id) }"
            @click="toggleNode(nodePickerOption, node.node_id)"
          >
            <span class="node-row-name">{{ node.display_name || node.node_id }}</span>
            <span class="node-row-meta">
              {{ node.stage_display_name || node.stage }} · {{ node.node_type }}
            </span>
            <span class="node-row-id">{{ node.node_id }}</span>
          </button>
          <p v-if="nodeResults.rows.length === 0" class="empty-state">No nodes match that search.</p>
        </div>
        <p v-if="nodeResults.hiddenCount > 0" class="node-dialog-footer">
          {{ nodeResults.hiddenCount }} more match - keep typing to narrow the list.
        </p>
      </div>
    </div>

    <code class="expression-preview" @dblclick="replaceFromModel(parsed)">
      {{ modelValue || '<blank>' }}
    </code>
  </section>
</template>