#!/usr/bin/env python3
"""Graph-aware SMO randomizer planning.

This module is intentionally host-side. The logic graph is large, editable, and
better suited to pre-generation than to Switch runtime hooks.
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter
from itertools import combinations
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Iterator

import subarea_keys
from entrance_fallbacks import arrival_places_player_at_entrance
from start_position import (
    NODELESS_START_POSITION_MODES,
    START_POSITION_MODES,
    collect_candidates as collect_start_position_candidates,
    load_node_anchors,
    normalize_start_position_mode,
    shuffled_candidates as shuffled_start_position_candidates,
)

try:
    from .moon_allocation import (
        MoonAllocationError,
        allocate_moons,
        compute_reward_multi_values,
        difficulty_name,
        coin_policy_name,
    )
except ImportError:
    from moon_allocation import (
        MoonAllocationError,
        allocate_moons,
        compute_reward_multi_values,
        difficulty_name,
        coin_policy_name,
    )


WORLD_IDS = {
    "Cap Kingdom": 0,
    "Cascade Kingdom": 1,
    "Sand Kingdom": 2,
    "Wooded Kingdom": 3,
    "Lake Kingdom": 4,
    "Cloud Kingdom": 5,
    "Lost Kingdom": 6,
    "Metro Kingdom": 7,
    "Seaside Kingdom": 8,
    "Snow Kingdom": 9,
    "Luncheon Kingdom": 10,
    "Ruined Kingdom": 11,
    "Bowser's Kingdom": 12,
    "Moon Kingdom": 13,
    "Mushroom Kingdom": 14,
    "Dark Side": 15,
    "Darker Side": 16,
}

KINGDOM_BY_WORLD_ID = {world_id: kingdom for kingdom, world_id in WORLD_IDS.items()}

MUSHROOM_KINGDOM = "Mushroom Kingdom"
MUSHROOM_REACHED_TOKEN = "ReachedMushroomKingdom"
FINAL_OBJECTIVE_NODE_ID = "MoonWorldHomeStage::checkpoint::obj1348"

ROUTE_DIFFICULTY_CASUAL = "casual"
ROUTE_DIFFICULTY_ADVANCED = "advanced"
ROUTE_DIFFICULTY_EXPERT = "expert"
ROUTE_DIFFICULTY_GLITCHED = "glitched"
ROUTE_DIFFICULTY_LEVELS = (
    ROUTE_DIFFICULTY_CASUAL,
    ROUTE_DIFFICULTY_ADVANCED,
    ROUTE_DIFFICULTY_EXPERT,
    ROUTE_DIFFICULTY_GLITCHED,
)
# Strictly cumulative: every level allows every option at or below its rank, so
# Glitched is Expert plus the glitch-only options.
ROUTE_DIFFICULTY_RANKS = {
    ROUTE_DIFFICULTY_CASUAL: 0,
    ROUTE_DIFFICULTY_ADVANCED: 1,
    ROUTE_DIFFICULTY_EXPERT: 2,
    ROUTE_DIFFICULTY_GLITCHED: 3,
}

SEED_STYLES = (
    "standard",
    "ability-rush",
    "focused-route",
    "explorer",
    "mystery",
)
UNLOCK_TIMINGS = ("early", "standard", "late")
ROUTE_BREADTHS = ("focused", "standard", "expansive")
PROGRESSION_DEPTHS = ("shallow", "standard", "deep")


def mix_seed_style_axis(value: int) -> int:
    value = (value + 0x9E3779B9) & 0xFFFFFFFF
    value = ((value ^ (value >> 16)) * 0x85EBCA6B) & 0xFFFFFFFF
    value = ((value ^ (value >> 13)) * 0xC2B2AE35) & 0xFFFFFFFF
    return (value ^ (value >> 16)) & 0xFFFFFFFF

MAIN_STAGE_BY_KINGDOM = {
    "Cap Kingdom": "CapWorldHomeStage",
    "Cascade Kingdom": "WaterfallWorldHomeStage",
    "Sand Kingdom": "SandWorldHomeStage",
    "Wooded Kingdom": "ForestWorldHomeStage",
    "Lake Kingdom": "LakeWorldHomeStage",
    "Cloud Kingdom": "CloudWorldHomeStage",
    "Lost Kingdom": "ClashWorldHomeStage",
    "Metro Kingdom": "CityWorldHomeStage",
    "Seaside Kingdom": "SeaWorldHomeStage",
    "Snow Kingdom": "SnowWorldHomeStage",
    "Luncheon Kingdom": "LavaWorldHomeStage",
    "Ruined Kingdom": "BossRaidWorldHomeStage",
    "Bowser's Kingdom": "SkyWorldHomeStage",
    "Moon Kingdom": "MoonWorldHomeStage",
    MUSHROOM_KINGDOM: "PeachWorldHomeStage",
    "Dark Side": "Special1WorldHomeStage",
    "Darker Side": "Special2WorldHomeStage",
}

KINGDOM_ORDER = [
    "Cap Kingdom",
    "Cascade Kingdom",
    "Sand Kingdom",
    "Lake Kingdom",
    "Wooded Kingdom",
    "Cloud Kingdom",
    "Lost Kingdom",
    "Metro Kingdom",
    "Snow Kingdom",
    "Seaside Kingdom",
    "Luncheon Kingdom",
    "Ruined Kingdom",
    "Bowser's Kingdom",
    "Moon Kingdom",
]

KINGDOM_TIERS = [
    ("Cap Kingdom",),
    ("Cascade Kingdom",),
    ("Sand Kingdom",),
    ("Lake Kingdom", "Wooded Kingdom"),
    ("Cloud Kingdom",),
    ("Lost Kingdom",),
    ("Metro Kingdom",),
    ("Snow Kingdom", "Seaside Kingdom"),
    ("Luncheon Kingdom",),
    ("Ruined Kingdom",),
    ("Bowser's Kingdom",),
    ("Moon Kingdom",),
]

VANILLA_MOON_REQUIREMENTS = {
    "Cap Kingdom": 0,
    "Cascade Kingdom": 5,
    "Sand Kingdom": 16,
    "Wooded Kingdom": 16,
    "Lake Kingdom": 8,
    "Cloud Kingdom": 0,
    "Lost Kingdom": 10,
    "Metro Kingdom": 20,
    "Seaside Kingdom": 10,
    "Snow Kingdom": 10,
    "Luncheon Kingdom": 18,
    "Ruined Kingdom": 3,
    "Bowser's Kingdom": 8,
    "Moon Kingdom": 0,
}

# Scenario masks use bit N for scenario N (1-based ScenarioNo), so bits 0..15
# cover every scenario a kingdom can reach (the widest is 8). extend_top_scenario
# fills up to this limit when it makes a mask's top open-ended.
#
# This replaces MAX_EDITOR_SCENARIO. The editor used to expose only toggles 1-5,
# so both runtimes ignored an edge's option_scenarios entirely above scenario 5
# on the grounds that the author "could not have excluded it". The editor now
# offers every scenario a kingdom can actually be in, so the mask is honoured at
# every scenario and open-endedness is expressed in the mask itself.
SCENARIO_BIT_LIMIT = 16

# Runtime ScenarioPatcher loads these randomized multi-prize rooms in the
# scenario that contains every prize and promotes their Moon Rock event-flow
# check as well. Their physical locations therefore must not retain the source
# kingdom's scenario gate in host-side seed generation.
PROMOTED_MULTI_PRIZE_SUBAREA_SCENARIOS = {
    "IceWalkerExStage": 4,       # Koopa Walking in Snow
    "FukuwaraiKuriboStage": 4,  # Picture Match: Goomba
    "FukuwaraiMarioStage": 3,   # Picture Match: Mario
    "CityPeopleRoadStage": 10,  # Crowded Alleyway
}

SCENARIO_START_BY_KINGDOM = {
    kingdom: 1 for kingdom in WORLD_IDS
}
SCENARIO_START_BY_KINGDOM.update({
    "Cap Kingdom": 3,
    "Cascade Kingdom": 1,
    "Lost Kingdom": 3,
    "Moon Kingdom": 2,  # Moon Kingdom starts in scenario 2.
    MUSHROOM_KINGDOM: 2,
    "Dark Side": 2,  # Dark Side starts in scenario 2.
})

# LOGIC-ONLY start-scenario overrides. These adjust how the placement logic
# (this runner + the C++ MoonRando) models a kingdom's starting scenario WITHOUT
# touching the generated sStartScenarioByKingdom table that the in-game
# ScenarioPatcher reads. Cloud Kingdom is only ever visited in scenario 3
# (post-Bowser) + 4 (Moon Rock), but its in-game patcher must stay as-is.
LOGIC_SCENARIO_START_OVERRIDE = {
    "Cloud Kingdom": 3,
}

SCENARIO_PEACE_BY_KINGDOM = {
    "Cap Kingdom": 2,
    "Cascade Kingdom": 2,
    "Sand Kingdom": 3,
    "Wooded Kingdom": 3,
    "Lake Kingdom": 2,
    "Cloud Kingdom": 2,
    "Lost Kingdom": 2,
    "Metro Kingdom": 4,
    "Seaside Kingdom": 2,
    "Snow Kingdom": 2,
    "Luncheon Kingdom": 7,
    "Ruined Kingdom": 2,
    "Bowser's Kingdom": 2,
    "Moon Kingdom": 2,
    MUSHROOM_KINGDOM: 3,
    "Dark Side": 2,
    "Darker Side": 2,
}

SCENARIO_POSTGAME_BY_KINGDOM = {
    "Cap Kingdom": 3,
    "Cascade Kingdom": 3,
    "Sand Kingdom": 4,
    "Wooded Kingdom": 4,
    "Lake Kingdom": 3,
    "Cloud Kingdom": 3,
    "Lost Kingdom": 3,
    "Metro Kingdom": 5,
    "Seaside Kingdom": 3,
    "Snow Kingdom": 3,
    "Luncheon Kingdom": 7,
    "Ruined Kingdom": 3,
    "Bowser's Kingdom": 3,
    "Moon Kingdom": 2,
    MUSHROOM_KINGDOM: 3,
    "Dark Side": 2,
    "Darker Side": 2,
}

SCENARIO_MOON_ROCK_BY_KINGDOM = {
    "Cap Kingdom": 4,
    "Cascade Kingdom": 4,
    "Sand Kingdom": 5,
    "Wooded Kingdom": 5,
    "Lake Kingdom": 4,
    "Cloud Kingdom": 4,
    "Lost Kingdom": 4,
    "Metro Kingdom": 8,
    "Seaside Kingdom": 4,
    "Snow Kingdom": 4,
    "Luncheon Kingdom": 8,
    "Ruined Kingdom": 4,
    "Bowser's Kingdom": 4,
    "Moon Kingdom": 3,
}

# Scripted scenario exceptions: a single (non-triple) story beat that advances
# ScenarioNo in-game without a grand/multi moon. Metro's festival is scripted to
# begin when the player collects the Power Plant beat ("Powering Up the Station")
# and talks to Pauline, putting the kingdom into scenario 3. Keyed by kingdom ->
# the set of story_move_to targets a single story moon is allowed to trigger.
SCRIPTED_SCENARIO_ADVANCE = {
    "Metro Kingdom": {3},
}

SCENARIO_MAX_LOGIC_BY_KINGDOM = {
    kingdom: max(
        SCENARIO_PEACE_BY_KINGDOM.get(kingdom, 1),
        SCENARIO_POSTGAME_BY_KINGDOM.get(kingdom, 1),
        SCENARIO_MOON_ROCK_BY_KINGDOM.get(kingdom, 1),
    )
    for kingdom in WORLD_IDS
}

SCENARIO_BLOCKED_BY_KINGDOM = {
    "Cap Kingdom": {5, 6, 7},
    "Cascade Kingdom": {5, 6, 7, 8},
    "Sand Kingdom": {6, 7, 8},
    "Wooded Kingdom": {6, 7},
    "Lake Kingdom": {5, 6},
    "Lost Kingdom": {5, 6},
    "Metro Kingdom": {6, 9, 10, 11, 12, 13, 14, 15},
    "Seaside Kingdom": {5, 6},
    "Snow Kingdom": {5, 6},
    "Luncheon Kingdom": {4, 5, 6, 9, 10},
    "Bowser's Kingdom": {5, 6},
    "Moon Kingdom": {4, 5},
    MUSHROOM_KINGDOM: {4, 5},
}

SCENARIO_TEMPORARY_BY_KINGDOM = {
    "Metro Kingdom": {7},
}

# Each Randomized Starting Position probe is a full reachability fixed point, so
# the shuffled candidate pool is walked only this far before giving up and
# falling back to a vanilla start. Mirrors StartPositionRando::kMaxProbes.
START_POSITION_MAX_PROBES = 48

RNG_STREAM_SALTS = {
    "Moon": 0x4D6F6F6E,
    "Entrance": 0x456E7472,
    "Requirement": 0x52657175,
    "Skybox": 0xB0B0CAFE,
    "Music": 0xBEE7BEE7,
    "Logic": 0x10C1C10C,
    "Capture": 0x0CA97A11,
    "Ability": 0xAB1117ED,
    "Painting": 0xFA1171A6,
    "Sphinx": 0x5F1A11A5,
    "Kingdom": 0xD14C0D3D,
    "Chest": 0x43686573,
    "StartPosition": 0x53746172,
    "MoonAllocation": 0x4D416C6C,
}


class Rng:
    def __init__(self, seed: int, stream: str = "Logic") -> None:
        self.state = (seed ^ RNG_STREAM_SALTS.get(stream, 0)) & 0xFFFFFFFF

    def next(self) -> int:
        self.state = (self.state * 1664525 + 1013904223) & 0xFFFFFFFF
        return self.state

    def next_modulo(self, value: int) -> int:
        if value <= 0:
            return 0
        return self.next() % value

    def next_float(self) -> float:
        return (self.next() >> 8) / float(1 << 24)

    def shuffle(self, values: list) -> None:
        for index in range(len(values) - 1, 0, -1):
            other = self.next_modulo(index + 1)
            values[index], values[other] = values[other], values[index]


def initial_scenario_by_kingdom(
    seed: int, kingdom: str, randomized: bool = False
) -> int:
    """Return the scenario the runtime gives ``kingdom`` at seed start.

    Keep this calculation in lockstep with Randomizer::getInitialKingdomScenario:
    the start-state option is generated per kingdom from an independent logic RNG
    stream, so changing the iteration order cannot change another kingdom's state.
    """
    vanilla_start = LOGIC_SCENARIO_START_OVERRIDE.get(
        kingdom, SCENARIO_START_BY_KINGDOM.get(kingdom, 1)
    )
    if not randomized:
        return vanilla_start

    minimum_scenario = vanilla_start
    max_scenario = SCENARIO_MAX_LOGIC_BY_KINGDOM.get(kingdom, -1)
    candidates: list[int] = []

    def add_candidate(scenario: int | None) -> None:
        if scenario is None or scenario <= 0:
            return
        scenario = max(scenario, minimum_scenario)
        if max_scenario >= 0 and scenario > max_scenario:
            scenario = max_scenario
        if scenario < minimum_scenario or scenario in candidates:
            return
        candidates.append(scenario)

    add_candidate(vanilla_start)
    add_candidate(SCENARIO_POSTGAME_BY_KINGDOM.get(kingdom))
    # A missing/negative Moon Rock scenario means this kingdom has no selectable
    # Moon Cube state, matching the runtime's candidate filter.
    add_candidate(SCENARIO_MOON_ROCK_BY_KINGDOM.get(kingdom))
    if not candidates:
        return vanilla_start

    world_id = WORLD_IDS.get(kingdom)
    if world_id is None:
        return vanilla_start
    kingdom_salt = ((world_id + 1) * 0x9E3779B9) & 0xFFFFFFFF
    rng_seed = (seed ^ kingdom_salt ^ 0x53544152) & 0xFFFFFFFF
    return candidates[Rng(rng_seed, "Logic").next_modulo(len(candidates))]


@dataclass
class Requirement:
    mode: str
    alternatives: list[frozenset[str]] = field(default_factory=list)
    raw: str = ""
    # Index-parallel with ``alternatives``: the node-access prerequisites of the
    # same OR option, authored as ``@<node_id>`` atoms in the expression. They are
    # kept out of ``alternatives`` so every existing token consumer (enemy swap
    # gates, item search, capture validation) keeps seeing tokens only.
    node_alternatives: list[frozenset[str]] = field(default_factory=list)
    # Computed once, not a property: every evaluation of every alternative on the
    # reachability hot path consults this, and almost no requirement has node
    # atoms, so it has to be a plain attribute read rather than a scan.
    has_node_requirements: bool = field(init=False, default=False)

    def __post_init__(self) -> None:
        self.has_node_requirements = any(self.node_alternatives)

    def node_alternative(self, index: int) -> frozenset[str]:
        if 0 <= index < len(self.node_alternatives):
            return self.node_alternatives[index]
        return frozenset()

    def node_alternative_satisfied(
        self, index: int, reachable_nodes: set[str] | None
    ) -> bool:
        # reachable_nodes None means "evaluated without a reachability set" - the
        # maximal-state traversability proofs. Those already grant every token, so
        # granting node access too keeps them answering "is this physically
        # crossable at all" rather than silently going impossible.
        nodes = self.node_alternative(index)
        if not nodes or reachable_nodes is None:
            return True
        return nodes <= reachable_nodes

    def is_satisfied_by(
        self,
        state: "LogicState",
        world: "LogicWorld",
        stage: str | None = None,
        reachable_nodes: set[str] | None = None,
    ) -> bool:
        if self.mode in ("unset", "free"):
            return True
        if self.mode in ("impossible", "raw"):
            return self.mode == "raw" and world.treat_raw_requirements_as_free
        # stage: where the requirement is evaluated; Enemy Swap can remap a
        # stage's capture tokens (world.remap_token), mirroring the C++
        # EnemySwapRando::remapLogicToken call in MoonRando. A token-less
        # full-pool result disables only alternatives that require that token.
        check_nodes = self.has_node_requirements and reachable_nodes is not None
        return any(
            world.is_requirement_alternative_enabled(stage, alt)
            and (not check_nodes or self.node_alternative_satisfied(index, reachable_nodes))
            and all(state.has_token(world.remap_token(stage, token), world) for token in alt)
            for index, alt in enumerate(self.alternatives)
        )


@dataclass
class LogicToken:
    token: str
    token_type: str
    display_name: str
    category: str
    prerequisite: str
    always_unlocked: bool
    requires_any_cap_throw: bool
    can_start_with_seed: bool


@dataclass
class Room:
    stage: str
    kingdom: str
    stage_display_name: str
    area_scope: str


@dataclass
class LogicNode:
    node_id: str
    kingdom: str
    stage: str
    node_type: str
    status: str
    side: str = ""
    scenario_indices: str = ""


@dataclass
class RouteEdge:
    edge_id: str
    from_node_id: str
    to_node_id: str
    kingdom: str
    scenario_mask: int
    requirement: Requirement
    bidirectional: bool
    status: str
    difficulty: str = ROUTE_DIFFICULTY_CASUAL
    option_scenario_masks: tuple[int, ...] = ()


@dataclass
class Moon:
    moon_id: str
    kingdom: str
    stage: str
    stage_display_name: str
    moon_name: str
    obj_id: int
    uid: int
    flags: str
    from_node_id: str
    state_requirement: Requirement
    local_requirement: Requirement
    local_option_scenario_masks: tuple[int, ...]
    scenario_floor: int | None
    scenario_mask: int
    scenario_move_to: int | None
    story_order: int | None
    story_move_to: int | None
    effort_level: str
    local_difficulty: str = ROUTE_DIFFICULTY_CASUAL
    capture_unlocks: list[str] = field(default_factory=list)
    ability_unlocks: list[str] = field(default_factory=list)
    # The home/physical kingdom remains in kingdom. Allocation only changes
    # the credited item kingdom, and the reward-triple override only changes
    # item value; location/scenario compatibility keeps using the fields above.
    reward_is_multi: bool | None = None
    assigned_kingdom: str | None = None

    @property
    def is_multi(self) -> bool:
        return "ShineFlag_Multi" in self.flags

    @property
    def is_reward_multi(self) -> bool:
        return self.is_multi if self.reward_is_multi is None else self.reward_is_multi

    @property
    def credit_kingdom(self) -> str:
        return self.assigned_kingdom if self.assigned_kingdom is not None else self.kingdom

    @property
    def is_story(self) -> bool:
        return "ShineFlag_Story" in self.flags

    @property
    def is_hint_art(self) -> bool:
        return "ShineFlag_HintArt" in self.flags

    @property
    def is_toadette(self) -> bool:
        return "ShineFlag_Toadette" in self.flags

    @property
    def is_princess_peach(self) -> bool:
        return is_princess_peach_moon(self)

    @property
    def moon_value(self) -> int:
        return 3 if self.is_reward_multi else 1


@dataclass
class EntranceLink:
    link_index: int
    kingdom: str
    home_stage: str
    home_entrance_in: str
    home_entrance_out: str
    sub_stage: str
    sub_entrance_in: str
    sub_entrance_out: str
    home_in_node_id: str
    home_out_node_id: str
    sub_near_node_id: str
    sub_far_node_id: str
    status: str


def is_ice_cave_link(entrance: EntranceLink) -> bool:
    return (
        entrance.home_stage == "SandWorldHomeStage"
        and entrance.sub_stage == "SandWorldPressExStage"
    )


def is_ice_cave_fixed_return_alias(entrance: EntranceLink) -> bool:
    return (
        is_ice_cave_link(entrance)
        and entrance.status == "vanilla"
        and entrance.sub_entrance_out == "arijigoku1"
    )


def is_ice_cave_exit_only_home_side(entrance: EntranceLink, side: str) -> bool:
    return is_ice_cave_link(entrance) and side == "home_out"


@dataclass
class Placement:
    location_id: str
    item_id: str


@dataclass
class EntrancePlacement:
    source_index: int
    destination_index: int
    is_home_warp: bool = False


@dataclass
class PaintingLink:
    link_id: str
    source_node_id: str
    destination_node_id: str
    bidirectional: bool
    status: str


@dataclass
class PathStep:
    location_id: str
    item_id: str
    reason: str
    sphere: int
    unlocked_tokens: list[str] = field(default_factory=list)


@dataclass
class RandomizationResult:
    placements: list[Placement]
    entrance_placements: list[EntrancePlacement]
    path: list[PathStep]
    warnings: list[str]
    final_objective_reached: bool = False
    final_objective_note: str = ""


@dataclass
class LogicSettings:
    seed: int
    starting_kingdoms: list[str] = field(default_factory=lambda: ["Cap Kingdom", "Cascade Kingdom"])
    kingdom_order: list[str] = field(default_factory=lambda: list(KINGDOM_ORDER))
    kingdom_tiers: list[tuple[str, ...]] = field(default_factory=lambda: list(KINGDOM_TIERS))
    moon_requirements: dict[str, int] = field(default_factory=lambda: dict(VANILLA_MOON_REQUIREMENTS))
    capture_rando: bool = True
    ability_rando: bool = True
    entrance_rando: bool = True
    # No Logic bypasses capture/ability and trick requirements only. Authored
    # topology, impossible edges, scenarios and physical story order still apply.
    no_logic: bool = False
    logic_difficulty: str = ROUTE_DIFFICULTY_CASUAL
    # Prioritize Difficult Movement: during the fill, requirements offering both
    # a casual and a harder (still allowed) alternative ignore the casual one
    # unless the fill is completely stuck, and sphere expansion prefers unlocks
    # opening harder alternatives. Validation always re-proves the seed with
    # every allowed method. No effect on casual difficulty or no_logic.
    prioritize_difficult_movement: bool = False
    # Subarea Keys (see logic/subarea_keys.py). The C++ runtime is what actually
    # generates seeds, so this side exists to keep the audits' reachability model
    # in step with it. "unlocked" leaves every sub-area open, as before.
    subarea_unlock_mode: str = subarea_keys.MODE_UNLOCKED
    # Carrier moon per key slot, {slot_index: moon_id}. Left empty a slot counts
    # as already held, so merely selecting a mode cannot make an audit report
    # everything unreachable - a caller has to supply the assignment it wants to
    # model.
    subarea_key_items: dict[int, str] = field(default_factory=dict)
    cloud_kingdom_moons_unlock_paintings: bool = False
    world_peace_requirement: bool = False
    triple_moons_anywhere: bool = False
    starting_tokens: list[str] = field(default_factory=list)
    seed_style: str = "standard"
    # Enemy/Capture Swap: when on, swappable capture requirements are
    # unavailable (config/enemy_swap_rules.csv; see logic/enemy_swap.py).
    enemy_swap: bool = False
    # ChaosSwapMode values: 0 off, 1 same category, 2 same category + combat,
    # and (props only) 3 the same pool plus deterministic deletion.
    npc_swap: int = 0
    prop_swap: int = 0
    prop_swap_chance: int = 0
    two_d_swap: bool = False
    # Preserve configured Policy_Logic capture sources and their matching stage
    # requirements. Defaults off so Chaos may randomize every capture source.
    keep_critical_captures: bool = False
    # Mirrors the gameplay map mode that exposes every kingdom as a travel
    # destination immediately while route progression remains ordered.
    all_kingdoms_start_unlocked: bool = False
    # Mirrors Randomizer::isStartKingdomStateRandoEnabled().
    start_kingdom_state_rando: bool = False
    # Randomized Starting Position (Randomizer::getStartPositionMode): "vanilla",
    # "overworld", "max", or "odyssey". See logic/start_position.py and the
    # transient-access model in LogicRandomizer.apply_start_position. "odyssey"
    # is logic-equivalent to "vanilla"; it only changes where the player is
    # standing when the file opens.
    start_position_mode: str = "vanilla"
    # Attempt salt for the start-position roll, mirroring the per-attempt
    # dependency rebuild the runtime does.
    start_position_attempt: int = 0
    # Moon Allocation settings mirror the runtime accessors. The optional
    # aliases accept names used by older host callers without changing the
    # serialized/player-facing values.
    moon_rando_enabled: bool = True
    moon_randomization_enabled: bool | None = None
    moon_reallocation_enabled: bool = False
    randomize_moon_allocation: bool = False
    moon_allocation_difficulty: str | int = "vanilla"
    moon_coin_policy: str | int = "vanilla"
    cap_kingdom_moon_use: str | int = "coins"
    cloud_kingdom_moon_use: str | int = "coins"
    moon_kingdom_moon_use: str | int = "coins"
    cap_moon_use: str | int | None = None
    cloud_moon_use: str | int | None = None
    moon_moon_use: str | int | None = None
    randomize_triple_moons: bool = False
    starting_equipment: str | int = "normal"
    # Test/host overrides are intentionally optional; normal seeds use the
    # fixed runtime constants and the configured starting-equipment reduction.
    required_capture_slots: int | None = None
    required_ability_slots: int | None = None
    moon_reallocation: bool | None = None
    enable_moon_reallocation: bool | None = None
    moon_allocation_randomize: bool | None = None
    allocation_difficulty: str | int | None = None
    coin_policy: str | int | None = None

    def __post_init__(self) -> None:
        self.logic_difficulty = normalize_route_difficulty(self.logic_difficulty, preserve_invalid=False)
        if self.seed_style not in SEED_STYLES:
            self.seed_style = "standard"
        self.start_position_mode = normalize_start_position_mode(self.start_position_mode)
        if self.moon_randomization_enabled is not None:
            self.moon_rando_enabled = bool(self.moon_randomization_enabled)
        if self.moon_reallocation is not None:
            self.moon_reallocation_enabled = bool(self.moon_reallocation)
        if self.enable_moon_reallocation is not None:
            self.moon_reallocation_enabled = bool(self.enable_moon_reallocation)
        if self.moon_allocation_randomize is not None:
            self.randomize_moon_allocation = bool(self.moon_allocation_randomize)
        if self.allocation_difficulty is not None:
            self.moon_allocation_difficulty = self.allocation_difficulty
        if self.coin_policy is not None:
            self.moon_coin_policy = self.coin_policy
        if self.cap_moon_use is not None:
            self.cap_kingdom_moon_use = self.cap_moon_use
        if self.cloud_moon_use is not None:
            self.cloud_kingdom_moon_use = self.cloud_moon_use
        if self.moon_moon_use is not None:
            self.moon_kingdom_moon_use = self.moon_moon_use

    def resolved_unlock_timing(self) -> str:
        if self.seed_style == "ability-rush":
            return "early"
        if self.seed_style == "mystery":
            return UNLOCK_TIMINGS[
                mix_seed_style_axis(self.seed ^ 0xA341316C) % 3
            ]
        return "standard"

    def resolved_route_breadth(self) -> str:
        if self.seed_style == "focused-route":
            return "focused"
        if self.seed_style == "explorer":
            return "expansive"
        if self.seed_style == "mystery":
            return ROUTE_BREADTHS[
                mix_seed_style_axis(self.seed ^ 0xC8013EA4) % 3
            ]
        return "standard"

    def resolved_progression_depth(self) -> str:
        if self.seed_style == "focused-route":
            return "shallow"
        if self.seed_style == "explorer":
            return "deep"
        if self.seed_style == "mystery":
            return PROGRESSION_DEPTHS[
                mix_seed_style_axis(self.seed ^ 0xAD90777D) % 3
            ]
        return "standard"


class LogicState:
    def __init__(self, settings: LogicSettings, world: "LogicWorld") -> None:
        self.accessible_kingdoms = (
            set(WORLD_IDS)
            if settings.all_kingdoms_start_unlocked
            else set(settings.starting_kingdoms)
        )
        self.accessible_stages = {MAIN_STAGE_BY_KINGDOM[k] for k in self.accessible_kingdoms if k in MAIN_STAGE_BY_KINGDOM}
        self.tokens = {token.token for token in world.tokens.values() if token.always_unlocked}
        if not settings.ability_rando:
            self.tokens.update(token.token for token in world.tokens.values() if token.token_type == "ability")
        if not settings.capture_rando:
            self.tokens.update(token.token for token in world.tokens.values() if token.token_type == "capture")
        def grant_starting_token(token_name: str) -> None:
            token = world.tokens.get(token_name)
            if not token or token_name in self.tokens:
                return
            prerequisite = normalize_prerequisite(token.prerequisite)
            if prerequisite:
                grant_starting_token(prerequisite)
            self.tokens.add(token_name)
        for token_name in settings.starting_tokens:
            grant_starting_token(token_name)
        self.kingdom_scenarios = {
            kingdom: initial_scenario_by_kingdom(
                settings.seed, kingdom, settings.start_kingdom_state_rando
            )
            for kingdom in WORLD_IDS
        }
        self.moon_counts = {kingdom: 0 for kingdom in WORLD_IDS}
        self.story_counts = {kingdom: 0 for kingdom in WORLD_IDS}
        # Seed-selected post-game/Moon Rock starts already include the matching
        # state events. This mirrors MoonRando::initState so a randomized start
        # does not make the host runner replay story beats the save already has.
        for kingdom in WORLD_IDS:
            initial_scenario = self.kingdom_scenarios[kingdom]
            peace_token = f"WorldPeace_{world.develop_name(kingdom)}"
            if initial_scenario >= SCENARIO_POSTGAME_BY_KINGDOM.get(kingdom, 0):
                if peace_token in world.tokens:
                    self.tokens.add(peace_token)
                self.story_counts[kingdom] = world.story_totals.get(kingdom, 0)

            moon_rock_scenario = SCENARIO_MOON_ROCK_BY_KINGDOM.get(kingdom)
            moon_rock_token = f"MoonRockActive_{world.develop_name(kingdom)}"
            if (
                moon_rock_scenario is not None
                and initial_scenario >= moon_rock_scenario
                and moon_rock_token in world.tokens
            ):
                self.tokens.add(moon_rock_token)

            # A kingdom with no story moons has world peace trivially (Cap,
            # Cloud, Lost, and the already-post-game special kingdoms).
            if world.story_totals.get(kingdom, 0) <= 0 and peace_token in world.tokens:
                self.tokens.add(peace_token)
        self.collected_items: set[str] = set()
        self.collected_locations: set[str] = set()
        self.accessible_nodes: set[str] = set()
        self.accessible_sub_entries: set[tuple[int, str]] = set()

        starting_cap_throw = world.pick_starting_cap_throw(settings.seed) if settings.ability_rando else None
        if starting_cap_throw:
            self.tokens.add(starting_cap_throw)
        for kingdom in self.accessible_kingdoms:
            self.grant_kingdom_access(kingdom, world)

    def copy(self) -> "LogicState":
        clone = object.__new__(LogicState)
        clone.accessible_kingdoms = set(self.accessible_kingdoms)
        clone.accessible_stages = set(self.accessible_stages)
        clone.tokens = set(self.tokens)
        clone.kingdom_scenarios = dict(self.kingdom_scenarios)
        clone.moon_counts = dict(self.moon_counts)
        clone.story_counts = dict(self.story_counts)
        clone.collected_items = set(self.collected_items)
        clone.collected_locations = set(self.collected_locations)
        clone.accessible_nodes = set(self.accessible_nodes)
        clone.accessible_sub_entries = set(self.accessible_sub_entries)
        return clone

    def mark_kingdom_reached(self, kingdom: str) -> None:
        if kingdom == MUSHROOM_KINGDOM:
            self.tokens.add(MUSHROOM_REACHED_TOKEN)

    def grant_kingdom_access(self, kingdom: str, world: "LogicWorld") -> None:
        self.accessible_kingdoms.add(kingdom)
        access_token = f"KingdomAccess_{world.develop_name(kingdom)}"
        if access_token in world.tokens:
            self.tokens.add(access_token)
        self.mark_kingdom_reached(kingdom)

    def has_token(self, token_name: str, world: "LogicWorld") -> bool:
        token = world.tokens.get(token_name)
        if not token:
            return False

        if token.token_type == "event":
            return token_name in self.tokens

        if token_name not in self.tokens:
            return False

        prerequisite = normalize_prerequisite(token.prerequisite)
        if prerequisite and not self.has_token(prerequisite, world):
            return False

        if token.requires_any_cap_throw:
            return any(cap_throw in self.tokens for cap_throw in world.starting_cap_throw_tokens)

        return True

    def collect(self, item: Moon, world: "LogicWorld") -> list[str]:
        """Collect a randomized ITEM: contributes its kingdom's moon count and any
        capture/ability unlock tokens. Story/scenario progression is physical and
        lives in collect_location."""
        if item.moon_id in self.collected_items:
            return []

        self.collected_items.add(item.moon_id)
        credited_kingdom = item.credit_kingdom
        self.moon_counts[credited_kingdom] = (
            self.moon_counts.get(credited_kingdom, 0) + item.moon_value
        )

        unlocked: list[str] = []
        for token in [*item.capture_unlocks, *item.ability_unlocks]:
            if token not in self.tokens:
                self.tokens.add(token)
                unlocked.append(token)

        return unlocked

    def collect_location(self, location: Moon, item: Moon | None, world: "LogicWorld") -> list[str]:
        """Physical collection: story/scenario progression follows the LOCATION
        (defeating Spewart advances Wooded no matter which item was credited there)."""
        if location.moon_id in self.collected_locations:
            return []

        self.collected_locations.add(location.moon_id)

        unlocked: list[str] = []
        # A kingdom's scenario only moves forward when the player collects a multi
        # ("triple") moon — the grand moon that completes a story beat. Single story
        # moons (seals, barriers, band members, ...) count toward world peace but are
        # sub-objectives that never bump ScenarioNo. Post-peace, the Moon Rock drives
        # advancement instead. So gate the per-moon scenario advance on is_multi; the
        # story-count / world-peace bookkeeping stays unconditional.
        if location.scenario_move_to is not None and location.is_multi:
            self.advance_scenario(location.kingdom, location.scenario_move_to)

        if location.is_story and self.is_next_story_moon(location):
            if location.story_order is None:
                self.story_counts[location.kingdom] = self.story_counts.get(location.kingdom, 0) + 1
            else:
                self.story_counts[location.kingdom] = location.story_order + 1
            if location.story_move_to is not None and location.is_multi:
                self.advance_scenario(location.kingdom, location.story_move_to)
            elif (
                location.story_move_to is not None
                and location.story_move_to in SCRIPTED_SCENARIO_ADVANCE.get(location.kingdom, frozenset())
            ):
                # Scripted scenario exception (e.g. Metro festival): a single story
                # beat advances ScenarioNo without a triple moon.
                self.advance_scenario(location.kingdom, location.story_move_to)

            if world.story_totals.get(location.kingdom, 0) > 0 and self.story_counts[location.kingdom] >= world.story_totals[location.kingdom]:
                self.advance_scenario(location.kingdom, SCENARIO_POSTGAME_BY_KINGDOM.get(location.kingdom))
                token = f"WorldPeace_{world.develop_name(location.kingdom)}"
                if token in world.tokens and token not in self.tokens:
                    self.tokens.add(token)
                    unlocked.append(token)

        if item is not None:
            unlocked.extend(self.collect(item, world))

        return unlocked

    def is_next_story_moon(self, item: Moon) -> bool:
        if not item.is_story:
            return False
        if item.story_order is None:
            return True
        return self.story_counts.get(item.kingdom, 0) == item.story_order

    def advance_scenario(self, kingdom: str, scenario: int | None) -> bool:
        if scenario is None or scenario <= 0:
            return False
        scenario = normalize_scenario_target(kingdom, scenario)
        if scenario is None:
            return False
        current = self.kingdom_scenarios.get(kingdom, SCENARIO_START_BY_KINGDOM.get(kingdom, 1))
        if scenario <= current:
            return False
        self.kingdom_scenarios[kingdom] = scenario
        return True

    def grant_token(self, token: str, world: "LogicWorld") -> bool:
        if token not in world.tokens or token in self.tokens:
            return False
        self.tokens.add(token)
        return True

    def refresh_reachable_events(self, world: "LogicWorld", reachable_nodes: set[str]) -> list[str]:
        unlocked: list[str] = []
        for node_id in reachable_nodes:
            token = world.event_token_by_node.get(node_id)
            if not token:
                continue
            for kingdom in WORLD_IDS:
                if token != f"MoonRockActive_{world.develop_name(kingdom)}":
                    continue
                if f"WorldPeace_{world.develop_name(kingdom)}" not in self.tokens:
                    continue
                if token in world.tokens and token not in self.tokens:
                    self.tokens.add(token)
                    unlocked.append(token)
                    self.advance_scenario(kingdom, SCENARIO_MOON_ROCK_BY_KINGDOM.get(kingdom))
        return unlocked


class LogicWorld:
    def __init__(
        self,
        tokens: list[LogicToken],
        rooms: list[Room],
        nodes: list[LogicNode],
        edges: list[RouteEdge],
        moons: list[Moon],
        entrances: list[EntranceLink],
        painting_links: list[PaintingLink] | None = None,
    ) -> None:
        self.tokens = {token.token: token for token in tokens}
        if MUSHROOM_REACHED_TOKEN not in self.tokens:
            self.tokens[MUSHROOM_REACHED_TOKEN] = LogicToken(
                token=MUSHROOM_REACHED_TOKEN,
                token_type="event",
                display_name="Reached Mushroom Kingdom",
                category="Progression",
                prerequisite="",
                always_unlocked=False,
                requires_any_cap_throw=False,
                can_start_with_seed=False,
            )
        self.rooms = {room.stage: room for room in rooms}
        self.nodes = {node.node_id: node for node in nodes}
        self.event_token_by_node = {
            node.node_id: node.node_id.rsplit("::event::", 1)[-1]
            for node in nodes
            if node.node_type == "state_event" and "::event::" in node.node_id
        }
        self.edges = apply_default_bidirectionality(edges)
        self.moons = {moon.moon_id: moon for moon in moons}
        self.moon_allocation_result = None
        self.moon_allocation_settings = None
        self.entrances = entrances
        self.painting_links = painting_links or []
        self.treat_raw_requirements_as_free = True
        # (logic stage, original capture token) -> replacement capture token,
        # built per seed/attempt by apply_enemy_swap(); empty = identity.
        self.enemy_swap_remap: dict[tuple[str, str], str] = {}
        self.enemy_swap_config = None
        self.enemy_swap_assignment: tuple[int, ...] = ()
        self.enemy_swap_witnesses: tuple[str, ...] = ()
        # Enemy/Capture chaos makes captures unreliable for logic. When active,
        # a capture token that can be swapped away is treated as unavailable
        # (remap_token returns a disabled sentinel), so capture-only routes are
        # impossible; captures in reliable_captures stay usable, as does any
        # (stage, token) with no authored gate in gated_stage_tokens -- vanilla
        # until gated, scoped per physical stage. Mirrors
        # EnemySwapRando::remapLogicToken / isReliableCaptureToken.
        self.captures_unreliable = False
        self.reliable_captures: set[str] = set()
        self.gated_stage_tokens: set[tuple[str, str]] = set()
        self.keep_critical_captures = False
        self.edges_by_source: dict[str, list[RouteEdge]] = {}
        self.stage_has_routes: dict[str, bool] = {}
        self.multi_overworld_stages = find_multi_overworld_stages(rooms)
        self.starting_cap_throw_tokens = [
            token.token for token in tokens if token.token_type == "ability" and token.can_start_with_seed
        ]
        self.story_totals = count_story_moons(moons)

        # Nodes that can be reached through the graph; everything else is treated as
        # free from the stage entry (no edges = free, matching the C++ engine).
        self.nodes_with_incoming_edges: set[str] = set()
        for edge in self.edges:
            self.edges_by_source.setdefault(edge.from_node_id, []).append(edge)
            if edge.bidirectional and edge.to_node_id != edge.from_node_id:
                self.edges_by_source.setdefault(edge.to_node_id, []).append(edge)
            if edge.status != "excluded":
                self.nodes_with_incoming_edges.add(edge.to_node_id)
                if edge.bidirectional:
                    self.nodes_with_incoming_edges.add(edge.from_node_id)
            from_stage = self.nodes.get(edge.from_node_id, LogicNode("", "", "", "", "")).stage
            to_stage = self.nodes.get(edge.to_node_id, LogicNode("", "", "", "", "")).stage
            if from_stage:
                self.stage_has_routes[from_stage] = True
            if to_stage:
                self.stage_has_routes[to_stage] = True

    def is_capture_reliable(self, stage: str | None, token: str) -> bool:
        """Whether Enemy/Capture chaos leaves this capture usable by logic.

        Mirrors EnemySwapRando::isReliableCaptureToken. A capture that can
        never be swapped away is always reliable. Otherwise it is reliable in
        a given stage unless a gate was authored for that exact (stage,
        token) pair -- vanilla until gated, scoped per physical stage.
        ``stage=None`` checks across every stage (used by mandatory-item
        decisions that are not tied to one place), matching a gate in any
        stage. When a gate does apply, the keep-critical-captures difficulty
        setting can still exempt it.
        """
        if token in self.reliable_captures:
            return True
        gated = (
            any(gate_token == token for _, gate_token in self.gated_stage_tokens)
            if stage is None else
            (stage, token) in self.gated_stage_tokens
        )
        if not gated:
            return True
        return self.keep_critical_captures

    def remap_token(self, stage: str | None, token: str) -> str:
        remapped = token
        if self.enemy_swap_remap and stage:
            remapped = self.enemy_swap_remap.get((stage, token), token)
        # Capture-gated routes and moon checks use authored physical witnesses
        # through route_capture_replacements. Other capture requirements keep
        # this conservative remap behavior.
        if self.captures_unreliable:
            from enemy_swap import is_disabled_capture_token

            if not is_disabled_capture_token(remapped):
                entry = self.tokens.get(remapped)
                if (entry is not None and entry.token_type == "capture"
                        and not self.is_capture_reliable(stage, remapped)):
                    from enemy_swap import disabled_capture_token, is_stage_locked_capture

                    if stage and is_stage_locked_capture(stage, remapped):
                        return remapped

                    return disabled_capture_token(stage or "", remapped)
        return remapped

    def is_requirement_alternative_enabled(
        self, stage: str | None, alternative: Iterable[str]
    ) -> bool:
        """Whether Enemy Swap left every token in this logic path available."""
        from enemy_swap import is_disabled_capture_token

        return not any(
            is_disabled_capture_token(self.remap_token(stage, token))
            for token in alternative
        )

    def edge_stage(self, edge: "RouteEdge") -> str | None:
        node = self.nodes.get(edge.from_node_id)
        if node:
            return node.stage
        return edge.from_node_id.split("::", 1)[0] or None

    def moon_capture_edge(
        self, moon: "Moon", is_state_requirement: bool
    ) -> "RouteEdge":
        """Make the non-traversal gate target for one moon requirement.

        The runtime generator emits the same self-edge IDs for capture-gate
        assignment. Keeping this edge out of ``self.edges`` is intentional:
        moon access must not create a graph connection or mark a stage routed.
        """
        requirement = (
            moon.state_requirement
            if is_state_requirement else moon.local_requirement
        )
        return RouteEdge(
            edge_id=f"moon_{'state' if is_state_requirement else 'local'}_{moon.uid}",
            from_node_id=moon.from_node_id or f"{moon.stage}::start",
            to_node_id=moon.from_node_id or f"{moon.stage}::start",
            kingdom=moon.kingdom,
            scenario_mask=0,
            requirement=requirement,
            bidirectional=True,
            status="",
            difficulty=(
                ROUTE_DIFFICULTY_CASUAL
                if is_state_requirement else moon.local_difficulty
            ),
            option_scenario_masks=(
                () if is_state_requirement
                else moon.local_option_scenario_masks
            ),
        )

    def apply_enemy_swap(
        self, seed: int, attempt: int = 0, keep_critical_captures: bool = False,
        enemy_enabled: bool = True,
        npc_mode: int = 0, prop_mode: int = 0,
        prop_chance: int = 0, two_d_enabled: bool = False,
        logic_difficulty: str = ROUTE_DIFFICULTY_CASUAL,
    ) -> None:
        """Construct and install the deterministic assignment for a seed attempt.

        This mirrors EnemySwapRando::buildRemapTable: gate witnesses are solved
        constructively before unconstrained placements receive their versioned
        hash-derived values. No shared RNG stream advances here.
        """
        from enemy_swap import load_swap_config

        config = load_swap_config()
        assignment = config.build_assignment(
            seed,
            attempt,
            enemy_enabled=enemy_enabled,
            npc_mode=npc_mode,
            prop_mode=prop_mode,
            prop_chance=prop_chance,
            two_d_enabled=two_d_enabled,
            logic_difficulty=logic_difficulty,
            keep_critical_captures=keep_critical_captures,
        )
        self.enemy_swap_config = config
        self.enemy_swap_assignment = assignment.entities
        self.enemy_swap_witnesses = assignment.witness_gate_ids
        self.enemy_swap_remap = config.build_stage_token_remap(seed, attempt)
        self.captures_unreliable = enemy_enabled
        self.reliable_captures = set(config.reliable_captures)
        self.gated_stage_tokens = {
            (gate.logic_stage, gate.source_capture_token)
            for gate in config.gates
        }
        self.keep_critical_captures = keep_critical_captures

    def clear_enemy_swap(self) -> None:
        self.enemy_swap_remap = {}
        self.enemy_swap_config = None
        self.enemy_swap_assignment = ()
        self.enemy_swap_witnesses = ()
        self.captures_unreliable = False
        self.reliable_captures = set()
        self.gated_stage_tokens = set()
        self.keep_critical_captures = False

    def route_capture_satisfied(
        self,
        edge: "RouteEdge",
        source_token: str,
        state: LogicState,
        scenario: int,
    ) -> bool:
        return any(
            state.has_token(token, self)
            for token in self.route_capture_replacements(
                edge, source_token, scenario
            )
        )

    def route_capture_replacements(
        self,
        edge: "RouteEdge",
        source_token: str,
        scenario: int,
    ) -> tuple[str, ...]:
        """Physically present capture tokens that may satisfy this route token.

        Callers check these tokens directly in LogicState, deliberately
        bypassing enemy-swap remapping a second time.
        """
        if not self.captures_unreliable:
            return (source_token,)
        stage = self.edge_stage(edge) or ""
        if self.is_capture_reliable(stage, source_token):
            return (source_token,)
        from enemy_swap import is_stage_locked_capture

        if is_stage_locked_capture(stage, source_token):
            return (source_token,)
        config = self.enemy_swap_config
        if config is None or not self.enemy_swap_assignment:
            return ()
        gate_indices = config.gates_by_edge_token.get(
            (edge.kingdom, edge.edge_id, source_token), [])
        return tuple(sorted({
            config.gates[gate_index].replacement_capture_token
            for gate_index in gate_indices
            if config.gate_matches(
                config.gates[gate_index],
                self.enemy_swap_assignment,
                scenario,
            )
        }))

    def develop_name(self, kingdom: str) -> str:
        if kingdom == "Bowser's Kingdom":
            return "Bowsers"
        if kingdom == "Dark Side":
            return "Dark"
        if kingdom == "Darker Side":
            return "Darker"
        return kingdom.split(" Kingdom")[0].replace("'", "").replace(" ", "")

    def pick_starting_cap_throw(self, seed: int) -> str | None:
        if not self.starting_cap_throw_tokens:
            return None
        rng = Rng(seed, "Ability")
        return self.starting_cap_throw_tokens[rng.next_modulo(len(self.starting_cap_throw_tokens))]

    def moon_pool(self, settings: LogicSettings) -> list[Moon]:
        return [
            moon
            for moon in self.moons.values()
            if is_moon_pool_eligible(moon, settings.capture_rando, settings.ability_rando)
        ]

    def location_pool(self, settings: LogicSettings) -> list[Moon]:
        return self.moon_pool(settings)


class LogicRandomizer:
    def __init__(self, world: LogicWorld, settings: LogicSettings) -> None:
        self.world = world
        self.settings = settings
        self.rng = Rng(settings.seed, "Logic")
        self.warnings: list[str] = []
        self.progression_item_ids: set[str] = set()
        self.location_spheres: dict[str, int] = {}
        # Prioritize Difficult Movement fill state (mirrors MoonRando.cpp
        # sPrioritizeDifficultFill / sCasualFallbackActive).
        self.difficult_fill_active = False
        self.casual_fallback_active = False
        # Randomized Starting Position: chosen once per seed by
        # choose_start_position, None for a vanilla start.
        self.start_node: str | None = None

    def mask_casual_methods(self) -> bool:
        return self.difficult_fill_active and not self.casual_fallback_active

    def randomize(self) -> RandomizationResult:
        # Enemy Swap changes which capture each stage's requirements name, so it
        # must be resolved before any reachability is evaluated (the C++ engine
        # rebuilds EnemySwapRando in the same dependency pass).
        if (
            self.settings.enemy_swap
            or self.settings.npc_swap
            or self.settings.prop_swap
            or self.settings.two_d_swap
        ):
            self.world.apply_enemy_swap(
                self.settings.seed,
                keep_critical_captures=self.settings.keep_critical_captures,
                enemy_enabled=self.settings.enemy_swap,
                npc_mode=self.settings.npc_swap,
                prop_mode=self.settings.prop_swap,
                two_d_enabled=self.settings.two_d_swap,
                prop_chance=self.settings.prop_swap_chance,
                logic_difficulty=self.settings.logic_difficulty,
            )
        else:
            self.world.clear_enemy_swap()
        # Match MoonRando's preprocessing order: reward values are settled
        # first, then credited kingdoms, then capture/ability role builders.
        compute_reward_multi_values(
            self.world,
            self.settings.seed,
            self.settings.randomize_triple_moons,
        )
        self.world.moon_allocation_result = allocate_moons(
            self.world,
            self.settings,
            seed=self.settings.seed,
        )
        self.world.moon_allocation_settings = self.settings
        if not self.world.moon_allocation_result.success:
            raise MoonAllocationError(self.world.moon_allocation_result.failure)
        self.assign_unlocks()
        entrances = self.build_entrance_mapping()
        # Last dependency before placement, matching the runtime's ordering: the
        # walk-back proof reads the finished entrance mapping and the starting
        # equipment assign_unlocks just settled.
        self.choose_start_position(
            {placement.source_index: placement for placement in entrances}
        )
        placements, path, final_objective_reached, final_objective_note = (
            self.buffer_managed_sphere_placement(entrances)
        )
        return RandomizationResult(
            placements,
            entrances,
            path,
            self.warnings,
            final_objective_reached,
            final_objective_note,
        )

    def is_final_objective_reachable(
        self,
        state: LogicState,
        all_unlocks_reachable: bool,
    ) -> bool:
        final_kingdom = self.settings.kingdom_order[-1] if self.settings.kingdom_order else "Moon Kingdom"
        final_requirement = self.settings.moon_requirements.get(final_kingdom, 0)
        return (
            all_unlocks_reachable
            and final_kingdom in state.accessible_kingdoms
            and state.moon_counts.get(final_kingdom, 0) >= final_requirement
            and FINAL_OBJECTIVE_NODE_ID in self.reachable_nodes(state)
        )

    def assign_unlocks(self) -> None:
        self.progression_item_ids.clear()
        for moon in self.world.moons.values():
            moon.capture_unlocks.clear()
            moon.ability_unlocks.clear()
        if self.settings.capture_rando:
            self.assign_capture_unlocks()
        if self.settings.ability_rando:
            self.assign_ability_unlocks()

    def assign_capture_unlocks(self) -> None:
        capture_tokens = [token.token for token in self.world.tokens.values() if token.token_type == "capture"]
        capture_rng = Rng(self.settings.seed, "Capture")
        capture_rng.shuffle(capture_tokens)

        # The capture assignment pool matches C++ isMushroomMoon
        mushroom_moons = [
            moon for moon in self.world.moons.values()
            if moon.credit_kingdom == "Mushroom Kingdom"
            and is_moon_pool_eligible(moon, True, self.settings.ability_rando)
        ]

        index = 0
        first_copy: set[str] = set()
        for moon in mushroom_moons:
            for _ in range(moon.moon_value):
                if not capture_tokens:
                    return
                token = capture_tokens[index % len(capture_tokens)]
                moon.capture_unlocks.append(token)
                # Chaos makes all swappable captures unavailable to logic.
                # Only a capture that cannot be swapped away still needs a
                # guaranteed progression slot (Mini Rocket / Spark Pylon).
                # stage=None checks reliability across every stage, since a
                # capture-lock item is not tied to one specific place.
                capture_is_mandatory = (
                    not self.settings.enemy_swap
                    or self.world.is_capture_reliable(None, token)
                )
                if capture_is_mandatory and token not in first_copy:
                    first_copy.add(token)
                    self.progression_item_ids.add(moon.moon_id)
                index += 1

    def assign_ability_unlocks(self) -> None:
        ability_rng = Rng(self.settings.seed, "Ability")
        starting = None
        if self.world.starting_cap_throw_tokens:
            starting = self.world.starting_cap_throw_tokens[
                ability_rng.next_modulo(len(self.world.starting_cap_throw_tokens))
            ]
            if starting not in self.settings.starting_tokens:
                self.settings.starting_tokens.append(starting)

        # Matches C++ isAbilityMoon.
        moon_slots: list[Moon] = []
        for moon in self.world.moons.values():
            if (
                moon.credit_kingdom not in {"Dark Side", "Darker Side"}
                or not is_moon_pool_eligible(moon, True, True)
            ):
                continue
            moon_slots.extend([moon] * moon.moon_value)
        ability_rng.shuffle(moon_slots)

        abilities = [
            token.token
            for token in self.world.tokens.values()
            if token.token_type == "ability"
            and not token.always_unlocked
            and token.token != starting
        ]
        ability_rng.shuffle(abilities)

        if not abilities:
            return

        first_copy: set[str] = set()
        for index, moon in enumerate(moon_slots):
            token = abilities[index % len(abilities)]
            moon.ability_unlocks.append(token)
            if token not in first_copy:
                first_copy.add(token)
                self.progression_item_ids.add(moon.moon_id)

    def build_entrance_mapping(self) -> list[EntrancePlacement]:
        active = [link for link in self.world.entrances if self.is_entrance_randomizable(link)]
        fixed = [
            link
            for link in self.world.entrances
            if link.status == "vanilla"
            and self.entrance_has_valid_home_side(link)
            and self.entrance_has_valid_sub_side(link)
        ]
        if not self.settings.entrance_rando:
            return [
                EntrancePlacement(link.link_index, link.link_index)
                for link in (*active, *fixed)
            ]

        shuffled = [link.link_index for link in active]
        entrance_rng = Rng(self.settings.seed, "Entrance")
        entrance_rng.shuffle(shuffled)

        result: list[EntrancePlacement] = []
        for source, destination in zip(active, shuffled):
            result.append(EntrancePlacement(source.link_index, destination, False))
        result.extend(
            EntrancePlacement(link.link_index, link.link_index)
            for link in fixed
        )

        return result

    def requires_route_story_completion(self, kingdom: str) -> bool:
        # Route progression no longer requires collecting Bowser's grand moon.
        # Add future authored departure blockers here if that changes.
        # NOTE: do NOT gate this on world_peace_requirement — that forces a full
        # per-kingdom story proof inside every buffer-loop iteration and makes
        # generation take unacceptably long (mirrors MoonRando.cpp).
        return False

    def is_route_kingdom(self, kingdom: str) -> bool:
        return any(kingdom in tier for tier in self.settings.kingdom_tiers)

    def update_frontier_spheres(
        self,
        state: LogicState,
        unfilled: set[str],
        entrance_lookup: dict[int, EntrancePlacement],
        frontier: list[int | bool],
    ) -> list[Moon]:
        reachable = self.reachable_locations(state, unfilled, entrance_lookup)
        newly_reachable = [
            location for location in reachable
            if location.moon_id not in self.location_spheres
        ]
        if newly_reachable:
            if bool(frontier[1]):
                frontier[0] = int(frontier[0]) + 1
            for location in newly_reachable:
                self.location_spheres[location.moon_id] = int(frontier[0])
        if reachable:
            frontier[1] = True
        return reachable

    def can_place_item_at_location(self, item: Moon, location: Moon) -> bool:
        return (
            self.settings.triple_moons_anywhere
            or location.is_multi == item.is_reward_multi
        )

    def pick_depth_weighted_location(
        self, item: Moon, locations: list[Moon]
    ) -> Moon | None:
        compatible = [
            location for location in locations
            if self.can_place_item_at_location(item, location)
        ]
        if not compatible:
            return None
        known = [
            self.location_spheres[location.moon_id]
            for location in compatible
            if location.moon_id in self.location_spheres
        ]
        minimum = min(known, default=0)
        provisional = max(known, default=minimum - 1) + 1
        relative_depths = [
            self.location_spheres.get(location.moon_id, provisional) - minimum
            for location in compatible
        ]
        maximum = max(relative_depths, default=0)
        depth_style = self.settings.resolved_progression_depth()
        exponents = relative_depths
        if depth_style == "shallow":
            exponents = [maximum - depth for depth in relative_depths]
        elif depth_style == "deep":
            exponents = [depth * 2 for depth in relative_depths]
        weights = [1 << min(exponent, 30) for exponent in exponents]
        total = sum(weights)
        pick = ((self.rng.next() << 32) | self.rng.next()) % total
        for location, weight in zip(compatible, weights):
            if pick < weight:
                return location
            pick -= weight
        return None

    def pick_even_location(
        self,
        item: Moon,
        locations: list[Moon],
        placements: list[Placement],
    ) -> Moon | None:
        compatible = [
            location for location in locations
            if self.can_place_item_at_location(item, location)
        ]
        if not compatible:
            return None
        distribution: Counter[tuple[str, str]] = Counter(
            (
                self.world.moons[placement.location_id].kingdom,
                self.world.moons[placement.item_id].credit_kingdom,
            )
            for placement in placements
        )
        best = min(
            distribution[(location.kingdom, item.credit_kingdom)]
            for location in compatible
        )
        candidates = [
            location for location in compatible
            if distribution[(location.kingdom, item.credit_kingdom)] == best
        ]
        return candidates[self.rng.next_modulo(len(candidates))]

    def pick_random_compatible_item(
        self,
        unplaced: set[str],
        reachable: list[Moon],
        *,
        progression_only: bool = False,
    ) -> Moon | None:
        if not reachable:
            return None
        has_single = any(not location.is_multi for location in reachable)
        has_multi = any(location.is_multi for location in reachable)
        candidates = [
            self.world.moons[item_id]
            for item_id in sorted(unplaced)
            if (not progression_only or item_id in self.progression_item_ids)
            and (
                self.settings.triple_moons_anywhere
                or (self.world.moons[item_id].is_reward_multi and has_multi)
                or (not self.world.moons[item_id].is_reward_multi and has_single)
            )
        ]
        if not candidates:
            return None
        return candidates[self.rng.next_modulo(len(candidates))]

    def pick_high_buffer_compatible_item(
        self,
        unplaced: set[str],
        reachable: list[Moon],
    ) -> Moon | None:
        timing = self.settings.resolved_unlock_timing()
        if timing == "standard":
            return self.pick_random_compatible_item(unplaced, reachable)

        if not reachable:
            return None

        has_single = any(not location.is_multi for location in reachable)
        has_multi = any(location.is_multi for location in reachable)
        candidates = [
            self.world.moons[item_id]
            for item_id in sorted(unplaced)
            if (
                self.settings.triple_moons_anywhere
                or (self.world.moons[item_id].is_reward_multi and has_multi)
                or (not self.world.moons[item_id].is_reward_multi and has_single)
            )
            and (
                timing == "early"
            ) == bool(
                self.world.moons[item_id].capture_unlocks
                or self.world.moons[item_id].ability_unlocks
            )
        ]
        if candidates:
            return candidates[self.rng.next_modulo(len(candidates))]
        return self.pick_random_compatible_item(unplaced, reachable)

    def buffer_thresholds(self) -> tuple[int, int, int]:
        breadth = self.settings.resolved_route_breadth()
        if breadth == "focused":
            return (10, 6, 10)
        if breadth == "expansive":
            return (16, 1, 3)
        return (10, 3, 6)

    def pick_required_moon_for_kingdom(
        self,
        state: LogicState,
        kingdom: str,
        unplaced: set[str],
        reachable: list[Moon],
    ) -> Moon | None:
        remaining = max(
            0,
            self.settings.moon_requirements.get(kingdom, 0)
            - state.moon_counts.get(kingdom, 0),
        )
        if remaining <= 0:
            return None
        single_slots = sum(not location.is_multi for location in reachable)
        multi_slots = sum(location.is_multi for location in reachable)
        single_items = [
            self.world.moons[item_id]
            for item_id in sorted(unplaced)
            if self.world.moons[item_id].credit_kingdom == kingdom
            and not self.world.moons[item_id].is_reward_multi
        ]
        multi_items = [
            self.world.moons[item_id]
            for item_id in sorted(unplaced)
            if self.world.moons[item_id].credit_kingdom == kingdom
            and self.world.moons[item_id].is_reward_multi
        ]
        self.rng.shuffle(single_items)
        self.rng.shuffle(multi_items)

        total_slots = len(reachable)
        available_multi_slots = total_slots if self.settings.triple_moons_anywhere else multi_slots
        best: tuple[int, int, int, int] | None = None
        for multi in range(min(available_multi_slots, len(multi_items)) + 1):
            available_single_slots = (
                min(total_slots - multi, len(single_items))
                if self.settings.triple_moons_anywhere
                else min(single_slots, len(single_items))
            )
            for single in range(available_single_slots + 1):
                value = multi * 3 + single
                if value < remaining:
                    continue
                candidate = (multi + single, value - remaining, multi, single)
                if best is None or candidate[:2] < best[:2]:
                    best = candidate
        if best is None:
            return None
        _, _, planned_multi, planned_single = best
        choose_multi = planned_multi > 0 and (
            planned_single <= 0
            or self.rng.next_modulo(planned_multi + planned_single) < planned_multi
        )
        pool = multi_items if choose_multi else single_items
        if not pool:
            return None
        return pool[self.rng.next_modulo(len(pool))]

    def is_kingdom_story_reachable(
        self,
        state: LogicState,
        kingdom: str,
        unfilled: set[str],
        placements: list[Placement],
        entrance_lookup: dict[int, EntrancePlacement],
    ) -> bool:
        if (
            not self.requires_route_story_completion(kingdom)
            or state.story_counts.get(kingdom, 0)
            >= self.world.story_totals.get(kingdom, 0)
        ):
            return True
        sim = state.copy()
        placed_by_location = {
            placement.location_id: placement.item_id for placement in placements
        }
        consider = (
            set(unfilled) | set(placed_by_location)
        ) - sim.collected_locations
        changed = True
        while changed:
            changed = False
            for location in self.reachable_locations(sim, consider, entrance_lookup):
                item_id = placed_by_location.get(location.moon_id)
                sim.collect_location(
                    location,
                    self.world.moons[item_id] if item_id else None,
                    self.world,
                )
                consider.discard(location.moon_id)
                changed = True
            sim.refresh_reachable_events(self.world, self.reachable_nodes(sim))
        return (
            sim.story_counts.get(kingdom, 0)
            >= self.world.story_totals.get(kingdom, 0)
        )

    def is_story_driver(
        self, state: LogicState, location: Moon, kingdom: str
    ) -> bool:
        if location.kingdom != kingdom:
            return False
        if location.is_story and state.is_next_story_moon(location):
            return True
        current = state.kingdom_scenarios.get(
            kingdom, SCENARIO_START_BY_KINGDOM.get(kingdom, 1)
        )
        if location.is_multi and (location.scenario_move_to or -1) > current:
            return True
        return (location.story_move_to or -1) > current

    def materialize_kingdom_story(
        self,
        state: LogicState,
        kingdom: str,
        unfilled: set[str],
        unplaced: set[str],
        placements: list[Placement],
        path: list[PathStep],
        entrance_lookup: dict[int, EntrancePlacement],
        frontier: list[int | bool],
    ) -> bool:
        target = self.world.story_totals.get(kingdom, 0)
        while state.story_counts.get(kingdom, 0) < target:
            reachable = self.update_frontier_spheres(
                state, unfilled, entrance_lookup, frontier
            )
            drivers = [
                location for location in reachable
                if self.is_story_driver(state, location, kingdom)
            ]
            if not drivers:
                return False
            location = drivers[self.rng.next_modulo(len(drivers))]
            item = self.pick_random_compatible_item(unplaced, [location])
            if not item:
                return False
            path.append(
                self.place_and_collect(
                    state,
                    item,
                    location,
                    unfilled,
                    unplaced,
                    placements,
                    entrance_lookup,
                    self.location_spheres[location.moon_id],
                    "story progression",
                )
            )
        return True

    def run_buffer_for_kingdom(
        self,
        state: LogicState,
        kingdom: str,
        unfilled: set[str],
        unplaced: set[str],
        placements: list[Placement],
        path: list[PathStep],
        entrance_lookup: dict[int, EntrancePlacement],
        frontier: list[int | bool],
    ) -> bool:
        high_buffer, low_buffer, expansion_backlog = self.buffer_thresholds()
        fallback_floor: int | None = None
        while True:
            # Release the "completely stuck" casual fallback once a placement
            # landed under it (or when a previous phase left it active), resuming
            # the strict difficult-movement fill. Mirrors settleCasualFallback.
            if self.casual_fallback_active and (
                fallback_floor is None or len(placements) > fallback_floor
            ):
                self.casual_fallback_active = False
                fallback_floor = None
            reachable = self.update_frontier_spheres(
                state, unfilled, entrance_lookup, frontier
            )
            remaining = max(
                0,
                self.settings.moon_requirements.get(kingdom, 0)
                - state.moon_counts.get(kingdom, 0),
            )
            needs_story = (
                self.requires_route_story_completion(kingdom)
                and state.story_counts.get(kingdom, 0)
                < self.world.story_totals.get(kingdom, 0)
            )
            if remaining <= 0 and not needs_story:
                return True

            if remaining <= 0 and needs_story:
                if self.is_kingdom_story_reachable(
                    state, kingdom, unfilled, placements, entrance_lookup
                ):
                    if self.materialize_kingdom_story(
                        state,
                        kingdom,
                        unfilled,
                        unplaced,
                        placements,
                        path,
                        entrance_lookup,
                        frontier,
                    ):
                        continue
                    if self.mask_casual_methods():
                        self.casual_fallback_active = True
                        fallback_floor = len(placements)
                        continue
                    return False
                steps = self.place_reachability_unlock(
                    state,
                    unfilled,
                    unplaced,
                    placements,
                    entrance_lookup,
                    int(frontier[0]),
                    kingdom,
                    frontier=frontier,
                )
                if not steps:
                    if self.mask_casual_methods():
                        self.casual_fallback_active = True
                        fallback_floor = len(placements)
                        continue
                    return False
                path.extend(steps)
                continue

            has_single_item = any(
                not self.world.moons[item_id].is_reward_multi for item_id in unplaced
            )
            has_multi_item = any(
                self.world.moons[item_id].is_reward_multi for item_id in unplaced
            )
            available = (
                len(reachable)
                if self.settings.triple_moons_anywhere
                and (has_single_item or has_multi_item)
                else sum(
                    (location.is_multi and has_multi_item)
                    or (not location.is_multi and has_single_item)
                    for location in reachable
                )
            )
            buffer = available - remaining

            if buffer <= 0:
                steps = self.place_reachability_unlock(
                    state,
                    unfilled,
                    unplaced,
                    placements,
                    entrance_lookup,
                    int(frontier[0]),
                    kingdom,
                    frontier=frontier,
                )
                if not steps:
                    if self.mask_casual_methods():
                        self.casual_fallback_active = True
                        fallback_floor = len(placements)
                        continue
                    return False
                path.extend(steps)
                continue

            if buffer > high_buffer:
                item = self.pick_high_buffer_compatible_item(unplaced, reachable)
                location = (
                    self.pick_depth_weighted_location(item, reachable)
                    if item else None
                )
                if not item or not location:
                    if self.mask_casual_methods():
                        self.casual_fallback_active = True
                        fallback_floor = len(placements)
                        continue
                    return False
                path.append(
                    self.place_and_collect(
                        state,
                        item,
                        location,
                        unfilled,
                        unplaced,
                        placements,
                        entrance_lookup,
                        self.location_spheres[location.moon_id],
                        "buffer placement",
                    )
                )
                continue

            if buffer > low_buffer or remaining > expansion_backlog:
                steps = self.place_reachability_unlock(
                    state,
                    unfilled,
                    unplaced,
                    placements,
                    entrance_lookup,
                    int(frontier[0]),
                    kingdom,
                    frontier=frontier,
                )
                if steps:
                    path.extend(steps)
                    continue

            item = self.pick_required_moon_for_kingdom(
                state, kingdom, unplaced, reachable
            )
            location = (
                self.pick_depth_weighted_location(item, reachable)
                if item else None
            )
            if item and location:
                path.append(
                    self.place_and_collect(
                        state,
                        item,
                        location,
                        unfilled,
                        unplaced,
                        placements,
                        entrance_lookup,
                        self.location_spheres[location.moon_id],
                        "moon requirement",
                    )
                )
                continue

            steps = self.place_reachability_unlock(
                state,
                unfilled,
                unplaced,
                placements,
                entrance_lookup,
                int(frontier[0]),
                kingdom,
                frontier=frontier,
            )
            if not steps:
                if self.mask_casual_methods():
                    self.casual_fallback_active = True
                    fallback_floor = len(placements)
                    continue
                return False
            path.extend(steps)

    def collect_preplaced_reachable(
        self,
        state: LogicState,
        placements: list[Placement],
        entrance_lookup: dict[int, EntrancePlacement],
    ) -> None:
        placed = {
            placement.location_id: placement.item_id for placement in placements
        }
        changed = True
        while changed:
            changed = False
            remaining = set(placed) - state.collected_locations
            for location in self.reachable_locations(
                state, remaining, entrance_lookup
            ):
                state.collect_location(
                    location, self.world.moons[placed[location.moon_id]], self.world
                )
                changed = True
            state.refresh_reachable_events(self.world, self.reachable_nodes(state))

    def place_remaining_progress_evenly(
        self,
        state: LogicState,
        unfilled: set[str],
        unplaced: set[str],
        placements: list[Placement],
        path: list[PathStep],
        entrance_lookup: dict[int, EntrancePlacement],
        frontier: list[int | bool],
    ) -> bool:
        fallback_floor: int | None = None
        while any(item_id in self.progression_item_ids for item_id in unplaced):
            if self.casual_fallback_active and (
                fallback_floor is None or len(placements) > fallback_floor
            ):
                self.casual_fallback_active = False
                fallback_floor = None
            reachable = self.update_frontier_spheres(
                state, unfilled, entrance_lookup, frontier
            )
            if not reachable:
                if self.mask_casual_methods():
                    self.casual_fallback_active = True
                    fallback_floor = len(placements)
                    continue
                return False
            item = self.pick_random_compatible_item(
                unplaced, reachable, progression_only=True
            )
            placed_progress = item is not None
            placement_candidates = reachable
            if item is None:
                story_drivers = [
                    location for location in reachable
                    if self.is_story_driver(state, location, location.kingdom)
                ]
                if story_drivers:
                    placement_candidates = story_drivers
                item = self.pick_random_compatible_item(
                    unplaced, placement_candidates
                )
                if item is None and placement_candidates is not reachable:
                    placement_candidates = reachable
                    item = self.pick_random_compatible_item(
                        unplaced, placement_candidates
                    )
            location = (
                self.pick_even_location(item, placement_candidates, placements)
                if item else None
            )
            if not item or not location:
                if self.mask_casual_methods():
                    self.casual_fallback_active = True
                    fallback_floor = len(placements)
                    continue
                return False
            path.append(
                self.place_and_collect(
                    state,
                    item,
                    location,
                    unfilled,
                    unplaced,
                    placements,
                    entrance_lookup,
                    self.location_spheres[location.moon_id],
                    "progression tail" if placed_progress else "tail expansion",
                )
            )
            self.update_frontier_spheres(
                state, unfilled, entrance_lookup, frontier
            )
        return True

    def fill_remaining_evenly(
        self,
        unfilled: set[str],
        unplaced: set[str],
        placements: list[Placement],
    ) -> None:
        items = [self.world.moons[item_id] for item_id in sorted(unplaced)]
        self.rng.shuffle(items)
        for item in items:
            locations = [
                self.world.moons[location_id]
                for location_id in sorted(unfilled)
                if self.can_place_item_at_location(
                    item, self.world.moons[location_id]
                )
            ]
            location = self.pick_even_location(item, locations, placements)
            if not location:
                continue
            placements.append(Placement(location.moon_id, item.moon_id))
            unfilled.remove(location.moon_id)
            unplaced.remove(item.moon_id)
        if unfilled or unplaced:
            self.warnings.append(
                "Moon pool and location pool sizes did not match during even fill."
            )

    def buffer_managed_sphere_placement(
        self, entrances: list[EntrancePlacement]
    ) -> tuple[list[Placement], list[PathStep], bool, str]:
        state = LogicState(self.settings, self.world)
        unfilled = {
            location.moon_id for location in self.world.location_pool(self.settings)
        }
        unplaced = {
            item.moon_id for item in self.world.moon_pool(self.settings)
        }
        placements: list[Placement] = []
        path: list[PathStep] = []
        entrance_lookup = {
            placement.source_index: placement for placement in entrances
        }
        self.location_spheres.clear()
        frontier: list[int | bool] = [0, False]
        # The strict difficult-movement mask applies to construction only; it is
        # released below before validation, which must prove beatability with
        # every method the selected difficulty allows (mirrors MoonRando.cpp).
        self.difficult_fill_active = (
            self.settings.prioritize_difficult_movement
            and not self.settings.no_logic
            and self.settings.logic_difficulty != ROUTE_DIFFICULTY_CASUAL
        )
        self.casual_fallback_active = False
        self.apply_start_position(state, entrance_lookup)
        self.expand_stage_access(
            state, entrance_lookup, self.reachable_nodes(state)
        )

        success = True
        for tier_index, tier in enumerate(self.settings.kingdom_tiers):
            if not tier:
                continue
            if tier_index > 0 and not self.is_route_prefix_complete(state, tier_index - 1):
                self.warnings.append(
                    f"Route tier {tier_index - 1} was incomplete before tier {tier_index}."
                )
                success = False
                break

            if len(tier) == 2:
                base_state = state.copy()
                base_sphere = int(frontier[0])
                branch_states: list[LogicState] = []
                branch_frontiers: list[list[int | bool]] = []
                for kingdom in tier:
                    branch_state = base_state.copy()
                    self.unlock_kingdom(branch_state, kingdom)
                    self.expand_stage_access(branch_state, entrance_lookup)
                    self.collect_preplaced_reachable(
                        branch_state, placements, entrance_lookup
                    )
                    branch_frontier: list[int | bool] = [
                        base_sphere, bool(frontier[1])
                    ]
                    if not self.run_buffer_for_kingdom(
                        branch_state,
                        kingdom,
                        unfilled,
                        unplaced,
                        placements,
                        path,
                        entrance_lookup,
                        branch_frontier,
                    ):
                        self.warnings.append(
                            f"Buffer engine could not independently satisfy {kingdom}."
                        )
                        success = False
                        break
                    branch_states.append(branch_state)
                    branch_frontiers.append(branch_frontier)
                if not success:
                    break
                state = base_state
                for branch_state in branch_states:
                    self.merge_state_progress(state, branch_state)
                frontier[0] = max(
                    [base_sphere]
                    + [int(branch[0]) for branch in branch_frontiers]
                )
                frontier[1] = any(bool(branch[1]) for branch in branch_frontiers)
                continue

            kingdom = tier[0]
            self.unlock_kingdom(state, kingdom)
            self.expand_stage_access(state, entrance_lookup)
            self.collect_preplaced_reachable(state, placements, entrance_lookup)
            if not self.run_buffer_for_kingdom(
                state,
                kingdom,
                unfilled,
                unplaced,
                placements,
                path,
                entrance_lookup,
                frontier,
            ):
                self.warnings.append(f"Buffer engine could not satisfy {kingdom}.")
                success = False
                break

        if success and self.is_tier_complete(
            state, len(self.settings.kingdom_tiers) - 1
        ):
            self.unlock_kingdom(state, MUSHROOM_KINGDOM)
            self.expand_stage_access(state, entrance_lookup)
            self.collect_preplaced_reachable(state, placements, entrance_lookup)

        if success and not self.place_remaining_progress_evenly(
            state,
            unfilled,
            unplaced,
            placements,
            path,
            entrance_lookup,
            frontier,
        ):
            self.warnings.append(
                "One or more capture/ability unlocks lacked a reachable slot."
            )
            success = False

        self.fill_remaining_evenly(unfilled, unplaced, placements)

        # Construction is done: validation and the final replay evaluate with
        # every allowed method again, so a seed that needed the casual fallback
        # still validates as beatable.
        self.difficult_fill_active = False
        self.casual_fallback_active = False

        placed_by_location = {
            placement.location_id: placement.item_id for placement in placements
        }
        final_state = LogicState(self.settings, self.world)
        self.apply_start_position(final_state, entrance_lookup)
        remaining = set(placed_by_location)
        self.collect_reachable_to_fixed_point(
            final_state, remaining, placed_by_location, entrance_lookup
        )
        final_kingdom = (
            self.settings.kingdom_order[-1]
            if self.settings.kingdom_order else "Moon Kingdom"
        )
        final_requirement = self.settings.moon_requirements.get(final_kingdom, 0)
        all_unlocks_reachable = self.progression_item_ids.issubset(
            final_state.collected_items
        )
        final_objective_reached = success and self.is_final_objective_reachable(
            final_state, all_unlocks_reachable
        )
        if final_objective_reached:
            final_objective_note = (
                f"{final_kingdom} is reachable with its {final_requirement}-moon "
                f"requirement, {FINAL_OBJECTIVE_NODE_ID} is reachable, and every "
                "mandatory capture/ability unlock is proven reachable."
            )
        else:
            final_objective_note = (
                "Buffer-managed placement did not prove the final objective and "
                "every mandatory capture/ability unlock."
            )
            self.warnings.append(final_objective_note)

        if self.settings.entrance_rando:
            self.all_entrance_destinations_reachable(final_state, entrance_lookup)

        if final_objective_reached and not self.validate_final_placement(
            placements, entrance_lookup
        ):
            final_objective_reached = False
            final_objective_note = (
                "Independent forward validation rejected the completed placement."
            )
            self.warnings.append(final_objective_note)

        # No dedicated world-peace simulation here, by design (mirrors MoonRando.cpp):
        # the construction state can hold story beats collected out of story order
        # (no story credit, never re-collectable), so a post-fill peace sim rejects
        # beatable seeds over a modeling artifact. validate_final_placement's fresh
        # replay already proves every kingdom reachable, every capture/ability
        # collectable, and the chapel objective reachable — with those, world peace
        # completes naturally in story order in-game.

        return (
            placements,
            path,
            final_objective_reached,
            final_objective_note,
        )

    def collect_reachable_to_fixed_point(
        self,
        state: LogicState,
        remaining: set[str],
        placed_by_location: dict[str, str],
        entrance_lookup: dict[int, EntrancePlacement],
        allow_route_advance: bool = True,
    ) -> None:
        """Collect everything reachable, optionally advancing the route as it goes.

        Pass allow_route_advance=False when the caller is *proving* a specific
        tier's requirement. Otherwise the fixed point opens later kingdoms
        mid-proof and the requirement is then satisfied by moons that were only
        reachable after that advance — which is exactly how an unplayable route
        passes validation.
        """
        changed = True
        while changed:
            changed = False
            if allow_route_advance:
                self.advance_kingdom_order(state)
            reachable = self.reachable_locations(state, remaining, entrance_lookup)
            for location in reachable:
                item_id = placed_by_location.get(location.moon_id)
                if not item_id:
                    continue
                state.collect_location(location, self.world.moons[item_id], self.world)
                remaining.discard(location.moon_id)
                changed = True

    def validate_branch_order(
        self,
        placements: list[Placement],
        entrance_lookup: dict[int, EntrancePlacement],
        branch_mask: int,
    ) -> bool:
        state = LogicState(self.settings, self.world)
        self.apply_start_position(state, entrance_lookup)
        placed_by_location = {placement.location_id: placement.item_id for placement in placements}
        remaining = set(placed_by_location)
        branch_index = 0

        for tier_index, tier in enumerate(self.settings.kingdom_tiers):
            if tier_index > 0 and not self.is_route_prefix_complete(state, tier_index - 1):
                return False
            order = list(tier)
            if len(order) == 2:
                if (branch_mask >> branch_index) & 1:
                    order.reverse()
                branch_index += 1
            for kingdom in order:
                self.unlock_kingdom(state, kingdom)
                # This walk already unlocks each tier in order, so the fixed
                # point must not advance the route on its own: the requirement
                # below has to be proven against exactly the unlocks the game
                # would have granted at this point, and nothing further.
                self.collect_reachable_to_fixed_point(
                    state, remaining, placed_by_location, entrance_lookup,
                    allow_route_advance=False,
                )
                if not self.is_kingdom_requirement_met(state, kingdom):
                    return False

        self.collect_reachable_to_fixed_point(state, remaining, placed_by_location, entrance_lookup)
        self.unlock_kingdom(state, MUSHROOM_KINGDOM)
        self.collect_reachable_to_fixed_point(state, remaining, placed_by_location, entrance_lookup)
        if any(item_id not in state.collected_items for item_id in self.progression_item_ids):
            return False
        # No world-peace scenario check here, by design (mirrors MoonRando.cpp):
        # peace follows from the guarantees above plus in-game story ordering.
        return True

    def validate_final_placement(
        self,
        placements: list[Placement],
        entrance_lookup: dict[int, EntrancePlacement],
    ) -> bool:
        locations = self.world.location_pool(self.settings)
        items = self.world.moon_pool(self.settings)
        if len(placements) != len(locations) or len({p.location_id for p in placements}) != len(locations):
            return False
        if {p.item_id for p in placements} != {item.moon_id for item in items}:
            return False
        for placement in placements:
            if not self.can_place_item_at_location(
                self.world.moons[placement.item_id],
                self.world.moons[placement.location_id],
            ):
                return False

        branch_count = sum(1 for tier in self.settings.kingdom_tiers if len(tier) == 2)
        return all(
            self.validate_branch_order(placements, entrance_lookup, branch_mask)
            for branch_mask in range(1 << branch_count)
        )

    def place_reachability_unlock(
        self,
        state: LogicState,
        unfilled: set[str],
        unplaced: set[str],
        placements: list[Placement],
        entrance_lookup: dict[int, EntrancePlacement],
        sphere: int,
        kingdom: str,
        *,
        frontier: list[int | bool] | None = None,
    ) -> list[PathStep] | None:
        before = len(self.reachable_locations(state, unfilled, entrance_lookup))
        before_stages = len(state.accessible_stages)
        before_story = state.story_counts.get(kingdom, 0)
        relevant_tokens: set[str] = set()
        difficult_tokens: set[str] = set()

        def add_missing_route(edge: RouteEdge, current_scenario: int) -> None:
            relevant_tokens.update(
                missing_route_requirement_tokens(
                    edge,
                    current_scenario,
                    state,
                    self.world,
                    self.settings.logic_difficulty,
                    prioritize_difficult=self.mask_casual_methods(),
                    difficult_tokens=difficult_tokens,
                    reachable_nodes=reachable_nodes,
                )
            )

        reachable_nodes = self.reachable_nodes(state)
        for edge in self.world.edges:
            current_scenario = state.kingdom_scenarios.get(
                edge.kingdom,
                SCENARIO_START_BY_KINGDOM.get(edge.kingdom, 1),
            )
            if edge.scenario_mask and not scenario_mask_contains(edge.scenario_mask, current_scenario):
                continue
            if not any_option_scenario_allowed(edge.option_scenario_masks, current_scenario):
                continue
            if edge.from_node_id in reachable_nodes and edge.to_node_id not in reachable_nodes:
                add_missing_route(edge, current_scenario)
            if edge.bidirectional and edge.to_node_id in reachable_nodes and edge.from_node_id not in reachable_nodes:
                add_missing_route(edge, current_scenario)
        for location_id in unfilled:
            moon = self.world.moons[location_id]
            if moon.stage not in state.accessible_stages:
                continue
            current_scenario = state.kingdom_scenarios.get(
                moon.kingdom,
                SCENARIO_START_BY_KINGDOM.get(moon.kingdom, 1),
            )
            relevant_tokens.update(missing_route_requirement_tokens(
                self.world.moon_capture_edge(moon, True),
                current_scenario,
                state,
                self.world,
                self.settings.logic_difficulty,
                prioritize_difficult=self.mask_casual_methods(),
                difficult_tokens=difficult_tokens,
                reachable_nodes=reachable_nodes,
            ))
            relevant_tokens.update(missing_route_requirement_tokens(
                self.world.moon_capture_edge(moon, False),
                current_scenario,
                state,
                self.world,
                self.settings.logic_difficulty,
                prioritize_difficult=self.mask_casual_methods(),
                difficult_tokens=difficult_tokens,
                reachable_nodes=reachable_nodes,
            ))

        def supports_tokens(item: Moon, tokens: set[str]) -> bool:
            for candidate in [*item.capture_unlocks, *item.ability_unlocks]:
                for required in tokens:
                    token_name = required
                    for _ in range(len(self.world.tokens)):
                        if candidate == token_name:
                            return True
                        token = self.world.tokens.get(token_name)
                        if not token:
                            break
                        token_name = normalize_prerequisite(token.prerequisite)
                        if not token_name:
                            break
            return False

        candidates = [
            self.world.moons[item_id]
            for item_id in sorted(unplaced)
            if item_id in self.progression_item_ids
            and supports_tokens(self.world.moons[item_id], relevant_tokens)
        ]
        if not candidates:
            return None
        self.rng.shuffle(candidates)
        if self.difficult_fill_active:
            # Expand through harder methods first: candidates that supply a token
            # some Advanced/Expert alternative is missing get tried before ones
            # that only serve casual-only requirements. Stable within the shuffle
            # (mirrors MoonRando.cpp placeReachabilityUnlock ordering).
            candidates.sort(
                key=lambda item: 0 if supports_tokens(item, difficult_tokens) else 1
            )

        search_nodes = 0
        for bundle_size in range(1, min(3, len(candidates)) + 1):
            for bundle in combinations(candidates, bundle_size):
                search_nodes += 1
                if search_nodes > 8192:
                    self.warnings.append("Progression bundle search exhausted its 8192-node budget.")
                    return None

                simulated = state.copy()
                simulated_unfilled = set(unfilled)
                chosen: list[tuple[Moon, Moon]] = []
                for item in bundle:
                    now_reachable = self.reachable_locations(simulated, simulated_unfilled, entrance_lookup)
                    compatible = [
                        location
                        for location in now_reachable
                        if self.can_place_item_at_location(item, location)
                    ]
                    if not compatible:
                        break
                    location = self.pick_depth_weighted_location(
                        item, compatible
                    )
                    if not location:
                        break
                    simulated.collect_location(location, item, self.world)
                    simulated_unfilled.remove(location.moon_id)
                    chosen.append((item, location))

                if len(chosen) != bundle_size:
                    continue
                after = len(self.reachable_locations(simulated, simulated_unfilled, entrance_lookup))
                useful = (
                    len(simulated.accessible_stages) > before_stages
                    or after > before
                    or simulated.story_counts.get(kingdom, 0) > before_story
                )
                if not useful:
                    continue

                steps: list[PathStep] = []
                for item, location in chosen:
                    if frontier is not None:
                        self.update_frontier_spheres(
                            state, unfilled, entrance_lookup, frontier
                        )
                    placement_sphere = self.location_spheres.get(
                        location.moon_id,
                        int(frontier[0]) if frontier is not None else sphere,
                    )
                    steps.append(
                        self.place_and_collect(
                            state,
                            item,
                            location,
                            unfilled,
                            unplaced,
                            placements,
                            entrance_lookup,
                            placement_sphere,
                            "unlock" if bundle_size == 1 else "unlock bundle",
                        )
                    )
                return steps

        return None

    def place_and_collect(
        self,
        state: LogicState,
        item: Moon,
        location: Moon,
        unfilled: set[str],
        unplaced: set[str],
        placements: list[Placement],
        entrance_lookup: dict[int, EntrancePlacement],
        sphere: int,
        reason: str,
    ) -> PathStep:
        placements.append(Placement(location.moon_id, item.moon_id))
        unfilled.remove(location.moon_id)
        unplaced.remove(item.moon_id)
        unlocked = state.collect_location(location, item, self.world)
        self.expand_stage_access(state, entrance_lookup, self.reachable_nodes(state))
        unlocked.extend(state.refresh_reachable_events(self.world, self.reachable_nodes(state)))
        return PathStep(location.moon_id, item.moon_id, reason, sphere, unlocked)

    def is_kingdom_requirement_met(self, state: LogicState, kingdom: str) -> bool:
        if (
            kingdom not in state.accessible_kingdoms
            or state.moon_counts.get(kingdom, 0)
            < self.settings.moon_requirements.get(kingdom, 0)
        ):
            return False
        return (
            not self.requires_route_story_completion(kingdom)
            or state.story_counts.get(kingdom, 0)
            >= self.world.story_totals.get(kingdom, 0)
        )

    def is_tier_complete(self, state: LogicState, tier_index: int) -> bool:
        if tier_index < 0 or tier_index >= len(self.settings.kingdom_tiers):
            return False
        return all(self.is_kingdom_requirement_met(state, kingdom) for kingdom in self.settings.kingdom_tiers[tier_index])

    def is_route_prefix_complete(self, state: LogicState, tier_index: int) -> bool:
        """Is every tier from 0 through tier_index done?

        The route is a chain, so this — not is_tier_complete on the single
        preceding tier — is what gates opening the tier after it. Checking one
        link lets an Entrance Rando door bridge into the middle of the route:
        a kingdom reached early through a loading zone accumulates its moons
        normally (which is legitimate), and its full counter would then open the
        NEXT tier even though an earlier one, including the starting kingdom, is
        still short. The game cannot do that — KingdomProgress advances strictly
        by route index and tryCompleteFrontier only ever completes the kingdom
        the player is standing in once it is the current frontier.
        """
        return all(
            self.is_tier_complete(state, index) for index in range(tier_index + 1)
        )

    def merge_state_progress(self, destination: LogicState, source: LogicState) -> None:
        destination.accessible_kingdoms.update(source.accessible_kingdoms)
        destination.accessible_stages.update(source.accessible_stages)
        destination.accessible_nodes.update(source.accessible_nodes)
        destination.accessible_sub_entries.update(source.accessible_sub_entries)
        destination.tokens.update(source.tokens)

        for kingdom, scenario in source.kingdom_scenarios.items():
            if scenario > destination.kingdom_scenarios.get(kingdom, 0):
                destination.kingdom_scenarios[kingdom] = scenario
        for kingdom, story_count in source.story_counts.items():
            if story_count > destination.story_counts.get(kingdom, 0):
                destination.story_counts[kingdom] = story_count

        destination.collected_locations.update(source.collected_locations)
        for item_id in source.collected_items - destination.collected_items:
            item = self.world.moons.get(item_id)
            destination.collected_items.add(item_id)
            if item:
                credited_kingdom = item.credit_kingdom
                destination.moon_counts[credited_kingdom] = (
                    destination.moon_counts.get(credited_kingdom, 0)
                    + item.moon_value
                )

    def advance_kingdom_order(self, state: LogicState) -> None:
        changed = True
        while changed:
            changed = False
            for tier_index, tier in enumerate(self.settings.kingdom_tiers):
                if not tier:
                    continue

                if tier_index == 0:
                    if len(tier) == 1 and tier[0] not in state.accessible_kingdoms:
                        self.unlock_kingdom(state, tier[0])
                        changed = True
                    continue

                if not self.is_route_prefix_complete(state, tier_index - 1):
                    continue

                if len(tier) == 1:
                    if tier[0] not in state.accessible_kingdoms:
                        self.unlock_kingdom(state, tier[0])
                        changed = True
                    continue

                if len(tier) != 2:
                    continue

                first, second = tier
                if first in state.accessible_kingdoms and self.is_kingdom_requirement_met(state, first) and second not in state.accessible_kingdoms:
                    self.unlock_kingdom(state, second)
                    changed = True
                if second in state.accessible_kingdoms and self.is_kingdom_requirement_met(state, second) and first not in state.accessible_kingdoms:
                    self.unlock_kingdom(state, first)
                    changed = True

    def unlock_kingdom(self, state: LogicState, kingdom: str) -> None:
        state.grant_kingdom_access(kingdom, self.world)
        stage = MAIN_STAGE_BY_KINGDOM.get(kingdom)
        if stage:
            self.add_accessible_stage(state, stage)

    def add_accessible_stage(self, state: LogicState, stage: str) -> None:
        state.accessible_stages.add(stage)
        room = self.world.rooms.get(stage)
        if room:
            state.grant_kingdom_access(room.kingdom, self.world)
            main_stage = MAIN_STAGE_BY_KINGDOM.get(room.kingdom)
            if main_stage:
                state.accessible_stages.add(main_stage)

    def add_accessible_node(self, state: LogicState, node_id: str) -> None:
        node = self.world.nodes.get(node_id)
        if not node:
            return
        state.accessible_nodes.add(node_id)
        self.add_accessible_stage(state, node.stage)

    # ── Randomized Starting Position ────────────────────────────────────────
    # Mirrors seedStartNode / canStartAtNode / applyRandomizedStartPosition in
    # MoonRando.cpp. See logic/start_position.py for candidate enumeration.

    def place_player_at_node(self, state: LogicState, node_id: str) -> None:
        """Mark a node and its stage accessible, and nothing more.

        Deliberately NOT add_accessible_node: that routes through
        add_accessible_stage, which also hands over the stage's kingdom and that
        kingdom's main overworld. Every ordinary caller has already walked in
        from somewhere, so the promotion is harmless there - but standing inside
        a sub-area is not the same as being able to reach the kingdom it happens
        to live in, and granting that to a randomized start would over-estimate
        reachability and let the fill build unbeatable seeds. Matches
        MoonRando::addAccessibleStageWithTrace, which only marks a kingdom
        accessible when the stage IS that kingdom's main stage.
        """
        node = self.world.nodes.get(node_id)
        if not node:
            return

        state.accessible_nodes.add(node_id)
        if node.stage:
            state.accessible_stages.add(node.stage)
            if node.stage in MAIN_STAGE_BY_KINGDOM.values():
                room = self.world.rooms.get(node.stage)
                if room:
                    state.grant_kingdom_access(room.kingdom, self.world)

    def seed_start_node(self, state: LogicState, node_id: str) -> None:
        """Put the player at ``node_id``: the node itself, plus - when it sits
        inside a sub-area - that sub-area's doors as available ways out.

        ``accessible_sub_entries`` normally records which side of a sub-area you
        entered from, which is what lets a chain leave through the *other* door.
        A player who simply starts in the room has both of its doors physically
        available to him however he got there, so record both. Route
        requirements still decide whether he can walk to either one.
        """
        node = self.world.nodes.get(node_id)
        if not node:
            return

        self.place_player_at_node(state, node_id)
        for link in self.world.entrances:
            if link.sub_stage == node.stage:
                state.accessible_sub_entries.add((link.link_index, "near"))
                state.accessible_sub_entries.add((link.link_index, "far"))

    def opening_walk_state(
        self,
        node_id: str,
        entrance_lookup: dict[int, EntrancePlacement],
        base_state: LogicState | None = None,
    ) -> tuple[LogicState, set[str]]:
        """The state reachable from ``node_id`` with nothing but the starting
        equipment, plus its reachable-node set."""
        opening = base_state.copy() if base_state is not None else LogicState(self.settings, self.world)
        # MUST wipe physical position first. The LogicState constructor grants
        # settings.starting_kingdoms, whose main stage is exactly the stage
        # can_start_at_node asks about - so without this the answer is "yes"
        # before the start node is even seeded and every candidate passes.
        # Tokens, scenarios and collected state stay: those are what he owns,
        # not where he is standing. Mirrors MoonRando::canStartAtNode.
        opening.accessible_kingdoms = set()
        opening.accessible_stages = set()
        opening.accessible_nodes = set()
        opening.accessible_sub_entries = set()
        self.seed_start_node(opening, node_id)

        while True:
            before = set(opening.accessible_stages)
            self.expand_stage_access(opening, entrance_lookup, self.reachable_nodes(opening))
            reachable_nodes = self.expand_cross_stage_route_access(opening)
            event_unlocks = opening.refresh_reachable_events(self.world, reachable_nodes)
            if before == opening.accessible_stages and not event_unlocks:
                break
        return opening, reachable_nodes

    def can_start_at_node(
        self, node_id: str, entrance_lookup: dict[int, EntrancePlacement], required_stage: str
    ) -> bool:
        opening, _ = self.opening_walk_state(node_id, entrance_lookup)
        return required_stage in opening.accessible_stages

    def stages_linked_from_kingdom(
        self, kingdom: str, entrance_lookup: dict[int, EntrancePlacement]
    ) -> set[str]:
        """Stages reachable FROM ``kingdom``'s overworld with only the starting
        equipment - the sub-area chains that actually hang off that kingdom's own
        doors under this seed's shuffle.

        This is the forward direction, and it is what bounds the Max start pool.
        It is not the same as "a path back to the kingdom exists": with entrances
        shuffled the two can pick out completely different sets of sub-areas.
        Mirrors MoonRando::computeStagesLinkedFromKingdom.
        """
        state = LogicState(self.settings, self.world)
        # Only this kingdom's overworld: the constructor may unlock others
        # (Cascade, or every kingdom under all_kingdoms_start_unlocked), and their
        # chains are not "linked from" the starting overworld.
        state.accessible_kingdoms = set()
        state.accessible_stages = set()
        state.accessible_nodes = set()
        state.accessible_sub_entries = set()
        self.unlock_kingdom(state, kingdom)

        while True:
            before = set(state.accessible_stages)
            self.expand_stage_access(state, entrance_lookup, self.reachable_nodes(state))
            reachable_nodes = self.expand_cross_stage_route_access(state)
            event_unlocks = state.refresh_reachable_events(self.world, reachable_nodes)
            if before == state.accessible_stages and not event_unlocks:
                break
        return set(state.accessible_stages)

    def choose_start_position(
        self, entrance_lookup: dict[int, EntrancePlacement]
    ) -> str | None:
        """Pick the start node for this seed, or None for a vanilla start."""
        self.start_node = None
        mode = normalize_start_position_mode(self.settings.start_position_mode)
        # "odyssey" starts inside the ship, which is not a logic node and whose
        # only exit is the vanilla drop-off point - nothing for the logic to seed.
        if mode in NODELESS_START_POSITION_MODES:
            return None

        start_kingdom = (
            self.settings.kingdom_order[0]
            if self.settings.kingdom_order
            else "Cap Kingdom"
        )
        main_stage = MAIN_STAGE_BY_KINGDOM.get(start_kingdom)
        if not main_stage:
            return None

        anchors = load_node_anchors()
        linked_stages = (
            self.stages_linked_from_kingdom(start_kingdom, entrance_lookup)
            if mode == "max"
            else set()
        )
        candidates = collect_start_position_candidates(
            self.world,
            anchors,
            mode,
            main_stage,
            set(MAIN_STAGE_BY_KINGDOM.values()),
            linked_stages,
        )
        if not candidates:
            self.warnings.append(
                f"Randomized Starting Position found no candidate nodes in {start_kingdom}."
            )
            return None

        ordered = shuffled_start_position_candidates(
            candidates, self.settings.seed, self.settings.start_position_attempt, Rng
        )
        # Accept the first candidate that proves its way back out. Each probe is a
        # full fixed point, so cap the work the way the runtime does rather than
        # walking a pool of 1300 on a hopeless configuration.
        for node_id in ordered[:START_POSITION_MAX_PROBES]:
            if node_id is None:
                return None  # the Odyssey drop-off won the roll
            if self.can_start_at_node(node_id, entrance_lookup, main_stage):
                self.start_node = node_id
                return node_id

        self.warnings.append(
            f"Randomized Starting Position: no candidate could reach the {start_kingdom} "
            f"overworld; using the vanilla start."
        )
        return None

    def apply_start_position(
        self, state: LogicState, entrance_lookup: dict[int, EntrancePlacement]
    ) -> None:
        """Seed the start node into a freshly built ``state``.

        The start node is permanently reachable: the map's Return to Start warp
        can always put the player back on it, exactly like a checkpoint flag.
        (This replaced an earlier "temporary opening access" model - without a
        way back, standing somewhere at t=0 would not have let the player return
        with items collected elsewhere.)
        """
        del entrance_lookup  # kept for symmetry with the rest of the fill API
        if self.start_node:
            self.seed_start_node(state, self.start_node)

    def expand_stage_access(
        self,
        state: LogicState,
        entrance_lookup: dict[int, EntrancePlacement],
        reachable_nodes: set[str] | None = None,
    ) -> None:
        changed = True
        by_index = {link.link_index: link for link in self.world.entrances}
        if reachable_nodes is None:
            reachable_nodes = self.reachable_nodes(state)
        while changed:
            changed = False
            for entrance in self.world.entrances:
                if entrance.status == "excluded":
                    continue

                mapped = entrance_lookup.get(entrance.link_index)
                if not mapped:
                    continue
                destination = by_index.get(mapped.destination_index)
                if not destination:
                    continue

                for source_side in self.reachable_home_entrance_sides(entrance, state, reachable_nodes):
                    changed |= self.add_entrance_destination(
                        state,
                        source=entrance,
                        destination=destination,
                        from_far_side=source_side == "home_out",
                        is_home_warp=mapped.is_home_warp,
                    )

            if self.settings.entrance_rando:
                for source in self.world.entrances:
                    if source.status == "excluded":
                        continue
                    if not self.can_use_subarea_forward_exit(source, state, reachable_nodes):
                        continue

                    mapped = entrance_lookup.get(source.link_index)
                    if not mapped:
                        continue
                    destination = by_index.get(mapped.destination_index)
                    if not destination:
                        continue

                    changed |= self.add_entrance_destination(
                        state,
                        source=source,
                        destination=destination,
                        from_far_side=False,
                        is_home_warp=mapped.is_home_warp,
                    )

            # Fixed (vanilla / non-shuffled) doors are physically two-way: once the
            # inside of a subarea is reachable, the player can walk back out to the
            # door's own home-side nodes. The editor records this as the "Entrance/
            # return" pair (home_in + home_out), but the engine previously only ever
            # propagated home->sub. Without this, a home-side node reachable only by
            # emerging from a subarea (e.g. the inverted-pyramid top, SandWorld's
            # new_place_2) was unreachable. Shuffled 'active' doors route their exit
            # through the entrance-rando block above, so they are skipped here.
            if reachable_nodes is not None:
                for link in self.world.entrances:
                    if link.status == "excluded":
                        continue
                    if link.status == "active" and self.settings.entrance_rando:
                        continue
                    if self.settings.entrance_rando and is_ice_cave_fixed_return_alias(link):
                        # This row describes the entrance pipe's coupled return
                        # half. It must not independently expose a second
                        # home-side destination in randomized logic.
                        continue
                    before_nodes = set(state.accessible_nodes)
                    # Preserve the physical side pairing for linear fixed rooms:
                    # near returns to home-in and far returns to home-out. Crediting
                    # both home nodes from either sub node bypasses the room's
                    # authored traversal requirement (notably Top-Hat Tower).
                    paired_returns = (
                        (link.sub_near_node_id, link.home_in_node_id),
                        (link.sub_far_node_id, link.home_out_node_id),
                    )
                    for sub_node, home_node in paired_returns:
                        if (
                            sub_node
                            and sub_node in reachable_nodes
                            and home_node
                            and home_node in self.world.nodes
                        ):
                            self.add_accessible_node(state, home_node)
                    changed |= before_nodes != state.accessible_nodes

            if reachable_nodes is not None:
                for painting in self.world.painting_links:
                    if painting.status == "excluded":
                        continue
                    if self.can_use_painting_node(painting.source_node_id, state, reachable_nodes):
                        before_stages = set(state.accessible_stages)
                        before_nodes = set(state.accessible_nodes)
                        if painting.destination_node_id in self.world.nodes:
                            self.add_accessible_node(state, painting.destination_node_id)
                        else:
                            dest_stage = painting.destination_node_id.split("::")[0] if "::" in painting.destination_node_id else painting.destination_node_id
                            if dest_stage:
                                self.add_accessible_stage(state, dest_stage)
                        changed |= before_stages != state.accessible_stages or before_nodes != state.accessible_nodes
                    if painting.bidirectional and self.can_use_painting_node(painting.destination_node_id, state, reachable_nodes):
                        before_stages = set(state.accessible_stages)
                        before_nodes = set(state.accessible_nodes)
                        if painting.source_node_id in self.world.nodes:
                            self.add_accessible_node(state, painting.source_node_id)
                        else:
                            src_stage = painting.source_node_id.split("::")[0] if "::" in painting.source_node_id else painting.source_node_id
                            if src_stage:
                                self.add_accessible_stage(state, src_stage)
                        changed |= before_stages != state.accessible_stages or before_nodes != state.accessible_nodes

            if changed and reachable_nodes is not None:
                reachable_nodes = self.reachable_nodes(state)

    def add_entrance_destination(
        self,
        state: LogicState,
        source: EntranceLink,
        destination: EntranceLink,
        from_far_side: bool,
        is_home_warp: bool,
    ) -> bool:
        before_stages = set(state.accessible_stages)
        before_nodes = set(state.accessible_nodes)
        before_entries = set(state.accessible_sub_entries)

        if is_home_warp:
            use_out_side = bool(destination.home_out_node_id)
            arrival_node = destination.home_out_node_id if use_out_side else destination.home_in_node_id

            if arrival_node and self.is_node_active(arrival_node, state) and (
                self.entrance_node_usable_in_scenario(arrival_node, state)
                or self.home_arrival_places_player_at_entrance(destination, use_out_side, state)
            ):
                self.add_accessible_node(state, arrival_node)
            else:
                # The entrance is missing this scenario and its fallback hands the
                # player to the Odyssey, so only the stage's default start is proven.
                self.add_accessible_stage(state, destination.home_stage)

            return before_stages != state.accessible_stages or before_nodes != state.accessible_nodes

        arrival_node = destination.sub_far_node_id if from_far_side else destination.sub_near_node_id
        if arrival_node:
            if not self.is_node_active(arrival_node, state):
                return before_stages != state.accessible_stages or before_nodes != state.accessible_nodes
            self.add_accessible_node(state, arrival_node)
            if arrival_node not in state.accessible_nodes:
                return before_stages != state.accessible_stages or before_nodes != state.accessible_nodes
        elif destination.sub_stage:
            self.add_accessible_stage(state, destination.sub_stage)

        entry_side = "far" if from_far_side else "near"
        state.accessible_sub_entries.add((destination.link_index, entry_side))

        return (
            before_stages != state.accessible_stages
            or before_nodes != state.accessible_nodes
            or before_entries != state.accessible_sub_entries
        )

    def can_use_entrance(self, entrance: EntranceLink, state: LogicState, reachable_nodes: set[str]) -> bool:
        return bool(self.reachable_home_entrance_sides(entrance, state, reachable_nodes))

    def is_entrance_randomizable(self, entrance: EntranceLink) -> bool:
        if entrance.status != "active":
            return False
        return self.entrance_has_valid_home_side(entrance) and self.entrance_has_valid_sub_side(entrance)

    def entrance_has_valid_home_side(self, entrance: EntranceLink) -> bool:
        candidates = [entrance.home_in_node_id]
        if (
            entrance.home_entrance_out
            and entrance.home_entrance_out != entrance.home_entrance_in
            and not is_ice_cave_exit_only_home_side(entrance, "home_out")
        ):
            candidates.append(entrance.home_out_node_id)
        return any(self.node_can_exist_in_logic_scenarios(node_id) for node_id in candidates)

    def entrance_has_valid_sub_side(self, entrance: EntranceLink) -> bool:
        candidates = [entrance.sub_near_node_id]
        if entrance.sub_entrance_out and entrance.sub_entrance_out != entrance.sub_entrance_in:
            candidates.append(entrance.sub_far_node_id)
        return any(self.node_can_exist_in_logic_scenarios(node_id) for node_id in candidates)

    def node_can_exist_in_logic_scenarios(self, node_id: str) -> bool:
        if not node_id:
            return True
        node = self.world.nodes.get(node_id)
        if not node or node.status == "excluded":
            return node is None

        scenarios = scenario_mask(node.scenario_indices)
        if not scenarios:
            return True

        room = self.world.rooms.get(node.stage)
        if not room:
            return True

        floor = node_scenario_floor_for_kingdom(node.node_type, room.kingdom, node.scenario_indices)
        if floor is not None:
            max_scenario = SCENARIO_MAX_LOGIC_BY_KINGDOM.get(room.kingdom)
            return max_scenario is None or floor <= max_scenario

        max_scenario = SCENARIO_MAX_LOGIC_BY_KINGDOM.get(room.kingdom)
        if max_scenario is None:
            return any(
                not is_scenario_blocked(room.kingdom, scenario)
                for scenario in range(32)
                if scenario_mask_contains(scenarios, scenario)
            )
        return any(
            not is_scenario_blocked(room.kingdom, scenario)
            for scenario in range(max_scenario + 1)
            if scenario_mask_contains(scenarios, scenario)
        )

    def entrance_node_usable_in_scenario(self, node_id: str, state: LogicState) -> bool:
        """Whether an entrance's loading-zone object physically exists in the
        kingdom's CURRENT scenario, so it can be *used to leave*.

        This is the only place node scenario data gates traversal. Plain nodes,
        checkpoints, and arriving at a node (spawning in from another subarea)
        are never gated by scenario: you can be standing on a loading zone that
        does not yet exist as an exit, you just can't take it out of the kingdom
        until its scenario arrives. Mirrors node_can_exist_in_logic_scenarios
        (which asks "ever") but resolves against the live kingdom scenario.
        Empty/absent scenario data => always usable."""
        if not node_id:
            return True
        node = self.world.nodes.get(node_id)
        if not node or node.status == "excluded":
            return True
        scenarios = scenario_mask(node.scenario_indices)
        if not scenarios:
            return True
        room = self.world.rooms.get(node.stage)
        if not room:
            return True
        current = state.kingdom_scenarios.get(
            room.kingdom,
            SCENARIO_START_BY_KINGDOM.get(room.kingdom, 1),
        )
        floor = node_scenario_floor_for_kingdom(node.node_type, room.kingdom, node.scenario_indices)
        if floor is not None:
            return current >= floor
        return scenario_mask_contains(scenarios, current)

    def home_arrival_places_player_at_entrance(
        self,
        destination: EntranceLink,
        use_out_side: bool,
        state: LogicState,
    ) -> bool:
        """Whether ARRIVING at a home-side entrance that is absent in the target
        kingdom's current scenario still leaves Mario standing on it.

        EntranceFallbacks.h authors this per entrance and per scenario: an
        explicit vanilla override, forced coordinates, or a generated fake
        pipe/moon pipe all place him at the loading zone, so its node is
        reached. Only Mode_Odyssey - and an unauthored entrance, which the
        runtime resolves the same way - actually costs him the node.

        This is strictly about arriving. Using a loading zone to *leave* stays
        gated by entrance_node_usable_in_scenario alone: a pipe generated for
        arrivals is not an exit back out. Mirrors MoonRando.cpp's
        homeArrivalPlacesPlayerAtEntrance.
        """
        room = self.world.rooms.get(destination.home_stage)
        if not room:
            return False
        scenario = state.kingdom_scenarios.get(
            room.kingdom,
            SCENARIO_START_BY_KINGDOM.get(room.kingdom, 1),
        )
        change_id = (
            destination.home_entrance_out if use_out_side else destination.home_entrance_in
        )
        return arrival_places_player_at_entrance(
            destination.home_stage, change_id, scenario
        )

    def subarea_key_db(self) -> "subarea_keys.SubareaKeyDB":
        db = getattr(self, "_subarea_key_db", None)
        if db is None:
            db = subarea_keys.SubareaKeyDB()
            self._subarea_key_db = db
        return db

    def can_enter_subarea_link(self, state: LogicState, entrance: EntranceLink) -> bool:
        """Mirror of MoonRando::canEnterSubareaLink.

        Keys on the link's HOME side - the door the player physically touches -
        so entrance rando never changes which key a door asks for. Only entering
        is gated; emerging stays free, or a player dropped inside a sub-area by
        the shuffle would be stranded there.
        """
        mode = self.settings.subarea_unlock_mode
        if mode == subarea_keys.MODE_UNLOCKED:
            return True
        slot = self.subarea_key_db().slot_for_link(mode, entrance.link_index)
        if slot < 0:
            return True
        carrier = self.settings.subarea_key_items.get(slot)
        if carrier is None:
            return True
        return carrier in state.collected_items

    def reachable_home_entrance_sides(
        self,
        entrance: EntranceLink,
        state: LogicState,
        reachable_nodes: set[str] | None,
    ) -> list[str]:
        if is_ice_cave_fixed_return_alias(entrance):
            return []
        if entrance.home_stage not in state.accessible_stages:
            return []
        if not self.can_enter_subarea_link(state, entrance):
            return []

        candidates = [("home_in", entrance.home_in_node_id)]
        if (
            entrance.home_entrance_out
            and entrance.home_entrance_out != entrance.home_entrance_in
            and not is_ice_cave_exit_only_home_side(entrance, "home_out")
        ):
            candidates.append(("home_out", entrance.home_out_node_id))

        if not self.world.stage_has_routes.get(entrance.home_stage, False):
            return [
                side
                for side, node_id in candidates
                if (not node_id or self.is_node_active(node_id, state))
                and self.entrance_node_usable_in_scenario(node_id, state)
            ]

        if reachable_nodes is None:
            return []
        if not any(node_id for _, node_id in candidates):
            return ["home_in"]
        return [
            side
            for side, node_id in candidates
            if node_id
            and node_id in reachable_nodes
            and self.entrance_node_usable_in_scenario(node_id, state)
        ]

    def can_use_subarea_forward_exit(
        self,
        entrance: EntranceLink,
        state: LogicState,
        reachable_nodes: set[str] | None,
    ) -> bool:
        if is_ice_cave_fixed_return_alias(entrance):
            return False
        if entrance.sub_entrance_in == entrance.sub_entrance_out:
            return False
        if entrance.sub_stage not in state.accessible_stages:
            return False

        exit_nodes = [entrance.sub_far_node_id]
        if (entrance.link_index, "far") in state.accessible_sub_entries:
            exit_nodes.append(entrance.sub_near_node_id)

        if not self.world.stage_has_routes.get(entrance.sub_stage, False):
            return any(
                (not node_id or self.is_node_active(node_id, state))
                and self.entrance_node_usable_in_scenario(node_id, state)
                for node_id in exit_nodes
            )

        return bool(
            reachable_nodes is not None
            and any(
                node_id
                and node_id in reachable_nodes
                and self.entrance_node_usable_in_scenario(node_id, state)
                for node_id in exit_nodes
            )
        )

    def all_entrance_destinations_reachable(
        self,
        state: LogicState,
        entrance_lookup: dict[int, EntrancePlacement],
    ) -> bool:
        proof_state = self.entrance_proof_state()
        self.expand_stage_access(proof_state, entrance_lookup, self.reachable_nodes(proof_state))
        destinations = {
            placement.destination_index
            for placement in entrance_lookup.values()
            if not placement.is_home_warp
        }
        missing = [
            destination
            for destination in destinations
            if (destination, "near") not in proof_state.accessible_sub_entries
            and (destination, "far") not in proof_state.accessible_sub_entries
        ]
        if not missing:
            return True

        sample = ", ".join(str(index) for index in sorted(missing)[:5])
        suffix = "..." if len(missing) > 5 else ""
        self.warnings.append(
            f"Could not prove every randomized entrance destination reachable from an overworld entrance "
            f"({len(missing)} missing: {sample}{suffix})."
        )
        return False

    def entrance_proof_state(self) -> LogicState:
        state = LogicState(self.settings, self.world)
        state.tokens.update(self.world.tokens)
        for kingdom in WORLD_IDS:
            state.accessible_kingdoms.add(kingdom)
            state.mark_kingdom_reached(kingdom)
            stage = MAIN_STAGE_BY_KINGDOM.get(kingdom)
            if stage:
                state.accessible_stages.add(stage)
            max_scenario = SCENARIO_MAX_LOGIC_BY_KINGDOM.get(kingdom)
            if max_scenario is not None:
                state.kingdom_scenarios[kingdom] = max_scenario
        return state

    def can_use_painting_node(self, node_id: str, state: LogicState, reachable_nodes: set[str]) -> bool:
        # Paintings are always enabled regardless of scenario or stage accessibility.
        # The only gate is cloud_kingdom_moons_unlock_paintings, which is not yet
        # modeled per-painting in the logic (the runtime handles it correctly).
        if node_id not in self.world.nodes:
            return True
        return node_id in reachable_nodes and self.is_node_active(node_id, state)

    def expand_cross_stage_route_access(self, state: LogicState) -> set[str]:
        reachable_nodes = self.reachable_nodes(state)
        reached_stages = {
            self.world.nodes[node_id].stage
            for node_id in reachable_nodes
            if node_id in self.world.nodes and self.world.nodes[node_id].stage
        }
        for stage in reached_stages - state.accessible_stages:
            self.add_accessible_stage(state, stage)
        return reachable_nodes

    def reachable_locations(
        self,
        state: LogicState,
        unfilled: set[str],
        entrance_lookup: dict[int, EntrancePlacement],
    ) -> list[Moon]:
        while True:
            before = set(state.accessible_stages)
            self.expand_stage_access(state, entrance_lookup, self.reachable_nodes(state))
            reachable_nodes = self.expand_cross_stage_route_access(state)
            event_unlocks = state.refresh_reachable_events(self.world, reachable_nodes)
            if before == state.accessible_stages and not event_unlocks:
                break

        result = []
        for location_id in unfilled:
            moon = self.world.moons[location_id]
            if not self.is_location_reachable(moon, state, reachable_nodes):
                continue
            result.append(moon)
        result.sort(key=lambda moon: (moon.kingdom, moon.stage, moon.moon_name, moon.moon_id))
        return result

    def reachable_nodes(self, state: LogicState) -> set[str]:
        reachable: set[str] = set()
        queue: list[str] = []

        for node_id in state.accessible_nodes:
            if node_id in self.world.nodes and self.is_node_active(node_id, state):
                reachable.add(node_id)
                queue.append(node_id)

        for stage in state.accessible_stages:
            start = f"{stage}::start"
            if start in self.world.nodes and self.should_seed_stage_start(stage) and self.is_node_active(start, state):
                reachable.add(start)
                queue.append(start)

            for node in self.world.nodes.values():
                if (
                    node.stage == stage
                    and node.status != "excluded"
                    and node.node_type == "stage_start"
                    and self.should_seed_stage_start(stage)
                    and self.is_node_active(node.node_id, state)
                ):
                    if node.node_id not in reachable:
                        reachable.add(node.node_id)
                        queue.append(node.node_id)

            if not self.world.stage_has_routes.get(stage, False):
                # A stage with no authored routes is locally free once its stage
                # access has been proven. No Logic does not invent topology.
                for node in self.world.nodes.values():
                    if node.stage == stage and node.status != "excluded" and self.is_node_active(node.node_id, state):
                        reachable.add(node.node_id)
            # In a routed stage only the stage_start (seeded above), arrival nodes
            # (accessible_nodes) and edge-reachable nodes count. A node with no incoming
            # edge stays unreachable ("impossible"), matching the C++ runtime
            # (MoonRando.computeReachableNodes seeds only stage_start).

        # Source nodes holding an edge whose only unmet requirement is node access.
        # They are revisited rather than dropped: reaching more nodes can open those
        # edges later in the same sweep, so reachability is a monotone fixed point
        # instead of a single BFS pass.
        deferred: set[str] = set()

        def relax(node_id: str) -> None:
            for edge in self.world.edges_by_source.get(node_id, []):
                if edge.status == "excluded":
                    continue
                current_scenario = state.kingdom_scenarios.get(
                    edge.kingdom,
                    SCENARIO_START_BY_KINGDOM.get(edge.kingdom, 1),
                )
                if edge.scenario_mask != 0 and not scenario_mask_contains(
                    edge.scenario_mask, current_scenario
                ):
                    continue
                if edge.requirement.mode == "impossible":
                    continue
                if self.settings.no_logic:
                    if not any_option_scenario_allowed(edge.option_scenario_masks, current_scenario):
                        continue
                elif not route_requirement_satisfied(
                    edge,
                    state,
                    self.world,
                    self.settings.logic_difficulty,
                    current_scenario,
                    prioritize_difficult=self.mask_casual_methods(),
                    reachable_nodes=reachable,
                ):
                    if edge.requirement.has_node_requirements:
                        deferred.add(node_id)
                    continue
                if edge.from_node_id == node_id:
                    next_node_id = edge.to_node_id
                elif edge.bidirectional and edge.to_node_id == node_id:
                    next_node_id = edge.from_node_id
                else:
                    continue
                if not self.is_node_active(next_node_id, state):
                    continue
                if next_node_id not in reachable:
                    reachable.add(next_node_id)
                    queue.append(next_node_id)

        while queue:
            relax(queue.pop(0))

        while deferred:
            retry, deferred = deferred, set()
            before = len(reachable)
            for node_id in retry:
                relax(node_id)
            while queue:
                relax(queue.pop(0))
            if len(reachable) == before:
                break

        return reachable

    def should_seed_stage_start(self, stage: str) -> bool:
        room = self.world.rooms.get(stage)
        if not room:
            return True
        return room.area_scope != "subarea" or stage in MAIN_STAGE_BY_KINGDOM.values()

    def is_node_active(self, node_id: str, state: LogicState) -> bool:
        # Location/entrance NODES are always present. Scenario restrictions live
        # on route edges (option_scenarios) and on moons/moon-rock, never on the
        # nodes themselves. Gating node presence by scenario created deadlocks:
        # e.g. the MoonRockActive_<K> node carries the very scenario it unlocks,
        # so it could never be reached to trigger that scenario's advance.
        node = self.world.nodes.get(node_id)
        return bool(node) and node.status != "excluded"

    def is_location_reachable(self, moon: Moon, state: LogicState, reachable_nodes: set[str]) -> bool:
        # Costume-gated doors are always open, so CostumeStage moons are never
        # locked behind an outfit.
        if not self.has_stage_access_for_location(moon, state, reachable_nodes):
            return False
        if moon.is_princess_peach and not state.has_token(MUSHROOM_REACHED_TOKEN, self.world):
            return False
        if moon.story_order is not None and state.story_counts.get(moon.kingdom, 0) < moon.story_order:
            return False
        is_promoted_multi_prize_subarea = (
            moon.stage in PROMOTED_MULTI_PRIZE_SUBAREA_SCENARIOS
        )
        if (
            not is_promoted_multi_prize_subarea
            and moon.scenario_mask
            and not scenario_mask_contains(
                moon.scenario_mask,
                state.kingdom_scenarios.get(moon.kingdom, 1),
            )
        ):
            return False
        if (
            not is_promoted_multi_prize_subarea
            and moon.scenario_floor is not None
            and state.kingdom_scenarios.get(moon.kingdom, 1) < moon.scenario_floor
        ):
            return False
        # In a routed stage a moon must be anchored to a node reachable through the
        # graph. An empty from_node (or one that isn't reachable) means the spot is
        # unauthored/unreachable -> impossible, so stale data can't silently leave a
        # moon free (this is the gap that let "Pest Problem" skip its Sherm requirement).
        if self.world.stage_has_routes.get(moon.stage, False) and (
            not moon.from_node_id or moon.from_node_id not in reachable_nodes
        ):
            return False
        current_scenario = state.kingdom_scenarios.get(
            moon.kingdom,
            SCENARIO_START_BY_KINGDOM.get(moon.kingdom, 1),
        )
        if not any_option_scenario_allowed(moon.local_option_scenario_masks, current_scenario):
            return False
        if self.settings.no_logic:
            return True
        if not route_requirement_satisfied(
            self.world.moon_capture_edge(moon, True),
            state,
            self.world,
            self.settings.logic_difficulty,
            current_scenario,
            prioritize_difficult=self.mask_casual_methods(),
            reachable_nodes=reachable_nodes,
        ):
            return False
        if not route_requirement_satisfied(
            self.world.moon_capture_edge(moon, False),
            state,
            self.world,
            self.settings.logic_difficulty,
            current_scenario,
            prioritize_difficult=self.mask_casual_methods(),
            reachable_nodes=reachable_nodes,
        ):
            return False
        return True

    def has_stage_access_for_location(self, moon: Moon, state: LogicState, reachable_nodes: set[str]) -> bool:
        # Rule: a stage is reachable only if it is connected to something we have
        # proven access to -- either it was reached directly (accessible_stages) or a
        # node inside it is graph-reachable. A stage with no modeled connection (no
        # entrance, painting, route, or stage_change link wired into the graph) is NOT
        # reachable. There is deliberately no "the kingdom is open, so assume it" fall-
        # back: that crutch handed out phantom access to relocated door sub-areas and
        # to unmodeled stage_change arenas (boss rematches), producing unbeatable seeds.
        # Locations that legitimately need a still-unmodeled transition will surface as
        # unreachable and are tracked for a later authoring pass.
        if moon.stage in state.accessible_stages:
            return True

        if any(
            self.world.nodes[node_id].stage == moon.stage
            for node_id in reachable_nodes
            if node_id in self.world.nodes
        ):
            return True

        return False

NODE_REQUIREMENT_PREFIX = "@"


def is_node_requirement_atom(atom: str) -> bool:
    return atom.startswith(NODE_REQUIREMENT_PREFIX)


def node_requirement_atom(node_id: str) -> str:
    return f"{NODE_REQUIREMENT_PREFIX}{node_id}"


def parse_requirement(
    expression: str,
    token_set: Iterable[str],
    node_set: Iterable[str] | None = None,
) -> Requirement:
    """Parse one authored requirement expression into DNF.

    Atoms are either logic tokens (abilities, captures, events) or node-access
    prerequisites written ``@<node_id>``: the option only counts once the player
    can reach that node. ``node_set`` validates the referenced ids; passing None
    accepts them unchecked, for callers that hold no node table.
    """
    raw = (expression or "").strip()
    if not raw:
        return Requirement("unset", raw=expression or "")
    if raw.lower() == "free":
        return Requirement("free", raw=expression)
    if raw.lower() == "impossible":
        return Requirement("impossible", raw=expression)

    try:
        # Tokenizing is inside the try too: a bad character (or a bare "@") must
        # degrade to a raw requirement like a parse error does, not raise.
        dnf = ast_to_dnf(RequirementParser(raw).parse())
    except ValueError:
        return Requirement("raw", raw=expression)

    alternatives = [
        frozenset(atom for atom in alt if not is_node_requirement_atom(atom))
        for alt in dnf
    ]
    node_alternatives = [
        frozenset(atom[1:] for atom in alt if is_node_requirement_atom(atom))
        for alt in dnf
    ]

    known = set(token_set)
    if any(token not in known for alt in alternatives for token in alt):
        return Requirement("raw", raw=expression)
    if node_set is not None:
        known_nodes = set(node_set)
        if any(node not in known_nodes for alt in node_alternatives for node in alt):
            return Requirement("raw", raw=expression)
    return Requirement("custom", alternatives, expression, node_alternatives)


class RequirementParser:
    def __init__(self, expression: str) -> None:
        self.tokens = tokenize_requirement(expression)
        self.index = 0

    def parse(self):
        node = self.parse_or()
        if self.index != len(self.tokens):
            raise ValueError(f"Unexpected token {self.tokens[self.index]}")
        return node

    def parse_or(self):
        node = self.parse_and()
        while self.peek() == "|":
            self.index += 1
            node = ("or", node, self.parse_and())
        return node

    def parse_and(self):
        node = self.parse_primary()
        while self.peek() == "&":
            self.index += 1
            node = ("and", node, self.parse_primary())
        return node

    def parse_primary(self):
        token = self.peek()
        if token is None:
            raise ValueError("Unexpected end of expression")
        if token == "(":
            self.index += 1
            node = self.parse_or()
            if self.peek() != ")":
                raise ValueError("Missing closing parenthesis")
            self.index += 1
            return node
        if token in (")", "&", "|"):
            raise ValueError(f"Unexpected token {token}")
        self.index += 1
        return ("token", token)

    def peek(self) -> str | None:
        if self.index >= len(self.tokens):
            return None
        return self.tokens[self.index]


def tokenize_requirement(expression: str) -> list[str]:
    tokens: list[str] = []
    index = 0
    while index < len(expression):
        char = expression[index]
        if char.isspace():
            index += 1
            continue
        if char in "()&|":
            tokens.append(char)
            index += 1
            continue
        # Node-access atom: "@" plus a node id, which also contains ":".
        if char == NODE_REQUIREMENT_PREFIX:
            start = index
            index += 1
            while index < len(expression) and (
                expression[index].isalnum() or expression[index] in "_:"
            ):
                index += 1
            if index == start + 1:
                raise ValueError("Empty node reference")
            tokens.append(expression[start:index])
            continue
        if char.isalnum() or char == "_":
            start = index
            while index < len(expression) and (expression[index].isalnum() or expression[index] == "_"):
                index += 1
            tokens.append(expression[start:index])
            continue
        raise ValueError(f"Invalid character near {expression[index:index + 10]!r}")
    return tokens


def ast_to_dnf(ast) -> list[frozenset[str]]:
    kind = ast[0]
    if kind == "token":
        return [frozenset([ast[1]])]
    if kind == "or":
        return ast_to_dnf(ast[1]) + ast_to_dnf(ast[2])

    result = []
    for left in ast_to_dnf(ast[1]):
        for right in ast_to_dnf(ast[2]):
            result.append(frozenset([*left, *right]))
    return result


def load_world(input_dir: Path | None = None, project_json: Path | None = None) -> LogicWorld:
    if project_json:
        return load_project_world(project_json)
    if not input_dir:
        input_dir = Path(__file__).parent / "templates"
    return load_csv_world(input_dir)


def load_csv_world(input_dir: Path) -> LogicWorld:
    tables = {
        "logic_tokens": read_csv(input_dir / "logic_tokens.csv"),
        "rooms": read_csv(input_dir / "rooms.csv"),
        "logic_nodes": read_csv(input_dir / "logic_nodes.csv"),
        "route_edges": read_csv(input_dir / "route_edges.csv"),
        "moon_access": read_csv(input_dir / "moon_access.csv"),
        "entrance_links": read_csv(input_dir / "entrance_links.csv"),
        "painting_links": read_csv(input_dir / "painting_links.csv"),
    }
    return build_world_from_rows(tables)


def load_project_world(project_json: Path) -> LogicWorld:
    project = json.loads(project_json.read_text(encoding="utf-8"))
    workspace = project["workspace"] if "workspace" in project else project
    tables = {
        "logic_tokens": workspace.get("logicTokens", []),
        "rooms": workspace.get("rooms", []),
        "logic_nodes": workspace.get("logicNodes", []),
        "route_edges": workspace.get("routeEdges", []),
        "moon_access": workspace.get("moonAccess", []),
        "entrance_links": read_csv(Path(__file__).parent / "templates" / "entrance_links.csv"),
        "painting_links": read_csv(Path(__file__).parent / "templates" / "painting_links.csv"),
    }
    return build_world_from_rows(tables)


def build_world_from_rows(tables: dict[str, list[dict[str, str]]]) -> LogicWorld:
    token_rows = tables["logic_tokens"]
    token_set = {row["token"] for row in token_rows}
    story_order, story_move_to = infer_story_progression(tables["moon_access"])
    scenario_gated_subarea_stages = randomized_subarea_moon_rock_stages(
        tables["moon_access"], tables["entrance_links"]
    )

    tokens = [
        LogicToken(
            token=row["token"],
            token_type=row.get("token_type", ""),
            display_name=row.get("display_name", "") or row["token"],
            category=row.get("category", ""),
            prerequisite=row.get("prerequisite", ""),
            always_unlocked=parse_bool(row.get("always_unlocked", "")),
            requires_any_cap_throw=parse_bool(row.get("requires_any_cap_throw", "")),
            can_start_with_seed=parse_bool(row.get("can_start_with_seed", "")),
        )
        for row in token_rows
    ]
    rooms = [
        Room(row["stage"], row.get("kingdom", ""), row.get("stage_display_name", ""), row.get("area_scope", ""))
        for row in tables["rooms"]
        if row.get("stage")
    ]
    nodes = [
        LogicNode(
            row["node_id"],
            row.get("kingdom", ""),
            row.get("stage", ""),
            row.get("node_type", ""),
            row.get("status", ""),
            row.get("side", ""),
            node_scenario_indices_to_scenario_no(row.get("scenario_indices", "")),
        )
        for row in tables["logic_nodes"]
        if row.get("node_id")
    ]
    # Node-access atoms (@<node_id>) are validated against the authored node table,
    # so a stale reference degrades to a "raw" requirement instead of silently
    # gating on a node that no longer exists.
    node_set = {node.node_id for node in nodes}
    edges = []
    for row in tables["route_edges"]:
        if not row.get("from_node_id") or not row.get("to_node_id"):
            continue
        requirement = parse_requirement(row.get("requirement_expr", ""), token_set, node_set)
        scenario_masks = option_scenario_masks(
            row.get("option_scenarios", ""),
            requirement_option_count(requirement),
        )
        edges.append(RouteEdge(
            edge_id=row.get("edge_id", ""),
            from_node_id=row.get("from_node_id", ""),
            to_node_id=row.get("to_node_id", ""),
            kingdom=row.get("kingdom", ""),
            scenario_mask=combined_option_scenario_mask(scenario_masks),
            requirement=requirement,
            bidirectional=parse_bool(row.get("bidirectional", "")),
            status=row.get("status", ""),
            difficulty=normalize_route_difficulty_field(
                row.get("difficulty", ""),
                row.get("requirement_expr", ""),
                token_set,
            ),
            option_scenario_masks=scenario_masks,
        ))
    moons = []
    for row in tables["moon_access"]:
        if not row.get("moon_id"):
            continue
        local_requirement = parse_requirement(row.get("requirement_expr", ""), token_set, node_set)
        moons.append(Moon(
            moon_id=row["moon_id"],
            kingdom=row.get("kingdom", ""),
            stage=row.get("stage", ""),
            stage_display_name=row.get("stage_display_name", ""),
            moon_name=row.get("moon_name", ""),
            obj_id=parse_int(row.get("obj_id", ""), -1),
            uid=parse_int(row.get("uid", ""), -1),
            flags=row.get("flags", ""),
            from_node_id=row.get("from_node_id", ""),
            state_requirement=parse_requirement(row.get("state_requirement_expr", ""), token_set, node_set),
            local_requirement=local_requirement,
            local_option_scenario_masks=option_scenario_masks(
                row.get("option_scenarios", ""),
                requirement_option_count(local_requirement),
            ),
            scenario_floor=moon_scenario_floor_for_kingdom(
                row.get("kingdom", ""),
                row.get("shine_info_available_scenarios", ""),
                row["moon_id"] in story_order or parse_optional_int(row.get("scenario_move_to", "")) is not None,
                row.get("stage", "") in scenario_gated_subarea_stages,
            ),
            scenario_mask=scenario_mask(row.get("shine_info_available_scenarios", "")),
            scenario_move_to=parse_optional_int(row.get("scenario_move_to", "")),
            story_order=story_order.get(row["moon_id"]),
            story_move_to=story_move_to.get(row["moon_id"]),
            effort_level=row.get("effort_level", ""),
            local_difficulty=normalize_route_difficulty_field(
                row.get("difficulty", ""),
                row.get("requirement_expr", ""),
                token_set,
            ),
        ))
    entrances = []
    for row in tables["entrance_links"]:
        if not row.get("link_index"):
            continue
        entrances.append(
            EntranceLink(
                link_index=len(entrances),
                kingdom=row.get("kingdom", ""),
                home_stage=row.get("home_stage", ""),
                home_entrance_in=row.get("home_entrance_in", ""),
                home_entrance_out=row.get("home_entrance_out", ""),
                sub_stage=row.get("sub_stage", ""),
                sub_entrance_in=row.get("sub_entrance_in", ""),
                sub_entrance_out=row.get("sub_entrance_out", ""),
                home_in_node_id=row.get("home_in_node_id", ""),
                home_out_node_id=row.get("home_out_node_id", ""),
                sub_near_node_id=row.get("sub_near_node_id", ""),
                sub_far_node_id=row.get("sub_far_node_id", ""),
                # Every loading zone takes you to its (vanilla or shuffled)
                # destination; none "stops working". "excluded" was a legacy way to
                # drop a door from reachability, but that is identical to "vanilla"
                # for the shuffle pool and wrongly removed a working two-way door
                # from logic. Treat it as vanilla: reachable two-way, not shuffled.
                # One-way doors are expressed via node route-edge restrictions.
                status="vanilla" if row.get("status", "") == "excluded" else row.get("status", ""),
            )
        )
    painting_links = [
        PaintingLink(
            link_id=row.get("link_id", ""),
            source_node_id=row.get("source_node_id", ""),
            destination_node_id=row.get("destination_node_id", ""),
            bidirectional=parse_bool(row.get("bidirectional", "true")),
            status=row.get("status", ""),
        )
        for row in tables.get("painting_links", [])
        if row.get("source_node_id") and row.get("destination_node_id")
    ]
    return LogicWorld(tokens, rooms, nodes, edges, moons, entrances, painting_links)


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def write_json_result(path: Path, world: LogicWorld, result: RandomizationResult) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = {
        "placements": [
            {
                "location_moon_id": placement.location_id,
                "item_moon_id": placement.item_id,
            }
            for placement in result.placements
        ],
        "entrances": [
            {
                "source_index": placement.source_index,
                "destination_index": placement.destination_index,
                "is_home_warp": placement.is_home_warp,
            }
            for placement in result.entrance_placements
        ],
        "path": [
            {
                "sphere": step.sphere,
                "location_moon_id": step.location_id,
                "item_moon_id": step.item_id,
                "reason": step.reason,
                "unlocked_tokens": step.unlocked_tokens,
            }
            for step in result.path
        ],
        "warnings": result.warnings,
        "moon_allocation": (
            world.moon_allocation_result.as_dict()
            if world.moon_allocation_result is not None
            else None
        ),
        "final_objective": {
            "reached": result.final_objective_reached,
            "note": result.final_objective_note,
        },
    }
    path.write_text(json.dumps(data, indent=2), encoding="utf-8")


def write_spoiler(path: Path, world: LogicWorld, result: RandomizationResult, seed: int) -> None:
    from enemy_swap import DELETED_PLACEMENT_ENTITY

    path.parent.mkdir(parents=True, exist_ok=True)
    placements = {placement.location_id: placement.item_id for placement in result.placements}
    entrances_by_source = {placement.source_index: placement for placement in result.entrance_placements}
    entrance_by_index = {entrance.link_index: entrance for entrance in world.entrances}

    lines = [f"=== SMO Randomizer Logic Spoiler ===", f"Seed: {seed}", ""]
    allocation = world.moon_allocation_result
    if allocation is not None:
        lines.append("=== Moon Allocation ===")
        allocation_settings = world.moon_allocation_settings
        if allocation.active and allocation_settings is not None:
            lines.append(
                f"Allocation: "
                f"{difficulty_name(allocation_settings.moon_allocation_difficulty)}/"
                f"{coin_policy_name(allocation_settings.moon_coin_policy)}"
            )
        else:
            lines.append("Allocation: Off")
        if allocation.failure is not None:
            lines.append(f"Failure: {allocation.failure.message}")
        elif allocation.summaries:
            for kingdom, summary in allocation.summaries.items():
                special = ""
                if summary.special_kind != 0:
                    special = (
                        f" | {summary.special_kind}="
                        f"{summary.special_capacity}/{summary.special_minimum}"
                    )
                lines.append(
                    f"  {kingdom}: entries={summary.assigned_entries} "
                    f"value={summary.assigned_value} "
                    f"minimum={summary.minimum_value}"
                    f"{special}"
                )
        else:
            lines.append("  Allocation: Off")
        lines.append(f"Role layout hash: {allocation.role_layout_hash}")
        lines.append("")
    lines.append("=== Moon Placements by Final Location ===")
    for kingdom in sorted({moon.kingdom for moon in world.moons.values() if moon.moon_id in placements}):
        lines.append("")
        lines.append(f"{kingdom}:")
        for location in sorted(
            [moon for moon in world.moons.values() if moon.kingdom == kingdom and moon.moon_id in placements],
            key=lambda moon: (moon.stage, moon.moon_name, moon.moon_id),
        ):
            item = world.moons[placements[location.moon_id]]
            suffix = unlock_suffix(world, item)
            lines.append(
                f"  {item.moon_name} - {item.credit_kingdom}{suffix} "
                f"@ {location.moon_name}"
            )

    def entrance_has_forward_exit(link: EntranceLink) -> bool:
        # A sub-area chains onward only when it has a distinct far exit (same
        # condition the C++ chain-fill shuffle and resolveExit use).
        return bool(link.sub_entrance_in and link.sub_entrance_out and link.sub_entrance_in != link.sub_entrance_out)

    def describe_entrance_chain(first: EntranceLink, first_is_home_warp: bool) -> str:
        # Follow the far-exit chain: entering a sub-area whose far exit is
        # remapped leads into the next sub-area, and so on until a sub with no
        # far exit (dead end), an overworld surface, or a loop.
        parts: list[str] = []
        visited: set[int] = set()
        destination: EntranceLink | None = first
        is_home_warp = first_is_home_warp
        while destination:
            parts.append(describe_entrance_destination(world, destination, is_home_warp))
            if is_home_warp or not destination.sub_stage:
                break  # surfaced at an overworld
            if not entrance_has_forward_exit(destination):
                parts[-1] += " [dead end]"
                break
            if destination.link_index in visited:
                parts[-1] += " [loops]"
                break
            visited.add(destination.link_index)
            mapped = entrances_by_source.get(destination.link_index)
            next_index = mapped.destination_index if mapped else destination.link_index
            if next_index == destination.link_index:
                # Identity mapping (non-randomized link): the far exit is vanilla
                # and surfaces back at its own home overworld.
                parts.append(stage_display(world, destination.home_stage))
                break
            is_home_warp = mapped.is_home_warp if mapped else False
            destination = entrance_by_index.get(next_index)
        return " -> ".join(parts)

    lines.append("")
    lines.append("=== Entrance Randomizer ===")
    current_home = None
    for entrance in sorted(
        [link for link in world.entrances if link.status != "excluded"],
        key=lambda link: (link.home_stage, link.link_index),
    ):
        if entrance.home_stage != current_home:
            current_home = entrance.home_stage
            lines.append("")
            lines.append(f"{stage_display(world, current_home)}:")
        mapped = entrances_by_source.get(entrance.link_index)
        destination = entrance_by_index.get(mapped.destination_index) if mapped else entrance
        if not destination:
            continue
        lines.append(
            f"  {stage_display(world, entrance.home_stage)} {entrance.home_entrance_in} -> "
            f"{describe_entrance_chain(destination, mapped.is_home_warp if mapped else False)}"
        )

    lines.append("")
    lines.append("=== Painting Links ===")
    active_paintings = [painting for painting in world.painting_links if painting.status != "excluded"]
    if not active_paintings:
        lines.append("No painting links are defined.")
    for painting in active_paintings:
        arrow = "<->" if painting.bidirectional else "->"
        lines.append(
            f"  {describe_painting_node(world, painting.source_node_id)} "
            f"{arrow} {describe_painting_node(world, painting.destination_node_id)}"
        )

    lines.append("")
    lines.append("=== Enemy/Capture Assignment ===")
    if world.enemy_swap_config is None or not world.enemy_swap_assignment:
        lines.append("Enemy/Capture Swap is disabled.")
    else:
        lines.append("Route witnesses:")
        if world.enemy_swap_witnesses:
            lines.extend(f"  {gate_id}" for gate_id in world.enemy_swap_witnesses)
        else:
            lines.append("  No authored capture gates were loaded.")
        lines.append("Changed placements:")
        changed = 0
        for placement, entity_index in zip(
            world.enemy_swap_config.placements,
            world.enemy_swap_assignment,
        ):
            if entity_index == placement.entity:
                continue
            if entity_index == DELETED_PLACEMENT_ENTITY:
                lines.append(
                    f"  {placement.stage}:{placement.object_id} "
                    f"({placement.unit_config_name}) -> [deleted]"
                )
                changed += 1
                continue
            if entity_index < 0:
                continue
            entity = world.enemy_swap_config.entities[entity_index]
            lines.append(
                f"  {placement.stage}:{placement.object_id} "
                f"({placement.unit_config_name}) -> "
                f"{entity.friendly_name} ({entity.name})"
            )
            changed += 1
        if changed == 0:
            lines.append("  No placements changed.")

    lines.append("")
    lines.append("=== Suggested Progress Path ===")
    if not result.path:
        lines.append("No required progress placements were needed or the graph could not prove a path.")
    for step in result.path:
        item = world.moons[step.item_id]
        location = world.moons[step.location_id]
        unlocks = f" | unlocks: {', '.join(step.unlocked_tokens)}" if step.unlocked_tokens else ""
        lines.append(
            f"{step.sphere:03d}. {location.kingdom}: collect {item.moon_name} "
            f"at {location.moon_name} ({step.reason}){unlocks}"
        )

    lines.append("")
    lines.append("=== Final Objective ===")
    lines.append("Proven" if result.final_objective_reached else "Not proven")
    if result.final_objective_note:
        lines.append(result.final_objective_note)

    if result.warnings:
        lines.append("")
        lines.append("=== Warnings ===")
        lines.extend(f"- {warning}" for warning in result.warnings)

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def unlock_suffix(world: LogicWorld, moon: Moon) -> str:
    tokens = [*moon.capture_unlocks, *moon.ability_unlocks]
    if not tokens:
        return ""
    names = [world.tokens.get(token).display_name if token in world.tokens else token for token in tokens]
    return f" ({', '.join(names)})"


def describe_entrance_destination(world: LogicWorld, entrance: EntranceLink, is_home_warp: bool) -> str:
    if is_home_warp:
        return f"Home Warp to {stage_display(world, entrance.home_stage)}"
    # Deep Woods is presented as its own region even though its entrances live
    # in Wooded Kingdom's overworld (mirrors the C++ spoiler).
    region = "Deep Woods" if entrance.sub_stage == "ForestWorldWoodsStage" else stage_display(world, entrance.home_stage)
    return f"{region}: {entrance.sub_stage} ({entrance.sub_entrance_in})"


def describe_painting_node(world: LogicWorld, node_id: str) -> str:
    node = world.nodes.get(node_id)
    if not node:
        return node_id
    return f"{stage_display(world, node.stage)} ({node_id})"


def stage_display(world: LogicWorld, stage: str) -> str:
    room = world.rooms.get(stage)
    if room and room.stage_display_name:
        return room.stage_display_name
    for kingdom, main_stage in MAIN_STAGE_BY_KINGDOM.items():
        if stage == main_stage:
            return kingdom
    return stage


def is_moon_pool_eligible(moon: Moon, capture_rando_enabled: bool, ability_rando_enabled: bool) -> bool:
    # Achievement (Toadette) moons are the only excluded category. Hint-art moons and
    # the special kingdoms' moons are always in the pool; when a special kingdom's
    # feature toggle is off, its moon items pay filler coins at runtime instead.
    if moon.uid == 0 or moon.obj_id < 0:
        return False
    if moon.is_toadette:
        return False
    return True


def apply_default_bidirectionality(edges: list[RouteEdge]) -> list[RouteEdge]:
    for edge in edges:
        has_reverse = any(
            candidate is not edge
            and candidate.from_node_id == edge.to_node_id
            and candidate.to_node_id == edge.from_node_id
            for candidate in edges
        )
        edge.bidirectional = not has_reverse
    return edges


def normalize_route_difficulty(value: str, preserve_invalid: bool = True) -> str:
    normalized = str(value or "").strip().lower()
    if normalized in ROUTE_DIFFICULTY_RANKS:
        return normalized
    if normalized in {"easy", "low"}:
        return ROUTE_DIFFICULTY_CASUAL
    if normalized in {"medium", "mid"}:
        return ROUTE_DIFFICULTY_ADVANCED
    if normalized == "hard":
        return ROUTE_DIFFICULTY_EXPERT
    if normalized in {"glitch", "glitches"}:
        return ROUTE_DIFFICULTY_GLITCHED
    if preserve_invalid and normalized:
        return normalized
    return ROUTE_DIFFICULTY_CASUAL


def normalize_route_difficulty_field(value: str, requirement_expr: str, token_set: Iterable[str]) -> str:
    requirement = parse_requirement(requirement_expr, token_set)
    option_count = len(requirement.alternatives) if requirement.mode == "custom" else 1
    difficulties = route_option_difficulties(value, option_count)
    if all(difficulty == difficulties[0] for difficulty in difficulties):
        return difficulties[0]
    return ";".join(difficulties)


def route_difficulty_allowed(route_difficulty: str, logic_difficulty: str) -> bool:
    route_rank = ROUTE_DIFFICULTY_RANKS.get(normalize_route_difficulty(route_difficulty, preserve_invalid=False), 0)
    logic_rank = ROUTE_DIFFICULTY_RANKS.get(normalize_route_difficulty(logic_difficulty, preserve_invalid=False), 0)
    return route_rank <= logic_rank


def route_option_difficulties(value: str, option_count: int) -> list[str]:
    count = max(1, option_count)
    parts = [
        normalize_route_difficulty(part)
        for part in str(value or "").split(";")
        if part.strip()
    ]
    if not parts:
        return [ROUTE_DIFFICULTY_CASUAL] * count
    if len(parts) == 1:
        return parts * count
    return [
        parts[index] if index < len(parts) else ROUTE_DIFFICULTY_CASUAL
        for index in range(count)
    ]


def route_option_difficulty(value: str, option_index: int) -> str:
    parts = [
        normalize_route_difficulty(part)
        for part in str(value or "").split(";")
        if part.strip()
    ]
    if not parts:
        return ROUTE_DIFFICULTY_CASUAL
    if len(parts) == 1:
        return parts[0]
    if 0 <= option_index < len(parts):
        return parts[option_index]
    return ROUTE_DIFFICULTY_CASUAL


def requirement_option_count(requirement: Requirement) -> int:
    return max(1, len(requirement.alternatives) if requirement.mode == "custom" else 1)


def extend_top_scenario(mask: int) -> int:
    """Make the highest checked scenario open-ended ("sticky top").

    Authored option_scenarios list the scenarios a path works in. Anything at or
    above the HIGHEST checked scenario stays available, because a route that
    still works at the last scenario an author ticked has no reason to close
    later. Gaps BELOW that top are preserved: they are deliberate (Metro excludes
    scenario 3 for its festival state, Luncheon and Sand exclude 2), and filling
    them would silently re-open routes the author shut.

    This replaces the old MAX_EDITOR_SCENARIO hack, which ignored the mask
    entirely above scenario 5 because the editor could not express 6+. The editor
    now exposes every scenario a kingdom can reach, so the mask is honoured at
    all scenarios and the top simply never closes. Mask 0 stays unrestricted.
    """
    if mask <= 0:
        return 0
    top = mask.bit_length() - 1
    for bit in range(top, SCENARIO_BIT_LIMIT):
        mask |= 1 << bit
    return mask


def option_scenario_masks(value: str, option_count: int) -> tuple[int, ...]:
    """Parse editor option scenarios.

    Commas separate scenarios for one requirement alternative; semicolons
    separate alternatives. A single group applies to every alternative, matching
    the graph editor. Mask 0 means unrestricted.

    Bit N is scenario N (1-based ScenarioNo), matching scenario_mask_contains
    here and MoonRando::scenarioMaskContains. The parsed mask is passed through
    extend_top_scenario, so every consumer - this runner, the generated
    LogicData.h, and the tracker - sees the same sticky-top semantics without
    needing a special case of its own.
    """
    count = max(1, option_count)
    raw = str(value or "").strip()
    if not raw:
        return (0,) * count

    groups = raw.split(";")

    def parse_group(group: str) -> int:
        mask = 0
        for part in group.split(","):
            scenario = parse_int(part.strip(), -1)
            if 0 <= scenario < 32:
                mask |= 1 << scenario
        return extend_top_scenario(mask)

    parsed = [parse_group(group) for group in groups]
    if len(parsed) == 1:
        return (parsed[0],) * count
    return tuple(parsed[index] if index < len(parsed) else 0 for index in range(count))


def combined_option_scenario_mask(masks: Iterable[int]) -> int:
    combined = 0
    saw_mask = False
    for mask in masks:
        saw_mask = True
        if mask == 0:
            return 0
        combined |= mask
    return combined if saw_mask else 0


def option_scenario_allowed(masks: tuple[int, ...], option_index: int, scenario: int) -> bool:
    # No scenario ceiling here: the masks are already sticky-topped by
    # extend_top_scenario, so a high scenario is allowed because the mask says
    # so, not because we gave up on checking it.
    if not masks or scenario < 0:
        return True
    index = option_index if len(masks) > 1 else 0
    if index < 0 or index >= len(masks):
        return True
    mask = masks[index]
    return mask == 0 or scenario_mask_contains(mask, scenario)


def any_option_scenario_allowed(masks: tuple[int, ...], scenario: int) -> bool:
    return not masks or any(option_scenario_allowed(masks, index, scenario) for index in range(len(masks)))


def requirement_satisfied_for_scenario(
    requirement: Requirement,
    masks: tuple[int, ...],
    current_scenario: int,
    state: LogicState,
    world: LogicWorld,
    stage: str | None = None,
    reachable_nodes: set[str] | None = None,
) -> bool:
    if requirement.mode in ("unset", "free"):
        return option_scenario_allowed(masks, 0, current_scenario)
    if requirement.mode == "impossible":
        return False
    if requirement.mode == "raw":
        return world.treat_raw_requirements_as_free and option_scenario_allowed(
            masks, 0, current_scenario
        )
    check_nodes = requirement.has_node_requirements and reachable_nodes is not None
    return any(
        option_scenario_allowed(masks, index, current_scenario)
        and world.is_requirement_alternative_enabled(stage, alternative)
        and (not check_nodes or requirement.node_alternative_satisfied(index, reachable_nodes))
        and all(state.has_token(world.remap_token(stage, token), world) for token in alternative)
        for index, alternative in enumerate(requirement.alternatives)
    )


def missing_requirement_tokens_for_scenario(
    requirement: Requirement,
    masks: tuple[int, ...],
    current_scenario: int,
    state: LogicState,
    world: LogicWorld,
    stage: str | None = None,
    reachable_nodes: set[str] | None = None,
) -> set[str]:
    if requirement.mode != "custom":
        return set()
    check_nodes = requirement.has_node_requirements and reachable_nodes is not None
    return {
        remapped
        for index, alternative in enumerate(requirement.alternatives)
        if option_scenario_allowed(masks, index, current_scenario)
        and world.is_requirement_alternative_enabled(stage, alternative)
        and (not check_nodes or requirement.node_alternative_satisfied(index, reachable_nodes))
        for token in alternative
        for remapped in (world.remap_token(stage, token),)
        if not state.has_token(remapped, world)
    }


def requirement_has_eligible_difficult_option(
    edge: RouteEdge,
    current_scenario: int,
    logic_difficulty: str,
    reachable_nodes: set[str] | None = None,
) -> bool:
    """Whether this evaluation offers a scenario-eligible alternative harder than
    casual yet still allowed. Only then does Prioritize Difficult Movement ignore
    the casual alternatives: Advanced never consults Expert methods, while Expert
    keeps Advanced ones and Glitched keeps both. Mirrors MoonRando.cpp
    hasEligibleDifficultOption."""
    requirement = edge.requirement
    option_count = (
        len(requirement.alternatives) if requirement.mode == "custom" else 1
    )
    check_nodes = requirement.has_node_requirements and reachable_nodes is not None
    for index in range(option_count):
        difficulty = route_option_difficulty(edge.difficulty, index)
        if difficulty == ROUTE_DIFFICULTY_CASUAL:
            continue
        if not route_difficulty_allowed(difficulty, logic_difficulty):
            continue
        if check_nodes and not requirement.node_alternative_satisfied(index, reachable_nodes):
            continue
        if option_scenario_allowed(
            edge.option_scenario_masks, index, current_scenario
        ):
            return True
    return False


def missing_route_requirement_tokens(
    edge: RouteEdge,
    current_scenario: int,
    state: LogicState,
    world: LogicWorld,
    logic_difficulty: str,
    prioritize_difficult: bool = False,
    difficult_tokens: set[str] | None = None,
    reachable_nodes: set[str] | None = None,
) -> set[str]:
    """Edge-aware counterpart to route_requirement_satisfied for item search.

    When ``difficult_tokens`` is given, tokens missing on harder-than-casual
    alternatives are also collected there (the sphere-expansion priority tier
    for Prioritize Difficult Movement)."""
    requirement = edge.requirement
    if requirement.mode != "custom":
        return set()
    missing: set[str] = set()
    stage = world.edge_stage(edge)
    skip_casual = prioritize_difficult and requirement_has_eligible_difficult_option(
        edge, current_scenario, logic_difficulty, reachable_nodes
    )
    check_nodes = requirement.has_node_requirements and reachable_nodes is not None
    for index, alternative in enumerate(requirement.alternatives):
        if not option_scenario_allowed(
            edge.option_scenario_masks, index, current_scenario
        ):
            continue
        option_difficulty = route_option_difficulty(edge.difficulty, index)
        if not route_difficulty_allowed(option_difficulty, logic_difficulty):
            continue
        if skip_casual and option_difficulty == ROUTE_DIFFICULTY_CASUAL:
            continue
        # An option still waiting on a node the player cannot reach yet is not
        # opened by any item, so its tokens are not useful progression here.
        if check_nodes and not requirement.node_alternative_satisfied(index, reachable_nodes):
            continue
        difficult_option = option_difficulty != ROUTE_DIFFICULTY_CASUAL
        for token in alternative:
            entry = world.tokens.get(token)
            if (
                world.captures_unreliable
                and entry is not None
                and entry.token_type == "capture"
            ):
                replacements = world.route_capture_replacements(
                    edge, token, current_scenario
                )
                if not any(
                    state.has_token(replacement, world)
                    for replacement in replacements
                ):
                    missing.update(replacements)
                    if difficult_tokens is not None and difficult_option:
                        difficult_tokens.update(replacements)
                continue
            remapped = world.remap_token(stage, token)
            if not state.has_token(remapped, world):
                missing.add(remapped)
                if difficult_tokens is not None and difficult_option:
                    difficult_tokens.add(remapped)
    return missing


def route_requirement_satisfied(
    edge: RouteEdge,
    state: LogicState,
    world: LogicWorld,
    logic_difficulty: str,
    current_scenario: int,
    prioritize_difficult: bool = False,
    reachable_nodes: set[str] | None = None,
) -> bool:
    requirement = edge.requirement
    if requirement.mode in ("unset", "free"):
        return option_scenario_allowed(edge.option_scenario_masks, 0, current_scenario) and route_difficulty_allowed(
            route_option_difficulty(edge.difficulty, 0), logic_difficulty
        )
    if requirement.mode == "impossible":
        return False
    if requirement.mode == "raw":
        return (
            world.treat_raw_requirements_as_free
            and option_scenario_allowed(edge.option_scenario_masks, 0, current_scenario)
            and route_difficulty_allowed(route_option_difficulty(edge.difficulty, 0), logic_difficulty)
        )

    edge_stage = world.edge_stage(edge)
    skip_casual = prioritize_difficult and requirement_has_eligible_difficult_option(
        edge, current_scenario, logic_difficulty, reachable_nodes
    )
    check_nodes = requirement.has_node_requirements and reachable_nodes is not None
    for index, alternative in enumerate(requirement.alternatives):
        if not option_scenario_allowed(edge.option_scenario_masks, index, current_scenario):
            continue
        option_difficulty = route_option_difficulty(edge.difficulty, index)
        if not route_difficulty_allowed(option_difficulty, logic_difficulty):
            continue
        if skip_casual and option_difficulty == ROUTE_DIFFICULTY_CASUAL:
            continue
        if check_nodes and not requirement.node_alternative_satisfied(index, reachable_nodes):
            continue
        if all(
            world.route_capture_satisfied(edge, token, state, current_scenario)
            if (
                world.captures_unreliable
                and world.tokens.get(token) is not None
                and world.tokens[token].token_type == "capture"
            )
            else state.has_token(world.remap_token(edge_stage, token), world)
            for token in alternative
        ):
            return True
    return False


def find_multi_overworld_stages(rooms: Iterable[Room]) -> set[str]:
    stages_by_kingdom: dict[str, list[str]] = {}
    for room in rooms:
        if room.area_scope != "overworld":
            continue
        stages_by_kingdom.setdefault(room.kingdom, []).append(room.stage)

    return {
        stage
        for stages in stages_by_kingdom.values()
        if len(stages) > 1
        for stage in stages
    }


def node_uses_scenario_floor(node: LogicNode) -> bool:
    return node.node_type in {
        "stage_start",
        "home_entrance",
        "substage_entrance",
        "checkpoint",
        "custom_place",
        "stage_change",
    }


def is_princess_peach_moon(moon: Moon) -> bool:
    name = moon.moon_name.strip()
    return name.startswith("Peach in ") or name == "Princess Peach, Home Again!"


def safe_scenario_floor(value: str) -> int | None:
    values = sorted({parse_int(part, -1) for part in split_semicolon(value) if parse_int(part, -1) >= 0})
    return safe_scenario_floor_from_values(values)


def safe_scenario_floor_for_kingdom(kingdom: str, value: str) -> int | None:
    raw_values = [
        scenario
        for scenario in (parse_int(part, -1) for part in split_semicolon(value))
        if scenario >= 0
    ]
    if not raw_values:
        return None

    max_scenario = SCENARIO_MAX_LOGIC_BY_KINGDOM.get(kingdom)
    values = sorted({
        scenario
        for scenario in raw_values
        if (max_scenario is None or scenario <= max_scenario) and not is_scenario_blocked(kingdom, scenario)
    })
    if not values:
        return (max_scenario + 1) if max_scenario is not None else None
    return safe_scenario_floor_from_values(values)


def min_scenario_floor_for_kingdom(kingdom: str, value: str) -> int | None:
    raw_values = [
        scenario
        for scenario in (parse_int(part, -1) for part in split_semicolon(value))
        if scenario >= 0
    ]
    if not raw_values:
        return None

    max_scenario = SCENARIO_MAX_LOGIC_BY_KINGDOM.get(kingdom)
    values = sorted({
        scenario
        for scenario in raw_values
        if (max_scenario is None or scenario <= max_scenario) and not is_scenario_blocked(kingdom, scenario)
    })
    if not values:
        return (max_scenario + 1) if max_scenario is not None else None
    return values[0]


def node_scenario_floor_for_kingdom(node_type: str, kingdom: str, value: str) -> int | None:
    if node_type == "checkpoint":
        return min_scenario_floor_for_kingdom(kingdom, value)
    return safe_scenario_floor_for_kingdom(kingdom, value)


def randomized_subarea_moon_rock_stages(
    moon_rows: Iterable[dict[str, str]],
    entrance_rows: Iterable[dict[str, str]],
) -> set[str]:
    """Return shuffled subareas whose room mixes ordinary and Moon Rock moons.

    These rooms must be entered in their kingdom's Moon Rock scenario so the
    room's scenario archive exposes both moons.  Do not apply that rule to
    every subarea: most rooms either have no Moon Rock moon or are already
    correctly represented by their normal ShineInfo scenario data.
    """
    shuffled_stages = {
        row.get("sub_stage", "")
        for row in entrance_rows
        if row.get("status") == "active" and row.get("sub_stage", "")
    }
    moons_by_stage: dict[str, list[dict[str, str]]] = {}
    for row in moon_rows:
        stage = row.get("stage", "")
        if stage in shuffled_stages:
            moons_by_stage.setdefault(stage, []).append(row)

    return {
        stage
        for stage, rows in moons_by_stage.items()
        if any(row.get("shine_info_is_moon_rock", "").lower() == "true" for row in rows)
        and any(row.get("shine_info_is_moon_rock", "").lower() != "true" for row in rows)
    }


def moon_scenario_floor_for_kingdom(
    kingdom: str,
    value: str,
    advances_progression: bool,
    force_moon_rock_scenario: bool = False,
) -> int | None:
    """Story moons and scenario-advancing moons gate their own progression: the
    safe-suffix floor would demand a scenario only they can unlock (e.g. Luncheon's
    first story moon getting floor 8), so they use their earliest scenario instead.
    The scenario mask still applies at collection time."""
    if advances_progression:
        floor = min_scenario_floor_for_kingdom(kingdom, value)
    else:
        floor = safe_scenario_floor_for_kingdom(kingdom, value)

    if force_moon_rock_scenario:
        moon_rock_scenario = SCENARIO_MOON_ROCK_BY_KINGDOM.get(kingdom)
        if moon_rock_scenario is not None:
            floor = max(floor or moon_rock_scenario, moon_rock_scenario)
    return floor


def safe_scenario_floor_from_values(values: list[int]) -> int | None:
    if not values:
        return None

    floor = values[-1]
    for index in range(len(values) - 2, -1, -1):
        if values[index] == floor - 1:
            floor = values[index]
        else:
            break
    return floor


def scenario_mask(value: str) -> int:
    mask = 0
    for part in str(value or "").replace(",", ";").split(";"):
        scenario = parse_int(part, -1)
        if 0 <= scenario < 32:
            mask |= 1 << scenario
    return mask


def node_scenario_indices_to_scenario_no(value: str) -> str:
    """StageData-derived node scenario_indices are 0-based scenario-array indices, but
    they are compared against kingdom scenarios in 1-based ScenarioNo space (the space
    moons use via shine_info_available_scenarios). Shift index -> ScenarioNo (+1)."""
    parts = []
    for part in split_semicolon(value):
        scenario = parse_int(part, -1)
        if scenario >= 0:
            parts.append(str(scenario + 1))
    return ";".join(parts)


def scenario_mask_contains(mask: int, scenario: int) -> bool:
    return 0 <= scenario < 32 and bool(mask & (1 << scenario))


def is_scenario_blocked(kingdom: str, scenario: int) -> bool:
    max_scenario = SCENARIO_MAX_LOGIC_BY_KINGDOM.get(kingdom)
    return (
        (max_scenario is not None and scenario > max_scenario)
        or scenario in SCENARIO_BLOCKED_BY_KINGDOM.get(kingdom, set())
    )


def normalize_scenario_target(kingdom: str, scenario: int) -> int | None:
    if scenario <= 0:
        return None
    if scenario == SCENARIO_PEACE_BY_KINGDOM.get(kingdom):
        scenario = SCENARIO_POSTGAME_BY_KINGDOM.get(kingdom, scenario)
    max_scenario = SCENARIO_MAX_LOGIC_BY_KINGDOM.get(kingdom)
    if max_scenario is not None and scenario > max_scenario:
        scenario = max_scenario
    if is_scenario_blocked(kingdom, scenario):
        return None
    return scenario


def min_accessible_scenario(kingdom: str, value: str) -> int | None:
    values = [
        scenario
        for scenario in (parse_int(part, -1) for part in split_semicolon(value))
        if scenario > 0 and not is_scenario_blocked(kingdom, scenario)
    ]
    return min(values) if values else None


def story_sort_key(row: dict[str, str]) -> tuple[int, int, int]:
    return (
        parse_int(row.get("shine_info_hint_idx", ""), 9999),
        parse_int(row.get("shine_info_main_scenario_no", ""), 9999),
        parse_int(row.get("uid", ""), 9999),
    )


def infer_story_progression(rows: list[dict[str, str]]) -> tuple[dict[str, int], dict[str, int]]:
    by_kingdom: dict[str, list[dict[str, str]]] = {}
    for row in rows:
        if "ShineFlag_Story" not in row.get("flags", ""):
            continue
        by_kingdom.setdefault(row.get("kingdom", ""), []).append(row)

    story_order: dict[str, int] = {}
    story_move_to: dict[str, int] = {}
    for kingdom, story_rows in by_kingdom.items():
        ordered = sorted(story_rows, key=story_sort_key)
        for index, row in enumerate(ordered):
            moon_id = row.get("moon_id", "")
            if not moon_id:
                continue
            story_order[moon_id] = index
            if index + 1 < len(ordered):
                target = min_accessible_scenario(kingdom, ordered[index + 1].get("shine_info_available_scenarios", ""))
            else:
                target = SCENARIO_POSTGAME_BY_KINGDOM.get(kingdom)
            normalized = normalize_scenario_target(kingdom, target or -1)
            if normalized is not None:
                story_move_to[moon_id] = normalized
    return story_order, story_move_to


def split_semicolon(value: str) -> list[str]:
    return [part.strip() for part in (value or "").split(";") if part.strip()]


def count_story_moons(moons: Iterable[Moon]) -> dict[str, int]:
    totals: dict[str, int] = {}
    for moon in moons:
        if moon.is_story:
            totals[moon.kingdom] = totals.get(moon.kingdom, 0) + 1
    return totals


def previous_kingdom(kingdom: str, order: list[str]) -> str | None:
    try:
        index = order.index(kingdom)
    except ValueError:
        return None
    if index <= 0:
        return None
    return order[index - 1]


def normalize_prerequisite(prerequisite: str) -> str:
    if not prerequisite or prerequisite == "kNoPrerequisite":
        return ""
    if prerequisite.startswith("AbilityId_"):
        return prerequisite.removeprefix("AbilityId_")
    return prerequisite


def parse_bool(value: str) -> bool:
    return str(value).strip().lower() in ("true", "1", "yes")


def parse_int(value: str, default: int = 0) -> int:
    try:
        return int(str(value).strip())
    except (TypeError, ValueError):
        return default


def parse_optional_int(value: str) -> int | None:
    text = str(value or "").strip()
    if not text:
        return None
    try:
        return int(text)
    except ValueError:
        return None


def make_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Generate graph-aware SMO randomizer logic output.")
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--input", type=Path, default=Path(__file__).parent / "templates")
    parser.add_argument("--project", type=Path, help="Logic editor project JSON export.")
    parser.add_argument("--output-json", type=Path, default=Path("logic/out/logic_result.json"))
    parser.add_argument("--spoiler", type=Path, default=Path("logic/out/SpoilerLog.txt"))
    parser.add_argument("--no-entrance-rando", action="store_true")
    parser.add_argument("--no-capture-rando", action="store_true")
    parser.add_argument("--no-ability-rando", action="store_true")
    parser.add_argument(
        "--moon-allocation",
        choices=("off", "easy", "vanilla", "hard", "expert", "max"),
        default="off",
        help="Enable full moon allocation at the selected difficulty",
    )
    parser.add_argument(
        "--moon-coin-policy",
        choices=("vanilla", "standard", "ability-rush"),
        default="vanilla",
        help="Destination policy for classified Moon Coin items",
    )
    parser.add_argument("--enemy-swap", action="store_true",
                        help="Resolve Enemy/Capture Swap placements and capture gates")
    parser.add_argument("--npc-swap", type=int, choices=(0, 1, 2), default=0,
                        help="NPC swap mode: 0 off, 1 NPCs only, 2 NPCs or combat")
    parser.add_argument("--prop-swap", type=int, choices=(0, 1, 2, 3), default=0,
                        help="Prop swap mode: 0 off, 1 props, 2 props/combat, "
                             "3 props/combat with 25% deletion")
    parser.add_argument("--prop-swap-chance", type=int, default=0,
                        help="Prop participation percentage (0-100)")
    parser.add_argument("--two-d-swap", action="store_true",
                        help="Shuffle 2D section enemies among themselves")
    parser.add_argument("--keep-critical-captures", action="store_true",
                        help="Keep configured logic capture sources vanilla under Enemy/Capture Swap")
    parser.add_argument("--logic-difficulty", choices=ROUTE_DIFFICULTY_LEVELS, default=ROUTE_DIFFICULTY_CASUAL)
    parser.add_argument("--all-kingdoms-start-unlocked", action="store_true",
                        help="Make every kingdom available to logic at seed start while preserving route order")
    parser.add_argument("--randomize-start-kingdom-state", action="store_true",
                        help="Use the seed-selected initial scenario for each kingdom")
    parser.add_argument("--prioritize-difficult-movement", action="store_true",
                        help="Ignore casual alternatives during the fill when a harder allowed one exists (falls back when stuck)")
    parser.add_argument("--seed-style", choices=SEED_STYLES, default="standard")
    parser.add_argument("--start-position", choices=START_POSITION_MODES, default="vanilla",
                        help="Where a new save opens: at the Odyssey (vanilla), at a sub-area "
                             "exit in the first kingdom (overworld), or anywhere it can walk "
                             "back from (max)")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = make_arg_parser().parse_args(argv)
    world = load_world(args.input, args.project)
    allocation_enabled = args.moon_allocation != "off"
    settings = LogicSettings(
        seed=args.seed,
        entrance_rando=not args.no_entrance_rando,
        capture_rando=not args.no_capture_rando,
        ability_rando=not args.no_ability_rando,
        moon_reallocation_enabled=allocation_enabled,
        randomize_moon_allocation=allocation_enabled,
        moon_allocation_difficulty=(
            "vanilla" if args.moon_allocation == "off" else args.moon_allocation
        ),
        moon_coin_policy=args.moon_coin_policy,
        logic_difficulty=args.logic_difficulty,
        all_kingdoms_start_unlocked=args.all_kingdoms_start_unlocked,
        start_kingdom_state_rando=args.randomize_start_kingdom_state,
        start_position_mode=args.start_position,
        prioritize_difficult_movement=args.prioritize_difficult_movement,
        seed_style=args.seed_style,
        enemy_swap=args.enemy_swap,
        npc_swap=args.npc_swap,
        prop_swap=args.prop_swap,
        prop_swap_chance=max(0, min(100, args.prop_swap_chance)),
        two_d_swap=args.two_d_swap,
        keep_critical_captures=args.keep_critical_captures,
    )
    result = LogicRandomizer(world, settings).randomize()
    write_json_result(args.output_json, world, result)
    write_spoiler(args.spoiler, world, result, args.seed)

    for warning in result.warnings:
        print(f"warning: {warning}")
    print(f"wrote {args.output_json}")
    print(f"wrote {args.spoiler}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
