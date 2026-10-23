export type CsvRow = Record<string, string>;

export type LogicTable =
  | 'logicNodes'
  | 'routeEdges'
  | 'moonAccess'
  | 'logicTokens'
  | 'rooms'
  | 'progressionEvents'
  | 'scenarioMetadata'
  | 'captureGates'
  | 'captureGateObjects';

export interface CsvDocument<T extends CsvRow = CsvRow> {
  headers: string[];
  rows: T[];
}

export interface LogicNode extends CsvRow {
  node_id: string;
  kingdom: string;
  stage: string;
  stage_display_name: string;
  area_scope: string;
  node_type: string;
  source: string;
  entrance_id: string;
  side: string;
  obj_id: string;
  display_name: string;
  x: string;
  y: string;
  z: string;
  scenario_indices: string;
  notes: string;
  status: string;
}

export interface RouteEdge extends CsvRow {
  edge_id: string;
  kingdom: string;
  area_scope: string;
  from_node_id: string;
  to_node_id: string;
  requirement_expr: string;
  requirement_notes: string;
  edge_type: string;
  difficulty: string;
  option_scenarios: string;
  bidirectional: string;
  source: string;
  notes: string;
}

export interface MoonAccess extends CsvRow {
  __editor_id: string;
  moon_id: string;
  kingdom: string;
  stage: string;
  stage_display_name: string;
  area_scope: string;
  moon_name: string;
  runtime_shine_key: string;
  obj_id: string;
  uid: string;
  flags: string;
  from_node_id: string;
  state_requirement_expr: string;
  requirement_expr: string;
  difficulty: string;
  option_scenarios: string;
  world_peace_token: string;
  moon_rock_token: string;
  access_type: string;
  effort_level: string;
  effort_notes: string;
  legacy_requirement_notes: string;
  placement_found: string;
  placement_unit_config_names: string;
  placement_layer_config_names: string;
  placement_scenario_no_values: string;
  scenario_move_to: string;
  placement_stage_map_indices: string;
  placement_quest_no_values: string;
  placement_shine_optional_ids: string;
  placement_change_stage_ids: string;
  placement_x: string;
  placement_y: string;
  placement_z: string;
  placement_source_archives: string;
  placement_source_files: string;
  placement_count: string;
  shine_info_hint_idx: string;
  shine_info_main_scenario_no: string;
  shine_info_progress_bit_flag: string;
  shine_info_is_grand: string;
  shine_info_is_moon_rock: string;
  shine_info_available_scenarios: string;
  shine_info_object_name: string;
  shine_info_scenario_name: string;
  notes: string;
}

export interface LogicToken extends CsvRow {
  token: string;
  token_type: string;
  display_name: string;
  category: string;
  prerequisite: string;
  always_unlocked: string;
  requires_any_cap_throw: string;
  can_start_with_seed: string;
  aliases: string;
  notes: string;
}

export interface Room extends CsvRow {
  stage: string;
  kingdom: string;
  stage_display_name: string;
  stage_start_node_id: string;
  area_scope: string;
  listed_in_stage_warp: string;
  has_shine_db_moons: string;
  active_entrance_link_count: string;
  excluded_entrance_link_count: string;
  has_raw_stage_data_points: string;
  notes: string;
}

export interface ProgressionEvent extends CsvRow {
  event_token: string;
  event_type: string;
  kingdom: string;
  world_id: string;
  develop_name: string;
  main_stage: string;
  event_node_id: string;
  event_location_node_id: string;
  requirement_expr: string;
  notes: string;
}

export interface ScenarioMetadata extends CsvRow {
  stage: string;
  kingdom: string;
  stage_display_name: string;
  area_scope: string;
  scenario_no: string;
  active_layers: string;
  stage_map_indices: string;
  source_archive: string;
  source_file: string;
  notes: string;
}

export type CaptureGateMatchMode = 'all' | 'any' | 'at_least' | 'at_most';

export interface CaptureGate extends CsvRow {
  gate_id: string;
  kingdom: string;
  logic_stage: string;
  edge_id: string;
  source_capture_token: string;
  replacement_capture_token: string;
  difficulty: string;
  match_mode: CaptureGateMatchMode;
  minimum_count: string;
  notes: string;
}

export interface CaptureGateObject extends CsvRow {
  gate_id: string;
  placement_stage: string;
  object_id: string;
}

export interface EnemySwapEntity extends CsvRow {
  unit_config_name: string;
  friendly_name: string;
  class_name: string;
  category: string;
  capture_token: string;
  swap_out: string;
  any_pool: string;
  notes: string;
}

export interface EnemySwapPlacement extends CsvRow {
  stage: string;
  object_id: string;
  unit_config_name: string;
  list_name: string;
  layer: string;
  scenarios: string;
  x: string;
  y: string;
  z: string;
  logic_stage: string;
  event_name: string;
}

export interface EnemySwapRule extends CsvRow {
  stage: string;
  object_id: string;
  unit_config_name: string;
  policy: string;
  pool: string;
  logic_stage: string;
  notes: string;
}

export interface Position {
  x: number;
  y: number;
}

export interface LogicWorkspace {
  schemaVersion: 2;
  loadedAt: string;
  csvHeaders: Record<LogicTable, string[]>;
  logicNodes: LogicNode[];
  routeEdges: RouteEdge[];
  moonAccess: MoonAccess[];
  logicTokens: LogicToken[];
  rooms: Room[];
  progressionEvents: ProgressionEvent[];
  scenarioMetadata: ScenarioMetadata[];
  captureGates: CaptureGate[];
  captureGateObjects: CaptureGateObject[];
  nodePositions: Record<string, Position>;
}

export interface EditorProject {
  app: 'smo-randomizer-logic-editor';
  schemaVersion: 2;
  kingdom?: string;
  savedAt: string;
  workspace: LogicWorkspace;
}

export interface RequirementModel {
  mode: 'unset' | 'free' | 'impossible' | 'custom' | 'raw';
  alternatives: string[][];
  // Index-parallel with alternatives: node ids this OR option additionally needs
  // the player to be able to reach, written `@<node_id>` in the expression.
  nodeAlternatives: string[][];
  raw: string;
  errors: string[];
}

export interface GraphFilters {
  showExcluded: boolean;
  showMoons: boolean;
  showEvents: boolean;
  showRawAnchors: boolean;
  search: string;
}

export interface GraphNode {
  id: string;
  kind: 'logic' | 'moon';
  label: string;
  subtitle: string;
  type: string;
  stage: string;
  kingdom: string;
  areaScope: string;
  x: number;
  y: number;
  sourceRow: LogicNode | MoonAccess;
}

export interface GraphEdge {
  id: string;
  kind: 'route' | 'moon_access';
  from: string;
  to: string;
  label: string;
  bidirectional: boolean;
  sourceRow: RouteEdge | MoonAccess;
  reverseRow?: RouteEdge;
}

export interface RouteConnection {
  id: string;
  from: string;
  to: string;
  forward: RouteEdge;
  reverse?: RouteEdge;
  routeEdges: RouteEdge[];
  duplicateCount: number;
}

export type Selection =
  | { kind: 'none' }
  | { kind: 'node'; id: string }
  | { kind: 'edge'; id: string }
  | { kind: 'moon'; id: string };

export interface ValidationIssue {
  severity: 'error' | 'warning' | 'info';
  table: LogicTable | 'graph';
  rowId: string;
  field: string;
  message: string;
}

export type IssueCountSummary = Record<ValidationIssue['severity'], number>;
