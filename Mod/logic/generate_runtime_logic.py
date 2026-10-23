#!/usr/bin/env python3
"""Generate formatted Switch-runtime logic tables from authored CSVs."""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.format_generated_cpp import format_generated_cpp  # noqa: E402
from randomizer_logic import (
    FINAL_OBJECTIVE_NODE_ID,
    KINGDOM_ORDER,
    MAIN_STAGE_BY_KINGDOM,
    MUSHROOM_REACHED_TOKEN,
    SCENARIO_BLOCKED_BY_KINGDOM,
    SCENARIO_MAX_LOGIC_BY_KINGDOM,
    SCENARIO_MOON_ROCK_BY_KINGDOM,
    SCENARIO_PEACE_BY_KINGDOM,
    SCENARIO_POSTGAME_BY_KINGDOM,
    SCENARIO_START_BY_KINGDOM,
    VANILLA_MOON_REQUIREMENTS,
    WORLD_IDS,
    Requirement,
    combined_option_scenario_mask,
    infer_story_progression,
    is_princess_peach_moon,
    moon_scenario_floor_for_kingdom,
    randomized_subarea_moon_rock_stages,
    node_scenario_floor_for_kingdom,
    node_scenario_indices_to_scenario_no,
    normalize_prerequisite,
    option_scenario_masks,
    parse_bool,
    parse_int,
    parse_requirement,
    requirement_option_count,
    scenario_mask,
)


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def q(value: str) -> str:
    return '"' + (value or "").replace("\\", "\\\\").replace('"', '\\"') + '"'


def develop_name(kingdom: str) -> str:
    if kingdom == "Bowser's Kingdom":
        return "Bowsers"
    if kingdom == "Dark Side":
        return "Dark"
    if kingdom == "Darker Side":
        return "Darker"
    return kingdom.split(" Kingdom")[0].replace("'", "").replace(" ", "")


def node_type(value: str) -> str:
    names = {
        "stage_start": "NodeType_StageStart",
        "home_entrance": "NodeType_HomeEntrance",
        "substage_entrance": "NodeType_SubstageEntrance",
        "checkpoint": "NodeType_Checkpoint",
        "state_event": "NodeType_StateEvent",
        "custom_place": "NodeType_CustomPlace",
        "stage_change": "NodeType_StageChange",
    }
    return names.get(value, "NodeType_Other")


def anchor_kind(value: str) -> str:
    names = {
        "stage_start": "AnchorKind_StageStart",
        "entrance_side": "AnchorKind_EntranceSide",
        "change_stage_id": "AnchorKind_ChangeStageId",
        "checkpoint_obj": "AnchorKind_Checkpoint",
        "state_event": "AnchorKind_StateEvent",
    }
    return names.get(value, "AnchorKind_None")


def anchor_coordinate(value: str) -> str:
    # Emitted as a float literal; the CSV keeps full precision from StageData.
    return f"{float(value):.6f}f"


def node_side(value: str) -> str:
    names = {
        "home_in": "NodeSide_HomeIn",
        "home_out": "NodeSide_HomeOut",
        "sub_near": "NodeSide_SubNear",
        "sub_far": "NodeSide_SubFar",
        "raw": "NodeSide_Raw",
    }
    return names.get(value, "NodeSide_None")


def route_difficulty(value: str) -> str:
    names = {
        "casual": "RouteDifficulty_Casual",
        "advanced": "RouteDifficulty_Advanced",
        "expert": "RouteDifficulty_Expert",
        "glitched": "RouteDifficulty_Glitched",
        "easy": "RouteDifficulty_Casual",
        "low": "RouteDifficulty_Casual",
        "medium": "RouteDifficulty_Advanced",
        "mid": "RouteDifficulty_Advanced",
        "hard": "RouteDifficulty_Expert",
        "glitch": "RouteDifficulty_Glitched",
        "glitches": "RouteDifficulty_Glitched",
    }
    return names.get((value or "").strip().lower(), "RouteDifficulty_Casual")


def route_option_difficulties(value: str, requirement: Requirement) -> list[str]:
    option_count = len(requirement.alternatives) if requirement.mode == "custom" else 1
    option_count = max(1, option_count)
    parts = [part.strip() for part in (value or "").split(";") if part.strip()]
    if not parts:
        return ["RouteDifficulty_Casual"] * option_count
    if len(parts) == 1:
        return [route_difficulty(parts[0])] * option_count
    return [
        route_difficulty(parts[index]) if index < len(parts) else "RouteDifficulty_Casual"
        for index in range(option_count)
    ]


def node_exists_in_logic_scenarios(row: dict[str, str] | None) -> bool:
    if row is None:
        return True
    if row.get("status") == "excluded":
        return False
    scenarios = scenario_mask(row.get("scenario_indices", ""))
    if scenarios == 0:
        return True

    kingdom = row.get("kingdom", "")
    floor = node_scenario_floor_for_kingdom(
        row.get("node_type", ""),
        kingdom,
        row.get("scenario_indices", ""),
    )
    if floor is not None:
        max_scenario = SCENARIO_MAX_LOGIC_BY_KINGDOM.get(kingdom)
        return max_scenario is None or floor <= max_scenario

    max_scenario = SCENARIO_MAX_LOGIC_BY_KINGDOM.get(kingdom)
    scenario_range = range(32) if max_scenario is None else range(max_scenario + 1)
    return any(
        bool(scenarios & (1 << scenario))
        and scenario not in SCENARIO_BLOCKED_BY_KINGDOM.get(kingdom, set())
        for scenario in scenario_range
    )


def token_type(value: str) -> str:
    names = {
        "ability": "TokenType_Ability",
        "capture": "TokenType_Capture",
        "event": "TokenType_Event",
    }
    return names.get(value, "TokenType_Event")


def req_mode(value: str) -> str:
    names = {
        "unset": "RequirementMode_Unset",
        "free": "RequirementMode_Free",
        "impossible": "RequirementMode_Impossible",
        "raw": "RequirementMode_Raw",
        "custom": "RequirementMode_Custom",
    }
    return names[value]


def route_pair_key(from_node: str, to_node: str) -> tuple[str, str]:
    return from_node, to_node


def scenario_move_to(row: dict[str, str]) -> int:
    text = (row.get("scenario_move_to") or "").strip()
    return parse_int(text, -1) if text else -1


def scenario_mask_from_indices(value: str) -> int:
    mask = 0
    for part in (value or "").split(";"):
        scenario = parse_int(part.strip(), -1)
        if 0 <= scenario < 32:
            mask |= 1 << scenario
    return mask


def build_requirement_tables(
    requirements: list[Requirement],
    token_index: dict[str, int],
    node_index: dict[str, int],
):
    unique: dict[tuple, int] = {}
    requirement_rows = []
    alternatives = []
    alternative_tokens = []
    alternative_nodes = []
    requirement_indices = []

    for requirement in requirements:
        normalized_alts = tuple(
            tuple(sorted(token_index[token] for token in alt))
            for alt in requirement.alternatives
        )
        # Index-parallel with normalized_alts: the node-access prerequisites
        # authored as @<node_id> on the same OR option.
        normalized_nodes = tuple(
            tuple(sorted(node_index[node] for node in requirement.node_alternative(index)))
            for index in range(len(normalized_alts))
        )
        key = (requirement.mode, normalized_alts, normalized_nodes)
        existing = unique.get(key)
        if existing is not None:
            requirement_indices.append(existing)
            continue

        alt_offset = len(alternatives)
        for alt, nodes in zip(normalized_alts, normalized_nodes):
            token_offset = len(alternative_tokens)
            alternative_tokens.extend(alt)
            node_offset = len(alternative_nodes)
            alternative_nodes.extend(nodes)
            alternatives.append((token_offset, len(alt), node_offset, len(nodes)))

        req_index = len(requirement_rows)
        requirement_rows.append((requirement.mode, alt_offset, len(normalized_alts)))
        unique[key] = req_index
        requirement_indices.append(req_index)

    return requirement_rows, alternatives, alternative_tokens, alternative_nodes, requirement_indices


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate runtime C++ logic tables.")
    parser.add_argument("--input", type=Path, default=Path("logic/templates"))
    parser.add_argument("--output", type=Path, default=Path("include/Randomizer/LogicData.h"))
    args = parser.parse_args()

    root = args.input
    token_rows = read_csv(root / "logic_tokens.csv")
    room_rows = read_csv(root / "rooms.csv")
    node_rows = read_csv(root / "logic_nodes.csv")
    # StageData-derived node scenario_indices are 0-based scenario-array indices; the
    # runtime compares them against 1-based ScenarioNo (kingdomScenarios). Normalize to
    # ScenarioNo here so both the C++ tables and the Python sim agree (see
    # randomizer_logic.node_scenario_indices_to_scenario_no).
    for _row in node_rows:
        _row["scenario_indices"] = node_scenario_indices_to_scenario_no(_row.get("scenario_indices", ""))
    route_rows = read_csv(root / "route_edges.csv")
    moon_rows = read_csv(root / "moon_access.csv")
    entrance_rows = read_csv(root / "entrance_links.csv")
    scenario_gated_subarea_stages = randomized_subarea_moon_rock_stages(
        moon_rows, entrance_rows
    )
    event_rows = read_csv(root / "progression_events.csv")
    painting_rows = read_csv(root / "painting_links.csv")
    # Physical anchors (change-stage ids and world coordinates) for the nodes that
    # have them. Regenerated alongside the other templates by
    # export_saves_to_templates.make_runtime_anchors; the runtime needs it to place
    # the player at a randomized starting node (Randomizer::StartPositionRando).
    anchor_rows = read_csv(root / "runtime_anchors.csv")

    if not any(row.get("token") == MUSHROOM_REACHED_TOKEN for row in token_rows):
        token_rows.append({
            "token": MUSHROOM_REACHED_TOKEN,
            "token_type": "event",
            "display_name": "Reached Mushroom Kingdom",
            "category": "Progression",
            "prerequisite": "",
            "always_unlocked": "false",
            "requires_any_cap_throw": "false",
            "can_start_with_seed": "false",
        })

    token_index = {row["token"]: index for index, row in enumerate(token_rows)}
    token_set = set(token_index)

    stage_rows = list(room_rows)
    known_stages = {row["stage"] for row in stage_rows}
    for row in node_rows:
        if row.get("stage") and row["stage"] not in known_stages:
            stage_rows.append({
                "stage": row["stage"],
                "kingdom": row.get("kingdom", ""),
                "stage_display_name": row.get("stage_display_name", row["stage"]),
                "area_scope": row.get("area_scope", ""),
            })
            known_stages.add(row["stage"])
    stage_index = {row["stage"]: index for index, row in enumerate(stage_rows)}

    overworld_by_kingdom: dict[str, list[str]] = {}
    for row in stage_rows:
        if row.get("area_scope") == "overworld":
            overworld_by_kingdom.setdefault(row.get("kingdom", ""), []).append(row["stage"])
    multi_overworld = {
        stage
        for stages in overworld_by_kingdom.values()
        if len(stages) > 1
        for stage in stages
    }

    node_index = {row["node_id"]: index for index, row in enumerate(node_rows)}
    event_token_by_node: dict[str, int] = {}
    for row in event_rows:
        token = token_index.get(row.get("event_token", ""), -1)
        if token < 0:
            continue
        for field in ("event_node_id", "event_location_node_id"):
            node_id = row.get(field, "")
            if node_id:
                event_token_by_node[node_id] = token
    for row in node_rows:
        if row.get("node_type") == "state_event" and "::event::" in row.get("node_id", ""):
            token_name = row["node_id"].rsplit("::event::", 1)[-1]
            event_token_by_node.setdefault(row["node_id"], token_index.get(token_name, -1))
    pairs = {route_pair_key(row.get("from_node_id", ""), row.get("to_node_id", "")) for row in route_rows}
    stage_has_routes = {stage: False for stage in stage_index}

    # Moon access requirements are not traversal edges, but capture-gate
    # assignment needs to solve them with the same per-scenario machinery as a
    # route. Emit inert self-edges as internal gate targets: they never expose a
    # new node, while their requirement IDs make both the assignment solver and
    # moon reachability consult the same physical witness group.
    capture_tokens = {
        row["token"] for row in token_rows
        if row.get("token_type") == "capture"
    }

    def has_capture_requirement(expression: str) -> bool:
        requirement = parse_requirement(expression, token_set, node_index)
        return any(
            token in capture_tokens
            for alternative in requirement.alternatives
            for token in alternative
        )

    moon_gate_rows: list[dict[str, str]] = []
    for moon in moon_rows:
        uid = parse_int(moon.get("uid", ""), -1)
        if uid < 0:
            continue
        from_node = moon.get("from_node_id", "") or (
            f"{moon.get('stage', '')}::start"
        )
        for kind, field, option_scenarios, difficulty in (
            ("state", "state_requirement_expr", "", ""),
            (
                "local", "requirement_expr",
                moon.get("option_scenarios", ""),
                moon.get("difficulty", ""),
            ),
        ):
            if not has_capture_requirement(moon.get(field, "")):
                continue
            moon_gate_rows.append({
                "edge_id": f"moon_{kind}_{uid}",
                "kingdom": moon.get("kingdom", ""),
                "from_node_id": from_node,
                "to_node_id": from_node,
                "requirement_expr": moon.get(field, ""),
                "difficulty": difficulty,
                "option_scenarios": option_scenarios,
                "bidirectional": "true",
                "_moon_gate": "true",
            })
    runtime_route_rows = [*route_rows, *moon_gate_rows]

    requirements: list[Requirement] = []
    edge_requirement_index = []
    for row in runtime_route_rows:
        requirements.append(parse_requirement(row.get("requirement_expr", ""), token_set, node_index))
        edge_requirement_index.append(len(requirements) - 1)
        if row.get("_moon_gate"):
            continue
        for field in ("from_node_id", "to_node_id"):
            node = node_rows[node_index[row[field]]] if row.get(field) in node_index else None
            if node:
                stage_has_routes[node["stage"]] = True

    moon_state_requirement_index = []
    moon_local_requirement_index = []
    for row in moon_rows:
        requirements.append(parse_requirement(row.get("state_requirement_expr", ""), token_set, node_index))
        moon_state_requirement_index.append(len(requirements) - 1)
        requirements.append(parse_requirement(row.get("requirement_expr", ""), token_set, node_index))
        moon_local_requirement_index.append(len(requirements) - 1)

    requirement_rows, alternatives, alternative_tokens, alternative_nodes, req_indices = build_requirement_tables(
        requirements, token_index, node_index
    )
    edge_req_indices = [req_indices[index] for index in edge_requirement_index]
    moon_state_req_indices = [req_indices[index] for index in moon_state_requirement_index]
    moon_local_req_indices = [req_indices[index] for index in moon_local_requirement_index]

    edge_difficulty_offsets = []
    edge_difficulty_counts = []
    route_option_difficulty_rows = []
    option_scenario_mask_rows: list[int] = []
    edge_scenario_offsets = []
    edge_scenario_counts = []
    edge_scenario_masks = []
    for row, requirement_index in zip(runtime_route_rows, edge_requirement_index):
        difficulties = route_option_difficulties(row.get("difficulty", ""), requirements[requirement_index])
        edge_difficulty_offsets.append(len(route_option_difficulty_rows))
        edge_difficulty_counts.append(len(difficulties))
        route_option_difficulty_rows.extend(difficulties)
        masks = option_scenario_masks(
            row.get("option_scenarios", ""),
            requirement_option_count(requirements[requirement_index]),
        )
        edge_scenario_offsets.append(len(option_scenario_mask_rows))
        edge_scenario_counts.append(len(masks))
        edge_scenario_masks.append(combined_option_scenario_mask(masks))
        option_scenario_mask_rows.extend(masks)

    moon_scenario_offsets = []
    moon_scenario_counts = []
    moon_difficulty_offsets = []
    moon_difficulty_counts = []
    for row, requirement_index in zip(moon_rows, moon_local_requirement_index):
        # Per-option difficulties for the moon's local requirement, sharing the
        # route option difficulty table. The state requirement is always casual.
        difficulties = route_option_difficulties(row.get("difficulty", ""), requirements[requirement_index])
        moon_difficulty_offsets.append(len(route_option_difficulty_rows))
        moon_difficulty_counts.append(len(difficulties))
        route_option_difficulty_rows.extend(difficulties)
        masks = option_scenario_masks(
            row.get("option_scenarios", ""),
            requirement_option_count(requirements[requirement_index]),
        )
        moon_scenario_offsets.append(len(option_scenario_mask_rows))
        moon_scenario_counts.append(len(masks))
        option_scenario_mask_rows.extend(masks)

    active_entrances = entrance_rows
    node_by_id = {row["node_id"]: row for row in node_rows}

    def entrance_node_exists(node_id: str) -> bool:
        if not node_id:
            return True
        return node_exists_in_logic_scenarios(node_by_id.get(node_id))

    def entrance_is_randomizable(row: dict[str, str]) -> bool:
        if row.get("status") != "active":
            return False

        home_candidates = [row.get("home_in_node_id", "")]
        if row.get("home_entrance_out", "") and row.get("home_entrance_out", "") != row.get("home_entrance_in", ""):
            home_candidates.append(row.get("home_out_node_id", ""))

        sub_candidates = [row.get("sub_near_node_id", "")]
        if row.get("sub_entrance_out", "") and row.get("sub_entrance_out", "") != row.get("sub_entrance_in", ""):
            sub_candidates.append(row.get("sub_far_node_id", ""))

        return any(entrance_node_exists(node_id) for node_id in home_candidates) and any(
            entrance_node_exists(node_id)
            for node_id in sub_candidates
        )

    story_totals = {kingdom: 0 for kingdom in WORLD_IDS}
    for row in moon_rows:
        if "ShineFlag_Story" in row.get("flags", ""):
            story_totals[row.get("kingdom", "")] = story_totals.get(row.get("kingdom", ""), 0) + 1
    story_order, story_move_to = infer_story_progression(moon_rows)

    def stage_idx(stage: str) -> int:
        return stage_index.get(stage, -1)

    def node_idx(node: str) -> int:
        return node_index.get(node, -1)

    def painting_node_idx(row: dict[str, str], node_key: str, stage_key: str) -> int:
        index = node_idx(row.get(node_key, ""))
        if index >= 0:
            return index
        return node_idx(f"{row.get(stage_key, '')}::start")

    world_peace_tokens = []
    moon_rock_tokens = []
    kingdom_access_tokens = []
    for kingdom in WORLD_IDS:
        develop = develop_name(kingdom)
        world_peace_tokens.append(token_index.get(f"WorldPeace_{develop}", -1))
        moon_rock_tokens.append(token_index.get(f"MoonRockActive_{develop}", -1))
        kingdom_access_tokens.append(token_index.get(f"KingdomAccess_{develop}", -1))

    output = []
    output.append("#pragma once\n")
    output.append("#include <basis/seadTypes.h>\n\n")
    output.append("// Generated by logic/generate_runtime_logic.py. Do not edit by hand.\n")
    output.append("namespace Randomizer::LogicData {\n\n")
    output.append("enum TokenType : u8 { TokenType_Ability, TokenType_Capture, TokenType_Event };\n")
    output.append("enum NodeType : u8 { NodeType_Other, NodeType_StageStart, NodeType_HomeEntrance, NodeType_SubstageEntrance, NodeType_Checkpoint, NodeType_StateEvent, NodeType_CustomPlace, NodeType_StageChange };\n")
    output.append("enum NodeSide : u8 { NodeSide_None, NodeSide_HomeIn, NodeSide_HomeOut, NodeSide_SubNear, NodeSide_SubFar, NodeSide_Raw };\n")
    output.append("enum AnchorKind : u8 { AnchorKind_None, AnchorKind_StageStart, AnchorKind_EntranceSide, AnchorKind_ChangeStageId, AnchorKind_Checkpoint, AnchorKind_StateEvent };\n")
    output.append("enum RouteDifficulty : u8 { RouteDifficulty_Casual, RouteDifficulty_Advanced, RouteDifficulty_Expert, RouteDifficulty_Glitched };\n")
    output.append("enum RequirementMode : u8 { RequirementMode_Unset, RequirementMode_Free, RequirementMode_Impossible, RequirementMode_Raw, RequirementMode_Custom };\n\n")
    output.append("struct Token { const char* key; const char* displayName; TokenType type; s16 prerequisite; bool alwaysUnlocked; bool requiresAnyCapThrow; bool canStartWithSeed; };\n")
    output.append("struct Stage { const char* stage; const char* displayName; s16 kingdom; bool hasRoutes; bool multiOverworld; };\n")
    output.append("struct Node { const char* nodeId; s16 stage; NodeType type; NodeSide side; u32 scenarioMask; s16 scenarioFloor; s16 eventToken; bool excluded; };\n")
    # Index-parallel with sNodes: sNodeAnchors[i] describes where sNodes[i] physically
    # sits. changeStageId is the loading-zone id a node belongs to (empty for the
    # kinds that have none); coordinates are only meaningful when hasCoordinates.
    output.append("struct NodeAnchor { AnchorKind kind; const char* changeStageId; bool hasCoordinates; float x; float y; float z; };\n")
    # nodeOffset/nodeCount index sReqNodes: the node-access prerequisites of this
    # OR option, authored as @<node_id>. The option only counts once every one of
    # those nodes is reachable.
    output.append("struct ReqAlternative { u16 tokenOffset; u16 tokenCount; u16 nodeOffset; u16 nodeCount; };\n")
    output.append("struct Requirement { RequirementMode mode; u16 altOffset; u16 altCount; };\n")
    output.append("struct RouteEdge { const char* edgeId; s16 fromNode; s16 toNode; u32 scenarioMask; u16 requirement; u16 difficultyOffset; u16 optionScenarioOffset; u8 difficultyCount; u8 optionScenarioCount; bool bidirectional; };\n")
    output.append("struct MoonAccess { s32 uid; const char* stage; s16 stageIndex; s16 kingdom; s16 fromNode; u16 stateRequirement; u16 localRequirement; u16 localOptionScenarioOffset; u8 localOptionScenarioCount; u16 localDifficultyOffset; u8 localDifficultyCount; u32 scenarioMask; s16 scenarioFloor; s16 scenarioMoveTo; s16 storyOrder; s16 storyMoveTo; bool isPrincessPeach; };\n")
    output.append("struct EntranceAccess { s16 homeInNode; s16 homeOutNode; s16 subNearNode; s16 subFarNode; bool randomizable; };\n\n")
    output.append("struct PaintingLink { s16 sourceNode; s16 destinationNode; bool bidirectional; };\n\n")

    output.append(f"static constexpr s32 sTokenCount = {len(token_rows)};\n")
    output.append("static constexpr Token sTokens[] = {\n")
    for row in token_rows:
        prereq = normalize_prerequisite(row.get("prerequisite", ""))
        output.append(
            f"    {{ {q(row['token'])}, {q(row.get('display_name', row['token']))}, "
            f"{token_type(row.get('token_type', 'event'))}, {token_index.get(prereq, -1)}, "
            f"{str(parse_bool(row.get('always_unlocked', ''))).lower()}, "
            f"{str(parse_bool(row.get('requires_any_cap_throw', ''))).lower()}, "
            f"{str(parse_bool(row.get('can_start_with_seed', ''))).lower()} }},\n"
        )
    output.append("};\n\n")

    output.append(f"static constexpr s32 sStageCount = {len(stage_rows)};\n")
    output.append("static constexpr Stage sStages[] = {\n")
    for row in stage_rows:
        display = row.get("stage_display_name") or row["stage"]
        output.append(
            f"    {{ {q(row['stage'])}, {q(display)}, {WORLD_IDS.get(row.get('kingdom', ''), -1)}, "
            f"{str(stage_has_routes.get(row['stage'], False)).lower()}, "
            f"{str(row['stage'] in multi_overworld).lower()} }},\n"
        )
    output.append("};\n\n")

    output.append(f"static constexpr s32 sNodeCount = {len(node_rows)};\n")
    output.append(
        f"static constexpr s16 sFinalObjectiveNode = "
        f"{node_index.get(FINAL_OBJECTIVE_NODE_ID, -1)};\n"
    )
    output.append("static constexpr Node sNodes[] = {\n")
    for row in node_rows:
        node_scenario_floor = node_scenario_floor_for_kingdom(
            row.get("node_type", ""),
            row.get("kingdom", ""),
            row.get("scenario_indices", ""),
        )
        output.append(
            f"    {{ {q(row['node_id'])}, {stage_idx(row.get('stage', ''))}, "
            f"{node_type(row.get('node_type', ''))}, {node_side(row.get('side', ''))}, "
            f"0x{scenario_mask_from_indices(row.get('scenario_indices', '')):08X}u, "
            f"{node_scenario_floor if node_scenario_floor is not None else -1}, "
            f"{event_token_by_node.get(row['node_id'], -1)}, "
            # Node logic-exclusion matches the Python mirror (randomizer_logic.py reads the
            # `status` column). The `source` column's "EntranceDB:excluded" only means the
            # endpoint is not in the EntranceDB pipe table; it is still a valid, reachable
            # logic node, so it must NOT be excluded from reachability.
            f"{str(row.get('status') == 'excluded').lower()} }},\n"
        )
    output.append("};\n\n")

    anchor_by_node = {row.get("node_id", ""): row for row in anchor_rows}
    output.append("static constexpr NodeAnchor sNodeAnchors[] = {\n")
    for row in node_rows:
        anchor = anchor_by_node.get(row["node_id"])
        x = (anchor or {}).get("x", "").strip()
        y = (anchor or {}).get("y", "").strip()
        z = (anchor or {}).get("z", "").strip()
        has_coordinates = bool(anchor) and bool(x) and bool(y) and bool(z)
        output.append(
            f"    {{ {anchor_kind((anchor or {}).get('anchor_kind', ''))}, "
            f"{q((anchor or {}).get('change_stage_id', ''))}, "
            f"{str(has_coordinates).lower()}, "
            f"{anchor_coordinate(x) if has_coordinates else '0.0f'}, "
            f"{anchor_coordinate(y) if has_coordinates else '0.0f'}, "
            f"{anchor_coordinate(z) if has_coordinates else '0.0f'} }},\n"
        )
    output.append("};\n")
    # sNodeAnchors[i] describes sNodes[i]; a length mismatch would silently
    # mis-attribute every coordinate past the first gap.
    output.append(
        "static_assert(sizeof(sNodeAnchors) / sizeof(sNodeAnchors[0]) == sNodeCount,\n"
        '    "sNodeAnchors must stay index-parallel with sNodes");\n\n'
    )

    output.append(f"static constexpr s32 sRequirementCount = {len(requirement_rows)};\n")
    output.append("static constexpr Requirement sRequirements[] = {\n")
    for mode, alt_offset, alt_count in requirement_rows:
        output.append(f"    {{ {req_mode(mode)}, {alt_offset}, {alt_count} }},\n")
    output.append("};\n\n")

    output.append(f"static constexpr s32 sReqAlternativeCount = {len(alternatives)};\n")
    output.append("static constexpr ReqAlternative sReqAlternatives[] = {\n")
    for token_offset, token_count, node_offset, node_count in alternatives:
        output.append(f"    {{ {token_offset}, {token_count}, {node_offset}, {node_count} }},\n")
    output.append("};\n\n")

    output.append(f"static constexpr s32 sReqTokenCount = {len(alternative_tokens)};\n")
    output.append("static constexpr u16 sReqTokens[] = {\n")
    for index in range(0, len(alternative_tokens), 16):
        output.append("    " + ", ".join(str(value) for value in alternative_tokens[index:index + 16]) + ",\n")
    output.append("};\n\n")

    # Node indexes referenced by ReqAlternative.nodeOffset/nodeCount. Kept a real
    # array even when empty so the runtime can index it unconditionally.
    output.append(f"static constexpr s32 sReqNodeCount = {len(alternative_nodes)};\n")
    output.append("static constexpr s16 sReqNodes[] = {\n")
    if alternative_nodes:
        for index in range(0, len(alternative_nodes), 16):
            output.append("    " + ", ".join(str(value) for value in alternative_nodes[index:index + 16]) + ",\n")
    else:
        output.append("    -1,\n")
    output.append("};\n\n")

    output.append(f"static constexpr s32 sRouteOptionDifficultyCount = {len(route_option_difficulty_rows)};\n")
    output.append("static constexpr RouteDifficulty sRouteOptionDifficulties[] = {\n")
    for index in range(0, len(route_option_difficulty_rows), 12):
        output.append("    " + ", ".join(route_option_difficulty_rows[index:index + 12]) + ",\n")
    output.append("};\n\n")

    output.append(f"static constexpr s32 sOptionScenarioMaskCount = {len(option_scenario_mask_rows)};\n")
    output.append("static constexpr u32 sOptionScenarioMasks[] = {\n")
    for index in range(0, len(option_scenario_mask_rows), 8):
        output.append(
            "    "
            + ", ".join(f"0x{mask:08X}u" for mask in option_scenario_mask_rows[index:index + 8])
            + ",\n"
        )
    output.append("};\n\n")

    output.append(f"static constexpr s32 sRouteEdgeCount = {len(runtime_route_rows)};\n")
    output.append("static constexpr RouteEdge sRouteEdges[] = {\n")
    for row, req_index, difficulty_offset, difficulty_count, scenario_offset, scenario_count, edge_scenario_mask in zip(
        runtime_route_rows,
        edge_req_indices,
        edge_difficulty_offsets,
        edge_difficulty_counts,
        edge_scenario_offsets,
        edge_scenario_counts,
        edge_scenario_masks,
    ):
        from_node = row.get("from_node_id", "")
        to_node = row.get("to_node_id", "")
        has_reverse = route_pair_key(to_node, from_node) in pairs
        bidirectional = parse_bool(row.get("bidirectional", "")) or not has_reverse
        output.append(
            f"    {{ {q(row.get('edge_id', ''))}, {node_idx(from_node)}, {node_idx(to_node)}, "
            f"0x{edge_scenario_mask:08X}u, {req_index}, "
            f"{difficulty_offset}, {scenario_offset}, {difficulty_count}, {scenario_count}, "
            f"{str(bidirectional).lower()} }},\n"
        )
    output.append("};\n\n")

    output.append(f"static constexpr s32 sMoonCount = {len(moon_rows)};\n")
    output.append("static constexpr MoonAccess sMoons[] = {\n")
    for row, state_req, local_req, local_scenario_offset, local_scenario_count, local_difficulty_offset, local_difficulty_count in zip(
        moon_rows,
        moon_state_req_indices,
        moon_local_req_indices,
        moon_scenario_offsets,
        moon_scenario_counts,
        moon_difficulty_offsets,
        moon_difficulty_counts,
    ):
        advances_progression = row.get("moon_id", "") in story_order or scenario_move_to(row) >= 0
        scenario_floor = moon_scenario_floor_for_kingdom(
            row.get("kingdom", ""),
            row.get("shine_info_available_scenarios", ""),
            advances_progression,
            row.get("stage", "") in scenario_gated_subarea_stages,
        )
        fake_moon = type("MoonLike", (), {"moon_name": row.get("moon_name", "")})()
        runtime_stage = (
            row.get("runtime_shine_key", "").split(":", 1)[0]
            or row.get("stage", "")
        )
        output.append(
            f"    {{ {parse_int(row.get('uid', ''), -1)}, {q(runtime_stage)}, "
            f"{stage_idx(row.get('stage', ''))}, {WORLD_IDS.get(row.get('kingdom', ''), -1)}, "
            f"{node_idx(row.get('from_node_id', ''))}, {state_req}, {local_req}, "
            f"{local_scenario_offset}, {local_scenario_count}, "
            f"{local_difficulty_offset}, {local_difficulty_count}, "
            f"0x{scenario_mask(row.get('shine_info_available_scenarios', '')):08X}u, "
            f"{scenario_floor if scenario_floor is not None else -1}, {scenario_move_to(row)}, "
            f"{story_order.get(row.get('moon_id', ''), -1)}, {story_move_to.get(row.get('moon_id', ''), -1)}, "
            f"{str(is_princess_peach_moon(fake_moon)).lower()} }},\n"
        )
    output.append("};\n\n")

    output.append(f"static constexpr s32 sEntranceCount = {len(active_entrances)};\n")
    # Legacy alias kept for callers that only need the total EntranceDB-aligned row
    # count. Randomizability is now per-entry via sEntrances[i].randomizable.
    output.append(f"static constexpr s32 sEntranceDbLinkCount = {len(active_entrances)};\n")
    output.append("static constexpr EntranceAccess sEntrances[] = {\n")
    for row in active_entrances:
        output.append(
            f"    {{ {node_idx(row.get('home_in_node_id', ''))}, {node_idx(row.get('home_out_node_id', ''))}, "
            f"{node_idx(row.get('sub_near_node_id', ''))}, {node_idx(row.get('sub_far_node_id', ''))}, "
            f"{str(entrance_is_randomizable(row)).lower()} }},\n"
        )
    output.append("};\n\n")

    output.append("static constexpr s16 sMainStageByKingdom[] = {\n")
    for world_id in range(len(WORLD_IDS)):
        kingdom = next(name for name, kid in WORLD_IDS.items() if kid == world_id)
        output.append(f"    {stage_idx(MAIN_STAGE_BY_KINGDOM.get(kingdom, ''))},\n")
    output.append("};\n\n")

    output.append("static constexpr s16 sKingdomOrder[] = {\n")
    for kingdom in KINGDOM_ORDER:
        output.append(f"    {WORLD_IDS[kingdom]},\n")
    output.append("};\n")
    output.append(f"static constexpr s32 sKingdomOrderCount = {len(KINGDOM_ORDER)};\n\n")

    output.append("static constexpr s16 sMoonRequirementsByKingdom[] = {\n")
    for world_id in range(len(WORLD_IDS)):
        kingdom = next(name for name, kid in WORLD_IDS.items() if kid == world_id)
        output.append(f"    {VANILLA_MOON_REQUIREMENTS.get(kingdom, 0)},\n")
    output.append("};\n\n")

    output.append("static constexpr s16 sStoryTotalsByKingdom[] = {\n")
    for world_id in range(len(WORLD_IDS)):
        kingdom = next(name for name, kid in WORLD_IDS.items() if kid == world_id)
        output.append(f"    {story_totals.get(kingdom, 0)},\n")
    output.append("};\n\n")

    def emit_scenario_table(name: str, values: dict[str, int]) -> None:
        output.append(f"static constexpr s16 {name}[] = {{\n")
        for world_id in range(len(WORLD_IDS)):
            kingdom = next(name for name, kid in WORLD_IDS.items() if kid == world_id)
            output.append(f"    {values.get(kingdom, -1)},\n")
        output.append("};\n\n")

    emit_scenario_table("sStartScenarioByKingdom", SCENARIO_START_BY_KINGDOM)
    emit_scenario_table("sPeaceScenarioByKingdom", SCENARIO_PEACE_BY_KINGDOM)
    emit_scenario_table("sPostGameScenarioByKingdom", SCENARIO_POSTGAME_BY_KINGDOM)
    emit_scenario_table("sMoonRockScenarioByKingdom", SCENARIO_MOON_ROCK_BY_KINGDOM)
    emit_scenario_table("sMaxLogicScenarioByKingdom", SCENARIO_MAX_LOGIC_BY_KINGDOM)

    output.append("static constexpr u32 sBlockedScenarioMaskByKingdom[] = {\n")
    for world_id in range(len(WORLD_IDS)):
        kingdom = next(name for name, kid in WORLD_IDS.items() if kid == world_id)
        mask = 0
        for scenario in SCENARIO_BLOCKED_BY_KINGDOM.get(kingdom, set()):
            if 0 <= scenario < 32:
                mask |= 1 << scenario
        output.append(f"    0x{mask:08X}u,\n")
    output.append("};\n\n")

    output.append("static constexpr s16 sWorldPeaceTokenByKingdom[] = {\n")
    for token in world_peace_tokens:
        output.append(f"    {token},\n")
    output.append("};\n\n")

    output.append("static constexpr s16 sMoonRockTokenByKingdom[] = {\n")
    for token in moon_rock_tokens:
        output.append(f"    {token},\n")
    output.append("};\n\n")

    output.append("static constexpr s16 sKingdomAccessTokenByKingdom[] = {\n")
    for token in kingdom_access_tokens:
        output.append(f"    {token},\n")
    output.append("};\n\n")

    active_paintings = [row for row in painting_rows if row.get("status") != "excluded"]
    output.append(f"static constexpr s32 sPaintingLinkCount = {len(active_paintings)};\n")
    output.append("static constexpr PaintingLink sPaintingLinks[] = {\n")
    for row in active_paintings:
        output.append(
            f"    {{ {painting_node_idx(row, 'source_node_id', 'source_stage')}, "
            f"{painting_node_idx(row, 'destination_node_id', 'destination_stage')}, "
            f"{str(parse_bool(row.get('bidirectional', 'true'))).lower()} }},\n"
        )
    output.append("};\n\n")

    output.append(f"static constexpr s16 sMushroomReachedToken = {token_index[MUSHROOM_REACHED_TOKEN]};\n")
    output.append("static constexpr s32 sKingdomCount = 17;\n")
    output.append("\n} // namespace Randomizer::LogicData\n")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    formatted = format_generated_cpp("".join(output), args.output, ROOT)
    args.output.write_text(formatted, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
