import { describe, expect, it } from 'vitest';
import { createDefaultWorkspace } from '../data/defaultData';
import { parseCsvDocument } from '../lib/csv';
import {
  arrangeLayoutForStage,
  arrangeMapLayoutForStage,
  cloneWorkspace,
  connectMoonToNode,
  createCustomNode,
  createReverseEdge,
  createRouteEdge,
  deleteRouteEdge,
  exportEditableCsvs,
  findRouteConnection,
  getGraphForStage,
  getRoom,
  listKingdoms,
  mergeKingdomWorkspace,
  moonNodeId,
  resetStageFromTemplate,
  routeConnectionId,
  sliceWorkspaceForKingdom,
  syncWorkspaceWithTemplate
} from '../lib/workspace';
import type { CaptureGate, GraphFilters, RouteEdge } from '../lib/types';

const filters: GraphFilters = {
  showExcluded: true,
  showMoons: true,
  showEvents: true,
  showRawAnchors: true,
  search: ''
};

describe('workspace graph editing', () => {
  it('loads the generated logic templates', () => {
    const workspace = createDefaultWorkspace();

    expect(workspace.moonAccess.length).toBeGreaterThan(700);
    expect(workspace.logicNodes.length).toBeGreaterThan(900);
    expect(workspace.routeEdges.some((edge) => edge.source === 'generated')).toBe(false);
    expect(listKingdoms(workspace)).toContain('Cap Kingdom');
    expect(
      workspace.logicNodes.filter((node) => node.stage === 'SandWorldHomeStage' && node.node_type === 'checkpoint')
    ).toHaveLength(9);
    expect(workspace.moonAccess.every((moon) => moon.moon_id)).toBe(true);
    expect(workspace.moonAccess.find((moon) => moon.moon_id === 'CapWorldHomeStage:926:227')?.shine_info_available_scenarios).toBe(
      '2;3;4'
    );
  });

  it('hides raw StageData transition anchors unless the raw layer is enabled', () => {
    const workspace = createDefaultWorkspace();
    const hiddenGraph = getGraphForStage(workspace, 'SandWorldHomeStage', { ...filters, showRawAnchors: false });
    const rawGraph = getGraphForStage(workspace, 'SandWorldHomeStage', { ...filters, showRawAnchors: true });

    expect(
      hiddenGraph.nodes.some(
        (node) =>
          node.type === 'stage_change' &&
          node.sourceRow.source === 'StageData' &&
          node.sourceRow.side === 'raw' &&
          !node.sourceRow.notes.includes('UnitConfigName=WorldWarpHole')
      )
    ).toBe(false);
    expect(hiddenGraph.nodes.some((node) => node.id === 'SandWorldHomeStage::stage_change::Go::raw')).toBe(true);
    expect(hiddenGraph.nodes.some((node) => node.type === 'checkpoint')).toBe(true);
    expect(
      rawGraph.nodes.some(
        (node) =>
          node.type === 'stage_change' &&
          node.sourceRow.source === 'StageData' &&
          node.sourceRow.side === 'raw' &&
          !node.sourceRow.notes.includes('UnitConfigName=WorldWarpHole')
      )
    ).toBe(true);
  });

  it('hides non-physical progression states while keeping physical event locations', () => {
    const workspace = createDefaultWorkspace();
    const graph = getGraphForStage(workspace, 'CapWorldHomeStage', filters);

    expect(graph.nodes.some((node) => node.id === 'CapWorldHomeStage::event::WorldPeace_Cap')).toBe(false);
    expect(graph.nodes.some((node) => node.id === 'CapWorldHomeStage::event::MoonRockActive_Cap')).toBe(true);
  });

  it('removes stale generated routes while keeping editor routes when cloning', () => {
    const workspace = createDefaultWorkspace();
    const room = getRoom(workspace, 'CapWorldHomeStage')!;
    const source = createCustomNode(workspace, room, 'Town');
    const target = createCustomNode(workspace, room, 'Top Hat Tower');
    const editorEdge = createRouteEdge(workspace, source.node_id, target.node_id, room);
    const generatedEdge: RouteEdge = {
      ...editorEdge,
      edge_id: 'edge_generated_0001',
      from_node_id: 'CapWorldHomeStage::start',
      to_node_id: source.node_id,
      source: 'generated'
    };
    workspace.routeEdges.push(generatedEdge);

    const cloned = cloneWorkspace(workspace);

    expect(cloned.routeEdges.some((edge) => edge.source === 'generated')).toBe(false);
    expect(cloned.routeEdges.some((edge) => edge.edge_id === editorEdge.edge_id)).toBe(true);
  });

  it('syncs older saved workspaces with newly generated template places', () => {
    const template = createDefaultWorkspace();
    const olderWorkspace = cloneWorkspace(template);
    olderWorkspace.logicNodes = olderWorkspace.logicNodes.filter(
      (node) => !(node.stage === 'SandWorldHomeStage' && node.node_type === 'checkpoint')
    );
    const room = getRoom(olderWorkspace, 'CapWorldHomeStage')!;
    const source = createCustomNode(olderWorkspace, room, 'Saved Custom Place');
    const target = createCustomNode(olderWorkspace, room, 'Saved Custom Target');
    const savedRoute = createRouteEdge(olderWorkspace, source.node_id, target.node_id, room);

    const synced = syncWorkspaceWithTemplate(olderWorkspace, template);

    expect(
      synced.logicNodes.filter((node) => node.stage === 'SandWorldHomeStage' && node.node_type === 'checkpoint')
    ).toHaveLength(9);
    expect(synced.logicNodes.some((node) => node.node_id === source.node_id)).toBe(true);
    expect(synced.routeEdges.some((edge) => edge.edge_id === savedRoute.edge_id)).toBe(true);
  });

  it('slices and merges one kingdom without replacing other kingdom edits', () => {
    const sourceWorkspace = cloneWorkspace(createDefaultWorkspace());
    const capRoom = getRoom(sourceWorkspace, 'CapWorldHomeStage')!;
    const cascadeRoom = getRoom(sourceWorkspace, 'WaterfallWorldHomeStage')!;
    const capSource = createCustomNode(sourceWorkspace, capRoom, 'Saved Cap Place');
    const capTarget = createCustomNode(sourceWorkspace, capRoom, 'Saved Cap Target');
    const capRoute = createRouteEdge(sourceWorkspace, capSource.node_id, capTarget.node_id, capRoom);
    const cascadeSource = createCustomNode(sourceWorkspace, cascadeRoom, 'Not In Cap Slice');
    const capPosition = { x: 321, y: 654 };
    sourceWorkspace.nodePositions[capSource.node_id] = capPosition;
    sourceWorkspace.nodePositions[cascadeSource.node_id] = { x: 111, y: 222 };

    const capSlice = sliceWorkspaceForKingdom(sourceWorkspace, 'Cap Kingdom');

    expect(capSlice.rooms.every((room) => room.kingdom === 'Cap Kingdom')).toBe(true);
    expect(capSlice.logicNodes.some((node) => node.node_id === capSource.node_id)).toBe(true);
    expect(capSlice.logicNodes.some((node) => node.node_id === cascadeSource.node_id)).toBe(false);
    expect(capSlice.routeEdges.some((edge) => edge.edge_id === capRoute.edge_id)).toBe(true);
    expect(capSlice.logicTokens).toHaveLength(sourceWorkspace.logicTokens.length);
    expect(capSlice.nodePositions[capSource.node_id]).toEqual(capPosition);
    expect(capSlice.nodePositions[cascadeSource.node_id]).toBeUndefined();

    const currentWorkspace = cloneWorkspace(createDefaultWorkspace());
    const currentCascadeRoom = getRoom(currentWorkspace, 'WaterfallWorldHomeStage')!;
    const keptCascadeNode = createCustomNode(currentWorkspace, currentCascadeRoom, 'Kept Cascade Place');
    const merged = mergeKingdomWorkspace(currentWorkspace, capSlice, 'Cap Kingdom');

    expect(merged.logicNodes.some((node) => node.node_id === capSource.node_id)).toBe(true);
    expect(merged.routeEdges.some((edge) => edge.edge_id === capRoute.edge_id)).toBe(true);
    expect(merged.nodePositions[capSource.node_id]).toEqual(capPosition);
    expect(merged.logicNodes.some((node) => node.node_id === keptCascadeNode.node_id)).toBe(true);
  });

  it('round-trips kingdom capture gates and cascades memberships when an edge is deleted', () => {
    const workspace = cloneWorkspace(createDefaultWorkspace());
    const room = getRoom(workspace, 'CapWorldHomeStage')!;
    const source = createCustomNode(workspace, room, 'Capture Gate Source');
    const target = createCustomNode(workspace, room, 'Capture Gate Target');
    const edge = createRouteEdge(workspace, source.node_id, target.node_id, room);
    edge.requirement_expr = 'Frog';
    const gate: CaptureGate = {
      gate_id: 'gate_workspace_roundtrip',
      kingdom: room.kingdom,
      logic_stage: room.stage,
      edge_id: edge.edge_id,
      source_capture_token: 'Frog',
      replacement_capture_token: 'Frog',
      difficulty: 'casual',
      match_mode: 'any',
      minimum_count: '',
      notes: 'test gate'
    };
    workspace.captureGates.push(gate);
    workspace.captureGateObjects.push({
      gate_id: gate.gate_id,
      placement_stage: 'CapWorldHomeStage',
      object_id: 'obj-test'
    });

    const slice = sliceWorkspaceForKingdom(workspace, room.kingdom);
    const merged = mergeKingdomWorkspace(createDefaultWorkspace(), slice, room.kingdom);
    const exported = exportEditableCsvs(merged);

    expect(slice.captureGates.map((row) => row.gate_id)).toContain(gate.gate_id);
    expect(slice.captureGateObjects.map((row) => row.gate_id)).toContain(gate.gate_id);
    expect(exported['capture_gates.csv']).toContain(gate.gate_id);
    expect(exported['capture_gate_objects.csv']).toContain('obj-test');
    expect(deleteRouteEdge(merged, edge.edge_id)).toBe(true);
    expect(merged.captureGates.some((row) => row.gate_id === gate.gate_id)).toBe(false);
    expect(merged.captureGateObjects.some((row) => row.gate_id === gate.gate_id)).toBe(false);
  });

  it('keeps legacy route rows with missing kingdom when endpoints are in the kingdom', () => {
    const sourceWorkspace = cloneWorkspace(createDefaultWorkspace());
    const capRoom = getRoom(sourceWorkspace, 'CapWorldHomeStage')!;
    const source = createCustomNode(sourceWorkspace, capRoom, 'Legacy Cap Source');
    const target = createCustomNode(sourceWorkspace, capRoom, 'Legacy Cap Target');
    const route = createRouteEdge(sourceWorkspace, source.node_id, target.node_id, capRoom);
    route.kingdom = '';

    const capSlice = sliceWorkspaceForKingdom(sourceWorkspace, 'Cap Kingdom');
    const slicedRoute = capSlice.routeEdges.find((edge) => edge.edge_id === route.edge_id);

    expect(slicedRoute?.kingdom).toBe('Cap Kingdom');

    const currentWorkspace = cloneWorkspace(sourceWorkspace);
    const merged = mergeKingdomWorkspace(currentWorkspace, capSlice, 'Cap Kingdom');
    const mergedRoutes = merged.routeEdges.filter((edge) => edge.edge_id === route.edge_id);

    expect(mergedRoutes).toHaveLength(1);
    expect(mergedRoutes[0].kingdom).toBe('Cap Kingdom');
  });

  it('creates bidirectional route rows and independent reverse edges', () => {
    const workspace = cloneWorkspace(createDefaultWorkspace());
    const room = getRoom(workspace, 'CapWorldHomeStage');
    expect(room).toBeDefined();

    const source = createCustomNode(workspace, room!, 'Town');
    const target = createCustomNode(workspace, room!, 'Top Hat Tower');
    const edge = createRouteEdge(workspace, source.node_id, target.node_id, room!);
    expect(edge.bidirectional).toBe('true');
    expect(edge.difficulty).toBe('casual');

    edge.requirement_expr = 'Frog';
    edge.difficulty = 'advanced';
    const reverse = createReverseEdge(workspace, edge);

    expect(edge.from_node_id).toBe(source.node_id);
    expect(edge.to_node_id).toBe(target.node_id);
    expect(edge.bidirectional).toBe('false');
    expect(reverse.from_node_id).toBe(target.node_id);
    expect(reverse.to_node_id).toBe(source.node_id);
    expect(reverse.bidirectional).toBe('false');
    expect(reverse.requirement_expr).toBe('');
    expect(reverse.difficulty).toBe('advanced');
  });

  it('defaults legacy route rows and headers to casual difficulty', () => {
    const workspace = cloneWorkspace(createDefaultWorkspace());
    workspace.csvHeaders.routeEdges = workspace.csvHeaders.routeEdges.filter((header) => header !== 'difficulty');
    const room = getRoom(workspace, 'CapWorldHomeStage')!;
    const source = createCustomNode(workspace, room, 'Legacy Difficulty Source');
    const target = createCustomNode(workspace, room, 'Legacy Difficulty Target');
    const edge = createRouteEdge(workspace, source.node_id, target.node_id, room);
    delete (edge as Partial<RouteEdge>).difficulty;

    const cloned = cloneWorkspace(workspace);
    const exported = parseCsvDocument<RouteEdge>(exportEditableCsvs(cloned)['route_edges.csv']);
    const exportedEdge = exported.rows.find((row) => row.edge_id === edge.edge_id);

    expect(cloned.csvHeaders.routeEdges).toContain('difficulty');
    expect(cloned.routeEdges.find((row) => row.edge_id === edge.edge_id)?.difficulty).toBe('casual');
    expect(exported.headers).toContain('difficulty');
    expect(exportedEdge?.difficulty).toBe('casual');
  });

  it('renders a single route row as bidirectional by default', () => {
    const workspace = cloneWorkspace(createDefaultWorkspace());
    const room = getRoom(workspace, 'CapWorldHomeStage')!;
    const source = createCustomNode(workspace, room, 'Town');
    const target = createCustomNode(workspace, room, 'Top Hat Tower');
    const edge = createRouteEdge(workspace, source.node_id, target.node_id, room);
    edge.requirement_expr = 'free';

    const graph = getGraphForStage(workspace, 'CapWorldHomeStage', filters);
    const renderedConnection = graph.edges.find((candidate) =>
      candidate.id === routeConnectionId(edge.from_node_id, edge.to_node_id)
    );

    expect(renderedConnection?.bidirectional).toBe(true);
  });

  it('exports legacy single route rows as bidirectional by default', () => {
    const workspace = cloneWorkspace(createDefaultWorkspace());
    const room = getRoom(workspace, 'CapWorldHomeStage')!;
    const source = createCustomNode(workspace, room, 'Legacy Town');
    const target = createCustomNode(workspace, room, 'Legacy Tower');
    const edge = createRouteEdge(workspace, source.node_id, target.node_id, room);
    edge.bidirectional = 'false';

    const csv = exportEditableCsvs(workspace)['route_edges.csv'];
    const exported = parseCsvDocument<RouteEdge>(csv).rows.find((row) => row.edge_id === edge.edge_id);

    expect(exported?.bidirectional).toBe('true');
  });

  it('places new custom nodes in the current area instead of far off-canvas', () => {
    const workspace = cloneWorkspace(createDefaultWorkspace());
    const room = getRoom(workspace, 'CapWorldHomeStage')!;
    const node = createCustomNode(workspace, room, 'Visible Place');
    const position = workspace.nodePositions[node.node_id];

    expect(position.x).toBeLessThan(1200);
    expect(position.y).toBeLessThan(900);
  });

  it('renders reciprocal route rows as one editable graph connection', () => {
    const workspace = cloneWorkspace(createDefaultWorkspace());
    const room = getRoom(workspace, 'CapWorldHomeStage')!;
    const source = createCustomNode(workspace, room, 'Town');
    const target = createCustomNode(workspace, room, 'Top Hat Tower');
    const forward = createRouteEdge(workspace, source.node_id, target.node_id, room);
    forward.requirement_expr = 'Frog';
    const reverse = createReverseEdge(workspace, forward);
    reverse.requirement_expr = 'free';

    const connectionId = routeConnectionId(forward.from_node_id, forward.to_node_id);
    const connection = findRouteConnection(workspace, connectionId);
    const graph = getGraphForStage(workspace, 'CapWorldHomeStage', filters);
    const renderedConnections = graph.edges.filter((edge) => edge.id === connectionId);

    expect(connection?.forward.edge_id).toBe(forward.edge_id);
    expect(connection?.reverse?.edge_id).toBe(reverse.edge_id);
    expect(renderedConnections).toHaveLength(1);
    expect(renderedConnections[0].bidirectional).toBe(true);
    expect(renderedConnections[0].label).toContain('Frog');
    expect(renderedConnections[0].label).toContain('free');
  });

  it('connects moons through moon_access instead of route_edges', () => {
    const workspace = cloneWorkspace(createDefaultWorkspace());
    const room = getRoom(workspace, 'CapWorldHomeStage')!;
    const node = createCustomNode(workspace, room, 'Captain Toad Ledge');
    const moon = workspace.moonAccess.find((candidate) => candidate.stage === 'CapWorldHomeStage')!;

    connectMoonToNode(workspace, moonNodeId(moon.moon_id), node.node_id);

    expect(moon.from_node_id).toBe(node.node_id);
    expect(workspace.routeEdges.some((edge) => edge.to_node_id === moonNodeId(moon.moon_id))).toBe(false);
  });

  it('resets only the selected stage from template data', () => {
    const template = createDefaultWorkspace();
    const workspace = cloneWorkspace(template);
    const capRoom = getRoom(workspace, 'CapWorldHomeStage')!;
    const cascadeRoom = getRoom(workspace, 'WaterfallWorldHomeStage')!;
    const capSource = createCustomNode(workspace, capRoom, 'Temporary Cap Place');
    const capTarget = createCustomNode(workspace, capRoom, 'Temporary Cap Target');
    const cascadeSource = createCustomNode(workspace, cascadeRoom, 'Cascade Place');
    const cascadeTarget = createCustomNode(workspace, cascadeRoom, 'Cascade Target');
    const capRoute = createRouteEdge(workspace, capSource.node_id, capTarget.node_id, capRoom);
    const cascadeRoute = createRouteEdge(workspace, cascadeSource.node_id, cascadeTarget.node_id, cascadeRoom);
    const templateCapMoon = template.moonAccess.find((moon) => moon.stage === 'CapWorldHomeStage')!;
    const capMoon = workspace.moonAccess.find((moon) => moon.moon_id === templateCapMoon.moon_id)!;
    connectMoonToNode(workspace, moonNodeId(capMoon.moon_id), capSource.node_id);

    const reset = resetStageFromTemplate(workspace, template, 'CapWorldHomeStage');

    expect(reset).toBe(true);
    expect(workspace.logicNodes.some((node) => node.node_id === capSource.node_id)).toBe(false);
    expect(workspace.routeEdges.some((edge) => edge.edge_id === capRoute.edge_id)).toBe(false);
    expect(workspace.moonAccess.find((moon) => moon.moon_id === capMoon.moon_id)?.from_node_id).toBe(
      templateCapMoon.from_node_id
    );
    expect(workspace.nodePositions[capSource.node_id]).toBeUndefined();
    expect(workspace.logicNodes.some((node) => node.node_id === cascadeSource.node_id)).toBe(true);
    expect(workspace.routeEdges.some((edge) => edge.edge_id === cascadeRoute.edge_id)).toBe(true);
  });

  it('normalizes legacy effort values when loading a workspace', () => {
    const workspace = createDefaultWorkspace();
    workspace.moonAccess[0].effort_level = 'expert';

    const cloned = cloneWorkspace(workspace);

    expect(cloned.moonAccess[0].effort_level).toBe('high');
  });

  it('builds a visible graph for a stage and exports editable CSVs', () => {
    const workspace = createDefaultWorkspace();
    const graph = getGraphForStage(workspace, 'CapWorldHomeStage', filters);
    const csvs = exportEditableCsvs(workspace);
    const graphIds = graph.nodes.map((node) => node.id);

    expect(graph.nodes.some((node) => node.kind === 'moon')).toBe(true);
    expect(new Set(graphIds).size).toBe(graphIds.length);
    expect(workspace.moonAccess.every((moon) => moon.effort_level)).toBe(true);
    expect(csvs['route_edges.csv']).toContain('edge_id,kingdom,area_scope');
    expect(csvs['route_edges.csv']).toContain('difficulty');
    expect(csvs['moon_access.csv']).toContain('moon_id,kingdom,stage');
    expect(csvs['moon_access.csv']).toContain('effort_level');
    expect(csvs['moon_access.csv']).toContain('shine_info_available_scenarios');
    expect(csvs['moon_access.csv']).not.toContain('__editor_id');
  });

  it('surfaces fixed Mushroom boss paintings without exposing raw anchors', () => {
    const workspace = createDefaultWorkspace();
    const stage = 'PeachWorldPictureGiantWanderBossStage';
    const nodeId = `${stage}::stage_change::PictureGiantWanderBoss::raw`;
    const graph = getGraphForStage(workspace, stage, {
      ...filters,
      showRawAnchors: false
    });
    const node = graph.nodes.find((candidate) => candidate.id === nodeId);
    const fixedEdge = workspace.routeEdges.find(
      (edge) => edge.edge_id === 'fixed_mushroom_giant_wander_boss'
    );

    expect(node?.type).toBe('fixed_transition');
    expect(node?.label).toContain('Painting to Mollusque-Lanceur');
    expect(node?.subtitle).toBe('fixed vanilla transition');
    expect(fixedEdge?.source).toBe('fixed_transition');
    expect(fixedEdge?.to_node_id).toBe('RevengeGiantWanderBossStage::start');
    expect(graph.edges.some((edge) => edge.id.includes(encodeURIComponent(nodeId)))).toBe(true);
  });

  it('restores fixed painting transitions when syncing an older autosave', () => {
    const template = createDefaultWorkspace();
    const workspace = cloneWorkspace(template);
    workspace.routeEdges = workspace.routeEdges.filter((edge) => edge.source !== 'fixed_transition');

    const synced = syncWorkspaceWithTemplate(workspace, template);

    expect(synced.routeEdges.filter((edge) => edge.source === 'fixed_transition')).toHaveLength(6);
  });

  it('arranges a busy overworld into related hub clusters', () => {
    const workspace = createDefaultWorkspace();
    arrangeLayoutForStage(workspace, 'CapWorldHomeStage');
    const graph = getGraphForStage(workspace, 'CapWorldHomeStage', filters);
    const checkpoints = graph.nodes.filter((node) => node.type === 'checkpoint');
    const stageChanges = graph.nodes.filter((node) => node.type === 'stage_change');
    const moons = graph.nodes.filter((node) => node.kind === 'moon');
    const frogCheckpoint = graph.nodes.find(
      (node) => node.id === 'CapWorldHomeStage::checkpoint::obj381'
    )!;
    const frogMoons = workspace.moonAccess
      .filter((moon) => moon.stage === 'CapWorldHomeStage' && moon.from_node_id === frogCheckpoint.id)
      .map((moon) => graph.nodes.find((node) => node.id === moonNodeId(moon.__editor_id))!);
    const checkpointCenter = frogCheckpoint.x + 95;
    const moonCenters = frogMoons.map((moon) => moon.x + 89);

    expect(frogMoons.length).toBeGreaterThan(1);
    expect(Math.max(...frogMoons.map((moon) => distance(frogCheckpoint, moon)))).toBeLessThan(900);
    expect(moonCenters.some((center) => center < checkpointCenter)).toBe(true);
    expect(moonCenters.some((center) => center > checkpointCenter)).toBe(true);
    expect(Math.min(...moons.map((node) => node.x))).toBeLessThan(Math.max(...checkpoints.map((node) => node.x)));
    expect(Math.min(...moons.map((node) => node.x))).toBeLessThan(Math.max(...stageChanges.map((node) => node.x)));
    expect(hasTightOverlap(graph.nodes)).toBe(false);
  });

  it('can arrange coordinate-backed places into a physical map layout', () => {
    const workspace = createDefaultWorkspace();
    arrangeMapLayoutForStage(workspace, 'CapWorldHomeStage');

    const homeEntrance = workspace.nodePositions['CapWorldHomeStage::entrance::FrogSearchExStageEnt::home_in'];
    const rawStageChange = workspace.nodePositions['CapWorldHomeStage::stage_change::FrogSearchExStageEnt::raw'];
    const checkpoint = workspace.nodePositions['CapWorldHomeStage::checkpoint::obj381'];
    const captainToadMoon = workspace.nodePositions[moonNodeId('CapWorldHomeStage:926:227')];

    expect(distance(homeEntrance, rawStageChange)).toBeLessThan(260);
    expect(distance(homeEntrance, checkpoint)).toBeGreaterThan(500);
    expect(captainToadMoon.x).toBeLessThan(2600);
  });

  it('uses scenario metadata instead of inferred world-peace or moon-rock state on moons', () => {
    const workspace = createDefaultWorkspace();
    const moon = workspace.moonAccess.find((candidate) =>
      candidate.placement_unit_config_names.includes('ShineAddByRock')
    )!;
    const graph = getGraphForStage(workspace, moon.stage, filters);
    const node = graph.nodes.find((candidate) => candidate.id === moonNodeId(moon.__editor_id))!;

    expect(moon.state_requirement_expr).toBe('');
    expect(moon.placement_stage_map_indices).not.toBe('');
    expect(node.subtitle).toContain(`maps ${moon.placement_stage_map_indices}`);
    expect(node.subtitle).not.toContain('MoonRockActive');
    expect(node.subtitle).not.toContain('WorldPeace');
  });

  it('promotes raw moon rock markers into named progression event locations', () => {
    const workspace = createDefaultWorkspace();
    const moonRock = workspace.logicNodes.find((node) => node.node_id === 'CapWorldHomeStage::event::MoonRockActive_Cap')!;

    expect(workspace.logicNodes.some((node) => node.node_id === 'CapWorldHomeStage::stage_change::MoonRock::raw')).toBe(false);
    expect(moonRock.display_name).toBe('Moon Rock');
    expect(moonRock.node_type).toBe('state_event');
    expect(moonRock.obj_id).toBe('obj859');
    expect(moonRock.x).not.toBe('');
  });

  it('marks story moons distinctly and exports scenario transition edits', () => {
    const workspace = createDefaultWorkspace();
    const storyMoon = workspace.moonAccess.find((candidate) => candidate.flags.includes('ShineFlag_Story'))!;
    storyMoon.scenario_move_to = '3';

    const graph = getGraphForStage(workspace, storyMoon.stage, filters);
    const node = graph.nodes.find((candidate) => candidate.id === moonNodeId(storyMoon.__editor_id))!;
    const csvs = exportEditableCsvs(workspace);

    expect(node.type).toBe('story_moon');
    expect(node.subtitle).toContain('story');
    expect(node.subtitle).toContain('-> scenario 3');
    expect(csvs['moon_access.csv']).toContain('scenario_move_to');
  });

  it('places the Bowser treasure vault moon in the treasure vault stage', () => {
    const workspace = createDefaultWorkspace();
    const treasureMoon = workspace.moonAccess.find(
      (moon) => moon.moon_name === "Bowser's Castle Treasure Vault"
    )!;
    const shopMoon = workspace.moonAccess.find((moon) => moon.moon_name === "Shopping at Bowser's Castle")!;
    const graph = getGraphForStage(workspace, 'SkyWorldTreasureStage', filters);

    expect(treasureMoon.stage).toBe('SkyWorldTreasureStage');
    expect(treasureMoon.runtime_shine_key).toBe('SkyWorldTreasureStage:95:999');
    expect(shopMoon.stage).toBe('SkyWorldShopStage');
    expect(graph.nodes.some((node) => node.kind === 'moon' && node.label === "Bowser's Castle Treasure Vault")).toBe(true);
  });

  it('flattens embedded zones into their nearest loading-stage parent', () => {
    const workspace = createDefaultWorkspace();
    const expectations = [
      ['LakeWorldTownZone', 'LakeWorldHomeStage'],
      ['CityWorldFactory01Zone', 'CityWorldFactoryStage'],
      ['SnowWorldBalconyZone', 'SnowWorldTownStage'],
      ['SkyWorldCastleZone', 'SkyWorldHomeStage'],
      ['SkyWorldWallZone', 'SkyWorldHomeStage']
    ];

    for (const [zone, parent] of expectations) {
      expect(getRoom(workspace, zone)).toBeUndefined();
      expect(workspace.logicNodes.find((node) => node.node_id.startsWith(`${zone}::`))?.stage).toBe(parent);
    }

    const wallLandmark = workspace.logicNodes.find((node) => node.node_id === 'SkyWorldWallZone::start')!;
    const wallMoon = workspace.moonAccess.find(
      (moon) => moon.runtime_shine_key.startsWith('SkyWorldWallZone:')
    )!;
    expect(wallLandmark.node_type).toBe('custom_place');
    expect(wallLandmark.stage).toBe('SkyWorldHomeStage');
    expect(wallMoon.stage).toBe('SkyWorldHomeStage');
    expect(wallMoon.area_scope).toBe('overworld');
  });

  it('normalizes legacy embedded-zone autosaves on clone', () => {
    const workspace = createDefaultWorkspace();
    const parent = getRoom(workspace, 'SkyWorldHomeStage')!;
    const wall = {
      ...parent,
      stage: 'SkyWorldWallZone',
      stage_display_name: 'SkyWorldWallZone',
      stage_start_node_id: 'SkyWorldWallZone::start',
      area_scope: 'subarea',
      listed_in_stage_warp: 'false'
    };
    workspace.rooms.push(wall);
    const wallNode = workspace.logicNodes.find((node) => node.node_id === 'SkyWorldWallZone::start')!;
    const wallMoon = workspace.moonAccess.find(
      (moon) => moon.runtime_shine_key.startsWith('SkyWorldWallZone:')
    )!;
    wallNode.stage = 'SkyWorldWallZone';
    wallNode.area_scope = 'subarea';
    wallNode.stage_display_name = 'SkyWorldWallZone';
    wallNode.node_type = 'stage_start';
    wallMoon.stage = 'SkyWorldWallZone';
    wallMoon.area_scope = 'subarea';
    wallMoon.stage_display_name = 'SkyWorldWallZone';

    const cloned = cloneWorkspace(workspace);

    expect(getRoom(cloned, 'SkyWorldWallZone')).toBeUndefined();
    expect(cloned.logicNodes.find((node) => node.node_id === 'SkyWorldWallZone::start')?.stage).toBe(
      'SkyWorldHomeStage'
    );
    expect(cloned.logicNodes.find((node) => node.node_id === 'SkyWorldWallZone::start')?.node_type).toBe(
      'custom_place'
    );
    expect(
      cloned.moonAccess.find((moon) => moon.runtime_shine_key.startsWith('SkyWorldWallZone:'))?.stage
    ).toBe('SkyWorldHomeStage');
  });

  it('keeps a manually reconnected embedded-zone moon pointed at its new node across reclones', () => {
    // Regression test: flattenLegacyEmbeddedZones used to force any embedded-zone
    // moon's from_node_id back to `${child}::start` on every reclone unless it
    // still started with the child stage's own prefix. That silently reverted any
    // reconnection to a node elsewhere in the merged parent stage (e.g. a custom
    // place like Bowser's "Mech Fight" node) the moment the workspace recloned -
    // which happens on kingdom save load/merge and autosave restore.
    const workspace = createDefaultWorkspace();
    const wallMoon = workspace.moonAccess.find((moon) =>
      moon.runtime_shine_key.startsWith('SkyWorldWallZone:')
    )!;
    const targetNode = createCustomNode(
      workspace,
      getRoom(workspace, 'SkyWorldHomeStage')!,
      'Mech Fight'
    );

    connectMoonToNode(workspace, moonNodeId(wallMoon.moon_id), targetNode.node_id);
    expect(wallMoon.from_node_id).toBe(targetNode.node_id);

    const reclonedOnce = cloneWorkspace(workspace);
    const reclonedTwice = cloneWorkspace(reclonedOnce);
    const moonAfter = reclonedTwice.moonAccess.find((moon) => moon.moon_id === wallMoon.moon_id)!;

    expect(moonAfter.from_node_id).toBe(targetNode.node_id);
  });

  it('hides redundant generated default starts in subareas with entrance sides', () => {
    const workspace = createDefaultWorkspace();
    const graph = getGraphForStage(workspace, 'RollingExStage', filters);

    expect(graph.nodes.some((node) => node.id === 'RollingExStage::start')).toBe(false);
    expect(graph.nodes.some((node) => node.id === 'RollingExStage::entrance::rollingstart::sub_near')).toBe(true);
  });

  it('derives entrance exclusion from config, not a frozen save source', () => {
    const hideExcluded: GraphFilters = { ...filters, showExcluded: false };

    // Active in the current config (was previously a stale EntranceDB:excluded) -> visible.
    const peach = getGraphForStage(createDefaultWorkspace(), 'PeachWorldHomeStage', hideExcluded);
    expect(
      peach.nodes.some((node) => node.id === 'PeachWorldHomeStage::entrance::Fukuwarai2::home_in')
    ).toBe(true);

    // Genuinely excluded in the config (entrance_links status != active) -> hidden.
    const cap = getGraphForStage(createDefaultWorkspace(), 'CapWorldHomeStage', hideExcluded);
    expect(cap.nodes.some((node) => node.id === 'CapWorldHomeStage::entrance::Ex::home_in')).toBe(false);
  });

  it('keeps a subarea entrance and exit as two separate nodes', () => {
    const workspace = createDefaultWorkspace();
    const graph = getGraphForStage(workspace, 'FrogSearchExStage', filters);

    const near = graph.nodes.find(
      (node) => node.id === 'FrogSearchExStage::entrance::FrogSearchExStageEnt::sub_near'
    );
    const far = graph.nodes.find(
      (node) => node.id === 'FrogSearchExStage::entrance::FrogSearchExStageEnt::sub_far'
    );
    expect(near?.label.startsWith('Subarea entry:')).toBe(true);
    expect(far?.label.startsWith('Subarea return:')).toBe(true);
    expect(graph.edges.some((edge) => edge.from === edge.to)).toBe(false);
  });

  it('collapses an overworld door entrance and return into a single node', () => {
    const workspace = createDefaultWorkspace();
    const graph = getGraphForStage(workspace, 'CapWorldHomeStage', filters);

    expect(
      graph.nodes.some(
        (node) =>
          node.id === 'CapWorldHomeStage::entrance::FrogSearchExStageEnt::home_in' &&
          node.label.startsWith('Overworld entrance/return:')
      )
    ).toBe(true);
    expect(
      graph.nodes.some((node) => node.id === 'CapWorldHomeStage::entrance::FrogSearchExStageEnt::home_out')
    ).toBe(false);
  });

  it('tags one-way overworld doors as ENTRANCE or EXIT so they are not duplicates', () => {
    const workspace = createDefaultWorkspace();
    const graph = getGraphForStage(workspace, 'SandWorldHomeStage', filters);

    const byId = (id: string) => graph.nodes.find((node) => node.id === id);

    // aaa is the active enterable side of the Jaxi Driving door (entrance-only).
    expect(byId('SandWorldHomeStage::entrance::aaa::home_in')?.label).toBe(
      'Overworld entrance: Jaxi Driving (ENTRANCE)'
    );
    // run00 is only ever the active home_out (exit) side -> exit-only, no longer a
    // look-alike duplicate of the real entrance.
    expect(byId('SandWorldHomeStage::entrance::run00::home_in')?.label).toBe(
      'Overworld return: Jaxi Driving (EXIT)'
    );
    // A genuine two-way door stays untagged.
    const rocket = byId('SandWorldHomeStage::entrance::rocket::home_in')?.label ?? '';
    expect(rocket.startsWith('Overworld entrance/return:')).toBe(true);
    expect(rocket).not.toMatch(/\((ENTRANCE|EXIT)\)/);
  });
});

function hasTightOverlap(nodes: Array<{ x: number; y: number }>): boolean {
  for (let leftIndex = 0; leftIndex < nodes.length; leftIndex += 1) {
    for (let rightIndex = leftIndex + 1; rightIndex < nodes.length; rightIndex += 1) {
      const left = nodes[leftIndex];
      const right = nodes[rightIndex];
      if (Math.abs(left.x - right.x) < 170 && Math.abs(left.y - right.y) < 86) {
        return true;
      }
    }
  }
  return false;
}

function distance(left: { x: number; y: number }, right: { x: number; y: number }): number {
  return Math.hypot(left.x - right.x, left.y - right.y);
}
