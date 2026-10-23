<script setup lang="ts">
import { computed, onBeforeUnmount, reactive, ref, watch } from 'vue';
import type { GraphEdge, GraphNode, Position } from '../lib/types';

const NODE_WIDTH = 190;
const NODE_HEIGHT = 76;
const MOON_WIDTH = 178;
const MOON_HEIGHT = 72;

const props = defineProps<{
  nodes: GraphNode[];
  edges: GraphEdge[];
  selectedId: string;
  connectSource: string;
  focus: { id: string; nonce: number } | null;
}>();

const emit = defineEmits<{
  select: [kind: 'node' | 'edge' | 'moon', id: string];
  activateNode: [id: string];
  updatePosition: [id: string, position: Position];
}>();

const canvas = ref<HTMLElement | null>(null);
const viewport = reactive({ x: 24, y: 24, scale: 0.82 });
const pointerState = ref<
  | { mode: 'pan'; pointerId: number; startX: number; startY: number; originX: number; originY: number }
  | {
      mode: 'node';
      pointerId: number;
      id: string;
      startX: number;
      startY: number;
      origin: Position;
      moved: boolean;
    }
  | null
>(null);

// While a node is being dragged, its live position lives here instead of in
// props.nodes. This keeps `nodeMap` / `edgeLayouts` / `worldSize` (which are
// derived from props.nodes and are O(nodes) / O(edges)) from recomputing on
// every pointermove — they only recompute once, when the drag is committed.
const dragPreview = ref<{ id: string; x: number; y: number } | null>(null);
// Edge ids touching the node currently being dragged, snapshotted once at
// drag start so we don't re-filter every edge on every frame.
const draggedEdgeIds = ref<Set<string>>(new Set());

let pendingDelta: { dx: number; dy: number } | null = null;
let dragRaf: number | null = null;

const nodeMap = computed(() => new Map(props.nodes.map((node) => [node.id, node])));

const flashingId = ref('');
let activeFocusNonce = 0;

function centerOnNode(id: string) {
  const node = nodeMap.value.get(id);
  const rect = canvas.value?.getBoundingClientRect();
  if (!node || !rect) {
    return;
  }
  const size = nodeSize(node);
  const cx = node.x + size.width / 2;
  const cy = node.y + size.height / 2;
  viewport.x = rect.width / 2 - cx * viewport.scale;
  viewport.y = rect.height / 2 - cy * viewport.scale;
}

// Pan to and pulse a node when a validation issue is clicked. The nonce lets the
// same node re-flash on repeated clicks.
watch(
  () => props.focus,
  (focus) => {
    if (!focus) {
      return;
    }
    activeFocusNonce = focus.nonce;
    centerOnNode(focus.id);
    flashingId.value = focus.id;
    window.setTimeout(() => {
      if (activeFocusNonce === focus.nonce) {
        flashingId.value = '';
      }
    }, 1400);
  }
);

const worldSize = computed(() => {
  let width = 4200;
  let height = 3200;

  for (const node of props.nodes) {
    const size = nodeSize(node);
    width = Math.max(width, node.x + size.width + 420);
    height = Math.max(height, node.y + size.height + 340);
  }

  return { width, height };
});

function nodeSize(node: GraphNode) {
  return node.kind === 'moon'
    ? { width: MOON_WIDTH, height: MOON_HEIGHT }
    : { width: NODE_WIDTH, height: NODE_HEIGHT };
}

// Resolved on-screen position for a node: the live drag preview if it's the
// node currently being dragged, otherwise its committed position.
function resolvedPosition(node: GraphNode): Position {
  if (dragPreview.value && dragPreview.value.id === node.id) {
    return { x: dragPreview.value.x, y: dragPreview.value.y };
  }
  return { x: node.x, y: node.y };
}

function buildEdgeLayout(edge: GraphEdge, positionOf: (node: GraphNode) => Position) {
  const from = nodeMap.value.get(edge.from);
  const to = nodeMap.value.get(edge.to);
  if (!from && !to) {
    return undefined;
  }

  let start: Position | undefined;
  let end: Position | undefined;

  if (from && to) {
    const fromPos = positionOf(from);
    const toPos = positionOf(to);
    const fromSize = nodeSize(from);
    const toSize = nodeSize(to);
    start = { x: fromPos.x + fromSize.width / 2, y: fromPos.y + fromSize.height / 2 };
    end = { x: toPos.x + toSize.width / 2, y: toPos.y + toSize.height / 2 };
  } else if (from) {
    const fromPos = positionOf(from);
    const fromSize = nodeSize(from);
    start = { x: fromPos.x + fromSize.width / 2, y: fromPos.y + fromSize.height / 2 };
    end = { x: start.x + 160, y: start.y + 60 };
  } else if (to) {
    const toPos = positionOf(to);
    const toSize = nodeSize(to);
    end = { x: toPos.x + toSize.width / 2, y: toPos.y + toSize.height / 2 };
    start = { x: end.x - 160, y: end.y - 60 };
  }

  const dx = end!.x - start!.x;
  const dy = end!.y - start!.y;
  const curve = Math.max(-160, Math.min(160, dx * 0.12));
  const control = {
    x: start!.x + dx / 2,
    y: start!.y + dy / 2 - curve
  };
  return {
    edge,
    path: `M ${start!.x} ${start!.y} Q ${control.x} ${control.y} ${end!.x} ${end!.y}`,
    label: {
      x: (start!.x + end!.x + control.x) / 3,
      y: (start!.y + end!.y + control.y) / 3
    }
  };
}

// Baseline layout, built only from committed positions (never reads
// dragPreview). Because of that it only recomputes when props.nodes /
// props.edges actually change — i.e. once per drag, on commit — instead of
// on every pointermove.
const edgeLayouts = computed(() =>
  props.edges.map((edge) => buildEdgeLayout(edge, (node) => ({ x: node.x, y: node.y }))).filter(Boolean)
);

// Per-frame override: for the handful of edges touching the node currently
// being dragged, recompute their path live; everything else reuses the
// memoized layout above untouched.
function livePath(layout: NonNullable<ReturnType<typeof buildEdgeLayout>>): string {
  if (dragPreview.value && draggedEdgeIds.value.has(layout.edge.id)) {
    const live = buildEdgeLayout(layout.edge, resolvedPosition);
    if (live) {
      return live.path;
    }
  }
  return layout.path;
}

function liveLabel(layout: NonNullable<ReturnType<typeof buildEdgeLayout>>): Position {
  if (dragPreview.value && draggedEdgeIds.value.has(layout.edge.id)) {
    const live = buildEdgeLayout(layout.edge, resolvedPosition);
    if (live) {
      return live.label;
    }
  }
  return layout.label;
}

function worldFromPointer(event: PointerEvent): Position {
  const rect = canvas.value?.getBoundingClientRect();
  if (!rect) {
    return { x: 0, y: 0 };
  }

  return {
    x: (event.clientX - rect.left - viewport.x) / viewport.scale,
    y: (event.clientY - rect.top - viewport.y) / viewport.scale
  };
}

function beginPan(event: PointerEvent) {
  if (event.button !== 0) {
    return;
  }

  pointerState.value = {
    mode: 'pan',
    pointerId: event.pointerId,
    startX: event.clientX,
    startY: event.clientY,
    originX: viewport.x,
    originY: viewport.y
  };
  canvas.value?.setPointerCapture(event.pointerId);
}

function beginNodeDrag(event: PointerEvent, node: GraphNode) {
  if (event.button !== 0) {
    return;
  }

  event.stopPropagation();
  emit('select', node.kind === 'moon' ? 'moon' : 'node', node.id);
  pointerState.value = {
    mode: 'node',
    pointerId: event.pointerId,
    id: node.id,
    startX: event.clientX,
    startY: event.clientY,
    origin: { x: node.x, y: node.y },
    moved: false
  };
  dragPreview.value = { id: node.id, x: node.x, y: node.y };
  draggedEdgeIds.value = new Set(
    props.edges.filter((edge) => edge.from === node.id || edge.to === node.id).map((edge) => edge.id)
  );
  (event.currentTarget as HTMLElement).setPointerCapture(event.pointerId);
}

function movePointer(event: PointerEvent) {
  const state = pointerState.value;
  if (!state || state.pointerId !== event.pointerId) {
    return;
  }

  if (state.mode === 'pan') {
    viewport.x = state.originX + event.clientX - state.startX;
    viewport.y = state.originY + event.clientY - state.startY;
    return;
  }

  // Stash the latest delta and let a single rAF apply it, so bursts of
  // pointermove events (high-polling-rate mice/trackpads) collapse into at
  // most one update per rendered frame instead of one per raw event.
  pendingDelta = {
    dx: (event.clientX - state.startX) / viewport.scale,
    dy: (event.clientY - state.startY) / viewport.scale
  };

  if (dragRaf === null) {
    dragRaf = requestAnimationFrame(applyPendingDrag);
  }
}

function applyPendingDrag() {
  dragRaf = null;
  const state = pointerState.value;
  if (!state || state.mode !== 'node' || !pendingDelta) {
    return;
  }

  const { dx, dy } = pendingDelta;
  pendingDelta = null;

  if (Math.abs(dx) + Math.abs(dy) > 4) {
    state.moved = true;
  }

  dragPreview.value = {
    id: state.id,
    x: Math.max(0, state.origin.x + dx),
    y: Math.max(0, state.origin.y + dy)
  };
}

function endPointer(event: PointerEvent) {
  const state = pointerState.value;
  if (!state || state.pointerId !== event.pointerId) {
    return;
  }

  if (dragRaf !== null) {
    cancelAnimationFrame(dragRaf);
    dragRaf = null;
  }
  pendingDelta = null;

  if (state.mode === 'node') {
    if (state.moved && dragPreview.value) {
      // Commit exactly once, instead of on every intermediate frame.
      emit('updatePosition', state.id, { x: dragPreview.value.x, y: dragPreview.value.y });
    } else if (!state.moved) {
      emit('activateNode', state.id);
    }
    dragPreview.value = null;
    draggedEdgeIds.value = new Set();
  }

  pointerState.value = null;
}

function zoom(event: WheelEvent) {
  event.preventDefault();
  const before = worldFromPointer(event as unknown as PointerEvent);
  const nextScale = Math.min(1.5, Math.max(0.35, viewport.scale - event.deltaY * 0.001));
  const rect = canvas.value?.getBoundingClientRect();
  if (!rect) {
    viewport.scale = nextScale;
    return;
  }

  viewport.scale = nextScale;
  viewport.x = event.clientX - rect.left - before.x * viewport.scale;
  viewport.y = event.clientY - rect.top - before.y * viewport.scale;
}

function resetView() {
  viewport.x = 24;
  viewport.y = 24;
  viewport.scale = 0.82;
}

function fitLayout() {
  const rect = canvas.value?.getBoundingClientRect();
  if (!rect || props.nodes.length === 0) {
    resetView();
    return;
  }

  let minX = Number.POSITIVE_INFINITY;
  let minY = Number.POSITIVE_INFINITY;
  let maxX = 0;
  for (const node of props.nodes) {
    const size = nodeSize(node);
    minX = Math.min(minX, node.x);
    minY = Math.min(minY, node.y);
    maxX = Math.max(maxX, node.x + size.width);
  }

  const layoutWidth = Math.max(1, maxX - minX);
  const scale = Math.min(0.82, Math.max(0.28, (rect.width - 80) / layoutWidth));
  viewport.scale = scale;
  viewport.x = 40 - minX * scale;
  viewport.y = 40 - minY * scale;
}

defineExpose({ fitLayout });

function selectEdge(edgeId: string) {
  pointerState.value = null;
  emit('select', 'edge', edgeId);
}

onBeforeUnmount(() => {
  if (dragRaf !== null) {
    cancelAnimationFrame(dragRaf);
  }
});
</script>

<template>
  <section
    ref="canvas"
    class="graph-canvas"
    data-testid="graph-canvas"
    @pointerdown="beginPan"
    @pointermove="movePointer"
    @pointerup="endPointer"
    @pointercancel="endPointer"
    @wheel="zoom"
  >
    <button
      type="button"
      class="reset-view"
      title="Reset canvas view"
      draggable="false"
      @pointerdown.stop
      @mousedown.stop.prevent
      @dragstart.prevent
      @click.stop="resetView"
    >
      Reset
    </button>

    <div
      class="graph-world"
      :style="{
        transform: `translate(${viewport.x}px, ${viewport.y}px) scale(${viewport.scale})`
      }"
    >
      <svg
        class="edge-layer"
        :width="worldSize.width"
        :height="worldSize.height"
        :viewBox="`0 0 ${worldSize.width} ${worldSize.height}`"
      >
        <defs>
          <marker id="edge-arrow" viewBox="0 0 12 12" refX="10" refY="6" markerWidth="10" markerHeight="10" orient="auto">
            <path d="M 0 0 L 12 6 L 0 12 z" />
          </marker>
        </defs>
        <g v-for="layout in edgeLayouts" :key="layout!.edge.id">
          <path
            :d="livePath(layout!)"
            class="edge-path"
            :class="[
              layout!.edge.kind,
              {
                bidirectional: layout!.edge.bidirectional,
                selected: selectedId === layout!.edge.id
              }
            ]"
            :marker-start="layout!.edge.bidirectional ? 'url(#edge-arrow)' : undefined"
            marker-end="url(#edge-arrow)"
          />
          <path
            :d="livePath(layout!)"
            class="edge-hit"
            @pointerdown.stop.prevent="selectEdge(layout!.edge.id)"
            @click.stop="selectEdge(layout!.edge.id)"
          />
        </g>
      </svg>

      <button
        v-for="layout in edgeLayouts"
        :key="`${layout!.edge.id}:label`"
        type="button"
        class="edge-label"
        :class="{ selected: selectedId === layout!.edge.id }"
        :style="{
          transform: `translate(${liveLabel(layout!).x}px, ${liveLabel(layout!).y}px)`
        }"
        :title="layout!.edge.label"
        @pointerdown.stop
        @click.stop="selectEdge(layout!.edge.id)"
      >
        {{ layout!.edge.label }}
      </button>

      <article
        v-for="node in nodes"
        :key="node.id"
        class="graph-node"
        :class="[node.kind, node.type, { selected: selectedId === node.id, connectSource: connectSource === node.id, flashing: flashingId === node.id, dragging: dragPreview?.id === node.id }]"
        :style="{
          transform: `translate3d(${resolvedPosition(node).x}px, ${resolvedPosition(node).y}px, 0)`,
          width: `${nodeSize(node).width}px`,
          height: `${nodeSize(node).height}px`
        }"
        :title="node.id"
        @pointerdown="beginNodeDrag($event, node)"
      >
        <div class="node-label">{{ node.label }}</div>
        <div class="node-subtitle">{{ node.subtitle }}</div>
      </article>
    </div>
  </section>
</template>