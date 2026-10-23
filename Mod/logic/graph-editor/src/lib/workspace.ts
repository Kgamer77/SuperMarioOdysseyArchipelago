import { cloneRows, stringifyCsv } from './csv';
import { isActiveEntranceIn, isActiveEntranceOut, isExcludedEntranceNode } from '../data/entranceExclusions';
import { embeddedZoneByStage, type EmbeddedZoneRow } from '../data/embeddedZones';
import { effortLabel, normalizeEffortLevel } from './effort';
import {
  countRequirementOptions,
  describeRequirement,
  nodeRequirementLabel,
  parseRequirementExpression
} from './requirements';
import {
  normalizeRouteDifficulties,
  normalizeRouteDifficulty,
  routeDifficultyForOption,
  routeDifficultyLabel,
  stringifyRouteDifficulties
} from './routeDifficulty';
import type {
  CaptureGate,
  CaptureGateObject,
  CsvDocument,
  GraphEdge,
  GraphFilters,
  GraphNode,
  LogicNode,
  LogicTable,
  LogicToken,
  LogicWorkspace,
  MoonAccess,
  Position,
  ProgressionEvent,
  Room,
  RouteConnection,
  RouteEdge,
  ScenarioMetadata
} from './types';

export const LOGIC_TABLES: LogicTable[] = [
  'logicNodes',
  'routeEdges',
  'moonAccess',
  'logicTokens',
  'rooms',
  'progressionEvents',
  'scenarioMetadata',
  'captureGates',
  'captureGateObjects'
];

interface WorkspaceDocuments {
  logicNodes: CsvDocument<LogicNode>;
  routeEdges: CsvDocument<RouteEdge>;
  moonAccess: CsvDocument<MoonAccess>;
  logicTokens: CsvDocument<LogicToken>;
  rooms: CsvDocument<Room>;
  progressionEvents: CsvDocument<ProgressionEvent>;
  scenarioMetadata: CsvDocument<ScenarioMetadata>;
  captureGates: CsvDocument<CaptureGate>;
  captureGateObjects: CsvDocument<CaptureGateObject>;
}

type CoordinateLayoutRow = {
  id: string;
  row: LogicNode | MoonAccess;
  gameX: number;
  gameZ: number;
};

type CoordinateBounds = {
  minX: number;
  maxX: number;
  minZ: number;
  maxZ: number;
};

const FIXED_TRANSITION_NODES: Record<string, { label: string; destinationStage: string }> = {
  'PeachWorldPictureBossForestStage::stage_change::PictureBossForest::raw': {
    label: 'Painting to Torkdrift Boss Re-fight',
    destinationStage: 'RevengeForestBossStage'
  },
  'PeachWorldPictureBossKnuckleStage::stage_change::PictureBossKnuckle::raw': {
    label: 'Painting to Knucklotec Boss Re-fight',
    destinationStage: 'RevengeBossKnuckleStage'
  },
  'PeachWorldPictureBossMagmaStage::stage_change::PictureBossMagma::raw': {
    label: 'Painting to Cookatiel Boss Re-fight',
    destinationStage: 'RevengeBossMagmaStage'
  },
  'PeachWorldPictureBossRaidStage::stage_change::PictureBossRaid::raw': {
    label: 'Painting to Lord of Lightning Boss Re-fight',
    destinationStage: 'RevengeBossRaidStage'
  },
  'PeachWorldPictureGiantWanderBossStage::stage_change::PictureGiantWanderBoss::raw': {
    label: 'Painting to Mollusque-Lanceur Boss Re-fight',
    destinationStage: 'RevengeGiantWanderBossStage'
  },
  'PeachWorldPictureMofumofuStage::stage_change::PictureMofumofu::raw': {
    label: 'Painting to Mechawiggler Boss Re-fight',
    destinationStage: 'RevengeMofumofuStage'
  }
};
const FIXED_TRANSITION_STAGES = new Set(
  Object.keys(FIXED_TRANSITION_NODES).map((nodeId) => nodeId.split('::')[0])
);

const REMOVED_HEADERS = new Set(['status']);

export function buildWorkspace(documents: WorkspaceDocuments): LogicWorkspace {
  const moonAccess = normalizeMoonRows(cloneRows(documents.moonAccess.rows));
  const csvHeaders = Object.fromEntries(
    LOGIC_TABLES.map((table) => [table, normalizeHeaders(documents[table].headers)])
  ) as LogicWorkspace['csvHeaders'];
  csvHeaders.routeEdges = ensureRouteEdgeHeaders(csvHeaders.routeEdges);
  csvHeaders.moonAccess = ensureMoonAccessHeaders(csvHeaders.moonAccess);
  csvHeaders.captureGates = ensureCaptureGateHeaders(csvHeaders.captureGates);

  return flattenLegacyEmbeddedZones({
    schemaVersion: 2,
    loadedAt: new Date().toISOString(),
    csvHeaders,
    logicNodes: normalizeLogicNodeRows(cloneRows(documents.logicNodes.rows)),
    routeEdges: normalizeRouteRows(cloneRows(documents.routeEdges.rows)),
    moonAccess,
    logicTokens: stripRemovedFields(cloneRows(documents.logicTokens.rows)),
    rooms: normalizeRoomRows(cloneRows(documents.rooms.rows)),
    progressionEvents: stripRemovedFields(cloneRows(documents.progressionEvents.rows)),
    scenarioMetadata: stripRemovedFields(cloneRows(documents.scenarioMetadata.rows)),
    captureGates: normalizeCaptureGateRows(cloneRows(documents.captureGates.rows)),
    captureGateObjects: cloneRows(documents.captureGateObjects.rows),
    nodePositions: {}
  });
}

export function cloneWorkspace(workspace: LogicWorkspace): LogicWorkspace {
  const csvHeaders = Object.fromEntries(
    Object.entries(workspace.csvHeaders).map(([table, headers]) => [table, normalizeHeaders(headers)])
  ) as LogicWorkspace['csvHeaders'];
  csvHeaders.routeEdges = ensureRouteEdgeHeaders(csvHeaders.routeEdges);
  csvHeaders.moonAccess = ensureMoonAccessHeaders(csvHeaders.moonAccess);
  csvHeaders.captureGates = ensureCaptureGateHeaders(csvHeaders.captureGates);

  return flattenLegacyEmbeddedZones({
    ...workspace,
    csvHeaders,
    logicNodes: normalizeLogicNodeRows(cloneRows(workspace.logicNodes)),
    routeEdges: normalizeRouteRows(cloneRows(workspace.routeEdges)),
    moonAccess: normalizeMoonRows(cloneRows(workspace.moonAccess)),
    logicTokens: stripRemovedFields(cloneRows(workspace.logicTokens)),
    rooms: normalizeRoomRows(cloneRows(workspace.rooms)),
    progressionEvents: stripRemovedFields(cloneRows(workspace.progressionEvents)),
    scenarioMetadata: stripRemovedFields(cloneRows(workspace.scenarioMetadata)),
    captureGates: normalizeCaptureGateRows(cloneRows(workspace.captureGates ?? [])),
    captureGateObjects: cloneRows(workspace.captureGateObjects ?? []),
    nodePositions: Object.fromEntries(
      Object.entries(workspace.nodePositions).map(([id, position]) => [id, { ...position }])
    )
  });
}

export function syncWorkspaceWithTemplate(workspace: LogicWorkspace, template: LogicWorkspace): LogicWorkspace {
  const synced = cloneWorkspace(workspace);

  for (const table of LOGIC_TABLES) {
    synced.csvHeaders[table] = mergeHeaders(template.csvHeaders[table], synced.csvHeaders[table]);
  }
  synced.csvHeaders.routeEdges = ensureRouteEdgeHeaders(synced.csvHeaders.routeEdges);
  synced.csvHeaders.moonAccess = ensureMoonAccessHeaders(synced.csvHeaders.moonAccess);

  mergeTemplateRows(synced.logicTokens, template.logicTokens, (row) => row.token);
  mergeTemplateRows(synced.rooms, template.rooms, (row) => row.stage);
  mergeTemplateRows(synced.logicNodes, template.logicNodes, (row) => row.node_id);
  mergeTemplateRows(
    synced.routeEdges,
    template.routeEdges.filter((row) => row.source === 'fixed_transition'),
    (row) => row.edge_id
  );
  mergeTemplateRows(synced.progressionEvents, template.progressionEvents, (row) => row.event_token);
  mergeTemplateRows(
    synced.scenarioMetadata,
    template.scenarioMetadata,
    (row) => `${row.stage}::${row.scenario_no}::${row.source_archive}::${row.source_file}`
  );
  mergeTemplateRows(synced.moonAccess, template.moonAccess, (row) => moonRowKey(row));
  mergeTemplateRows(synced.captureGates, template.captureGates, (row) => row.gate_id);
  mergeTemplateRows(
    synced.captureGateObjects,
    template.captureGateObjects,
    (row) => `${row.gate_id}::${row.placement_stage}::${row.object_id}`
  );

  return cloneWorkspace(synced);
}

export function sliceWorkspaceForKingdom(workspace: LogicWorkspace, kingdom: string): LogicWorkspace {
  const sliced = cloneWorkspace(workspace);
  normalizeRouteKingdomsFromEndpoints(sliced, kingdom);

  sliced.logicNodes = sliced.logicNodes.filter((row) => row.kingdom === kingdom);
  sliced.routeEdges = sliced.routeEdges.filter((row) => row.kingdom === kingdom);
  sliced.moonAccess = sliced.moonAccess.filter((row) => row.kingdom === kingdom);
  sliced.rooms = sliced.rooms.filter((row) => row.kingdom === kingdom);
  sliced.progressionEvents = sliced.progressionEvents.filter((row) => row.kingdom === kingdom);
  sliced.scenarioMetadata = sliced.scenarioMetadata.filter((row) => row.kingdom === kingdom);
  sliced.captureGates = sliced.captureGates.filter((row) => row.kingdom === kingdom);
  const slicedGateIds = new Set(sliced.captureGates.map((row) => row.gate_id));
  sliced.captureGateObjects = sliced.captureGateObjects.filter((row) => slicedGateIds.has(row.gate_id));

  const positionIds = nodePositionIdsForKingdom(sliced, kingdom);
  sliced.nodePositions = Object.fromEntries(
    Object.entries(sliced.nodePositions).filter(([id]) => positionIds.has(id))
  );

  return sliced;
}

export function mergeKingdomWorkspace(
  workspace: LogicWorkspace,
  kingdomWorkspace: LogicWorkspace,
  kingdom: string
): LogicWorkspace {
  const merged = cloneWorkspace(workspace);
  const incoming = cloneWorkspace(kingdomWorkspace);
  normalizeRouteKingdomsFromEndpoints(merged, kingdom);
  normalizeRouteKingdomsFromEndpoints(incoming, kingdom);
  const replacedPositionIds = nodePositionIdsForKingdom(merged, kingdom);
  const incomingPositionIds = nodePositionIdsForKingdom(incoming, kingdom);

  for (const table of LOGIC_TABLES) {
    merged.csvHeaders[table] = mergeHeaders(merged.csvHeaders[table], incoming.csvHeaders[table]);
  }
  merged.csvHeaders.routeEdges = ensureRouteEdgeHeaders(merged.csvHeaders.routeEdges);
  merged.csvHeaders.moonAccess = ensureMoonAccessHeaders(merged.csvHeaders.moonAccess);

  merged.logicNodes = replaceRowsForKingdom(merged.logicNodes, incoming.logicNodes, kingdom);
  merged.routeEdges = replaceRowsForKingdom(merged.routeEdges, incoming.routeEdges, kingdom);
  merged.moonAccess = replaceRowsForKingdom(merged.moonAccess, incoming.moonAccess, kingdom);
  merged.rooms = replaceRowsForKingdom(merged.rooms, incoming.rooms, kingdom);
  merged.progressionEvents = replaceRowsForKingdom(merged.progressionEvents, incoming.progressionEvents, kingdom);
  merged.scenarioMetadata = replaceRowsForKingdom(merged.scenarioMetadata, incoming.scenarioMetadata, kingdom);
  const replacedGateIds = new Set(
    merged.captureGates.filter((row) => row.kingdom === kingdom).map((row) => row.gate_id)
  );
  merged.captureGates = replaceRowsForKingdom(merged.captureGates, incoming.captureGates, kingdom);
  merged.captureGateObjects = [
    ...merged.captureGateObjects.filter((row) => !replacedGateIds.has(row.gate_id)),
    ...cloneRows(incoming.captureGateObjects)
  ];
  if (incoming.logicTokens.length > 0) {
    merged.logicTokens = incoming.logicTokens;
  }

  merged.nodePositions = Object.fromEntries(
    Object.entries(merged.nodePositions).filter(([id]) => !replacedPositionIds.has(id))
  );
  for (const id of incomingPositionIds) {
    const position = incoming.nodePositions[id];
    if (position) {
      merged.nodePositions[id] = { ...position };
    }
  }

  return cloneWorkspace(merged);
}

export function listKingdoms(workspace: LogicWorkspace): string[] {
  return [...new Set(workspace.rooms.map((room) => room.kingdom).filter(Boolean))].sort();
}

/**
 * Highest ScenarioNo each kingdom can actually reach, mirroring
 * SCENARIO_MAX_LOGIC_BY_KINGDOM in logic/randomizer_logic.py (the max of its
 * peace, post-game and moon-rock scenarios). Metro and Luncheon go well past the
 * 5 this editor used to hard-code, which is why routes there could not express a
 * restriction above 5 and both runtimes had to ignore the mask entirely up there.
 *
 * Kingdoms absent from this map fall back to 5, the previous behaviour.
 */
const MAX_SCENARIO_BY_KINGDOM: Record<string, number> = {
  'Cap Kingdom': 4,
  'Cascade Kingdom': 4,
  'Sand Kingdom': 5,
  'Wooded Kingdom': 5,
  'Lake Kingdom': 4,
  'Cloud Kingdom': 4,
  'Lost Kingdom': 4,
  'Metro Kingdom': 8,
  'Seaside Kingdom': 4,
  'Snow Kingdom': 4,
  'Luncheon Kingdom': 8,
  'Ruined Kingdom': 4,
  "Bowser's Kingdom": 4,
  'Moon Kingdom': 3,
  'Mushroom Kingdom': 3,
  'Dark Side': 2,
  'Darker Side': 2
};

/**
 * Every scenario the kingdom can be in, as checkbox labels.
 *
 * Scenarios a kingdom can never reach (SCENARIO_BLOCKED_BY_KINGDOM) are still
 * listed: ticking one changes nothing, whereas hiding it would silently drop
 * that bit from option_scenarios the next time the row is saved.
 *
 * Note the mask's highest ticked scenario is open-ended at runtime - see
 * extend_top_scenario in randomizer_logic.py. Ticking the last box means "this
 * scenario and anything after it"; gaps below it stay closed.
 */
export function availableScenariosForKingdom(_workspace: LogicWorkspace, kingdom: string): string[] {
  const max = MAX_SCENARIO_BY_KINGDOM[kingdom] ?? 5;
  return Array.from({ length: max }, (_, index) => String(index + 1));
}

export function listRoomsForKingdom(workspace: LogicWorkspace, kingdom: string): Room[] {
  return workspace.rooms
    .filter((room) => room.kingdom === kingdom)
    .sort((a, b) => {
      const areaCompare = areaRank(a.area_scope) - areaRank(b.area_scope);
      if (areaCompare !== 0) {
        return areaCompare;
      }
      return (a.stage_display_name || a.stage).localeCompare(b.stage_display_name || b.stage);
    });
}

export function roomLabel(room: Room): string {
  return room.stage_display_name && room.stage_display_name !== room.stage
    ? `${room.stage_display_name} (${room.stage})`
    : room.stage;
}

export function getRoom(workspace: LogicWorkspace, stage: string): Room | undefined {
  return workspace.rooms.find((room) => room.stage === stage);
}

export function ensureLayoutForStage(workspace: LogicWorkspace, stage: string): void {
  layoutStage(workspace, stage, false);
}

export function arrangeLayoutForStage(workspace: LogicWorkspace, stage: string): void {
  layoutStage(workspace, stage, true);
}

export function arrangeMapLayoutForStage(workspace: LogicWorkspace, stage: string): void {
  arrangeCoordinateLayout(workspace, stage);
}

export function isSyntheticDefaultStart(node: LogicNode): boolean {
  return (
    node.node_type === 'stage_start' &&
    node.source === 'generated' &&
    !node.entrance_id &&
    !node.obj_id
  );
}

export function shouldHideDefaultStart(workspace: LogicWorkspace, stage: string): boolean {
  const room = getRoom(workspace, stage);
  if (room?.area_scope !== 'subarea') {
    return false;
  }

  return workspace.logicNodes.some(
    (node) =>
      node.stage === stage &&
      !isExcludedSource(node) &&
      node.node_type === 'substage_entrance' &&
      (node.side === 'sub_near' || node.side === 'sub_far')
  );
}

export function shouldShowLogicNode(workspace: LogicWorkspace, node: LogicNode): boolean {
  if (isNonPhysicalProgressionState(node)) {
    return false;
  }
  if (isSyntheticDefaultStart(node) && shouldHideDefaultStart(workspace, node.stage)) {
    return false;
  }

  return !isDuplicateEntranceSideNode(workspace, node);
}

export function isRawStageDataTransition(node: LogicNode): boolean {
  return (
    node.node_type === 'stage_change' &&
    node.side === 'raw' &&
    node.source === 'StageData' &&
    !isPaintingTransition(node)
  );
}

export function isPaintingTransition(node: LogicNode): boolean {
  if (node.source === 'fixed_transition' || node.node_type === 'fixed_transition') {
    return true;
  }
  return (
    node.node_type === 'stage_change' &&
    node.source === 'StageData' &&
    node.notes.includes('UnitConfigName=WorldWarpHole') &&
    (node.entrance_id === 'Come' || node.entrance_id === 'Go')
  );
}

export function isNonPhysicalProgressionState(node: LogicNode): boolean {
  return node.node_type === 'state_event' && node.source === 'progression_events';
}

// Exclusion is derived live from the config (entrance_links.csv), never from a
// frozen `source` value in the save — a stale `EntranceDB:excluded` marker is
// ignored here and scrubbed in normalizeLogicNodeRows.
function isExcludedSource(row: Record<string, string>): boolean {
  return isExcludedEntranceNode(row.node_id);
}

function graphRowsForStage(workspace: LogicWorkspace, stage: string): Array<LogicNode | MoonAccess> {
  return [
    ...workspace.logicNodes
      .filter((node) => node.stage === stage)
      .filter((node) => shouldShowLogicNode(workspace, node)),
    ...workspace.moonAccess.filter((moon) => moon.stage === stage)
  ];
}

function coordinateRowsFromGraphRows(graphRows: Array<LogicNode | MoonAccess>): CoordinateLayoutRow[] {
  return graphRows
    .map((row) => ({
      id: isMoonAccess(row) ? moonNodeId(moonRowKey(row)) : row.node_id,
      row,
      gameX: toNumber(isMoonAccess(row) ? row.placement_x : row.x),
      gameZ: toNumber(isMoonAccess(row) ? row.placement_z : row.z)
    }))
    .filter((row) => Number.isFinite(row.gameX) && Number.isFinite(row.gameZ));
}

function layoutStage(workspace: LogicWorkspace, stage: string, force: boolean): void {
  if (force) {
    arrangeEditorLayout(workspace, stage);
    return;
  }

  const graphRows = graphRowsForStage(workspace, stage);
  const coordinateRows = coordinateRowsFromGraphRows(graphRows);

  const bounds = makeBounds(coordinateRows);
  const fallbackCounters = new Map<string, number>();
  const occupied: Position[] = [];

  for (const row of graphRows) {
    const id = isMoonAccess(row) ? moonNodeId(moonRowKey(row)) : row.node_id;
    if (!force && workspace.nodePositions[id]) {
      occupied.push(workspace.nodePositions[id]);
      continue;
    }

    const gameX = toNumber(isMoonAccess(row) ? row.placement_x : row.x);
    const gameZ = toNumber(isMoonAccess(row) ? row.placement_z : row.z);
    let position: Position;
    if (bounds && Number.isFinite(gameX) && Number.isFinite(gameZ)) {
      position = mapGamePosition(gameX, gameZ, bounds);
    } else {
      const lane = layoutLane(row);
      const count = fallbackCounters.get(lane) ?? 0;
      fallbackCounters.set(lane, count + 1);
      position = fallbackPosition(lane, count);
    }

    const resolved = avoidCollision(position, occupied);
    workspace.nodePositions[id] = resolved;
    occupied.push(resolved);
  }
}

function arrangeEditorLayout(workspace: LogicWorkspace, stage: string): void {
  const logicRows = workspace.logicNodes
    .filter((node) => node.stage === stage)
    .filter((node) => shouldShowLogicNode(workspace, node));
  const moonRows = workspace.moonAccess.filter((moon) => moon.stage === stage);
  if (logicRows.length === 0) {
    return;
  }

  const logicById = new Map(logicRows.map((node) => [node.node_id, node]));
  const allNodeById = new Map(workspace.logicNodes.map((node) => [node.node_id, node]));
  const adjacency = new Map(logicRows.map((node) => [node.node_id, new Set<string>()]));
  const internalRoutes = workspace.routeEdges
    .map((edge) => ({
      from: visibleCanonicalNodeId(workspace, edge.from_node_id, allNodeById),
      to: visibleCanonicalNodeId(workspace, edge.to_node_id, allNodeById)
    }))
    .filter(
      (edge) =>
        edge.from !== edge.to &&
        logicById.has(edge.from) &&
        logicById.has(edge.to)
    );

  for (const edge of internalRoutes) {
    adjacency.get(edge.from)?.add(edge.to);
    adjacency.get(edge.to)?.add(edge.from);
  }

  const moonSourceIds = new Set(
    moonRows
      .map((moon) => visibleCanonicalNodeId(workspace, moon.from_node_id, allNodeById))
      .filter((nodeId) => logicById.has(nodeId))
  );
  const anchorIds = new Set(
    logicRows
      .filter(
        (node) =>
          isArrangeClusterAnchor(node) ||
          moonSourceIds.has(node.node_id) ||
          (adjacency.get(node.node_id)?.size ?? 0) >= 3
      )
      .map((node) => node.node_id)
  );

  if (anchorIds.size === 0) {
    anchorIds.add([...logicRows].sort(compareLogicNodesForLayout)[0].node_id);
  }

  const rootByNodeId = new Map<string, string>();
  const clusters = new Map<string, ArrangeCluster>();
  const ensureCluster = (rootId: string) => {
    let cluster = clusters.get(rootId);
    if (!cluster) {
      cluster = { root: logicById.get(rootId)!, logicMembers: [], moons: [] };
      clusters.set(rootId, cluster);
    }
    return cluster;
  };

  for (const node of [...logicRows].sort(compareLogicNodesForLayout)) {
    const rootId = anchorIds.has(node.node_id)
      ? node.node_id
      : nearestArrangeAnchor(node.node_id, anchorIds, adjacency) ?? node.node_id;
    rootByNodeId.set(node.node_id, rootId);
    const cluster = ensureCluster(rootId);
    if (node.node_id !== rootId) {
      cluster.logicMembers.push(node);
    }
  }

  const fallbackRootId =
    logicRows.find((node) => node.node_type === 'stage_start')?.node_id ??
    [...clusters.keys()].sort()[0];
  for (const moon of [...moonRows].sort(compareMoonsForLayout)) {
    const sourceId = visibleCanonicalNodeId(workspace, moon.from_node_id, allNodeById);
    const rootId = rootByNodeId.get(sourceId) ?? fallbackRootId;
    ensureCluster(rootId).moons.push(moon);
  }

  const clusterAdjacency = new Map([...clusters.keys()].map((rootId) => [rootId, new Set<string>()]));
  for (const edge of internalRoutes) {
    const fromRoot = rootByNodeId.get(edge.from);
    const toRoot = rootByNodeId.get(edge.to);
    if (!fromRoot || !toRoot || fromRoot === toRoot) {
      continue;
    }
    clusterAdjacency.get(fromRoot)?.add(toRoot);
    clusterAdjacency.get(toRoot)?.add(fromRoot);
  }

  const startRoots = logicRows
    .filter((node) => node.node_type === 'stage_start')
    .map((node) => rootByNodeId.get(node.node_id))
    .filter((rootId): rootId is string => !!rootId);
  const clusterDepths = arrangeClusterDepths(
    clusterAdjacency,
    startRoots.length > 0 ? startRoots : [fallbackRootId]
  );
  const clusterLayouts = [...clusters.values()].map((cluster) => ({
    cluster,
    layout: makeArrangeClusterLayout(cluster),
    depth: clusterDepths.get(cluster.root.node_id) ?? Number.MAX_SAFE_INTEGER
  }));
  const finiteDepths = clusterLayouts
    .map((entry) => entry.depth)
    .filter((depth) => Number.isFinite(depth) && depth !== Number.MAX_SAFE_INTEGER);
  const disconnectedDepth = (finiteDepths.length > 0 ? Math.max(...finiteDepths) : 0) + 1;
  const layers = new Map<number, typeof clusterLayouts>();

  for (const entry of clusterLayouts) {
    const depth = entry.depth === Number.MAX_SAFE_INTEGER ? disconnectedDepth : entry.depth;
    const layer = layers.get(depth) ?? [];
    layer.push(entry);
    layers.set(depth, layer);
  }

  const orderedLayers = [...layers.entries()].sort(([left], [right]) => left - right);
  const layerMetrics = orderedLayers.map(([depth, entries]) => {
    entries.sort((left, right) => compareArrangeClusters(left.cluster, right.cluster));
    return { depth, ...packArrangeLayer(entries) };
  });
  let layerY = 100;

  for (const layer of layerMetrics) {
    for (const placement of layer.placements) {
      const clusterX = 100 + placement.x;
      const clusterY = layerY + placement.y;
      const entry = placement.entry;
      for (const [nodeId, position] of entry.layout.positions) {
        workspace.nodePositions[nodeId] = {
          x: Math.round(clusterX + position.x),
          y: Math.round(clusterY + position.y)
        };
      }
    }
    layerY += layer.height + 300;
  }
}

function arrangeCoordinateLayout(workspace: LogicWorkspace, stage: string): void {
  const graphRows = graphRowsForStage(workspace, stage);
  const coordinateRows = coordinateRowsFromGraphRows(graphRows);
  const bounds = makeBounds(coordinateRows);

  if (!bounds) {
    arrangeEditorLayout(workspace, stage);
    return;
  }

  const occupied: Position[] = [];
  for (const row of coordinateRows.sort(compareCoordinateRowsForLayout)) {
    const position = mapGamePosition(row.gameX, row.gameZ, bounds, 2380, 1760);
    const resolved = avoidMapCollision(position, occupied);
    workspace.nodePositions[row.id] = resolved;
    occupied.push(resolved);
  }

  const coordinateIds = new Set(coordinateRows.map((row) => row.id));
  const fallbackRows = graphRows
    .map((row) => ({
      id: isMoonAccess(row) ? moonNodeId(moonRowKey(row)) : row.node_id,
      row
    }))
    .filter((row) => !coordinateIds.has(row.id));
  placeMapFallbackRows(workspace, fallbackRows, 2680, 120);
}

export function getGraphForStage(
  workspace: LogicWorkspace,
  stage: string,
  filters: GraphFilters
): { nodes: GraphNode[]; edges: GraphEdge[] } {
  ensureLayoutForStage(workspace, stage);

  const search = filters.search.trim().toLowerCase();
  const includeRow = (row: Record<string, string>) => filters.showExcluded || !isExcludedSource(row);

  const logicNodes = workspace.logicNodes
    .filter((node) => node.stage === stage)
    .filter((node) => shouldShowLogicNode(workspace, node))
    .filter((node) => filters.showRawAnchors || !isRawStageDataTransition(node))
    .filter((node) => filters.showEvents || node.node_type !== 'state_event')
    .filter(includeRow)
    .map((node) => logicGraphNode(workspace, node, workspace.nodePositions[node.node_id] ?? { x: 0, y: 0 }));

  const moonNodes = filters.showMoons
    ? workspace.moonAccess
        .filter((moon) => moon.stage === stage)
        .filter(includeRow)
        .map((moon) =>
          moonGraphNode(moon, workspace.nodePositions[moonNodeId(moonRowKey(moon))] ?? { x: 0, y: 0 })
        )
    : [];

  const nodes = [...logicNodes, ...moonNodes].filter((node) => {
    if (!search) {
      return true;
    }
    return `${node.id} ${node.label} ${node.subtitle} ${node.type}`.toLowerCase().includes(search);
  });

  const visibleIds = new Set(nodes.map((node) => node.id));
  // A merged overworld door hides its home_out side; redirect edges that point
  // at the hidden side to the surviving (home_in) node so authored return routes
  // attach to the single node instead of dangling as a between-levels link.
  //
  // visibleCanonicalNodeId resolves entrance-merging by scanning
  // workspace.logicNodes internally; without a shared index this ran an O(N)
  // scan twice per edge (from + to) for *every* route edge in the whole
  // workspace on every call to getGraphForStage — O(nodes x edges). Building
  // the id -> node map once here turns each lookup into O(1).
  const nodeById = new Map(workspace.logicNodes.map((node) => [node.node_id, node]));
  const visibleRouteEdges = workspace.routeEdges
    .map((edge) => {
      const from = visibleCanonicalNodeId(workspace, edge.from_node_id, nodeById);
      const to = visibleCanonicalNodeId(workspace, edge.to_node_id, nodeById);
      return from === edge.from_node_id && to === edge.to_node_id
        ? edge
        : { ...edge, from_node_id: from, to_node_id: to };
    })
    .filter(includeRow)
    .filter((edge) => edge.from_node_id !== edge.to_node_id)
    .filter((edge) => visibleIds.has(edge.from_node_id) || visibleIds.has(edge.to_node_id));

  const routeEdges = groupRouteConnections(visibleRouteEdges).map((connection) => ({
    id: connection.id,
    kind: 'route' as const,
    from: connection.from,
    to: connection.to,
    label: describeRouteConnection(connection, workspace.logicTokens, workspace.logicNodes, workspace),
    bidirectional: isConnectionBidirectional(connection),
    sourceRow: connection.forward,
    reverseRow: connection.reverse
  }));

  const moonEdges = filters.showMoons
    ? workspace.moonAccess
        .filter((moon) => moon.from_node_id)
        .filter(includeRow)
        .filter((moon) => visibleIds.has(moon.from_node_id) && visibleIds.has(moonNodeId(moonRowKey(moon))))
        .map((moon) => ({
          id: `moon_edge:${moonRowKey(moon)}`,
          kind: 'moon_access' as const,
          from: moon.from_node_id,
          to: moonNodeId(moonRowKey(moon)),
          label: describeMoonRequirement(moon, workspace.logicTokens),
          bidirectional: false,
          sourceRow: moon
        }))
    : [];

  return { nodes, edges: [...routeEdges, ...moonEdges] };
}

export function createCustomNode(workspace: LogicWorkspace, room: Room, name = 'New Place'): LogicNode {
  const base = slugify(name || 'place');
  let index = 1;
  let nodeId = `${room.stage}::place::${base}`;
  const existingIds = new Set(workspace.logicNodes.map((node) => node.node_id));
  while (existingIds.has(nodeId)) {
    index += 1;
    nodeId = `${room.stage}::place::${base}_${index}`;
  }

  const node = makeBlankRow(workspace.csvHeaders.logicNodes) as LogicNode;
  Object.assign(node, {
    node_id: nodeId,
    kingdom: room.kingdom,
    stage: room.stage,
    stage_display_name: room.stage_display_name,
    area_scope: room.area_scope,
    node_type: 'custom_place',
    source: 'editor',
    display_name: name,
    notes: 'Human-authored logic place.'
  });

  const localCustomIndex = workspace.logicNodes.filter(
    (existing) => existing.stage === room.stage && existing.node_type === 'custom_place'
  ).length;
  workspace.logicNodes.push(node);
  workspace.nodePositions[nodeId] = fallbackPosition('custom_place', localCustomIndex);
  return node;
}

export function createRouteEdge(
  workspace: LogicWorkspace,
  fromNodeId: string,
  toNodeId: string,
  room: Room
): RouteEdge {
  const edge = makeBlankRow(workspace.csvHeaders.routeEdges) as RouteEdge;
  edge.edge_id = nextEdgeId(workspace);
  edge.kingdom = room.kingdom;
  edge.area_scope = room.area_scope;
  edge.from_node_id = fromNodeId;
  edge.to_node_id = toNodeId;
  edge.requirement_expr = '';
  edge.edge_type = 'human_route';
  edge.difficulty = 'casual';
  edge.option_scenarios = '';
  edge.bidirectional = 'true';
  edge.source = 'editor';
  edge.notes = 'Created in graph editor.';
  workspace.routeEdges.push(edge);
  return edge;
}

export function createReverseEdge(workspace: LogicWorkspace, edge: RouteEdge): RouteEdge {
  const reverse = makeBlankRow(workspace.csvHeaders.routeEdges) as RouteEdge;
  edge.bidirectional = 'false';
  Object.assign(reverse, edge);
  reverse.edge_id = nextEdgeId(workspace);
  reverse.from_node_id = edge.to_node_id;
  reverse.to_node_id = edge.from_node_id;
  reverse.requirement_expr = '';
  reverse.requirement_notes = '';
  reverse.bidirectional = 'false';
  reverse.source = 'editor';
  reverse.notes = `Reverse of ${edge.edge_id}.`;
  workspace.routeEdges.push(reverse);
  return reverse;
}

export function connectMoonToNode(workspace: LogicWorkspace, moonNode: string, fromNodeId: string): MoonAccess | undefined {
  const moonKey = moonNode.replace(/^moon:/, '');
  const moon = workspace.moonAccess.find(
    (candidate) => moonRowKey(candidate) === moonKey || candidate.moon_id === moonKey
  );
  if (!moon) {
    return undefined;
  }

  moon.from_node_id = fromNodeId;
  return moon;
}

export function deleteCustomNode(workspace: LogicWorkspace, nodeId: string): boolean {
  const node = workspace.logicNodes.find((candidate) => candidate.node_id === nodeId);
  if (!node) {
    return false;
  }

  workspace.logicNodes = workspace.logicNodes.filter((candidate) => candidate.node_id !== nodeId);
  const removedEdgeIds = workspace.routeEdges
    .filter((edge) => edge.from_node_id === nodeId || edge.to_node_id === nodeId)
    .map((edge) => edge.edge_id);
  workspace.routeEdges = workspace.routeEdges.filter(
    (edge) => edge.from_node_id !== nodeId && edge.to_node_id !== nodeId
  );
  deleteCaptureGatesForEdges(workspace, removedEdgeIds);
  for (const moon of workspace.moonAccess) {
    if (moon.from_node_id === nodeId) {
      moon.from_node_id = '';
    }
  }
  delete workspace.nodePositions[nodeId];
  return true;
}

export function deleteRouteEdge(workspace: LogicWorkspace, edgeId: string): boolean {
  const initialLength = workspace.routeEdges.length;
  workspace.routeEdges = workspace.routeEdges.filter((edge) => edge.edge_id !== edgeId);
  if (workspace.routeEdges.length !== initialLength) {
    deleteCaptureGatesForEdges(workspace, [edgeId]);
  }
  return workspace.routeEdges.length !== initialLength;
}

export function deleteRouteConnection(workspace: LogicWorkspace, connectionId: string): boolean {
  const connection = findRouteConnection(workspace, connectionId);
  if (connection?.forward.source === 'fixed_transition') {
    return false;
  }
  const initialLength = workspace.routeEdges.length;
  const removedEdgeIds = workspace.routeEdges
    .filter((edge) => routeConnectionId(edge.from_node_id, edge.to_node_id) === connectionId)
    .map((edge) => edge.edge_id);
  workspace.routeEdges = workspace.routeEdges.filter(
    (edge) => routeConnectionId(edge.from_node_id, edge.to_node_id) !== connectionId
  );
  deleteCaptureGatesForEdges(workspace, removedEdgeIds);
  return workspace.routeEdges.length !== initialLength;
}

function deleteCaptureGatesForEdges(workspace: LogicWorkspace, edgeIds: string[]): void {
  const edgeIdSet = new Set(edgeIds);
  const gateIds = new Set(
    workspace.captureGates
      .filter((gate) => edgeIdSet.has(gate.edge_id))
      .map((gate) => gate.gate_id)
  );
  workspace.captureGates = workspace.captureGates.filter((gate) => !gateIds.has(gate.gate_id));
  workspace.captureGateObjects = workspace.captureGateObjects
    .filter((member) => !gateIds.has(member.gate_id));
}

export function resetStageFromTemplate(
  workspace: LogicWorkspace,
  template: LogicWorkspace,
  stage: string
): boolean {
  const templateNodes = template.logicNodes.filter((node) => node.stage === stage);
  const templateMoons = template.moonAccess.filter((moon) => moon.stage === stage);
  if (templateNodes.length === 0 && templateMoons.length === 0) {
    return false;
  }

  const currentNodeIds = new Set(
    workspace.logicNodes.filter((node) => node.stage === stage).map((node) => node.node_id)
  );
  const templateNodeIds = new Set(templateNodes.map((node) => node.node_id));
  const resetNodeIds = new Set([...currentNodeIds, ...templateNodeIds]);
  const resetMoonIds = new Set([
    ...workspace.moonAccess
      .filter((moon) => moon.stage === stage)
      .map((moon) => moonNodeId(moonRowKey(moon))),
    ...templateMoons.map((moon) => moonNodeId(moonRowKey(moon)))
  ]);

  workspace.logicNodes = [
    ...workspace.logicNodes.filter((node) => node.stage !== stage),
    ...cloneRows(templateNodes)
  ];
  workspace.moonAccess = [
    ...workspace.moonAccess.filter((moon) => moon.stage !== stage),
    ...cloneRows(templateMoons)
  ];

  const templateRoutes = template.routeEdges.filter(
    (edge) => templateNodeIds.has(edge.from_node_id) || templateNodeIds.has(edge.to_node_id)
  );
  workspace.routeEdges = [
    ...workspace.routeEdges.filter(
      (edge) =>
        !resetNodeIds.has(edge.from_node_id) &&
        !resetNodeIds.has(edge.to_node_id) &&
        !resetMoonIds.has(edge.from_node_id) &&
        !resetMoonIds.has(edge.to_node_id)
    ),
    ...cloneRows(templateRoutes)
  ];

  const replacedGateIds = new Set(
    workspace.captureGates.filter((gate) => gate.logic_stage === stage).map((gate) => gate.gate_id)
  );
  workspace.captureGates = [
    ...workspace.captureGates.filter((gate) => gate.logic_stage !== stage),
    ...cloneRows(template.captureGates.filter((gate) => gate.logic_stage === stage))
  ];
  workspace.captureGateObjects = [
    ...workspace.captureGateObjects.filter((member) => !replacedGateIds.has(member.gate_id)),
    ...cloneRows(template.captureGateObjects.filter((member) =>
      template.captureGates.some((gate) =>
        gate.logic_stage === stage && gate.gate_id === member.gate_id
      )
    ))
  ];

  for (const nodeId of resetNodeIds) {
    delete workspace.nodePositions[nodeId];
  }
  for (const moonId of resetMoonIds) {
    delete workspace.nodePositions[moonId];
  }

  return true;
}

export function exportEditableCsvs(workspace: LogicWorkspace): Record<string, string> {
  return {
    'logic_nodes.csv': stringifyCsv(workspace.logicNodes, workspace.csvHeaders.logicNodes),
    'route_edges.csv': stringifyCsv(normalizeRouteRows(workspace.routeEdges), workspace.csvHeaders.routeEdges),
    'moon_access.csv': stringifyCsv(workspace.moonAccess, workspace.csvHeaders.moonAccess),
    'capture_gates.csv': stringifyCsv(workspace.captureGates, workspace.csvHeaders.captureGates),
    'capture_gate_objects.csv': stringifyCsv(
      workspace.captureGateObjects,
      workspace.csvHeaders.captureGateObjects
    )
  };
}

export function moonNodeId(moonId: string): string {
  return `moon:${moonId}`;
}

export function moonRowKey(moon: MoonAccess): string {
  return moon.__editor_id || moon.moon_id;
}

export function isStoryMoon(moon: MoonAccess): boolean {
  return moon.flags.split('|').some((flag) => flag.trim() === 'ShineFlag_Story');
}

export function findNode(workspace: LogicWorkspace, nodeId: string): LogicNode | undefined {
  return workspace.logicNodes.find((node) => node.node_id === nodeId);
}

export function findEdge(workspace: LogicWorkspace, edgeId: string): RouteEdge | undefined {
  return workspace.routeEdges.find((edge) => edge.edge_id === edgeId);
}

export function routeConnectionId(fromNodeId: string, toNodeId: string): string {
  const [first, second] = sortRoutePair(fromNodeId, toNodeId);
  return `route_pair:${encodeURIComponent(first)}|${encodeURIComponent(second)}`;
}

export function findRouteConnection(
  workspace: LogicWorkspace,
  connectionId: string
): RouteConnection | undefined {
  return groupRouteConnections(
    workspace.routeEdges.filter((edge) => routeConnectionId(edge.from_node_id, edge.to_node_id) === connectionId)
  )[0];
}

export function findMoon(workspace: LogicWorkspace, moonIdOrNodeId: string): MoonAccess | undefined {
  const moonKey = moonIdOrNodeId.replace(/^moon:/, '');
  return workspace.moonAccess.find((moon) => moonRowKey(moon) === moonKey || moon.moon_id === moonKey);
}

function groupRouteConnections(edges: RouteEdge[]): RouteConnection[] {
  const grouped = new Map<string, RouteEdge[]>();
  for (const edge of edges) {
    const id = routeConnectionId(edge.from_node_id, edge.to_node_id);
    grouped.set(id, [...(grouped.get(id) ?? []), edge]);
  }

  return [...grouped.entries()]
    .map(([id, routeEdges]) => makeRouteConnection(id, routeEdges))
    .sort((a, b) => a.forward.edge_id.localeCompare(b.forward.edge_id));
}

function makeRouteConnection(id: string, routeEdges: RouteEdge[]): RouteConnection {
  const ordered = [...routeEdges].sort((a, b) => a.edge_id.localeCompare(b.edge_id));
  const forward = ordered[0];
  const reverse = ordered.find(
    (edge) => edge.from_node_id === forward.to_node_id && edge.to_node_id === forward.from_node_id
  );

  return {
    id,
    from: forward.from_node_id,
    to: forward.to_node_id,
    forward,
    reverse,
    routeEdges: ordered,
    duplicateCount: Math.max(0, ordered.length - (reverse ? 2 : 1))
  };
}

function describeRouteConnection(
  connection: RouteConnection,
  tokens: LogicToken[],
  _nodes: LogicNode[],
  _workspace: LogicWorkspace
): string {
  const forwardLabel = describeRouteDirection(connection.forward, tokens);
  if (!connection.reverse) {
    return forwardLabel;
  }

  const reverseLabel = describeRouteDirection(connection.reverse, tokens);
  if (forwardLabel === reverseLabel) {
    return forwardLabel;
  }

  return `${forwardLabel} / ${reverseLabel}`;
}

function describeRouteDirection(edge: RouteEdge, tokens: LogicToken[]): string {
  const byToken = new Map(tokens.map((token) => [token.token, token.display_name || token.token]));
  const model = parseRequirementExpression(edge.requirement_expr, byToken.keys());

  if (model.mode !== 'custom') {
    const requirement = describeRequirement(edge.requirement_expr, tokens);
    const difficulty = routeDifficultyLabel(routeDifficultyForOption(edge.difficulty || 'casual', 0));
    return difficulty === 'Casual' ? requirement : `${difficulty}: ${requirement}`;
  }

  const stage = edge.from_node_id.split('::', 1)[0];
  return model.alternatives
    .map((alternative, index) => {
      const requirement = [
        ...alternative.map((token) => byToken.get(token) ?? token),
        ...(model.nodeAlternatives[index] ?? []).map((node) => nodeRequirementLabel(node, stage))
      ].join(' + ');
      const difficulty = routeDifficultyLabel(routeDifficultyForOption(edge.difficulty || 'casual', index));
      return difficulty === 'Casual' ? requirement : `${difficulty}: ${requirement}`;
    })
    .join(' OR ');
}

function describeMoonRequirement(moon: MoonAccess, tokens: LogicToken[]): string {
  const byToken = new Map(tokens.map((token) => [token.token, token.display_name || token.token]));
  const model = parseRequirementExpression(moon.requirement_expr, byToken.keys());

  if (model.mode !== 'custom') {
    const requirement = describeRequirement(moon.requirement_expr, tokens);
    const difficulty = routeDifficultyLabel(routeDifficultyForOption(moon.difficulty || 'casual', 0));
    return difficulty === 'Casual' ? requirement : `${difficulty}: ${requirement}`;
  }

  return model.alternatives
    .map((alternative, index) => {
      const requirement = [
        ...alternative.map((token) => byToken.get(token) ?? token),
        ...(model.nodeAlternatives[index] ?? []).map((node) => nodeRequirementLabel(node, moon.stage))
      ].join(' + ');
      const difficulty = routeDifficultyLabel(routeDifficultyForOption(moon.difficulty || 'casual', index));
      return difficulty === 'Casual' ? requirement : `${difficulty}: ${requirement}`;
    })
    .join(' OR ');
}

function isConnectionBidirectional(connection: RouteConnection): boolean {
  if (!connection.reverse) {
    return true;
  }
  return connection.reverse.requirement_expr.trim().toLowerCase() !== 'impossible';
}

function isDuplicateEntranceSideNode(workspace: LogicWorkspace, node: LogicNode): boolean {
  if (!node.entrance_id) {
    return false;
  }

  // Only the overworld side of a door collapses into a single node: logic just
  // needs to reach "the door". A subarea always keeps its entrance and exit as
  // two separate nodes (arrive at one, traverse, leave from the other) so the
  // entrance randomizer can wire each side independently.
  if (node.node_type === 'home_entrance' && node.side === 'home_out') {
    return !!findEntranceSidePartner(workspace, node, 'home_in');
  }

  return false;
}

// Stage reached by crossing this entrance (its opposite-type partner on another
// stage). Home<->subarea partners always share an entrance_id even when the two
// *sides* of the door do not.
function entrancePartnerStage(workspace: LogicWorkspace, node: LogicNode): string {
  if (!node.entrance_id) {
    return '';
  }

  const partnerNodeType =
    node.node_type === 'home_entrance'
      ? 'substage_entrance'
      : node.node_type === 'substage_entrance'
        ? 'home_entrance'
        : node.node_type;

  const partner = workspace.logicNodes.find(
    (candidate) =>
      candidate.entrance_id === node.entrance_id &&
      candidate.node_type === partnerNodeType &&
      candidate.stage !== node.stage &&
      candidate.kingdom === node.kingdom &&
      !isExcludedSource(candidate)
  );

  return partner ? partner.stage : '';
}

// The opposite side (enter vs. return) of the same physical door, if any.
// Real doors give the two sides different entrance_ids (doukutu1 / doukutu2),
// so first try an exact id match (one id reused for both sides), then fall back
// to a strictly 1:1 pairing through the shared target stage.
function findEntranceSidePartner(
  workspace: LogicWorkspace,
  node: LogicNode,
  partnerSide: string
): LogicNode | undefined {
  if (!node.entrance_id) {
    return undefined;
  }

  const opposite = workspace.logicNodes.filter(
    (candidate) =>
      candidate.node_id !== node.node_id &&
      candidate.stage === node.stage &&
      candidate.node_type === node.node_type &&
      candidate.side === partnerSide &&
      !isExcludedSource(candidate)
  );

  const byId = opposite.find((candidate) => candidate.entrance_id === node.entrance_id);
  if (byId) {
    return byId;
  }

  // Only overworld doors ever collapse, so the target-stage fallback (different
  // id per side) only runs for home_entrance. Subarea sides never reach here.
  if (node.node_type !== 'home_entrance') {
    return undefined;
  }

  const targetStage = entrancePartnerStage(workspace, node);
  if (!targetStage) {
    return undefined;
  }

  const oppositeToTarget = opposite.filter(
    (candidate) => entrancePartnerStage(workspace, candidate) === targetStage
  );
  if (oppositeToTarget.length !== 1) {
    return undefined;
  }

  // Only collapse when this side is itself the unique door to that target,
  // so exit-only extras (a second return with no matching entrance) stay split.
  const sameSideToTarget = workspace.logicNodes.filter(
    (candidate) =>
      candidate.stage === node.stage &&
      candidate.node_type === node.node_type &&
      candidate.side === node.side &&
      !isExcludedSource(candidate) &&
      entrancePartnerStage(workspace, candidate) === targetStage
  );
  if (sameSideToTarget.length !== 1) {
    return undefined;
  }

  return oppositeToTarget[0];
}

function hasCollapsedEntrancePartner(workspace: LogicWorkspace, node: LogicNode): boolean {
  if (!node.entrance_id) {
    return false;
  }

  // Only the overworld entrance side merges its return into a single node.
  if (node.node_type === 'home_entrance' && node.side === 'home_in') {
    return !!findEntranceSidePartner(workspace, node, 'home_out');
  }

  return false;
}

// The randomizer role of a home-side door, derived from which sides the active
// entrance links actually use. EntranceDB generates both a home_in and a home_out
// node for every entrance_id, so node presence alone over-reports doors as two-way
// — here we look at the collapsed pair's ids against the active in/out sets:
//   - both sides used  -> 'both'   (a real two-way door, shown without a tag)
//   - only the in side -> 'entrance' (enter-only)
//   - only the out side -> 'exit'    (return/exit-only)
//   - neither          -> 'none'    (not part of an active link; legacy fallback)
function entranceHomeRole(
  workspace: LogicWorkspace,
  node: LogicNode
): 'both' | 'entrance' | 'exit' | 'none' {
  if (node.node_type !== 'home_entrance') {
    return 'none';
  }

  const partnerSide = node.side === 'home_in' ? 'home_out' : 'home_in';
  const partner = findEntranceSidePartner(workspace, node, partnerSide);
  const ids = partner ? [node.node_id, partner.node_id] : [node.node_id];

  const canEnter = ids.some((id) => isActiveEntranceIn(id));
  const canExit = ids.some((id) => isActiveEntranceOut(id));

  if (canEnter && canExit) {
    return 'both';
  }
  if (canEnter) {
    return 'entrance';
  }
  if (canExit) {
    return 'exit';
  }
  return 'none';
}

// If nodeId is the home_out side that the overworld merge hides, return the
// surviving home_in partner so authored return edges attach to the single node
// instead of dangling. Excluded/unpaired sides are left untouched.
function mergedEntranceCanonicalId(
  workspace: LogicWorkspace,
  nodeId: string,
  nodeById?: Map<string, LogicNode>
): string {
  const node = nodeById ? nodeById.get(nodeId) : workspace.logicNodes.find((candidate) => candidate.node_id === nodeId);
  if (node && node.node_type === 'home_entrance' && node.side === 'home_out') {
    const partner = findEntranceSidePartner(workspace, node, 'home_in');
    if (partner) {
      return partner.node_id;
    }
  }
  return nodeId;
}

function visibleCanonicalNodeId(
  workspace: LogicWorkspace,
  nodeId: string,
  nodeById?: Map<string, LogicNode>
): string {
  const mergedId = mergedEntranceCanonicalId(workspace, nodeId, nodeById);
  const node = nodeById ? nodeById.get(mergedId) : workspace.logicNodes.find((candidate) => candidate.node_id === mergedId);
  if (
    !node
    || !FIXED_TRANSITION_STAGES.has(node.stage)
    || node.node_type !== 'stage_change'
    || node.source !== 'StageData'
    || node.side !== 'raw'
    || !node.entrance_id
    || node.entrance_id.startsWith('Picture')
    || !node.obj_id
  ) {
    return mergedId;
  }

  const surfaced = workspace.logicNodes.find(
    (candidate) =>
      candidate.stage === node.stage
      && candidate.node_type === 'substage_entrance'
      && candidate.entrance_id === node.entrance_id
      && candidate.obj_id === node.obj_id
  );
  return surfaced?.node_id ?? mergedId;
}

function sortRoutePair(fromNodeId: string, toNodeId: string): [string, string] {
  return fromNodeId.localeCompare(toNodeId) <= 0 ? [fromNodeId, toNodeId] : [toNodeId, fromNodeId];
}

function routePairKey(fromNodeId: string, toNodeId: string): string {
  return `${fromNodeId}\0${toNodeId}`;
}

function logicGraphNode(workspace: LogicWorkspace, node: LogicNode, position: Position): GraphNode {
  const fixedTransition = FIXED_TRANSITION_NODES[node.node_id];
  return {
    id: node.node_id,
    kind: 'logic',
    label: logicNodeDisplayName(workspace, node),
    subtitle: fixedTransition ? 'fixed vanilla transition' : node.side ? `${node.node_type} / ${node.side}` : node.node_type,
    type: fixedTransition ? 'fixed_transition' : node.node_type,
    stage: node.stage,
    kingdom: node.kingdom,
    areaScope: node.area_scope,
    x: position.x,
    y: position.y,
    sourceRow: node
  };
}

function entranceTargetName(workspace: LogicWorkspace, node: LogicNode): string {
  if (!node.entrance_id) {
    return '';
  }

  const partnerNodeType =
    node.node_type === 'home_entrance' ? 'substage_entrance' :
    node.node_type === 'substage_entrance' ? 'home_entrance' :
    node.node_type;

  const partner = workspace.logicNodes.find(
    (candidate) =>
      candidate.entrance_id === node.entrance_id &&
      candidate.node_type === partnerNodeType &&
      candidate.stage !== node.stage &&
      candidate.kingdom === node.kingdom
  );

  if (partner) {
    const room = workspace.rooms.find((r) => r.stage === partner.stage);
    if (room?.stage_display_name) {
      return room.stage_display_name;
    }
  }

  return '';
}

function logicNodeDisplayName(workspace: LogicWorkspace, node: LogicNode): string {
  const fixedTransition = FIXED_TRANSITION_NODES[node.node_id];
  if (fixedTransition) {
    return fixedTransition.label;
  }
  const targetName = entranceTargetName(workspace, node);

  if (node.node_type === 'home_entrance') {
    const label = targetName || node.entrance_id || node.display_name || node.node_id;
    // Tag the door by the role the config actually gives it, so an entrance-only
    // and an exit-only door to the same subarea (which otherwise collapse to the
    // same target-name label) read as distinct rather than as a duplicate.
    const role = entranceHomeRole(workspace, node);
    if (role === 'entrance') {
      return `Overworld entrance: ${label} (ENTRANCE)`;
    }
    if (role === 'exit') {
      return `Overworld return: ${label} (EXIT)`;
    }
    if (role === 'both') {
      return `Overworld entrance/return: ${label}`;
    }
    // Node not referenced by any active link: keep the legacy side-based labels.
    if (node.side === 'home_in' && hasCollapsedEntrancePartner(workspace, node)) {
      return `Overworld entrance/return: ${label}`;
    }
    if (node.side === 'home_in') {
      return `Overworld entrance: ${label}`;
    }
    if (node.side === 'home_out') {
      return `Overworld return: ${label}`;
    }
  }

  if (node.node_type === 'substage_entrance') {
    // Subareas always keep a separate entrance and exit (never merged).
    const label = targetName || node.entrance_id || node.display_name || node.node_id;
    if (node.side === 'sub_near') {
      return `Subarea entry: ${label}`;
    }
    if (node.side === 'sub_far') {
      return `Subarea return: ${label}`;
    }
  }

  return node.display_name || node.node_id;
}

function moonGraphNode(moon: MoonAccess, position: Position): GraphNode {
  return {
    id: moonNodeId(moonRowKey(moon)),
    kind: 'moon',
    label: moon.moon_name || moon.moon_id,
    subtitle: moonSubtitle(moon),
    type: isStoryMoon(moon) ? 'story_moon' : 'moon',
    stage: moon.stage,
    kingdom: moon.kingdom,
    areaScope: moon.area_scope,
    x: position.x,
    y: position.y,
    sourceRow: moon
  };
}

function attachEditorIds<T extends Record<string, string>>(rows: T[], idField: keyof T & string): T[] {
  const seen = new Map<string, number>();

  return rows.map((row, index) => {
    const base = row.__editor_id || row[idField] || `row_${index}`;
    const count = seen.get(base) ?? 0;
    seen.set(base, count + 1);

    return {
      ...row,
      __editor_id: count === 0 ? base : `${base}#${count + 1}`
    };
  });
}

function mergeHeaders(templateHeaders: string[], currentHeaders: string[]): string[] {
  const normalizedTemplateHeaders = normalizeHeaders(templateHeaders);
  return [
    ...normalizedTemplateHeaders,
    ...normalizeHeaders(currentHeaders).filter((header) => !normalizedTemplateHeaders.includes(header))
  ];
}

function mergeTemplateRows<T extends Record<string, string>>(
  currentRows: T[],
  templateRows: T[],
  keyForRow: (row: T) => string
): void {
  const currentByKey = new Map(currentRows.map((row) => [keyForRow(row), row]));

  for (const templateRow of templateRows) {
    const key = keyForRow(templateRow);
    const currentRow = currentByKey.get(key);
    if (!currentRow) {
      currentRows.push(stripRemovedFields([{ ...templateRow }])[0]);
      continue;
    }

    const mutableRow = currentRow as Record<string, string>;
    for (const [field, value] of Object.entries(templateRow)) {
      if (REMOVED_HEADERS.has(field)) {
        delete mutableRow[field];
        continue;
      }
      if (mutableRow[field] === undefined || mutableRow[field] === '') {
        mutableRow[field] = value;
      }
    }
  }
}

function replaceRowsForKingdom<T extends Record<string, string> & { kingdom: string }>(
  currentRows: T[],
  incomingRows: T[],
  kingdom: string
): T[] {
  const replacementRows = cloneRows(incomingRows.filter((row) => row.kingdom === kingdom));
  const result: T[] = [];
  let inserted = false;

  for (const row of currentRows) {
    if (row.kingdom !== kingdom) {
      result.push(row);
      continue;
    }
    if (!inserted) {
      result.push(...replacementRows);
      inserted = true;
    }
  }

  if (!inserted) {
    result.push(...replacementRows);
  }

  return result;
}

function normalizeRouteKingdomsFromEndpoints(workspace: LogicWorkspace, kingdom: string): void {
  const kingdomNodeIds = new Set(
    workspace.logicNodes.filter((node) => node.kingdom === kingdom).map((node) => node.node_id)
  );

  workspace.routeEdges = workspace.routeEdges.map((edge) => {
    if (!kingdomNodeIds.has(edge.from_node_id) || !kingdomNodeIds.has(edge.to_node_id)) {
      return edge;
    }
    return edge.kingdom === kingdom ? edge : { ...edge, kingdom };
  });
}

function nodePositionIdsForKingdom(workspace: LogicWorkspace, kingdom: string): Set<string> {
  return new Set([
    ...workspace.logicNodes.filter((node) => node.kingdom === kingdom).map((node) => node.node_id),
    ...workspace.moonAccess
      .filter((moon) => moon.kingdom === kingdom)
      .map((moon) => moonNodeId(moonRowKey(moon)))
  ]);
}

type Matrix34 = [
  number, number, number, number,
  number, number, number, number,
  number, number, number, number
];

function embeddedMatrix(row: EmbeddedZoneRow): Matrix34 {
  const values: number[] = [];
  for (let matrixRow = 0; matrixRow < 3; matrixRow += 1) {
    for (let column = 0; column < 4; column += 1) {
      const key = `matrix_${matrixRow}${column}` as keyof EmbeddedZoneRow;
      const parsed = Number(row[key]);
      values.push(Number.isFinite(parsed) ? parsed : (column === matrixRow ? 1 : 0));
    }
  }
  return values as Matrix34;
}

function transformCoordinates(
  row: Record<string, string>,
  fields: [string, string, string],
  matrix: Matrix34
): void {
  const [xKey, yKey, zKey] = fields;
  if (!row[xKey]?.trim() || !row[yKey]?.trim() || !row[zKey]?.trim()) {
    return;
  }
  const x = Number(row[xKey]);
  const y = Number(row[yKey]);
  const z = Number(row[zKey]);
  if (![x, y, z].every(Number.isFinite)) {
    return;
  }
  const transformed = [
    matrix[0] * x + matrix[1] * y + matrix[2] * z + matrix[3],
    matrix[4] * x + matrix[5] * y + matrix[6] * z + matrix[7],
    matrix[8] * x + matrix[9] * y + matrix[10] * z + matrix[11]
  ];
  [row[xKey], row[yKey], row[zKey]] = transformed.map((value) => value.toFixed(3));
}

function runtimeMoonStage(moon: MoonAccess): string {
  return (moon.runtime_shine_key || moon.moon_id || '').split(':', 1)[0];
}

function boolText(value: boolean): string {
  return value ? 'true' : 'false';
}

function isTrue(value: string | undefined): boolean {
  return value?.trim().toLowerCase() === 'true';
}

function flattenLegacyEmbeddedZones(workspace: LogicWorkspace): LogicWorkspace {
  if (embeddedZoneByStage.size === 0) {
    return workspace;
  }

  const roomByStage = new Map(workspace.rooms.map((room) => [room.stage, room]));
  const contentStages = new Set<string>();
  for (const node of workspace.logicNodes) {
    const idStage = node.node_id.split('::', 1)[0];
    if (embeddedZoneByStage.has(node.stage)) contentStages.add(node.stage);
    else if (embeddedZoneByStage.has(idStage)) contentStages.add(idStage);
  }
  for (const moon of workspace.moonAccess) {
    const physicalStage = runtimeMoonStage(moon);
    if (embeddedZoneByStage.has(moon.stage)) contentStages.add(moon.stage);
    else if (embeddedZoneByStage.has(physicalStage)) contentStages.add(physicalStage);
  }
  for (const room of workspace.rooms) {
    if (embeddedZoneByStage.has(room.stage)) contentStages.add(room.stage);
  }

  const affectedParents = new Set<string>();
  const nodeIds = new Set(workspace.logicNodes.map((node) => node.node_id));

  for (const child of [...contentStages].sort()) {
    const mapping = embeddedZoneByStage.get(child)!;
    const landmarkId = `${child}::start`;
    if (nodeIds.has(landmarkId)) {
      continue;
    }
    const parentRoom = roomByStage.get(mapping.parent_stage);
    if (!parentRoom) {
      continue;
    }
    const matrix = embeddedMatrix(mapping);
    workspace.logicNodes.push({
      node_id: landmarkId,
      kingdom: parentRoom.kingdom,
      stage: parentRoom.stage,
      stage_display_name: parentRoom.stage_display_name,
      area_scope: parentRoom.area_scope,
      node_type: 'custom_place',
      source: 'embedded_zone',
      entrance_id: '',
      side: '',
      obj_id: '',
      display_name: child,
      x: matrix[3].toFixed(3),
      y: matrix[7].toFixed(3),
      z: matrix[11].toFixed(3),
      scenario_indices: mapping.scenario_indices,
      status: 'generated',
      notes: `Embedded map landmark from ${child}.`
    });
    nodeIds.add(landmarkId);
    affectedParents.add(parentRoom.stage);
  }

  for (const node of workspace.logicNodes) {
    const idStage = node.node_id.split('::', 1)[0];
    const child = embeddedZoneByStage.has(node.stage)
      ? node.stage
      : embeddedZoneByStage.has(idStage)
        ? idStage
        : '';
    if (!child) {
      continue;
    }
    const mapping = embeddedZoneByStage.get(child)!;
    const parentRoom = roomByStage.get(mapping.parent_stage);
    if (!parentRoom) {
      continue;
    }
    const wasChildStage = node.stage === child;
    let changed = node.stage !== parentRoom.stage;
    if (wasChildStage) {
      transformCoordinates(node, ['x', 'y', 'z'], embeddedMatrix(mapping));
    }
    node.kingdom = parentRoom.kingdom;
    node.stage = parentRoom.stage;
    node.stage_display_name = parentRoom.stage_display_name;
    node.area_scope = parentRoom.area_scope;
    if (node.node_id === `${child}::start` && node.node_type === 'stage_start') {
      changed = true;
      node.node_type = 'custom_place';
      node.source = 'embedded_zone';
      if (!node.display_name || node.display_name === 'Default start') {
        node.display_name = child;
      }
      if (!node.x || !node.y || !node.z) {
        const matrix = embeddedMatrix(mapping);
        node.x = matrix[3].toFixed(3);
        node.y = matrix[7].toFixed(3);
        node.z = matrix[11].toFixed(3);
      }
      node.notes = `Embedded map landmark from ${child}.`;
    }
    if (changed) {
      affectedParents.add(parentRoom.stage);
    }
  }

  for (const moon of workspace.moonAccess) {
    const physicalStage = runtimeMoonStage(moon);
    const child = embeddedZoneByStage.has(moon.stage)
      ? moon.stage
      : embeddedZoneByStage.has(physicalStage)
        ? physicalStage
        : '';
    if (!child) {
      continue;
    }
    const mapping = embeddedZoneByStage.get(child)!;
    const parentRoom = roomByStage.get(mapping.parent_stage);
    if (!parentRoom) {
      continue;
    }
    const oldStage = moon.stage;
    const oldFromNode = moon.from_node_id;
    const alreadyFlattened =
      oldStage === parentRoom.stage && oldFromNode.startsWith(`${child}::`);
    if (!alreadyFlattened) {
      transformCoordinates(
        moon,
        ['placement_x', 'placement_y', 'placement_z'],
        embeddedMatrix(mapping)
      );
    }
    // Only default a never-wired moon to the embedded stage's start node. Once a
    // moon has a from_node_id (whether it's the default or a user reassignment to
    // some other node in the merged stage), leave it alone — this runs on every
    // reclone (kingdom save load/merge, autosave restore), so re-forcing it here
    // whenever it doesn't match the child's own namespace silently reverted any
    // manual reconnection to a parent-stage node right after the user made it.
    if (!oldFromNode) {
      moon.from_node_id = `${child}::start`;
    }
    moon.kingdom = parentRoom.kingdom;
    moon.stage = parentRoom.stage;
    moon.stage_display_name = parentRoom.stage_display_name;
    moon.area_scope = parentRoom.area_scope;
    if (oldStage !== parentRoom.stage || moon.from_node_id !== oldFromNode) {
      affectedParents.add(parentRoom.stage);
    }
  }

  for (const scenario of workspace.scenarioMetadata) {
    const mapping = embeddedZoneByStage.get(scenario.stage);
    if (!mapping) {
      continue;
    }
    const parentRoom = roomByStage.get(mapping.parent_stage);
    if (!parentRoom) {
      continue;
    }
    scenario.kingdom = parentRoom.kingdom;
    scenario.stage = parentRoom.stage;
    scenario.stage_display_name = parentRoom.stage_display_name;
    scenario.area_scope = parentRoom.area_scope;
    affectedParents.add(parentRoom.stage);
  }

  const removedStages = new Set<string>();
  for (const room of workspace.rooms) {
    const mapping = embeddedZoneByStage.get(room.stage);
    if (!mapping) {
      continue;
    }
    const parentRoom = roomByStage.get(mapping.parent_stage);
    if (!parentRoom) {
      continue;
    }
    parentRoom.has_shine_db_moons = boolText(
      isTrue(parentRoom.has_shine_db_moons) || isTrue(room.has_shine_db_moons)
    );
    parentRoom.has_raw_stage_data_points = boolText(
      isTrue(parentRoom.has_raw_stage_data_points) || isTrue(room.has_raw_stage_data_points)
    );
    parentRoom.active_entrance_link_count = String(
      Number(parentRoom.active_entrance_link_count || 0) + Number(room.active_entrance_link_count || 0)
    );
    parentRoom.excluded_entrance_link_count = String(
      Number(parentRoom.excluded_entrance_link_count || 0) + Number(room.excluded_entrance_link_count || 0)
    );
    const note = `Includes embedded map ${room.stage}.`;
    if (!parentRoom.notes.includes(note)) {
      parentRoom.notes = `${parentRoom.notes.trim()} ${note}`.trim();
    }
    removedStages.add(room.stage);
    affectedParents.add(parentRoom.stage);
  }
  workspace.rooms = workspace.rooms.filter((room) => !removedStages.has(room.stage));

  const nodeById = new Map(workspace.logicNodes.map((node) => [node.node_id, node]));
  const finalRoomByStage = new Map(workspace.rooms.map((room) => [room.stage, room]));
  for (const route of workspace.routeEdges) {
    const endpoint = nodeById.get(route.from_node_id) ?? nodeById.get(route.to_node_id);
    if (!endpoint) {
      continue;
    }
    route.kingdom = endpoint.kingdom;
    route.area_scope = finalRoomByStage.get(endpoint.stage)?.area_scope ?? endpoint.area_scope;
  }

  if (affectedParents.size > 0) {
    const clearIds = new Set([
      ...workspace.logicNodes
        .filter((node) => affectedParents.has(node.stage))
        .map((node) => node.node_id),
      ...workspace.moonAccess
        .filter((moon) => affectedParents.has(moon.stage))
        .map((moon) => moonNodeId(moonRowKey(moon)))
    ]);
    for (const id of clearIds) {
      delete workspace.nodePositions[id];
    }
  }

  return workspace;
}

function normalizeMoonRows(rows: MoonAccess[]): MoonAccess[] {
  return attachEditorIds(stripRemovedFields(rows), 'moon_id').map((moon) => ({
    ...moon,
    runtime_shine_key: moon.runtime_shine_key || moon.moon_id,
    scenario_move_to: moon.scenario_move_to || '',
    shine_info_hint_idx: moon.shine_info_hint_idx || '',
    shine_info_main_scenario_no: moon.shine_info_main_scenario_no || '',
    shine_info_progress_bit_flag: moon.shine_info_progress_bit_flag || '',
    shine_info_is_grand: moon.shine_info_is_grand || '',
    shine_info_is_moon_rock: moon.shine_info_is_moon_rock || '',
    shine_info_available_scenarios: moon.shine_info_available_scenarios || '',
    shine_info_object_name: moon.shine_info_object_name || '',
    shine_info_scenario_name: moon.shine_info_scenario_name || '',
    difficulty: normalizeRouteDifficultyField(moon.difficulty || '', moon.requirement_expr),
    option_scenarios: moon.option_scenarios || '',
    effort_level: normalizeEffortLevel(moon.effort_level),
    effort_notes: moon.effort_notes || ''
  }));
}

function normalizeLogicNodeRows(rows: LogicNode[]): LogicNode[] {
  return stripRemovedFields(rows).map((node) => {
    const fixedTransition = FIXED_TRANSITION_NODES[node.node_id];
    return {
      ...node,
      // Scrub the obsolete frozen exclusion marker: exclusion is config-derived now.
      source: fixedTransition
        ? 'fixed_transition'
        : node.source === 'EntranceDB:excluded'
          ? 'EntranceDB'
          : node.source,
      node_type: fixedTransition ? 'fixed_transition' : node.node_type,
      side: fixedTransition ? '' : node.side,
      display_name: fixedTransition ? fixedTransition.label : node.display_name,
      notes: fixedTransition
        ? `Fixed vanilla painting transition to ${fixedTransition.destinationStage}; not randomized.`
        : node.notes
    };
  });
}

function normalizeRoomRows(rows: Room[]): Room[] {
  return stripRemovedFields(rows);
}

function normalizeRouteRows(rows: RouteEdge[]): RouteEdge[] {
  const routeRows = stripRemovedFields(rows).filter((edge) => edge.source !== 'generated');
  const directedPairs = new Set(routeRows.map((edge) => routePairKey(edge.from_node_id, edge.to_node_id)));

  return routeRows.map((edge) => {
    const normalized = { ...edge };
    normalized.difficulty = normalizeRouteDifficultyField(normalized.difficulty || '', normalized.requirement_expr);
    normalized.option_scenarios = normalized.option_scenarios || '';
    if (normalized.from_node_id && normalized.to_node_id) {
      normalized.bidirectional = directedPairs.has(routePairKey(normalized.to_node_id, normalized.from_node_id))
        ? 'false'
        : 'true';
    }
    return normalized;
  });
}

function normalizeCaptureGateRows(rows: CaptureGate[]): CaptureGate[] {
  return rows.map((gate) => ({
    ...gate,
    difficulty: normalizeRouteDifficulty(gate.difficulty || 'casual')
  }));
}

function moonSubtitle(moon: MoonAccess): string {
  const parts = [effortLabel(moon.effort_level || 'unrated')];
  if (isStoryMoon(moon)) {
    parts.push('story');
  }
  if (moon.placement_stage_map_indices) {
    parts.push(`maps ${moon.placement_stage_map_indices}`);
  }
  if (!moon.placement_stage_map_indices && moon.shine_info_available_scenarios) {
    parts.push(`scenarios ${moon.shine_info_available_scenarios}`);
  }
  if (moon.scenario_move_to) {
    parts.push(`-> scenario ${moon.scenario_move_to}`);
  }
  return parts.join(' - ');
}

function normalizeRouteDifficultyField(value: string, requirementExpr: string): string {
  const optionCount = routeOptionCount(requirementExpr);
  return stringifyRouteDifficulties(normalizeRouteDifficulties(value, optionCount));
}

function routeOptionCount(requirementExpr: string): number {
  return countRequirementOptions(requirementExpr);
}

function ensureRouteEdgeHeaders(headers: string[]): string[] {
  const result = [...headers];
  if (!result.includes('difficulty')) {
    const edgeTypeIndex = result.indexOf('edge_type');
    result.splice(edgeTypeIndex >= 0 ? edgeTypeIndex + 1 : result.length, 0, 'difficulty');
  }
  if (!result.includes('option_scenarios')) {
    const difficultyIndex = result.indexOf('difficulty');
    result.splice(difficultyIndex >= 0 ? difficultyIndex + 1 : result.length, 0, 'option_scenarios');
  }
  return result;
}

function ensureCaptureGateHeaders(headers: string[]): string[] {
  const result = [...headers];
  if (!result.includes('difficulty')) {
    const replacementIndex = result.indexOf('replacement_capture_token');
    result.splice(replacementIndex >= 0 ? replacementIndex + 1 : result.length, 0, 'difficulty');
  }
  return result;
}

function ensureMoonAccessHeaders(headers: string[]): string[] {
  const result = [...headers];
  if (!result.includes('runtime_shine_key')) {
    const moonNameIndex = result.indexOf('moon_name');
    result.splice(moonNameIndex >= 0 ? moonNameIndex + 1 : result.length, 0, 'runtime_shine_key');
  }
  if (!result.includes('effort_level')) {
    const accessTypeIndex = result.indexOf('access_type');
    result.splice(accessTypeIndex >= 0 ? accessTypeIndex + 1 : result.length, 0, 'effort_level');
  }
  if (!result.includes('effort_notes')) {
    const effortLevelIndex = result.indexOf('effort_level');
    result.splice(effortLevelIndex >= 0 ? effortLevelIndex + 1 : result.length, 0, 'effort_notes');
  }
  if (!result.includes('scenario_move_to')) {
    const scenarioIndex = result.indexOf('placement_scenario_no_values');
    result.splice(scenarioIndex >= 0 ? scenarioIndex + 1 : result.length, 0, 'scenario_move_to');
  }
  if (!result.includes('difficulty')) {
    const requirementIndex = result.indexOf('requirement_expr');
    result.splice(requirementIndex >= 0 ? requirementIndex + 1 : result.length, 0, 'difficulty');
  }
  if (!result.includes('option_scenarios')) {
    const difficultyIndex = result.indexOf('difficulty');
    result.splice(difficultyIndex >= 0 ? difficultyIndex + 1 : result.length, 0, 'option_scenarios');
  }
  const shineInfoHeaders = [
    'shine_info_hint_idx',
    'shine_info_main_scenario_no',
    'shine_info_progress_bit_flag',
    'shine_info_is_grand',
    'shine_info_is_moon_rock',
    'shine_info_available_scenarios',
    'shine_info_object_name',
    'shine_info_scenario_name'
  ];
  for (const header of shineInfoHeaders) {
    if (!result.includes(header)) {
      const notesIndex = result.indexOf('notes');
      result.splice(notesIndex >= 0 ? notesIndex : result.length, 0, header);
    }
  }
  return result;
}

function isMoonAccess(row: LogicNode | MoonAccess): row is MoonAccess {
  return 'moon_id' in row;
}

function toNumber(value: string): number {
  if (!value) {
    return Number.NaN;
  }
  return Number(value);
}

function makeBounds(rows: Array<{ gameX: number; gameZ: number }>): CoordinateBounds | undefined {
  if (rows.length < 2) {
    return undefined;
  }

  const xs = rows.map((row) => row.gameX);
  const zs = rows.map((row) => row.gameZ);
  const minX = Math.min(...xs);
  const maxX = Math.max(...xs);
  const minZ = Math.min(...zs);
  const maxZ = Math.max(...zs);

  if (maxX - minX < 1 || maxZ - minZ < 1) {
    return undefined;
  }

  return { minX, maxX, minZ, maxZ };
}

function mapGamePosition(
  gameX: number,
  gameZ: number,
  bounds: CoordinateBounds,
  width = 1500,
  height = 980
): Position {
  return {
    x: 120 + ((gameX - bounds.minX) / (bounds.maxX - bounds.minX)) * width,
    y: 120 + ((gameZ - bounds.minZ) / (bounds.maxZ - bounds.minZ)) * height
  };
}

function compareCoordinateRowsForLayout(
  left: { row: LogicNode | MoonAccess; gameX: number; gameZ: number },
  right: { row: LogicNode | MoonAccess; gameX: number; gameZ: number }
): number {
  return (
    coordinateRowRank(left.row) - coordinateRowRank(right.row) ||
    left.gameZ - right.gameZ ||
    left.gameX - right.gameX
  );
}

function coordinateRowRank(row: LogicNode | MoonAccess): number {
  if (isMoonAccess(row)) {
    return 5;
  }
  const ranks: Record<string, number> = {
    home_entrance: 0,
    substage_entrance: 0,
    checkpoint: 1,
    stage_change: 2,
    custom_place: 3,
    state_event: 4,
    stage_start: 6
  };
  return ranks[row.node_type] ?? 7;
}

function avoidMapCollision(position: Position, occupied: Position[]): Position {
  const offsets = [
    { x: 0, y: 0 },
    { x: 205, y: 0 },
    { x: -205, y: 0 },
    { x: 0, y: 96 },
    { x: 0, y: -96 },
    { x: 205, y: 96 },
    { x: -205, y: 96 },
    { x: 205, y: -96 },
    { x: -205, y: -96 }
  ];

  for (let ring = 0; ring < 6; ring += 1) {
    for (const offset of offsets) {
      const candidate = {
        x: Math.max(40, position.x + offset.x * ring),
        y: Math.max(40, position.y + offset.y * ring)
      };
      if (!occupied.some((other) => isTooClose(candidate, other))) {
        return candidate;
      }
    }
  }

  return position;
}

function placeMapFallbackRows(
  workspace: LogicWorkspace,
  rows: Array<{ id: string; row: LogicNode | MoonAccess }>,
  xStart: number,
  yStart: number
): void {
  const sorted = [...rows].sort((left, right) => {
    const laneCompare = layoutLane(left.row).localeCompare(layoutLane(right.row));
    if (laneCompare !== 0) {
      return laneCompare;
    }
    return fallbackLabel(left.row).localeCompare(fallbackLabel(right.row));
  });
  const rowsPerColumn = 12;
  sorted.forEach((entry, index) => {
    const column = Math.floor(index / rowsPerColumn);
    const row = index % rowsPerColumn;
    workspace.nodePositions[entry.id] = {
      x: xStart + column * 236,
      y: yStart + row * 112
    };
  });
}

function fallbackLabel(row: LogicNode | MoonAccess): string {
  return isMoonAccess(row) ? row.moon_name || row.moon_id : row.display_name || row.node_id;
}

type ArrangeCluster = {
  root: LogicNode;
  logicMembers: LogicNode[];
  moons: MoonAccess[];
};

type ArrangeClusterLayout = {
  width: number;
  height: number;
  positions: Map<string, Position>;
};

type ArrangeClusterEntry = {
  cluster: ArrangeCluster;
  layout: ArrangeClusterLayout;
  depth: number;
};

type ArrangeClusterPlacement = {
  entry: ArrangeClusterEntry;
  x: number;
  y: number;
};

function isArrangeClusterAnchor(node: LogicNode): boolean {
  return (
    node.node_type === 'stage_start' ||
    node.node_type === 'checkpoint' ||
    node.node_type === 'custom_place'
  );
}

function nearestArrangeAnchor(
  startId: string,
  anchorIds: Set<string>,
  adjacency: Map<string, Set<string>>
): string | undefined {
  const visited = new Set([startId]);
  let frontier = [startId];

  while (frontier.length > 0) {
    const matches = frontier.filter((nodeId) => anchorIds.has(nodeId)).sort();
    if (matches.length > 0) {
      return matches[0];
    }

    const next: string[] = [];
    for (const nodeId of [...frontier].sort()) {
      for (const neighbor of [...(adjacency.get(nodeId) ?? [])].sort()) {
        if (!visited.has(neighbor)) {
          visited.add(neighbor);
          next.push(neighbor);
        }
      }
    }
    frontier = next;
  }

  return undefined;
}

function arrangeClusterDepths(
  adjacency: Map<string, Set<string>>,
  startIds: string[]
): Map<string, number> {
  const depths = new Map<string, number>();
  let frontier = [...new Set(startIds)].filter((nodeId) => adjacency.has(nodeId)).sort();
  for (const nodeId of frontier) {
    depths.set(nodeId, 0);
  }

  let depth = 0;
  while (frontier.length > 0) {
    const next: string[] = [];
    for (const nodeId of frontier) {
      for (const neighbor of [...(adjacency.get(nodeId) ?? [])].sort()) {
        if (!depths.has(neighbor)) {
          depths.set(neighbor, depth + 1);
          next.push(neighbor);
        }
      }
    }
    frontier = next;
    depth += 1;
  }

  return depths;
}

function makeArrangeClusterLayout(cluster: ArrangeCluster): ArrangeClusterLayout {
  const logicMembers = [...cluster.logicMembers].sort(compareLogicNodesForLayout);
  const moonMembers = [...cluster.moons].sort(compareMoonsForLayout);
  const rawPositions = new Map<string, Position>();
  const sizes = new Map<string, { width: number; height: number }>();
  const logicColumns = Math.min(3, Math.max(1, Math.ceil(Math.sqrt(logicMembers.length))));
  const centralWidth = Math.max(190, (logicColumns - 1) * 230 + 190);
  const rootX = (centralWidth - 190) / 2;

  rawPositions.set(cluster.root.node_id, { x: rootX, y: 0 });
  sizes.set(cluster.root.node_id, { width: 190, height: 76 });

  logicMembers.forEach((node, index) => {
    rawPositions.set(node.node_id, {
      x: (index % logicColumns) * 230,
      y: 154 + Math.floor(index / logicColumns) * 112
    });
    sizes.set(node.node_id, { width: 190, height: 76 });
  });

  const leftMoons = moonMembers.filter((_, index) => index % 2 === 0);
  const rightMoons = moonMembers.filter((_, index) => index % 2 === 1);
  placeArrangeMoonWing(rawPositions, sizes, leftMoons, 'left', centralWidth);
  placeArrangeMoonWing(rawPositions, sizes, rightMoons, 'right', centralWidth);

  let minX = Number.POSITIVE_INFINITY;
  let minY = Number.POSITIVE_INFINITY;
  let maxX = Number.NEGATIVE_INFINITY;
  let maxY = Number.NEGATIVE_INFINITY;
  for (const [nodeId, position] of rawPositions) {
    const size = sizes.get(nodeId)!;
    minX = Math.min(minX, position.x);
    minY = Math.min(minY, position.y);
    maxX = Math.max(maxX, position.x + size.width);
    maxY = Math.max(maxY, position.y + size.height);
  }

  const positions = new Map<string, Position>();
  for (const [nodeId, position] of rawPositions) {
    positions.set(nodeId, {
      x: Math.round(position.x - minX),
      y: Math.round(position.y - minY)
    });
  }

  return {
    width: Math.round(maxX - minX),
    height: Math.round(maxY - minY),
    positions
  };
}

function placeArrangeMoonWing(
  positions: Map<string, Position>,
  sizes: Map<string, { width: number; height: number }>,
  moons: MoonAccess[],
  side: 'left' | 'right',
  centralWidth: number
): void {
  const rowsPerColumn = 5;
  const moonWidth = 178;
  const moonHeight = 72;
  const rowGap = 108;
  const columnGap = 86;
  const wingGap = 150;

  moons.forEach((moon, index) => {
    const column = Math.floor(index / rowsPerColumn);
    const row = index % rowsPerColumn;
    const rowsInColumn = Math.min(rowsPerColumn, moons.length - column * rowsPerColumn);
    const columnHeight = moonHeight + Math.max(0, rowsInColumn - 1) * rowGap;
    const y = 38 - columnHeight / 2 + row * rowGap;
    const x =
      side === 'left'
        ? -wingGap - moonWidth - column * (moonWidth + columnGap)
        : centralWidth + wingGap + column * (moonWidth + columnGap);
    const id = moonNodeId(moonRowKey(moon));
    positions.set(id, { x, y });
    sizes.set(id, { width: moonWidth, height: moonHeight });
  });
}

function packArrangeLayer(entries: ArrangeClusterEntry[]): {
  width: number;
  height: number;
  placements: ArrangeClusterPlacement[];
} {
  const targetWidth = 3000;
  const columnGap = 220;
  const rowGap = 220;
  const rows: Array<{ entries: ArrangeClusterEntry[]; width: number; height: number }> = [];
  let row = { entries: [] as ArrangeClusterEntry[], width: 0, height: 0 };

  for (const entry of entries) {
    const nextWidth =
      row.width + (row.entries.length > 0 ? columnGap : 0) + entry.layout.width;
    if (row.entries.length > 0 && nextWidth > targetWidth) {
      rows.push(row);
      row = { entries: [], width: 0, height: 0 };
    }

    row.entries.push(entry);
    row.width += (row.entries.length > 1 ? columnGap : 0) + entry.layout.width;
    row.height = Math.max(row.height, entry.layout.height);
  }
  if (row.entries.length > 0) {
    rows.push(row);
  }

  const placements: ArrangeClusterPlacement[] = [];
  let rowY = 0;
  for (const packedRow of rows) {
    let entryX = 0;
    for (const entry of packedRow.entries) {
      placements.push({
        entry,
        x: entryX,
        y: rowY
      });
      entryX += entry.layout.width + columnGap;
    }
    rowY += packedRow.height + rowGap;
  }

  return {
    width: Math.max(...rows.map((packedRow) => packedRow.width)),
    height:
      rows.reduce((sum, packedRow) => sum + packedRow.height, 0) +
      Math.max(0, rows.length - 1) * rowGap,
    placements
  };
}

function compareArrangeClusters(left: ArrangeCluster, right: ArrangeCluster): number {
  return (
    compareLogicNodesForLayout(left.root, right.root) ||
    right.logicMembers.length + right.moons.length - (left.logicMembers.length + left.moons.length)
  );
}

function compareLogicNodesForLayout(left: LogicNode, right: LogicNode): number {
  return (
    firstScenario(left.scenario_indices) - firstScenario(right.scenario_indices) ||
    nodeTypeRank(left) - nodeTypeRank(right) ||
    (left.display_name || left.node_id).localeCompare(right.display_name || right.node_id)
  );
}

function compareMoonsForLayout(left: MoonAccess, right: MoonAccess): number {
  return (
    firstScenario(left.placement_stage_map_indices) - firstScenario(right.placement_stage_map_indices) ||
    storyRank(left) - storyRank(right) ||
    (left.moon_name || left.moon_id).localeCompare(right.moon_name || right.moon_id)
  );
}

function nodeTypeRank(node: LogicNode): number {
  const ranks: Record<string, number> = {
    stage_start: 0,
    home_entrance: 1,
    substage_entrance: 2,
    checkpoint: 3,
    custom_place: 4,
    stage_change: 5,
    state_event: 6
  };
  return ranks[node.node_type] ?? 9;
}

function storyRank(moon: MoonAccess): number {
  return isStoryMoon(moon) ? 0 : 1;
}

function firstScenario(value: string): number {
  const match = value.match(/\d+/);
  return match ? Number(match[0]) : 999;
}

function layoutLane(row: LogicNode | MoonAccess): string {
  if (isMoonAccess(row)) {
    return 'moon';
  }
  if (row.node_type === 'home_entrance' || row.node_type === 'substage_entrance') {
    return `${row.node_type}:${row.side || 'unknown'}`;
  }
  return row.node_type || 'node';
}

function fallbackPosition(lane: string, index: number): Position {
  const lanes: Record<string, { x: number; y: number; rows: number; xGap: number; yGap: number }> = {
    stage_start: { x: 110, y: 360, rows: 6, xGap: 220, yGap: 130 },
    'home_entrance:home_in': { x: 390, y: 120, rows: 5, xGap: 230, yGap: 124 },
    'home_entrance:home_out': { x: 390, y: 560, rows: 5, xGap: 230, yGap: 124 },
    'substage_entrance:sub_near': { x: 210, y: 160, rows: 5, xGap: 230, yGap: 130 },
    'substage_entrance:sub_far': { x: 210, y: 560, rows: 5, xGap: 230, yGap: 130 },
    'home_entrance:unknown': { x: 390, y: 340, rows: 6, xGap: 230, yGap: 124 },
    'substage_entrance:unknown': { x: 210, y: 340, rows: 6, xGap: 230, yGap: 130 },
    checkpoint: { x: 700, y: 120, rows: 7, xGap: 230, yGap: 116 },
    stage_change: { x: 980, y: 120, rows: 7, xGap: 230, yGap: 116 },
    state_event: { x: 760, y: 560, rows: 5, xGap: 230, yGap: 116 },
    custom_place: { x: 700, y: 360, rows: 6, xGap: 230, yGap: 122 },
    moon: { x: 1320, y: 120, rows: 8, xGap: 220, yGap: 104 },
    node: { x: 700, y: 240, rows: 6, xGap: 230, yGap: 122 }
  };

  const config = lanes[lane] ?? lanes.node;
  const row = index % config.rows;
  const column = Math.floor(index / config.rows);

  return {
    x: config.x + column * config.xGap,
    y: config.y + row * config.yGap
  };
}

function avoidCollision(position: Position, occupied: Position[]): Position {
  const offsets = [
    { x: 0, y: 0 },
    { x: 230, y: 0 },
    { x: -230, y: 0 },
    { x: 0, y: 120 },
    { x: 0, y: -120 },
    { x: 230, y: 120 },
    { x: -230, y: 120 },
    { x: 230, y: -120 },
    { x: -230, y: -120 }
  ];

  for (let ring = 0; ring < 8; ring += 1) {
    for (const offset of offsets) {
      const candidate = {
        x: Math.max(40, position.x + offset.x * ring),
        y: Math.max(40, position.y + offset.y * ring)
      };
      if (!occupied.some((other) => isTooClose(candidate, other))) {
        return candidate;
      }
    }
  }

  return position;
}

function isTooClose(a: Position, b: Position): boolean {
  return Math.abs(a.x - b.x) < 210 && Math.abs(a.y - b.y) < 92;
}

function nextEdgeId(workspace: LogicWorkspace): string {
  const existing = new Set(workspace.routeEdges.map((edge) => edge.edge_id));
  let index = 1;
  let edgeId = `edge_user_${String(index).padStart(4, '0')}`;
  while (existing.has(edgeId)) {
    index += 1;
    edgeId = `edge_user_${String(index).padStart(4, '0')}`;
  }
  return edgeId;
}

function makeBlankRow(headers: string[]): Record<string, string> {
  return Object.fromEntries(normalizeHeaders(headers).map((header) => [header, '']));
}

function normalizeHeaders(headers: string[]): string[] {
  return headers.filter((header) => !REMOVED_HEADERS.has(header));
}

function stripRemovedFields<T extends Record<string, string>>(rows: T[]): T[] {
  return rows.map((row) => {
    const normalized = { ...row };
    for (const field of REMOVED_HEADERS) {
      delete normalized[field];
    }
    return normalized;
  });
}

function slugify(value: string): string {
  const slug = value
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '_')
    .replace(/^_+|_+$/g, '');
  return slug || 'place';
}

function areaRank(area: string): number {
  if (area === 'overworld') {
    return 0;
  }
  if (area === 'subarea') {
    return 1;
  }
  return 2;
}
