import logicNodesCsv from '../../../templates/logic_nodes.csv?raw';
import routeEdgesCsv from '../../../templates/route_edges.csv?raw';
import moonAccessCsv from '../../../templates/moon_access.csv?raw';
import logicTokensCsv from '../../../templates/logic_tokens.csv?raw';
import roomsCsv from '../../../templates/rooms.csv?raw';
import progressionEventsCsv from '../../../templates/progression_events.csv?raw';
import scenarioMetadataCsv from '../../../templates/scenario_metadata.csv?raw';
import captureGatesCsv from '../../../templates/capture_gates.csv?raw';
import captureGateObjectsCsv from '../../../templates/capture_gate_objects.csv?raw';
import { parseCsvDocument } from '../lib/csv';
import type {
  CaptureGate,
  CaptureGateObject,
  LogicNode,
  LogicToken,
  MoonAccess,
  ProgressionEvent,
  Room,
  RouteEdge,
  ScenarioMetadata
} from '../lib/types';
import { buildWorkspace } from '../lib/workspace';

export function createDefaultWorkspace() {
  return buildWorkspace({
    logicNodes: parseCsvDocument<LogicNode>(logicNodesCsv),
    routeEdges: parseCsvDocument<RouteEdge>(routeEdgesCsv),
    moonAccess: parseCsvDocument<MoonAccess>(moonAccessCsv),
    logicTokens: parseCsvDocument<LogicToken>(logicTokensCsv),
    rooms: parseCsvDocument<Room>(roomsCsv),
    progressionEvents: parseCsvDocument<ProgressionEvent>(progressionEventsCsv),
    scenarioMetadata: parseCsvDocument<ScenarioMetadata>(scenarioMetadataCsv),
    captureGates: parseCsvDocument<CaptureGate>(captureGatesCsv),
    captureGateObjects: parseCsvDocument<CaptureGateObject>(captureGateObjectsCsv)
  });
}
