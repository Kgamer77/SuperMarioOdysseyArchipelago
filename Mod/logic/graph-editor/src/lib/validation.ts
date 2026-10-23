import { ENTRANCE_DOOR_NODE_PAIRS, isExcludedEntranceNode } from '../data/entranceExclusions';
import { EFFORT_LEVEL_VALUES } from './effort';
import { parseOptionScenarios } from './optionScenarios';
import { collectNodeSet, collectTokenSet, parseRequirementExpression } from './requirements';
import {
  normalizeRouteDifficulty,
  parseRouteDifficulties,
  routeDifficultyForOption,
  routeDifficultyLabel,
  ROUTE_DIFFICULTY_LEVELS,
  ROUTE_DIFFICULTY_VALUES
} from './routeDifficulty';
import { isRawStageDataTransition, shouldShowLogicNode } from './workspace';
import {
  captureIsStageLocked,
  captureTokenForPlacement,
  enemyEntityByName,
  enemySwapPlacements,
  placementIsSwapEligible,
  reliableCaptureTokens,
  safeCaptureTokens
} from '../data/enemySwapData';
import type {
  CaptureGate,
  CaptureGateObject,
  EnemySwapPlacement,
  IssueCountSummary,
  LogicNode,
  LogicWorkspace,
  MoonAccess,
  RouteEdge,
  ValidationIssue
} from './types';

type StageVerificationOptions = {
  includeRawAnchors?: boolean;
  // Optional precomputed kingdom reachability so callers verifying many maps in a
  // loop don't recompute the whole-workspace BFS per map.
  kingdomReachable?: Set<string>;
};

export function validateWorkspace(workspace: LogicWorkspace): ValidationIssue[] {
  const issues: ValidationIssue[] = [];
  const tokenSet = collectTokenSet(workspace.logicTokens);
  const nodeSet = collectNodeSet(workspace.logicNodes);
  const nodeIds = workspace.logicNodes.map((node) => node.node_id);
  const nodeIdSet = new Set(nodeIds);

  addDuplicates(issues, 'logicNodes', 'node_id', nodeIds);
  addDuplicates(
    issues,
    'routeEdges',
    'edge_id',
    workspace.routeEdges.map((edge) => edge.edge_id)
  );
  addDuplicates(
    issues,
    'moonAccess',
    '__editor_id',
    workspace.moonAccess.map((moon) => moon.__editor_id)
  );
  addDuplicates(
    issues,
    'captureGates',
    'gate_id',
    workspace.captureGates.map((gate) => gate.gate_id)
  );
  addCaptureGateIssues(issues, workspace);
  addDuplicateRuntimeKeyWarnings(
    issues,
    workspace.moonAccess.map((moon) => moon.runtime_shine_key || moon.moon_id).filter(Boolean)
  );

  for (const node of workspace.logicNodes) {
    if (isRawStageDataTransition(node)) {
      continue;
    }
    if (!node.display_name.trim()) {
      issues.push({
        severity: 'warning',
        table: 'logicNodes',
        rowId: node.node_id,
        field: 'display_name',
        message: 'Node has no human-readable name.'
      });
    }
  }

  for (const edge of workspace.routeEdges) {
    if (!nodeIdSet.has(edge.from_node_id)) {
      issues.push({
        severity: 'error',
        table: 'routeEdges',
        rowId: edge.edge_id,
        field: 'from_node_id',
        message: `Missing source node "${edge.from_node_id}".`
      });
    }

    if (!nodeIdSet.has(edge.to_node_id)) {
      issues.push({
        severity: 'error',
        table: 'routeEdges',
        rowId: edge.edge_id,
        field: 'to_node_id',
        message: `Missing target node "${edge.to_node_id}".`
      });
    }

    if (edge.from_node_id && edge.from_node_id === edge.to_node_id) {
      issues.push({
        severity: 'warning',
        table: 'routeEdges',
        rowId: edge.edge_id,
        field: 'to_node_id',
        message: 'Route loops back to the same node.'
      });
    }

    addRequirementIssues(
      issues, edge.edge_id, 'routeEdges', 'requirement_expr', edge.requirement_expr, tokenSet, nodeSet
    );

    const unknownDifficulties = parseRouteDifficulties(edge.difficulty || '')
      .filter((difficulty) => !ROUTE_DIFFICULTY_VALUES.has(difficulty));
    if (unknownDifficulties.length > 0) {
      issues.push({
        severity: 'error',
        table: 'routeEdges',
        rowId: edge.edge_id,
        field: 'difficulty',
        message: `Unknown route difficulty "${unknownDifficulties[0]}".`
      });
    }

    if (!edge.requirement_expr.trim() && !isExcludedSource(edge)) {
      issues.push({
        severity: 'info',
        table: 'routeEdges',
        rowId: edge.edge_id,
        field: 'requirement_expr',
        message: 'Route requirement is still blank.'
      });
    }
  }

  // Whole-kingdom reachability: every non-excluded moon must be reachable from
  // the overworld through proper (non-raw, non-excluded) connections, not merely
  // valid inside its own map. This is the authoritative "is this placeable" check
  // and owns all moon connection/reachability/exclusion errors.
  addKingdomReachabilityIssues(issues, workspace, nodeIdSet);

  for (const moon of workspace.moonAccess) {
    if (moon.effort_level && !EFFORT_LEVEL_VALUES.has(moon.effort_level)) {
      issues.push({
        severity: 'error',
        table: 'moonAccess',
        rowId: moon.moon_id,
        field: 'effort_level',
        message: `Unknown effort level "${moon.effort_level}".`
      });
    }

    const unknownMoonDifficulties = parseRouteDifficulties(moon.difficulty || '')
      .filter((difficulty) => !ROUTE_DIFFICULTY_VALUES.has(difficulty));
    if (unknownMoonDifficulties.length > 0) {
      issues.push({
        severity: 'error',
        table: 'moonAccess',
        rowId: moon.moon_id,
        field: 'difficulty',
        message: `Unknown moon difficulty "${unknownMoonDifficulties[0]}".`
      });
    }

    addScenarioMoveIssue(issues, moon.moon_id, moon.scenario_move_to);

    addRequirementIssues(
      issues,
      moon.moon_id,
      'moonAccess',
      'requirement_expr',
      moon.requirement_expr,
      tokenSet,
      nodeSet
    );
    addRequirementIssues(
      issues,
      moon.moon_id,
      'moonAccess',
      'state_requirement_expr',
      moon.state_requirement_expr,
      tokenSet,
      nodeSet
    );
  }

  return issues;
}

function isGrandMoon(moon: MoonAccess): boolean {
  return (moon.flags || '').includes('ShineFlag_Multi');
}

function isProperNode(node: LogicNode | undefined): node is LogicNode {
  return Boolean(node) && !isExcludedSource(node!) && !isRawStageDataTransition(node!);
}

// Reachability across the whole loaded kingdom, mirroring the runtime model
// (randomizer_logic.py): seed only the overworld start(s), then reach a sub-area
// ONLY by crossing an entrance whose home side is reachable. Connectivity is
// structural (abilities/captures/scenarios assumed available) and counts only
// proper edges/nodes — never raw stage-data transitions or excluded rows. A
// sub-area that is only linked by a hidden/raw transition therefore reads as
// unreachable, which is the gap the editor must surface.
export function computeKingdomReachableNodes(workspace: LogicWorkspace): Set<string> {
  const nodeById = new Map(workspace.logicNodes.map((node) => [node.node_id, node]));
  const roomByStage = new Map(workspace.rooms.map((room) => [room.stage, room]));

  const properNodeId = (id: string): boolean => isProperNode(nodeById.get(id));

  const nodesByStage = new Map<string, LogicNode[]>();
  for (const node of workspace.logicNodes) {
    if (!isProperNode(node)) {
      continue;
    }
    const list = nodesByStage.get(node.stage) ?? [];
    list.push(node);
    nodesByStage.set(node.stage, list);
  }

  // Usable routes between proper nodes, treated bidirectionally unless an
  // explicit reverse route exists (same convention as buildUsableAdjacency).
  const usableRoutes = workspace.routeEdges.filter(
    (edge) => isUsableRoute(edge) && properNodeId(edge.from_node_id) && properNodeId(edge.to_node_id)
  );
  const adjacency = new Map<string, string[]>();
  const stageHasRoutes = new Set<string>();
  const usableRouteIndex = buildRouteDirectionIndex(usableRoutes);
  for (const edge of usableRoutes) {
    addAdjacentNode(adjacency, edge.from_node_id, edge.to_node_id);
    if (!hasExplicitReverseRoute(edge, usableRouteIndex)) {
      addAdjacentNode(adjacency, edge.to_node_id, edge.from_node_id);
    }
    const fromStage = nodeById.get(edge.from_node_id)?.stage;
    const toStage = nodeById.get(edge.to_node_id)?.stage;
    if (fromStage) stageHasRoutes.add(fromStage);
    if (toStage) stageHasRoutes.add(toStage);
  }

  // Entrance doors: home_entrance <-> substage_entrance sharing an entrance_id
  // across stages (home_in/sub_near share one id, home_out/sub_far another).
  // Keyed by kingdom + entrance_id: a door always links stages within one
  // kingdom, and some entrance_ids (aaa, shop, town) are reused across kingdoms,
  // so an id-only key would forge phantom cross-kingdom pairs.
  const doorGroups = new Map<string, { home: string[]; sub: string[] }>();
  for (const node of workspace.logicNodes) {
    if (!isProperNode(node) || !node.entrance_id) {
      continue;
    }
    if (node.node_type !== 'home_entrance' && node.node_type !== 'substage_entrance') {
      continue;
    }
    const key = `${node.kingdom}${node.entrance_id}`;
    const group = doorGroups.get(key) ?? { home: [], sub: [] };
    (node.node_type === 'home_entrance' ? group.home : group.sub).push(node.node_id);
    doorGroups.set(key, group);
  }
  const doorPairs: Array<[string, string]> = [];
  for (const group of doorGroups.values()) {
    for (const home of group.home) {
      for (const sub of group.sub) {
        if (nodeById.get(home)?.stage !== nodeById.get(sub)?.stage) {
          doorPairs.push([home, sub]);
        }
      }
    }
  }
  // Add the authoritative home<->sub pairs straight from the entrance-link config.
  // The id-grouping above misses doors whose two sides use different entrance_ids
  // (Wooded Tower001/Tower002, Snow RaceEntrance/RaceTrackExit); the link knows the
  // exact mapping. Restricted to proper, present, cross-stage node pairs.
  for (const { home, sub } of ENTRANCE_DOOR_NODE_PAIRS) {
    const homeNode = nodeById.get(home);
    const subNode = nodeById.get(sub);
    if (!homeNode || !subNode || homeNode.stage === subNode.stage) {
      continue;
    }
    if (properNodeId(home) && properNodeId(sub)) {
      doorPairs.push([home, sub]);
    }
  }

  const reachable = new Set<string>();
  // Overworld (non-subarea) stages are entered directly via the Odyssey.
  const accessibleStages = new Set<string>();
  for (const [stage] of nodesByStage) {
    if (roomByStage.get(stage)?.area_scope !== 'subarea') {
      accessibleStages.add(stage);
    }
  }

  // node -> the other side(s) of any door it's part of, so door propagation
  // can happen inline in the worklist below instead of a separate fixpoint pass.
  const doorPartners = new Map<string, string[]>();
  const addDoorPartner = (nodeId: string, partnerId: string): void => {
    const list = doorPartners.get(nodeId);
    if (list) {
      list.push(partnerId);
    } else {
      doorPartners.set(nodeId, [partnerId]);
    }
  };
  for (const [home, sub] of doorPairs) {
    addDoorPartner(home, sub);
    addDoorPartner(sub, home);
  }

  // Single work queue, drained with an index pointer instead of Array.shift()
  // (shift() is O(n) per call in JS, which turned every BFS step into an
  // O(n) operation). Every node is enqueued and processed exactly once.
  const queue: string[] = [];
  let cursor = 0;

  const markReachable = (nodeId: string): void => {
    if (reachable.has(nodeId)) {
      return;
    }
    reachable.add(nodeId);
    queue.push(nodeId);
  };

  const seedStage = (stage: string): void => {
    const stageNodes = nodesByStage.get(stage) ?? [];
    const free = !stageHasRoutes.has(stage);
    const startId = roomByStage.get(stage)?.stage_start_node_id;
    if (startId && properNodeId(startId)) {
      markReachable(startId);
    }
    for (const node of stageNodes) {
      if (free || node.node_type === 'stage_start') {
        markReachable(node.node_id);
      }
    }
  };

  for (const stage of accessibleStages) {
    seedStage(stage);
  }

  // The previous implementation re-seeded every accessible stage and re-walked
  // the whole reachable set from scratch on every "did anything change?" pass
  // until nothing changed — each pass redoing work already done in earlier
  // passes. This walks the frontier exactly once: when a node is dequeued we
  // fan out through its route adjacency *and* any door partner in the same
  // step, and if that partner opens up a stage we haven't seeded yet, we seed
  // it right there (which itself just enqueues more nodes onto this same
  // queue) instead of kicking off another full pass.
  while (cursor < queue.length) {
    const current = queue[cursor];
    cursor += 1;

    for (const next of adjacency.get(current) ?? []) {
      markReachable(next);
    }

    for (const partner of doorPartners.get(current) ?? []) {
      markReachable(partner);
      const partnerStage = nodeById.get(partner)?.stage;
      if (partnerStage && !accessibleStages.has(partnerStage)) {
        accessibleStages.add(partnerStage);
        seedStage(partnerStage);
      }
    }
  }

  return reachable;
}

// Reachability/exclusion rules for a single moon, shared by the global
// validation pass and per-map verification so both report identically. Every
// non-excluded moon must be reachable from the overworld through proper
// connections; Excluded is the only allowed way to mark a moon impossible, and a
// grand (triple) moon may never be excluded (the placement pool pairs grand
// items 1:1 with grand locations).
function moonReachabilityIssues(
  moon: MoonAccess,
  reachable: Set<string>,
  nodeIdSet: Set<string>,
  nodeById: Map<string, LogicNode>
): ValidationIssue[] {
  const at = (field: string, message: string): ValidationIssue => ({
    severity: 'error',
    table: 'moonAccess',
    rowId: moon.moon_id,
    field,
    message
  });

  if (isExcludedSource(moon)) {
    if (isGrandMoon(moon)) {
      return [at('status', 'Grand (triple) moons are exact-count and cannot be excluded; connect it to a reachable node.')];
    }
    return [];
  }

  if (!moon.from_node_id) {
    return [at('from_node_id', 'Moon is not connected to any node. Connect it to a reachable node, or mark it Excluded.')];
  }

  if (!nodeIdSet.has(moon.from_node_id)) {
    return [at('from_node_id', `Moon points at missing node "${moon.from_node_id}".`)];
  }

  const source = nodeById.get(moon.from_node_id);
  if (source && (isExcludedSource(source) || isRawStageDataTransition(source))) {
    return [at('from_node_id', 'Moon is connected through an excluded or raw stage-data node. Connect it to a proper logic node.')];
  }

  if (!reachable.has(moon.from_node_id)) {
    return [
      at(
        'from_node_id',
        'Moon is not reachable from the kingdom overworld through proper connections. Wire the entrance/route that leads here, or mark it Excluded.'
      )
    ];
  }

  return [];
}

function addKingdomReachabilityIssues(
  issues: ValidationIssue[],
  workspace: LogicWorkspace,
  nodeIdSet: Set<string>
): void {
  const reachable = computeKingdomReachableNodes(workspace);
  const nodeById = new Map(workspace.logicNodes.map((node) => [node.node_id, node]));
  for (const moon of workspace.moonAccess) {
    issues.push(...moonReachabilityIssues(moon, reachable, nodeIdSet, nodeById));
  }
}

export function issueCounts(issues: ValidationIssue[]): IssueCountSummary {
  return {
    error: issues.filter((issue) => issue.severity === 'error').length,
    warning: issues.filter((issue) => issue.severity === 'warning').length,
    info: issues.filter((issue) => issue.severity === 'info').length
  };
}

export function verifyStageMap(
  workspace: LogicWorkspace,
  stage: string,
  options: StageVerificationOptions = {}
): ValidationIssue[] {
  const issues: ValidationIssue[] = [];
  const tokenSet = collectTokenSet(workspace.logicTokens);
  const nodeSet = collectNodeSet(workspace.logicNodes);
  const nodeById = new Map(workspace.logicNodes.map((node) => [node.node_id, node]));
  const room = workspace.rooms.find((candidate) => candidate.stage === stage);
  const stageNodes = workspace.logicNodes.filter(
    (node) =>
      node.stage === stage &&
      !isExcludedSource(node) &&
      shouldShowLogicNode(workspace, node) &&
      (options.includeRawAnchors || !isRawStageDataTransition(node))
  );
  const stageNodeIds = new Set(stageNodes.map((node) => node.node_id));
  const hiddenStageNodeIds = new Set(
    workspace.logicNodes
      .filter((node) => node.stage === stage && !stageNodeIds.has(node.node_id))
      .map((node) => node.node_id)
  );
  const stageRoutes = workspace.routeEdges.filter(
    (edge) =>
      !isExcludedSource(edge) &&
      !hiddenStageNodeIds.has(edge.from_node_id) &&
      !hiddenStageNodeIds.has(edge.to_node_id) &&
      (stageNodeIds.has(edge.from_node_id) || stageNodeIds.has(edge.to_node_id))
  );
  const localRoutes = stageRoutes.filter(
    (edge) => stageNodeIds.has(edge.from_node_id) && stageNodeIds.has(edge.to_node_id)
  );
  const incomingTransitions = stageRoutes.filter(
    (edge) => !stageNodeIds.has(edge.from_node_id) && stageNodeIds.has(edge.to_node_id)
  );
  const outgoingTransitions = stageRoutes.filter(
    (edge) => stageNodeIds.has(edge.from_node_id) && !stageNodeIds.has(edge.to_node_id)
  );

  if (stageNodes.length === 0) {
    issues.push(stageIssue('error', stage, 'nodes', 'This map has no active logic nodes.'));
    return issues;
  }

  for (const edge of stageRoutes) {
    addRequirementIssues(
      issues, edge.edge_id, 'routeEdges', 'requirement_expr', edge.requirement_expr, tokenSet, nodeSet
    );
    if (!edge.requirement_expr.trim()) {
      issues.push({
        severity: 'error',
        table: 'routeEdges',
        rowId: edge.edge_id,
        field: 'requirement_expr',
        message: 'Route requirement must be filled before this map can be verified.'
      });
    }
  }

  const initialSources = collectInitialSources(room?.stage_start_node_id, stageNodes, incomingTransitions);
  if (initialSources.size === 0) {
    issues.push(stageIssue('error', stage, 'sources', 'This map has no active start or arrival nodes.'));
  }

  const adjacency = buildUsableAdjacency(stageNodeIds, localRoutes);
  const reachable = findReachable(initialSources, adjacency);

  for (const node of stageNodes) {
    if (!reachable.has(node.node_id)) {
      const isEntrance = node.node_type === 'home_entrance' || node.node_type === 'substage_entrance';
      const hasConnections = stageRoutes.some((edge) => edge.from_node_id === node.node_id || edge.to_node_id === node.node_id);
      issues.push({
        severity: isTransitionExit(node, outgoingTransitions) || (isEntrance && !hasConnections) ? 'error' : 'warning',
        table: 'graph',
        rowId: node.node_id,
        field: 'reachability',
        message: 'Node is not reachable from any start or arrival point in this map.'
      });
    }
  }

  for (const exitEdge of outgoingTransitions) {
    if (!reachable.has(exitEdge.from_node_id)) {
      issues.push({
        severity: 'error',
        table: 'graph',
        rowId: exitEdge.from_node_id,
        field: 'exit_reachability',
        message: 'Transition exit is not reachable from any start or arrival point.'
      });
    }
  }

  // Moon reachability is judged against the whole kingdom (overworld -> entrances
  // -> sub-areas), not just within this map, so the badge and Map Verification
  // panel match the global validation pass. Same shared rule set as
  // validateWorkspace.
  const kingdomReachable = options.kingdomReachable ?? computeKingdomReachableNodes(workspace);
  const allNodeIds = new Set(workspace.logicNodes.map((node) => node.node_id));
  for (const moon of workspace.moonAccess.filter((candidate) => candidate.stage === stage)) {
    issues.push(...moonReachabilityIssues(moon, kingdomReachable, allNodeIds, nodeById));

    if (isExcludedSource(moon)) {
      continue;
    }

    addRequirementIssues(
      issues,
      moon.moon_id,
      'moonAccess',
      'requirement_expr',
      moon.requirement_expr,
      tokenSet,
      nodeSet
    );
    addRequirementIssues(
      issues,
      moon.moon_id,
      'moonAccess',
      'state_requirement_expr',
      moon.state_requirement_expr,
      tokenSet,
      nodeSet
    );

    addScenarioMoveIssue(issues, moon.moon_id, moon.scenario_move_to);
  }

  return issues;
}

function addScenarioMoveIssue(issues: ValidationIssue[], rowId: string, value: string): void {
  const scenarioNo = value.trim();
  if (!scenarioNo) {
    return;
  }

  if (!/^\d+$/.test(scenarioNo)) {
    issues.push({
      severity: 'error',
      table: 'moonAccess',
      rowId,
      field: 'scenario_move_to',
      message: 'Scenario after collection must be a number.'
    });
  }
}

function addRequirementIssues(
  issues: ValidationIssue[],
  rowId: string,
  table: 'routeEdges' | 'moonAccess',
  field: string,
  expression: string,
  tokenSet: Set<string>,
  nodeSet?: Set<string>
): void {
  // nodeSet, when given, turns a stale @<node_id> into an error here rather than
  // letting the expression quietly degrade to a raw (ignored) requirement.
  const model = parseRequirementExpression(expression, tokenSet, nodeSet);
  for (const error of model.errors) {
    issues.push({
      severity: 'error',
      table,
      rowId,
      field,
      message: error
    });
  }
}

function collectInitialSources(
  stageStartNodeId: string | undefined,
  stageNodes: LogicNode[],
  incomingTransitions: RouteEdge[]
): Set<string> {
  const activeNodeIds = new Set(stageNodes.map((node) => node.node_id));
  const sources = new Set<string>();
  if (stageStartNodeId && activeNodeIds.has(stageStartNodeId)) {
    sources.add(stageStartNodeId);
  }

  for (const edge of incomingTransitions) {
    if (isUsableRoute(edge)) {
      sources.add(edge.to_node_id);
    }
  }

  for (const node of stageNodes) {
    if (isArrivalNode(node)) {
      sources.add(node.node_id);
    }
  }

  return sources;
}

function buildUsableAdjacency(stageNodeIds: Set<string>, routes: RouteEdge[]): Map<string, string[]> {
  const adjacency = new Map<string, string[]>();
  for (const nodeId of stageNodeIds) {
    adjacency.set(nodeId, []);
  }

  const routeIndex = buildRouteDirectionIndex(routes);
  for (const edge of routes) {
    if (!isUsableRoute(edge)) {
      continue;
    }
    if (!stageNodeIds.has(edge.from_node_id) || !stageNodeIds.has(edge.to_node_id)) {
      continue;
    }
    addAdjacentNode(adjacency, edge.from_node_id, edge.to_node_id);
    if (!hasExplicitReverseRoute(edge, routeIndex)) {
      addAdjacentNode(adjacency, edge.to_node_id, edge.from_node_id);
    }
  }

  return adjacency;
}

function addAdjacentNode(adjacency: Map<string, string[]>, fromNodeId: string, toNodeId: string): void {
  const nodes = adjacency.get(fromNodeId) ?? [];
  if (!nodes.includes(toNodeId)) {
    adjacency.set(fromNodeId, [...nodes, toNodeId]);
  }
}

// Index of (from -> to) direction pairs to edge_ids, built once per call site
// so hasExplicitReverseRoute is an O(1) map lookup instead of an O(edges)
// linear scan repeated for every edge (which made both callers below O(edges^2)
// on the full workspace route list — the dominant cost of kingdom-reachability
// validation on any workspace with a few thousand routes).
function buildRouteDirectionIndex(routes: RouteEdge[]): Map<string, string[]> {
  const index = new Map<string, string[]>();
  for (const route of routes) {
    const key = `${route.from_node_id}\u0000${route.to_node_id}`;
    const bucket = index.get(key);
    if (bucket) {
      bucket.push(route.edge_id);
    } else {
      index.set(key, [route.edge_id]);
    }
  }
  return index;
}

function hasExplicitReverseRoute(edge: RouteEdge, routeIndex: Map<string, string[]>): boolean {
  const bucket = routeIndex.get(`${edge.to_node_id}\u0000${edge.from_node_id}`);
  return bucket ? bucket.some((edgeId) => edgeId !== edge.edge_id) : false;
}

function findReachable(sources: Set<string>, adjacency: Map<string, string[]>): Set<string> {
  const reachable = new Set<string>();
  const queue = [...sources].filter((source) => adjacency.has(source));
  for (const source of queue) {
    reachable.add(source);
  }

  let cursor = 0;
  while (cursor < queue.length) {
    const current = queue[cursor];
    cursor += 1;
    for (const next of adjacency.get(current) ?? []) {
      if (reachable.has(next)) {
        continue;
      }
      reachable.add(next);
      queue.push(next);
    }
  }

  return reachable;
}

function isUsableRoute(edge: RouteEdge): boolean {
  const expression = edge.requirement_expr.trim().toLowerCase();
  return !isExcludedSource(edge) && expression !== '' && expression !== 'impossible';
}

function isExcludedSource(row: Record<string, string>): boolean {
  return isExcludedEntranceNode(row.node_id) || row.status === 'excluded';
}

function isArrivalNode(node: LogicNode): boolean {
  if (node.node_type === 'stage_start') {
    return true;
  }
  if (node.node_type === 'stage_change') {
    const entrance = node.entrance_id.toLowerCase();
    return entrance.includes('come') || entrance.includes('go');
  }
  if (node.side === 'home_out' || node.side === 'sub_near' || node.side === 'sub_far') {
    return true;
  }
  return false;
}

function isTransitionExit(node: LogicNode, outgoingTransitions: RouteEdge[]): boolean {
  return outgoingTransitions.some((edge) => edge.from_node_id === node.node_id);
}

function stageIssue(
  severity: ValidationIssue['severity'],
  stage: string,
  field: string,
  message: string
): ValidationIssue {
  return {
    severity,
    table: 'graph',
    rowId: stage,
    field,
    message
  };
}

function addDuplicates(
  issues: ValidationIssue[],
  table: 'logicNodes' | 'routeEdges' | 'moonAccess' | 'captureGates',
  field: string,
  values: string[]
): void {
  const seen = new Set<string>();
  const duplicates = new Set<string>();

  for (const value of values) {
    if (!value) {
      continue;
    }
    if (seen.has(value)) {
      duplicates.add(value);
    }
    seen.add(value);
  }

  for (const duplicate of duplicates) {
    issues.push({
      severity: 'error',
      table,
      rowId: duplicate,
      field,
      message: `Duplicate ${field}: ${duplicate}`
    });
  }
}

function addDuplicateRuntimeKeyWarnings(issues: ValidationIssue[], runtimeKeys: string[]): void {
  const seen = new Set<string>();
  const duplicates = new Set<string>();

  for (const runtimeKey of runtimeKeys) {
    if (seen.has(runtimeKey)) {
      duplicates.add(runtimeKey);
    }
    seen.add(runtimeKey);
  }

  for (const runtimeKey of duplicates) {
    issues.push({
      severity: 'warning',
      table: 'moonAccess',
      rowId: runtimeKey,
      field: 'runtime_shine_key',
      message: 'Runtime shine key is duplicated in the source data; keep moon_id unique for logic.'
    });
  }
}

// Moon requirements with capture tokens use the same authoring/solver model as
// routes. These self-edges are editor-only targets; runtime generation emits
// matching inert edges keyed by moon_state_<uid> / moon_local_<uid>.
function captureGateEdges(
  workspace: LogicWorkspace,
  captureTokens: Set<string>
): RouteEdge[] {
  const allTokens = workspace.logicTokens.map((token) => token.token);
  const targets: RouteEdge[] = [];
  for (const moon of workspace.moonAccess) {
    const fromNode = moon.from_node_id || `${moon.stage}::start`;
    const addTarget = (
      kind: 'state' | 'local',
      requirement: string,
      optionScenarios: string
    ) => {
      const parsed = parseRequirementExpression(requirement || '', allTokens);
      if (!parsed.alternatives.some((alternative) =>
        alternative.some((token) => captureTokens.has(token)))) {
        return;
      }
      targets.push({
        edge_id: `moon_${kind}_${moon.uid}`,
        kingdom: moon.kingdom,
        area_scope: moon.area_scope,
        from_node_id: fromNode,
        to_node_id: fromNode,
        requirement_expr: requirement,
        requirement_notes: `Capture requirement for ${moon.moon_name || moon.moon_id}`,
        edge_type: 'moon_capture_gate',
        difficulty: '',
        option_scenarios: optionScenarios,
        bidirectional: 'true',
        source: 'moon_capture_gate',
        notes: moon.moon_id
      });
    };
    addTarget('state', moon.state_requirement_expr, '');
    addTarget('local', moon.requirement_expr, moon.option_scenarios);
  }
  return [...workspace.routeEdges, ...targets];
}

function captureGateTargetName(edge: RouteEdge): string {
  return edge.source === 'moon_capture_gate' ? 'moon requirement' : 'route';
}

function addCaptureGateIssues(issues: ValidationIssue[], workspace: LogicWorkspace): void {
  const firstCaptureGateIssue = issues.length;
  const captureTokens = new Set(
    workspace.logicTokens
      .filter((token) => token.token_type === 'capture')
      .map((token) => token.token)
  );
  const safeTargets = new Set(safeCaptureTokens.map((target) => target.token));
  const allTokens = workspace.logicTokens.map((token) => token.token);
  const gateEdges = captureGateEdges(workspace, captureTokens);
  const edges = new Map(gateEdges.map((edge) => [edge.edge_id, edge]));
  const placements = new Map(
    enemySwapPlacements.map((placement) => [
      `${placement.stage}::${placement.object_id}`,
      placement
    ])
  );
  const membersByGate = new Map<string, CaptureGateObject[]>();
  for (const member of workspace.captureGateObjects) {
    const members = membersByGate.get(member.gate_id) ?? [];
    members.push(member);
    membersByGate.set(member.gate_id, members);
  }
  const gateIds = new Set(workspace.captureGates.map((gate) => gate.gate_id));
  for (const member of workspace.captureGateObjects) {
    if (!gateIds.has(member.gate_id)) {
      issues.push({
        severity: 'error',
        table: 'captureGateObjects',
        rowId: member.gate_id,
        field: 'gate_id',
        message: `Capture-gate member references missing gate "${member.gate_id}".`
      });
    }
  }

  for (const gate of workspace.captureGates) {
    const edge = edges.get(gate.edge_id);
    const members = membersByGate.get(gate.gate_id) ?? [];
    if (!gate.gate_id.trim()) {
      issues.push({
        severity: 'error',
        table: 'captureGates',
        rowId: '(empty gate id)',
        field: 'gate_id',
        message: 'Capture gate ID is required.'
      });
    }
    if (!edge) {
      issues.push({
        severity: 'error',
        table: 'captureGates',
        rowId: gate.gate_id,
        field: 'edge_id',
        message: `Capture gate references missing route or moon requirement "${gate.edge_id}".`
      });
    } else {
      const edgeTokens = captureTokensForEdge(
        edge, captureTokens, allTokens);
      if (!edgeTokens.has(gate.source_capture_token)) {
        issues.push({
          severity: 'error',
          table: 'captureGates',
          rowId: gate.gate_id,
          field: 'source_capture_token',
          message: `Source capture "${gate.source_capture_token}" is not in the ${captureGateTargetName(edge)}.`
        });
      }
      if (gate.kingdom !== edge.kingdom) {
        issues.push({
          severity: 'error',
          table: 'captureGates',
          rowId: gate.gate_id,
          field: 'kingdom',
          message: `Capture gate and ${captureGateTargetName(edge)} belong to different kingdoms.`
        });
      }
      const fromStage = workspace.logicNodes
        .find((node) => node.node_id === edge.from_node_id)?.stage
        || edge.from_node_id.split('::', 1)[0]
        || '';
      if (gate.logic_stage !== fromStage) {
        issues.push({
          severity: 'error',
          table: 'captureGates',
          rowId: gate.gate_id,
          field: 'logic_stage',
          message: `Capture gate belongs to "${gate.logic_stage}", but the ${captureGateTargetName(edge)} starts in "${fromStage}".`
        });
      }
    }
    if (!safeTargets.has(gate.replacement_capture_token)) {
      issues.push({
        severity: 'error',
        table: 'captureGates',
        rowId: gate.gate_id,
        field: 'replacement_capture_token',
        message: `Capture "${gate.replacement_capture_token}" has no vetted replacement producer.`
      });
    }
    if (!ROUTE_DIFFICULTY_VALUES.has(captureGateDifficulty(gate.difficulty))) {
      issues.push({
        severity: 'error',
        table: 'captureGates',
        rowId: gate.gate_id,
        field: 'difficulty',
        message: `Unknown capture-gate difficulty "${gate.difficulty}".`
      });
    }
    if (!['all', 'any', 'at_least', 'at_most'].includes(gate.match_mode)) {
      issues.push({
        severity: 'error',
        table: 'captureGates',
        rowId: gate.gate_id,
        field: 'match_mode',
        message: `Unknown capture count mode "${gate.match_mode}".`
      });
    }
    if (members.length === 0) {
      issues.push({
        severity: 'error',
        table: 'captureGates',
        rowId: gate.gate_id,
        field: 'gate_id',
        message: 'Capture gate must select at least one object.'
      });
    }
    const seenMembers = new Set<string>();
    for (const member of members) {
      const key = `${member.placement_stage}::${member.object_id}`;
      if (seenMembers.has(key)) {
        issues.push({
          severity: 'error',
          table: 'captureGateObjects',
          rowId: gate.gate_id,
          field: 'object_id',
          message: `Object "${key}" is selected more than once.`
        });
        continue;
      }
      seenMembers.add(key);
      const placement = placements.get(key);
      if (!placement) {
        issues.push({
          severity: 'error',
          table: 'captureGateObjects',
          rowId: gate.gate_id,
          field: 'object_id',
          message: `Selected object "${key}" is missing from extraction.`
        });
      } else if (placement.logic_stage !== gate.logic_stage) {
        issues.push({
          severity: 'error',
          table: 'captureGateObjects',
          rowId: gate.gate_id,
          field: 'placement_stage',
          message: `Selected object belongs to "${placement.logic_stage}", not "${gate.logic_stage}".`
        });
      } else if (!placementIsSwapEligible(placement)
          && captureTokenForPlacement(placement)
            !== gate.replacement_capture_token) {
        issues.push({
          severity: 'error',
          table: 'captureGateObjects',
          rowId: gate.gate_id,
          field: 'object_id',
          message: `Object "${key}" cannot be randomized to the requested capture.`
        });
      } else {
        const entity = enemyEntityByName.get(placement.unit_config_name);
        if (entity
            && entity.category === 'prop'
            && captureTokenForPlacement(placement)
              !== gate.replacement_capture_token) {
          issues.push({
            severity: 'warning',
            table: 'captureGateObjects',
            rowId: gate.gate_id,
            field: 'object_id',
            message: `Object "${key}" depends on the optional prop randomizer.`
          });
        } else if (entity
            && entity.category === 'npc'
            && captureTokenForPlacement(placement)
              !== gate.replacement_capture_token) {
          issues.push({
            severity: 'warning',
            table: 'captureGateObjects',
            rowId: gate.gate_id,
            field: 'object_id',
            message: `Object "${key}" depends on NPC randomization being enabled.`
          });
        }
      }
    }
    const minimum = Number.parseInt(gate.minimum_count || '0', 10);
    if (gate.match_mode === 'at_least'
        && (!Number.isInteger(minimum) || minimum < 1 || minimum > members.length)) {
      issues.push({
        severity: 'error',
        table: 'captureGates',
        rowId: gate.gate_id,
        field: 'minimum_count',
        message: 'At least X must be between 1 and the selected object count.'
      });
    }
    if (gate.match_mode === 'at_most'
        && (!Number.isInteger(minimum) || minimum < 1 || minimum > members.length)) {
      issues.push({
        severity: 'error',
        table: 'captureGates',
        rowId: gate.gate_id,
        field: 'minimum_count',
        message: 'At most X must be between 1 and the selected object count.'
      });
    }
    const applicableScenarios = edge
      ? sourceCaptureScenarios(
        edge,
        gate.source_capture_token,
        allTokens
      )
      : [];
    if (members.length > 0
        && applicableScenarios.length > 0
        && !applicableScenarios
          .some((scenario) => gateHasForcingOption(gate, members, placements, scenario))) {
      issues.push({
        severity: 'error',
        table: 'captureGates',
        rowId: gate.gate_id,
        field: 'gate_id',
        message: 'Capture gate has no scenario in which its selected objects can satisfy it.'
      });
    }
  }

  for (const edge of gateEdges) {
    const sourceTokens = captureTokensForEdge(
      edge, captureTokens, allTokens);
    const logicStage = routeLogicStage(edge, workspace);
    for (const sourceToken of sourceTokens) {
      if (reliableCaptureTokens.has(sourceToken)
          || captureIsStageLocked(logicStage, sourceToken)) {
        continue;
      }
      const earliestDifficulty = sourceCaptureMinimumDifficulty(
        edge,
        sourceToken,
        allTokens
      );
      if (earliestDifficulty === undefined) {
        continue;
      }
      if (!workspace.captureGates.some((gate) =>
        gate.edge_id === edge.edge_id
        && gate.source_capture_token === sourceToken
        && captureGateDifficultyRank(gate.difficulty)
          <= captureGateDifficultyRank(earliestDifficulty)
      )) {
        issues.push({
          severity: 'error',
          table: 'captureGates',
          rowId: edge.edge_id,
          field: 'source_capture_token',
          message: `${captureGateTargetName(edge)} capture "${sourceToken}" has no ${routeDifficultyLabel(earliestDifficulty)}-or-easier physical gate.`
        });
      }
    }
  }

  const hasStructuralError = issues
    .slice(firstCaptureGateIssue)
    .some((issue) => issue.severity === 'error');
  if (!hasStructuralError && workspace.captureGates.length > 0) {
    for (const difficulty of CAPTURE_GATE_DIFFICULTIES) {
      const satisfiability = captureGatesAreGloballySatisfiable(
        workspace,
        captureTokens,
        placements,
        membersByGate,
        difficulty
      );
      if (satisfiability === 'unsatisfiable') {
        issues.push({
          severity: 'error',
          table: 'captureGates',
          rowId: 'capture-gate-solver',
          field: 'difficulty',
          message: `Capture gates cannot be satisfied together for every applicable route or moon scenario at ${routeDifficultyLabel(difficulty)} logic.`
        });
      } else if (satisfiability === 'indeterminate') {
        issues.push({
          severity: 'warning',
          table: 'captureGates',
          rowId: 'capture-gate-solver',
          field: 'difficulty',
          message: `Capture-gate satisfiability search reached its authoring limit at ${routeDifficultyLabel(difficulty)} logic; run strict repository validation.`
        });
      }
    }
  }
}

function sourceCaptureScenarios(
  edge: RouteEdge,
  sourceToken: string,
  allTokens: string[]
): number[] {
  const requirement = parseRequirementExpression(
    edge.requirement_expr || '',
    allTokens
  );
  if (requirement.mode !== 'custom') {
    return [];
  }
  const groups = parseOptionScenarios(edge.option_scenarios);
  return Array.from({ length: 15 }, (_, index) => index + 1)
    .filter((scenario) => requirement.alternatives.some(
      (alternative, optionIndex) =>
        alternative.includes(sourceToken)
        && optionAppliesToScenario(groups, optionIndex, scenario)
    ));
}

function captureTokensForEdge(
  edge: RouteEdge,
  captureTokens: Set<string>,
  allTokens: Iterable<string> = captureTokens
): Set<string> {
  const parsed = parseRequirementExpression(
    edge.requirement_expr || '',
    allTokens
  );
  return new Set(parsed.alternatives.flat().filter((token) => captureTokens.has(token)));
}

function sourceCaptureMinimumDifficulty(
  edge: RouteEdge,
  sourceToken: string,
  allTokens: Iterable<string>
): string | undefined {
  const requirement = parseRequirementExpression(
    edge.requirement_expr || '',
    allTokens
  );
  if (requirement.mode !== 'custom') {
    return undefined;
  }
  let minimum: string | undefined;
  for (let optionIndex = 0; optionIndex < requirement.alternatives.length; optionIndex += 1) {
    if (!requirement.alternatives[optionIndex].includes(sourceToken)) {
      continue;
    }
    const difficulty = routeDifficultyForOption(edge.difficulty, optionIndex);
    if (!minimum
        || captureGateDifficultyRank(difficulty) < captureGateDifficultyRank(minimum)) {
      minimum = difficulty;
    }
  }
  return minimum;
}

type CaptureConstraint = {
  edgeId: string;
  scenario: number;
  alternatives: string[][];
};

type CaptureSatisfiability = 'satisfiable' | 'unsatisfiable' | 'indeterminate';

const CAPTURE_GATE_SEARCH_LIMIT = 50_000;
const CAPTURE_GATE_DIFFICULTIES = ROUTE_DIFFICULTY_LEVELS.map((level) => level.value);

function captureGateDifficulty(value: string): string {
  return normalizeRouteDifficulty(value || 'casual');
}

function captureGateDifficultyRank(value: string): number {
  return CAPTURE_GATE_DIFFICULTIES.indexOf(captureGateDifficulty(value));
}

function captureGatesAreGloballySatisfiable(
  workspace: LogicWorkspace,
  captureTokens: Set<string>,
  placements: Map<string, EnemySwapPlacement>,
  membersByGate: Map<string, CaptureGateObject[]>,
  logicDifficulty: string
): CaptureSatisfiability {
  const allTokens = new Set(workspace.logicTokens.map((token) => token.token));
  const logicDifficultyRank = captureGateDifficultyRank(logicDifficulty);
  const gatesByEdgeToken = new Map<string, CaptureGate[]>();
  for (const gate of workspace.captureGates) {
    if (captureGateDifficultyRank(gate.difficulty) > logicDifficultyRank) {
      continue;
    }
    const key = `${gate.edge_id}::${gate.source_capture_token}`;
    const gates = gatesByEdgeToken.get(key) ?? [];
    gates.push(gate);
    gatesByEdgeToken.set(key, gates);
  }
  for (const gates of gatesByEdgeToken.values()) {
    gates.sort((left, right) =>
      requiredGateCount(left, membersByGate.get(left.gate_id)?.length ?? 0)
        - requiredGateCount(right, membersByGate.get(right.gate_id)?.length ?? 0)
      || left.gate_id.localeCompare(right.gate_id)
    );
  }

  const constraints: CaptureConstraint[] = [];
  for (const edge of captureGateEdges(workspace, captureTokens)) {
    const logicStage = routeLogicStage(edge, workspace);
    const requirement = parseRequirementExpression(edge.requirement_expr || '', allTokens);
    if (requirement.mode !== 'custom') {
      continue;
    }
    const scenarioGroups = parseOptionScenarios(edge.option_scenarios);
    const alternatives = requirement.alternatives
      .map((alternative, optionIndex) => ({
        optionIndex,
        tokens: alternative.filter((token) =>
          captureTokens.has(token)
          && !reliableCaptureTokens.has(token)
          && !captureIsStageLocked(logicStage, token)
        )
      }))
      .filter(({ optionIndex }) =>
        captureGateDifficultyRank(routeDifficultyForOption(edge.difficulty, optionIndex))
          <= logicDifficultyRank
      );
    if (!alternatives.some(({ tokens }) => tokens.length > 0)) {
      continue;
    }
    for (let scenario = 1; scenario <= 15; scenario += 1) {
      const applicable = alternatives
        .filter(({ optionIndex }) =>
          optionAppliesToScenario(scenarioGroups, optionIndex, scenario)
        )
        .map(({ tokens }) => tokens);
      if (applicable.length > 0) {
        constraints.push({ edgeId: edge.edge_id, scenario, alternatives: applicable });
      }
    }
  }

  constraints.sort((left, right) => {
    const candidates = (constraint: CaptureConstraint) =>
      constraint.alternatives.reduce(
        (count, alternative) => count + alternative.reduce(
          (product, token) =>
            product * Math.max(1, gatesByEdgeToken.get(`${constraint.edgeId}::${token}`)?.length ?? 0),
          1
        ),
        0
      );
    return candidates(left) - candidates(right)
      || left.edgeId.localeCompare(right.edgeId)
      || left.scenario - right.scenario;
  });

  const forcedCaptures = new Map<string, string>();
  let searchSteps = 0;
  let truncated = false;

  const applyMembers = (gate: CaptureGate, memberKeys: string[]): string[] | undefined => {
    const changed: string[] = [];
    for (const memberKey of memberKeys) {
      const existing = forcedCaptures.get(memberKey);
      if (existing && existing !== gate.replacement_capture_token) {
        for (const key of changed) {
          forcedCaptures.delete(key);
        }
        return undefined;
      }
      if (!existing) {
        forcedCaptures.set(memberKey, gate.replacement_capture_token);
        changed.push(memberKey);
      }
    }
    return changed;
  };

  const rollback = (changed: string[]) => {
    for (const key of changed) {
      forcedCaptures.delete(key);
    }
  };

  const solveSources = (
    constraintIndex: number,
    constraint: CaptureConstraint,
    sourceTokens: string[],
    sourceIndex: number
  ): boolean => {
    if (searchSteps >= CAPTURE_GATE_SEARCH_LIMIT) {
      truncated = true;
      return false;
    }
    if (sourceIndex >= sourceTokens.length) {
      return solve(constraintIndex + 1);
    }
    const sourceToken = sourceTokens[sourceIndex];
    const gates = gatesByEdgeToken.get(`${constraint.edgeId}::${sourceToken}`) ?? [];
    for (const gate of gates) {
      const members = membersByGate.get(gate.gate_id) ?? [];
      const options = forcingMemberOptions(gate, members, placements, constraint.scenario);
      for (const option of options) {
        searchSteps += 1;
        const changed = applyMembers(gate, option);
        if (changed
            && solveSources(constraintIndex, constraint, sourceTokens, sourceIndex + 1)) {
          return true;
        }
        if (changed) {
          rollback(changed);
        }
        if (searchSteps >= CAPTURE_GATE_SEARCH_LIMIT) {
          truncated = true;
          return false;
        }
      }
    }
    return false;
  };

  const solve = (constraintIndex: number): boolean => {
    if (constraintIndex >= constraints.length) {
      return true;
    }
    const constraint = constraints[constraintIndex];
    for (const alternative of constraint.alternatives) {
      if (alternative.length === 0
          || solveSources(constraintIndex, constraint, alternative, 0)) {
        return true;
      }
    }
    return false;
  };

  const solved = solve(0);
  return solved ? 'satisfiable' : truncated ? 'indeterminate' : 'unsatisfiable';
}

function optionAppliesToScenario(
  groups: string[][],
  optionIndex: number,
  scenario: number
): boolean {
  if (groups.length === 0 || scenario > 5) {
    return true;
  }
  const group = groups.length === 1 ? groups[0] : groups[optionIndex];
  return !group || group.length === 0 || group.includes(String(scenario));
}

function routeLogicStage(edge: RouteEdge, workspace: LogicWorkspace): string {
  return workspace.logicNodes.find((node) => node.node_id === edge.from_node_id)?.stage
    || edge.from_node_id.split('::', 1)[0]
    || '';
}

function gateHasForcingOption(
  gate: CaptureGate,
  members: Array<{ placement_stage: string; object_id: string }>,
  placements: Map<string, EnemySwapPlacement>,
  scenario: number
): boolean {
  for (const _option of forcingMemberOptions(gate, members, placements, scenario)) {
    return true;
  }
  return false;
}

function forcingMemberOptions(
  gate: CaptureGate,
  members: Array<{ placement_stage: string; object_id: string }>,
  placements: Map<string, EnemySwapPlacement>,
  scenario: number
): Iterable<string[]> {
  const memberKeys = members.map((member) => `${member.placement_stage}::${member.object_id}`);
  const activeKeys = memberKeys.filter((key) => {
    const placement = placements.get(key);
    return placement && placementIsActiveInScenario(placement, scenario);
  });
  const eligibleKeys = activeKeys.filter((key) => {
    const placement = placements.get(key);
    return placement && placementCanProduceCapture(placement, gate.replacement_capture_token);
  });
  if (gate.match_mode === 'all') {
    return activeKeys.length === memberKeys.length && eligibleKeys.length === memberKeys.length
      ? [memberKeys]
      : [];
  }
  const required = requiredGateCount(gate, memberKeys.length);
  if (required < 1 || eligibleKeys.length < required) {
    return [];
  }
  return combinations(eligibleKeys, required);
}

function requiredGateCount(gate: CaptureGate, memberCount: number): number {
  if (gate.match_mode === 'all') {
    return memberCount;
  }
  if (gate.match_mode === 'any' || gate.match_mode === 'at_most') {
    return 1;
  }
  return Number.parseInt(gate.minimum_count || '0', 10);
}

function placementIsActiveInScenario(placement: EnemySwapPlacement, scenario: number): boolean {
  const scenarios = (placement.scenarios || '')
    .split(';')
    .map((value) => Number.parseInt(value.trim(), 10))
    .filter(Number.isInteger);
  return scenarios.length === 0 || scenarios.includes(scenario - 1);
}

function placementCanProduceCapture(
  placement: EnemySwapPlacement,
  replacementCapture: string
): boolean {
  if (enemyEntityByName.get(placement.unit_config_name)?.category === 'npc') {
    return false;
  }
  if (captureTokenForPlacement(placement) === replacementCapture) {
    return true;
  }
  const entity = enemyEntityByName.get(placement.unit_config_name);
  return Boolean(
    entity
    && ['capture', 'enemy', 'generator'].includes(entity.category)
    && placementIsSwapEligible(placement)
  );
}

function* combinations(values: string[], count: number): Generator<string[]> {
  const current: string[] = [];
  function* visit(start: number): Generator<string[]> {
    if (current.length === count) {
      yield [...current];
      return;
    }
    const remaining = count - current.length;
    for (let index = start; index <= values.length - remaining; index += 1) {
      current.push(values[index]);
      yield* visit(index + 1);
      current.pop();
    }
  }
  yield* visit(0);
}
