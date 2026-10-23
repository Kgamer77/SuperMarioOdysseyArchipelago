import { describe, expect, it } from 'vitest';
import { createDefaultWorkspace } from '../data/defaultData';
import {
  enemyEntityByName,
  enemySwapPlacements,
  placementIsSwapEligible
} from '../data/enemySwapData';
import { cloneWorkspace } from '../lib/workspace';
import { validateWorkspace, verifyStageMap } from '../lib/validation';
import type { LogicNode, MoonAccess, Room, RouteEdge } from '../lib/types';

describe('workspace validation', () => {
  it('reports missing endpoints and unknown requirement tokens', () => {
    const workspace = cloneWorkspace(createDefaultWorkspace());
    const badEdge = Object.fromEntries(workspace.csvHeaders.routeEdges.map((header) => [header, ''])) as RouteEdge;
    badEdge.edge_id = 'edge_test_bad';
    badEdge.kingdom = 'Cap Kingdom';
    badEdge.area_scope = 'overworld';
    badEdge.from_node_id = 'missing::source';
    badEdge.to_node_id = 'missing::target';
    badEdge.requirement_expr = 'Frog | MissingCapture';
    workspace.routeEdges.push(badEdge);

    const issues = validateWorkspace(workspace).filter((issue) => issue.rowId === 'edge_test_bad');

    expect(issues.some((issue) => issue.message.includes('Missing source node'))).toBe(true);
    expect(issues.some((issue) => issue.message.includes('Missing target node'))).toBe(true);
    expect(issues.some((issue) => issue.message === 'Unknown token: MissingCapture')).toBe(true);
  });

  it('reports invalid effort levels', () => {
    const workspace = cloneWorkspace(createDefaultWorkspace());
    const moon = workspace.moonAccess[0];
    moon.effort_level = 'basically_mars';

    const issues = validateWorkspace(workspace).filter((issue) => issue.rowId === moon.moon_id);

    expect(issues.some((issue) => issue.field === 'effort_level')).toBe(true);
  });

  it('reports invalid route difficulties', () => {
    const workspace = cloneWorkspace(createDefaultWorkspace());
    const room = workspace.rooms.find((candidate) => candidate.stage === 'CapWorldHomeStage')!;
    const source = makeNode(workspace, room, 'CapWorldHomeStage::place::difficulty_source', 'custom_place', 'Source');
    const target = makeNode(workspace, room, 'CapWorldHomeStage::place::difficulty_target', 'custom_place', 'Target');
    const edge = makeEdge(workspace, room, 'edge_bad_difficulty', source.node_id, target.node_id);
    edge.difficulty = 'basically_mars';

    workspace.logicNodes.push(source, target);
    workspace.routeEdges.push(edge);

    const issues = validateWorkspace(workspace).filter((issue) => issue.rowId === edge.edge_id);

    expect(issues.some((issue) => issue.field === 'difficulty')).toBe(true);
  });

  it('reports capture gates whose simultaneous placement assignments conflict', () => {
    const workspace = cloneWorkspace(createDefaultWorkspace());
    const placement = enemySwapPlacements.find((candidate) => {
      const entity = enemyEntityByName.get(candidate.unit_config_name);
      return placementIsSwapEligible(candidate)
        && Boolean(workspace.rooms.find((room) => room.stage === candidate.logic_stage))
        && Boolean(entity && ['capture', 'enemy', 'generator'].includes(entity.category));
    })!;
    const room = workspace.rooms.find((candidate) => candidate.stage === placement.logic_stage)!;
    const source = makeNode(
      workspace,
      room,
      `${room.stage}::place::gate_conflict_source`,
      'custom_place',
      'Gate conflict source'
    );
    const middle = makeNode(
      workspace,
      room,
      `${room.stage}::place::gate_conflict_middle`,
      'custom_place',
      'Gate conflict middle'
    );
    const target = makeNode(
      workspace,
      room,
      `${room.stage}::place::gate_conflict_target`,
      'custom_place',
      'Gate conflict target'
    );
    const frogEdge = makeEdge(workspace, room, 'edge_gate_conflict_frog', source.node_id, middle.node_id);
    const tankEdge = makeEdge(workspace, room, 'edge_gate_conflict_tank', middle.node_id, target.node_id);
    frogEdge.requirement_expr = 'Frog';
    tankEdge.requirement_expr = 'Tank';
    workspace.logicNodes.push(source, middle, target);
    workspace.routeEdges = [frogEdge, tankEdge];
    workspace.captureGates = [
      {
        gate_id: 'gate_conflict_frog',
        kingdom: room.kingdom,
        logic_stage: room.stage,
        edge_id: frogEdge.edge_id,
        source_capture_token: 'Frog',
        replacement_capture_token: 'Frog',
        difficulty: 'casual',
        match_mode: 'any',
        minimum_count: '',
        notes: ''
      },
      {
        gate_id: 'gate_conflict_tank',
        kingdom: room.kingdom,
        logic_stage: room.stage,
        edge_id: tankEdge.edge_id,
        source_capture_token: 'Tank',
        replacement_capture_token: 'Tank',
        difficulty: 'casual',
        match_mode: 'any',
        minimum_count: '',
        notes: ''
      }
    ];
    workspace.captureGateObjects = workspace.captureGates.map((gate) => ({
      gate_id: gate.gate_id,
      placement_stage: placement.stage,
      object_id: placement.object_id
    }));

    const issues = validateWorkspace(workspace);

    expect(issues.some((issue) =>
      issue.rowId === 'capture-gate-solver'
      && issue.message.includes('cannot be satisfied together')
    )).toBe(true);
  });

  it('reports orphan capture-gate memberships', () => {
    const workspace = cloneWorkspace(createDefaultWorkspace());
    workspace.captureGateObjects.push({
      gate_id: 'missing_gate',
      placement_stage: 'CapWorldHomeStage',
      object_id: 'obj1'
    });

    const issues = validateWorkspace(workspace);

    expect(issues.some((issue) =>
      issue.table === 'captureGateObjects'
      && issue.rowId === 'missing_gate'
      && issue.message.includes('missing gate')
    )).toBe(true);
  });

  it('verifies a complete map without changing source templates', () => {
    const workspace = cloneWorkspace(createDefaultWorkspace());
    const room = {
      ...workspace.rooms[0],
      stage: 'UnitTestStage',
      kingdom: 'Unit Kingdom',
      stage_display_name: 'Unit Test Stage',
      stage_start_node_id: 'UnitTestStage::start',
      area_scope: 'subarea'
    } as Room;
    const start = makeNode(workspace, room, 'UnitTestStage::start', 'stage_start', 'Default start');
    const checkpoint = makeNode(workspace, room, 'UnitTestStage::checkpoint', 'checkpoint', 'Checkpoint');
    const toCheckpoint = makeEdge(workspace, room, 'edge_unit_001', start.node_id, checkpoint.node_id);

    workspace.rooms.push(room);
    workspace.logicNodes.push(start, checkpoint);
    workspace.routeEdges.push(toCheckpoint);

    const issues = verifyStageMap(workspace, room.stage);

    expect(issues).toHaveLength(0);
  });

  it('treats a single route row as bidirectional by default', () => {
    const workspace = cloneWorkspace(createDefaultWorkspace());
    const room = {
      ...workspace.rooms[0],
      stage: 'UnitArrivalStage',
      kingdom: 'Unit Kingdom',
      stage_display_name: 'Unit Arrival Stage',
      stage_start_node_id: 'UnitArrivalStage::start',
      area_scope: 'subarea'
    } as Room;
    const start = makeNode(workspace, room, 'UnitArrivalStage::start', 'stage_start', 'Default start');
    const farSide = makeNode(
      workspace,
      room,
      'UnitArrivalStage::entrance::goal::sub_far',
      'substage_entrance',
      'Far side'
    );
    farSide.side = 'sub_far';
    const toFarSide = makeEdge(workspace, room, 'edge_unit_arrival_001', start.node_id, farSide.node_id);

    workspace.rooms.push(room);
    workspace.logicNodes.push(start, farSide);
    workspace.routeEdges.push(toFarSide);

    const issues = verifyStageMap(workspace, room.stage);

    expect(
      issues.some((issue) => issue.rowId === farSide.node_id && issue.field === 'outgoing_routes')
    ).toBe(false);
    expect(
      issues.some((issue) => issue.rowId === farSide.node_id && issue.field === 'mutual_reachability')
    ).toBe(false);
  });

  it('accepts explicitly blocked reverse routes as intentional one-way paths', () => {
    const workspace = cloneWorkspace(createDefaultWorkspace());
    const room = {
      ...workspace.rooms[0],
      stage: 'UnitBlockedStage',
      kingdom: 'Unit Kingdom',
      stage_display_name: 'Unit Blocked Stage',
      stage_start_node_id: 'UnitBlockedStage::start',
      area_scope: 'subarea'
    } as Room;
    const start = makeNode(workspace, room, 'UnitBlockedStage::start', 'stage_start', 'Default start');
    const farSide = makeNode(
      workspace,
      room,
      'UnitBlockedStage::entrance::goal::sub_far',
      'substage_entrance',
      'Far side'
    );
    farSide.side = 'sub_far';
    const toFarSide = makeEdge(workspace, room, 'edge_unit_blocked_001', start.node_id, farSide.node_id);
    const blockedReturn = makeEdge(workspace, room, 'edge_unit_blocked_002', farSide.node_id, start.node_id);
    blockedReturn.requirement_expr = 'impossible';

    workspace.rooms.push(room);
    workspace.logicNodes.push(start, farSide);
    workspace.routeEdges.push(toFarSide, blockedReturn);

    const issues = verifyStageMap(workspace, room.stage);

    expect(
      issues.some((issue) => issue.rowId === farSide.node_id && issue.field === 'mutual_reachability')
    ).toBe(false);
  });

  it('verifies connected moons reachable from the overworld', () => {
    const workspace = cloneWorkspace(createDefaultWorkspace());
    // An overworld map is entered directly via the Odyssey, so its connected moon
    // is reachable kingdom-wide.
    const room = {
      ...workspace.rooms[0],
      stage: 'UnitMoonStage',
      kingdom: 'Unit Kingdom',
      stage_display_name: 'Unit Moon Stage',
      stage_start_node_id: 'UnitMoonStage::start',
      area_scope: 'overworld'
    } as Room;
    const start = makeNode(workspace, room, 'UnitMoonStage::start', 'stage_start', 'Default start');
    const checkpoint = makeNode(workspace, room, 'UnitMoonStage::checkpoint', 'checkpoint', 'Checkpoint');
    const toCheckpoint = makeEdge(workspace, room, 'edge_unit_moon_001', start.node_id, checkpoint.node_id);
    const moon = makeMoon(workspace, room, 'UnitMoonStage:1:1', checkpoint.node_id);

    workspace.rooms.push(room);
    workspace.logicNodes.push(start, checkpoint);
    workspace.routeEdges.push(toCheckpoint);
    workspace.moonAccess.push(moon);

    const issues = verifyStageMap(workspace, room.stage).filter((issue) => issue.rowId === moon.moon_id);

    expect(issues).toHaveLength(0);
  });

  it('ignores raw StageData transition anchors during normal map verification', () => {
    const workspace = cloneWorkspace(createDefaultWorkspace());
    const room = {
      ...workspace.rooms[0],
      stage: 'UnitRawStage',
      kingdom: 'Unit Kingdom',
      stage_display_name: 'Unit Raw Stage',
      stage_start_node_id: 'UnitRawStage::start',
      area_scope: 'subarea'
    } as Room;
    const start = makeNode(workspace, room, 'UnitRawStage::start', 'stage_start', 'Default start');
    const raw = makeNode(
      workspace,
      room,
      'UnitRawStage::stage_change::pipe::raw',
      'stage_change',
      'Stage Change pipe'
    );
    raw.source = 'StageData';
    raw.side = 'raw';

    workspace.rooms.push(room);
    workspace.logicNodes.push(start, raw);

    const normalIssues = verifyStageMap(workspace, room.stage);
    const rawIssues = verifyStageMap(workspace, room.stage, { includeRawAnchors: true });

    expect(normalIssues.some((issue) => issue.rowId === raw.node_id)).toBe(false);
    expect(rawIssues.some((issue) => issue.rowId === raw.node_id && issue.field === 'reachability')).toBe(true);
  });

  it('flags a sub-area moon that is unreachable from the kingdom overworld', () => {
    const workspace = cloneWorkspace(createDefaultWorkspace());
    const overworld = makeRoom(workspace, 'KrHomeOverworld', 'overworld', true);
    const sub = makeRoom(workspace, 'KrBossSub', 'subarea', false);
    const owStart = makeNode(workspace, overworld, 'KrHomeOverworld::start', 'stage_start', 'OW start');
    const subStart = makeNode(workspace, sub, 'KrBossSub::start', 'stage_start', 'Sub start');
    const checkpoint = makeNode(workspace, sub, 'KrBossSub::checkpoint', 'checkpoint', 'Boss checkpoint');
    const internal = makeEdge(workspace, sub, 'kr_edge_internal', subStart.node_id, checkpoint.node_id);
    const moon = makeMoon(workspace, sub, 'KrBossSub:grand', checkpoint.node_id);
    moon.flags = 'ShineFlag_Multi';

    workspace.rooms.push(overworld, sub);
    workspace.logicNodes.push(owStart, subStart, checkpoint);
    workspace.routeEdges.push(internal);
    workspace.moonAccess.push(moon);

    const issues = validateWorkspace(workspace).filter((issue) => issue.rowId === moon.moon_id);
    expect(issues.some((issue) => issue.message.includes('not reachable from the kingdom overworld'))).toBe(true);

    // Wire the entrance: a route to the overworld door + the home<->sub door pair.
    const homeDoor = makeNode(workspace, overworld, 'KrHomeOverworld::entrance::krdoor::home_in', 'home_entrance', 'Door');
    homeDoor.side = 'home_in';
    homeDoor.entrance_id = 'krdoor';
    const subDoor = makeNode(workspace, sub, 'KrBossSub::entrance::krdoor::sub_near', 'substage_entrance', 'Door');
    subDoor.side = 'sub_near';
    subDoor.entrance_id = 'krdoor';
    const toDoor = makeEdge(workspace, overworld, 'kr_edge_to_door', owStart.node_id, homeDoor.node_id);

    workspace.logicNodes.push(homeDoor, subDoor);
    workspace.routeEdges.push(toDoor);

    const fixed = validateWorkspace(workspace).filter((issue) => issue.rowId === moon.moon_id);
    expect(fixed.some((issue) => issue.message.includes('not reachable from the kingdom overworld'))).toBe(false);
  });

  it('forbids excluding a grand (triple) moon', () => {
    const workspace = cloneWorkspace(createDefaultWorkspace());
    const sub = makeRoom(workspace, 'KrExcludeSub', 'subarea', false);
    const moon = makeMoon(workspace, sub, 'KrExcludeSub:grand', '');
    moon.flags = 'ShineFlag_Multi';
    moon.status = 'excluded';

    workspace.rooms.push(sub);
    workspace.moonAccess.push(moon);

    const issues = validateWorkspace(workspace).filter((issue) => issue.rowId === moon.moon_id);
    expect(issues.some((issue) => issue.message.includes('cannot be excluded'))).toBe(true);
  });

  it('rejects a moon connected through a raw stage-data node', () => {
    const workspace = cloneWorkspace(createDefaultWorkspace());
    const overworld = makeRoom(workspace, 'KrRawOverworld', 'overworld', true);
    const owStart = makeNode(workspace, overworld, 'KrRawOverworld::start', 'stage_start', 'OW start');
    const raw = makeNode(workspace, overworld, 'KrRawOverworld::stage_change::pipe::raw', 'stage_change', 'Raw pipe');
    raw.source = 'StageData';
    raw.side = 'raw';
    const toRaw = makeEdge(workspace, overworld, 'kr_edge_to_raw', owStart.node_id, raw.node_id);
    const moon = makeMoon(workspace, overworld, 'KrRawOverworld:moon', raw.node_id);

    workspace.rooms.push(overworld);
    workspace.logicNodes.push(owStart, raw);
    workspace.routeEdges.push(toRaw);
    workspace.moonAccess.push(moon);

    const issues = validateWorkspace(workspace).filter((issue) => issue.rowId === moon.moon_id);
    expect(issues.some((issue) => issue.message.includes('excluded or raw stage-data node'))).toBe(true);
  });
});

function makeRoom(
  workspace: ReturnType<typeof createDefaultWorkspace>,
  stage: string,
  areaScope: string,
  listedInStageWarp: boolean
): Room {
  const room = Object.fromEntries(workspace.csvHeaders.rooms.map((header) => [header, ''])) as Room;
  room.stage = stage;
  room.kingdom = 'KR Test Kingdom';
  room.stage_display_name = stage;
  room.stage_start_node_id = `${stage}::start`;
  room.area_scope = areaScope;
  room.listed_in_stage_warp = listedInStageWarp ? 'true' : 'false';
  return room;
}

function makeNode(
  workspace: ReturnType<typeof createDefaultWorkspace>,
  room: Room,
  nodeId: string,
  nodeType: string,
  displayName: string
): LogicNode {
  const node = Object.fromEntries(workspace.csvHeaders.logicNodes.map((header) => [header, ''])) as LogicNode;
  node.node_id = nodeId;
  node.kingdom = room.kingdom;
  node.stage = room.stage;
  node.stage_display_name = room.stage_display_name;
  node.area_scope = room.area_scope;
  node.node_type = nodeType;
  node.source = 'test';
  node.display_name = displayName;
  return node;
}

function makeEdge(
  workspace: ReturnType<typeof createDefaultWorkspace>,
  room: Room,
  edgeId: string,
  fromNodeId: string,
  toNodeId: string
): RouteEdge {
  const edge = Object.fromEntries(workspace.csvHeaders.routeEdges.map((header) => [header, ''])) as RouteEdge;
  edge.edge_id = edgeId;
  edge.kingdom = room.kingdom;
  edge.area_scope = room.area_scope;
  edge.from_node_id = fromNodeId;
  edge.to_node_id = toNodeId;
  edge.requirement_expr = 'free';
  edge.edge_type = 'human_route';
  edge.difficulty = 'casual';
  edge.source = 'test';
  return edge;
}

function makeMoon(
  workspace: ReturnType<typeof createDefaultWorkspace>,
  room: Room,
  moonId: string,
  fromNodeId: string
): MoonAccess {
  const moon = Object.fromEntries(workspace.csvHeaders.moonAccess.map((header) => [header, ''])) as MoonAccess;
  moon.__editor_id = moonId;
  moon.moon_id = moonId;
  moon.kingdom = room.kingdom;
  moon.stage = room.stage;
  moon.stage_display_name = room.stage_display_name;
  moon.area_scope = room.area_scope;
  moon.moon_name = 'Unit Status Moon';
  moon.runtime_shine_key = moonId;
  moon.from_node_id = fromNodeId;
  moon.requirement_expr = 'free';
  moon.effort_level = 'unrated';
  return moon;
}
