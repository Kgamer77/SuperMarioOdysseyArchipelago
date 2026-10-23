"""Shared Enemy/Capture Swap data model and deterministic assignment helpers.

The curated entity catalog, generated placement inventory, authored swap
rules, and graph-authored capture gates are deliberately separate:

* config/enemy_swap_entities.csv
* config/enemy_capture_map.csv
* config/enemy_swap_rules.csv
* logic/templates/capture_gates.csv
* logic/templates/capture_gate_objects.csv

tools/generate_enemy_swap_db.py imports this module to emit the console
database. logic/randomizer_logic.py imports it so host reachability uses the
same assignment rules. Generated files are never authoritative.
"""

from __future__ import annotations

import csv
import itertools
import re
from dataclasses import dataclass, field, replace
from pathlib import Path
from typing import Callable, Iterable, Sequence

ROOT = Path(__file__).resolve().parents[1]
CONFIG_DIR = ROOT / "config"
TEMPLATES_DIR = ROOT / "logic" / "templates"

U32 = 0xFFFFFFFF

# Prop targets need enough physical presence to stand on their own when moved
# away from their authored terrain.  Collisionless decorations at least one
# metre wide and very wide/shallow terrain overlays are source-only. Explicit
# gameplay props requested for the pool remain eligible despite their shape.
LARGE_COLLISIONLESS_PROP_FOOTPRINT = 1000
FLAT_WIDE_PROP_MIN_FOOTPRINT = 1000
FLAT_WIDE_PROP_MAX_HEIGHT = 600
FLAT_WIDE_PROP_ASPECT = 4
# A prop whose collision does not describe its model is the same defect twice
# over: the player walks through what they can see, and what they can see is
# nowhere near where the collision says it is. PeachWorldHomeFountain is the
# clearest case -- a 2280-unit fountain behind a 65-unit collision box -- and a
# bare model with no collision mesh at all is the degenerate version of it.
# Small collisionless actors (KickStone, BlowObjFootBall, birds) are exempt by
# size: they are physics/ambient objects, not scenery pretending to be solid.
BROKEN_COLLISION_MIN_MODEL_SPAN = 300
BROKEN_COLLISION_COVERAGE = 3
# Terrain dressing: ankle-high, and at least twice as broad as it is tall on
# both horizontal axes. Height has to be the Y axis and not simply the
# thinnest one -- a sign board, a banner and a wall poster are all thin sheets
# too, and they stand upright rather than lying on the ground.
LOW_OVERLAY_MAX_HEIGHT = 60
LOW_OVERLAY_ASPECT = 2
PROP_SHAPE_FILTER_EXCEPTIONS = frozenset({
    "CityWorldHomeFrame000",
    "CityWorldHomeSwingParts000",
    "SandWorldHomeIce000",
    "SandWorldHomeIce001",
    "SandWorldHomeIce002",
    "SandWorldHomeIce003",
    "SandWorldHomeIcePillar000",
    "SandWorldHomeIcePillar001",
    "SandWorldHomeIcePillar002",
    "SandWorldHomeIcePillar003",
    "SandWorldHomeIcePillar004",
    "CrystalBreakableIce",
    "CrystalSmallBreakableIce",
})

# Bump whenever assignment ordering, hashing, or memory eligibility changes.
ASSIGNMENT_DATA_VERSION = 12
MEMORY_DATA_VERSION = 2
ATTEMPT_SALT = 0x165667B1
DOMAIN_SALT = 0xA11CE5D2
ANY_SALT = 0x27220A95
PROP_SALT = 0xB5297A4D
PROP_DELETE_SALT = 0xD31E7E5B
PROP_DELETE_CHANCE_PERCENT = 25
SKIP_SALT = 0x7F4A9C2D
TOKEN_MIX = 0x9E3779B1
DISABLED_CAPTURE_TOKEN_PREFIX = "__enemy_swap_disabled__:"

POLICY_KEEP = "keep"
POLICY_RANDOMIZE = "randomize"
# Source compatibility aliases. Parsed records always use POLICY_RANDOMIZE.
POLICY_ANY = POLICY_RANDOMIZE
POLICY_LOGIC = POLICY_RANDOMIZE
LEGACY_RANDOMIZE_POLICIES = frozenset({"any", "logic", POLICY_RANDOMIZE})

MATCH_ALL = "all"
MATCH_ANY = "any"
MATCH_AT_LEAST = "at_least"
# Requires exactly 1 witness (same floor as MATCH_ANY) but additionally caps
# how many of the gate's own members may resolve to the replacement token at
# minimum_count -- once that cap is reached, the unconstrained-placement
# random fill excludes the replacement token from the domain of this gate's
# remaining members, guaranteeing no coincidental duplicate from the same
# physical pool.
MATCH_AT_MOST = "at_most"
MATCH_MODES = frozenset({MATCH_ALL, MATCH_ANY, MATCH_AT_LEAST, MATCH_AT_MOST})
MAX_PLACEMENT_SCENARIO = 15

# Which source domain a spawn restriction applies to. Kept in step with the
# runtime's EnemySwapDB::SourceMask. The restrictions themselves were measured
# on console; see logic/reports/enemy_swap_audit/FINDINGS.md for where the rows
# in config/enemy_spawn_restrictions.csv came from.
SOURCE_MASK_COMBAT = 1 << 0
SOURCE_MASK_NPC = 1 << 1
SOURCE_MASK_BOTH = SOURCE_MASK_COMBAT | SOURCE_MASK_NPC

FAILURE_NONE = 0
FAILURE_UNSATISFIED_CONSTRAINTS = 1
FAILURE_WITNESS_VERIFICATION = 2
FAILURE_MEMORY_BUDGET = 3
FAILURE_SPAWN_RESTRICTED = 4

# These are deliberately conservative fallbacks for the legacy archive-only
# CSV. They are not presented as measurements: a complete stock romfs and a
# runtime audit can replace them with measured values without changing the
# evaluator or its generated ABI.
UNKNOWN_RESOURCE_BYTES = 2 * 1024 * 1024
DEFAULT_INSTANCE_BYTES = {
    "capture": (512 * 1024, 128 * 1024),
    "enemy": (384 * 1024, 96 * 1024),
    "enemy2d": (256 * 1024, 64 * 1024),
    "generator": (256 * 1024, 128 * 1024),
    "npc": (512 * 1024, 128 * 1024),
    "prop": (128 * 1024, 32 * 1024),
}

# GpuMemAllocator owns monotonic ModelUBO and ShaderOptionUBO arenas.  Unlike
# the stage heaps, allocations from these arenas are not reclaimed between
# individual actor creations, so runtime admission must leave enough room for
# the target model and for vanilla models created later in the scene.  Audit
# promotion can replace these conservative category defaults with measured
# per-target values from enemy_swap_costs.csv.
DEFAULT_GPU_BYTES = {
    "capture": (512 * 1024, 256 * 1024),
    "enemy": (384 * 1024, 192 * 1024),
    "enemy2d": (256 * 1024, 128 * 1024),
    "generator": (1024 * 1024, 512 * 1024),
    "npc": (512 * 1024, 256 * 1024),
    "prop": (256 * 1024, 128 * 1024),
}


# Chaos swap modes. Mirrors Randomizer::ChaosSwapMode in
# include/Randomizer/Randomizer.h; the values are persisted in save headers, so
# they must not be reordered. Mode 3 preserves mode 2's target pool and adds
# deterministic deletion for props.
SWAP_MODE_OFF = 0
SWAP_MODE_SAME_CATEGORY = 1
SWAP_MODE_WITH_COMBAT = 2
SWAP_MODE_WITH_DELETION = 3
NPC_SWAP_MODES = (
    SWAP_MODE_OFF, SWAP_MODE_SAME_CATEGORY, SWAP_MODE_WITH_COMBAT,
)
PROP_SWAP_MODES = NPC_SWAP_MODES + (SWAP_MODE_WITH_DELETION,)

# Mirrors EnemySwapRando::kDeletedPlacementEntity. Unlike -1 (no assignment),
# this is an intentional result: the actor factory must return no actor.
DELETED_PLACEMENT_ENTITY = -2


DEFAULT_GENERATOR_PEAK_BYTES = 4 * 1024 * 1024

DIFFICULTY_RANK = {
    "casual": 0,
    "advanced": 1,
    "expert": 2,
    "glitched": 3,
}
MAX_DIFFICULTY_RANK = max(DIFFICULTY_RANK.values())

STAGE_LOCKED_CAPTURES = frozenset({
    ("KaronWingTowerStage", "KaronWing"),
})


def is_stage_locked_capture(logic_stage: str, capture_token: str) -> bool:
    return (logic_stage, capture_token) in STAGE_LOCKED_CAPTURES


def disabled_capture_token(logic_stage: str, capture_token: str) -> str:
    return f"{DISABLED_CAPTURE_TOKEN_PREFIX}{logic_stage}:{capture_token}"


def is_disabled_capture_token(token: str) -> bool:
    return token.startswith(DISABLED_CAPTURE_TOKEN_PREFIX)


def fnv1a(text: str) -> int:
    value = 2166136261
    for byte in text.encode("utf-8"):
        value = ((value ^ byte) * 16777619) & U32
    return value


def mix(value: int) -> int:
    value = (value + 0x9E3779B9) & U32
    value = ((value ^ (value >> 16)) * 0x85EBCA6B) & U32
    value = ((value ^ (value >> 13)) * 0xC2B2AE35) & U32
    return (value ^ (value >> 16)) & U32


def swap_base(seed: int, attempt: int) -> int:
    return (seed ^ ((attempt * ATTEMPT_SALT) & U32) ^ DOMAIN_SALT) & U32


def _parse_bool(value: str) -> bool:
    return str(value).strip().lower() in ("1", "true", "yes", "y")


def _parse_scenario_mask(value: str) -> int:
    value = (value or "").strip()
    if not value:
        return U32
    result = 0
    for item in value.replace("|", ";").split(";"):
        item = item.strip()
        if not item:
            continue
        try:
            scenario = int(item)
        except ValueError:
            continue
        if 0 <= scenario < 32:
            result |= 1 << scenario
    return result or U32


def _read_csv(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def _requirement_dnf(expression: str) -> list[tuple[str, ...]]:
    """Parse the graph editor's &, |, and parenthesis grammar into DNF."""
    raw = (expression or "").strip()
    if not raw or raw.lower() in {"free", "impossible"}:
        return []
    tokens: list[str] = []
    pattern = re.compile(r"\s*([A-Za-z0-9_]+|[()&|])\s*")
    cursor = 0
    while cursor < len(raw):
        match = pattern.match(raw, cursor)
        if not match:
            raise ValueError(
                f"invalid requirement near '{raw[cursor:cursor + 12]}'"
            )
        tokens.append(match.group(1))
        cursor = match.end()
    index = 0

    def parse_primary():
        nonlocal index
        if index >= len(tokens):
            raise ValueError("unexpected end of requirement")
        token = tokens[index]
        if token == "(":
            index += 1
            node = parse_or()
            if index >= len(tokens) or tokens[index] != ")":
                raise ValueError("missing ')' in requirement")
            index += 1
            return node
        if token in {")", "&", "|"}:
            raise ValueError(f"unexpected token '{token}'")
        index += 1
        return ("token", token)

    def parse_and():
        nonlocal index
        node = parse_primary()
        while index < len(tokens) and tokens[index] == "&":
            index += 1
            node = ("and", node, parse_primary())
        return node

    def parse_or():
        nonlocal index
        node = parse_and()
        while index < len(tokens) and tokens[index] == "|":
            index += 1
            node = ("or", node, parse_and())
        return node

    ast = parse_or()
    if index != len(tokens):
        raise ValueError(f"unexpected token '{tokens[index]}'")

    def to_dnf(node) -> list[set[str]]:
        kind = node[0]
        if kind == "token":
            return [{node[1]}]
        left = to_dnf(node[1])
        right = to_dnf(node[2])
        if kind == "or":
            return left + right
        return [a | b for a in left for b in right]

    return [
        tuple(sorted(alternative))
        for alternative in to_dnf(ast)
    ]


def _option_scenario_masks(value: str, option_count: int) -> list[int]:
    raw = (value or "").strip()
    if not raw:
        return [0] * max(1, option_count)

    def parse_group(group: str) -> int:
        mask = 0
        for item in group.split(","):
            try:
                scenario = int(item.strip())
            except ValueError:
                continue
            if 0 <= scenario < 32:
                mask |= 1 << scenario
        return mask

    parsed = [parse_group(group) for group in raw.split(";")]
    if len(parsed) == 1:
        return parsed * max(1, option_count)
    return [
        parsed[index] if index < len(parsed) else 0
        for index in range(max(1, option_count))
    ]


def _option_difficulties(value: str, option_count: int) -> list[int]:
    parsed = [
        DIFFICULTY_RANK.get(item.strip().lower(), 0)
        for item in (value or "").split(";")
        if item.strip()
    ]
    if not parsed:
        return [0] * max(1, option_count)
    if len(parsed) == 1:
        return parsed * max(1, option_count)
    return [
        parsed[index] if index < len(parsed) else 0
        for index in range(max(1, option_count))
    ]


@dataclass(frozen=True)
class SwapEntity:
    index: int
    name: str
    friendly_name: str
    class_name: str
    category: str
    capture_token: str
    swap_out: bool
    any_pool: bool
    prop_footprint: int = 0
    prop_height: int = 0
    prop_solid: bool = False
    # Raw axis extents of the collision mesh and of the model, both zero when
    # the archive has neither. Unlike footprint/height these are never a
    # clipping sphere, so a flat decal still reads as flat.
    prop_collision_axes: tuple[int, int, int] = (0, 0, 0)
    prop_model_axes: tuple[int, int, int] = (0, 0, 0)

    @property
    def prop_spatial_known(self) -> bool:
        return self.prop_footprint > 0 and self.prop_height > 0

    @property
    def combat_target(self) -> bool:
        return self.any_pool and self.category.lower() in {
            "capture", "enemy", "generator",
        }

    @property
    def menu_target(self) -> bool:
        return self.any_pool and self.category.lower() in {
            "capture", "enemy",
        }

    @property
    def prop_collision_span(self) -> int:
        return max(self.prop_collision_axes)

    @property
    def prop_model_span(self) -> int:
        return max(self.prop_model_axes)

    @property
    def prop_collision_is_broken(self) -> bool:
        """Whether the model is large and the collision does not describe it."""

        if self.prop_model_span < BROKEN_COLLISION_MIN_MODEL_SPAN:
            return False
        return (
            self.prop_collision_span * BROKEN_COLLISION_COVERAGE
            < self.prop_model_span
        )

    @property
    def prop_is_low_overlay(self) -> bool:
        """Whether the prop lies flat on the ground rather than standing on it.

        The collision box wins when the actor has one: it is what Mario stands
        on, and a manhole cover with a knee-high collision volume is still
        terrain dressing. It is also the box whose Y axis is reliably up --
        collision meshes are authored in placement space, while a model box
        comes out of the skeleton and a few actors are modelled lying down.
        Falling back to the model covers the collisionless decorations, whose
        footprint/height would otherwise come from a symmetric clipping sphere
        and never look flat at all.
        """

        width, height, depth = (
            self.prop_collision_axes if self.prop_collision_span > 0
            else self.prop_model_axes
        )
        if height <= 0 or min(width, depth) <= 0:
            return False
        return (
            height < LOW_OVERLAY_MAX_HEIGHT
            and min(width, depth) >= height * LOW_OVERLAY_ASPECT
        )

    @property
    def prop_target_eligible(self) -> bool:
        if self.category.lower() != "prop":
            return True
        if not self.prop_spatial_known:
            return False
        if (
            not self.prop_solid
            and self.prop_footprint >= LARGE_COLLISIONLESS_PROP_FOOTPRINT
        ):
            return False
        if self.prop_collision_is_broken:
            return False
        if self.name in PROP_SHAPE_FILTER_EXCEPTIONS:
            return True
        if self.prop_is_low_overlay:
            return False
        if (
            self.prop_footprint >= FLAT_WIDE_PROP_MIN_FOOTPRINT
            and self.prop_height <= FLAT_WIDE_PROP_MAX_HEIGHT
            and self.prop_footprint
                >= self.prop_height * FLAT_WIDE_PROP_ASPECT
        ):
            return False
        return True

    @property
    def full_actor_target(self) -> bool:
        # Every category that can be built as a replacement actor. NPCs and
        # props are only ever reachable through their own category's swap mode
        # (see mode_domain), but the runtime still needs the flag set to be
        # allowed to construct them at all.
        return (
            self.any_pool
            and self.prop_target_eligible
            and self.category.lower() in {
                "capture", "enemy", "generator", "npc", "prop", "enemy2d",
            }
        )


@dataclass(frozen=True)
class SwapPlacement:
    index: int
    stage: str
    logic_stage: str
    object_id: str
    unit_config_name: str
    entity: int
    category: str
    capture_token: str
    scenario_mask: int
    list_name: str = ""
    layer: str = ""
    x: str = ""
    y: str = ""
    z: str = ""
    event_name: str = ""

    @property
    def key(self) -> tuple[str, str]:
        return self.stage, self.object_id

    @property
    def runtime_source_safe(self) -> bool:
        if self.category != "npc":
            return True
        event_name = self.event_name.strip().lower()
        return (
            not event_name
            or event_name == "none"
            or "onlyballoon" in event_name
        )


@dataclass(frozen=True)
class SwapRule:
    index: int
    placement: int
    policy: str
    pool: tuple[int, ...] = ()


@dataclass(frozen=True)
class CaptureGate:
    index: int
    gate_id: str
    kingdom: str
    logic_stage: str
    edge_id: str
    source_capture_token: str
    replacement_capture_token: str
    difficulty: int
    match_mode: str
    minimum_count: int
    members: tuple[int, ...]
    notes: str = ""

    def required_count(self) -> int:
        if self.match_mode == MATCH_ALL:
            return len(self.members)
        if self.match_mode in (MATCH_ANY, MATCH_AT_MOST):
            return 1
        return self.minimum_count

    def maximum_count(self) -> int | None:
        """Cap on how many of this gate's members may resolve to the
        replacement token (MATCH_AT_MOST only); minimum_count is repurposed
        to hold it. None means no cap."""
        return self.minimum_count if self.match_mode == MATCH_AT_MOST else None


@dataclass(frozen=True)
class RouteCaptureOption:
    source_capture_tokens: tuple[str, ...]
    scenario_mask: int
    difficulty: int
    # True when every token in this alternative (not just the ones needing a
    # gate) is a capture -- e.g. "Frog" alone, not "Frog & WallJump". Such an
    # alternative existed as a capture-only solution before randomization, so
    # at least one capture-only alternative on a requirement must always stay
    # satisfiable independent of the selected logic difficulty (see solve()).
    is_capture_only: bool = False


@dataclass(frozen=True)
class RouteCaptureConstraint:
    edge_id: str
    kingdom: str
    logic_stage: str
    options: tuple[RouteCaptureOption, ...]


class UnsatisfiedCaptureGates(RuntimeError):
    def __init__(self, message: str, code: int) -> None:
        super().__init__(message)
        self.code = code


class MemoryBudgetExceeded(UnsatisfiedCaptureGates):
    """The resolved assignment would exhaust an authored stage budget."""

    def __init__(self, message: str, *, stage: str, scenario: int,
                 extra_resource_bytes: int, max_extra_resource_bytes: int,
                 extra_scene_bytes: int, max_extra_scene_bytes: int) -> None:
        super().__init__(message, FAILURE_MEMORY_BUDGET)
        self.stage = stage
        self.scenario = scenario
        self.extra_resource_bytes = extra_resource_bytes
        self.max_extra_resource_bytes = max_extra_resource_bytes
        self.extra_scene_bytes = extra_scene_bytes
        self.max_extra_scene_bytes = max_extra_scene_bytes


class SpawnRestrictionViolated(UnsatisfiedCaptureGates):
    """The resolved assignment places an entity where the audit forbids it."""

    def __init__(self, message: str, *, stage: str, scenario: int,
                 entity: int, count: int, max_instances: int) -> None:
        super().__init__(message, FAILURE_SPAWN_RESTRICTED)
        self.stage = stage
        self.scenario = scenario
        self.entity = entity
        self.count = count
        self.max_instances = max_instances


@dataclass(frozen=True)
class EntityMemoryCost:
    """Per-entity memory data shared by host evaluation and code generation.

    ``resource_keys`` and ``resource_key_bytes`` are parallel arrays. A key is
    charged once per stage/scenario, while instance fields are charged for
    every live placement. Keeping the resource table separate is important
    for variants such as TRexPatrol that share an archive with TRex.
    """

    first_resource_bytes: int
    first_instance_bytes: int
    additional_instance_bytes: int
    generator_peak_bytes: int
    resource_keys: tuple[str, ...]
    resource_key_bytes: tuple[int, ...]
    heap_domain: str
    measurement_status: str
    confidence: str
    measured: bool
    model_ubo_bytes: int = 512 * 1024
    shader_option_ubo_bytes: int = 256 * 1024

    @property
    def bounded(self) -> bool:
        return (
            self.first_resource_bytes > 0
            and self.first_instance_bytes > 0
            and self.additional_instance_bytes > 0
            and (self.generator_peak_bytes > 0)
            and self.model_ubo_bytes > 0
            and self.shader_option_ubo_bytes > 0
            and bool(self.resource_keys)
            and len(self.resource_keys) == len(self.resource_key_bytes)
            and all(value > 0 for value in self.resource_key_bytes)
        )


@dataclass(frozen=True)
class StageMemoryBudget:
    stage: str
    scenario: int
    max_extra_resource_bytes: int
    max_extra_scene_bytes: int
    reserve_resource_bytes: int
    reserve_scene_bytes: int


def scenario_mask_bit(scenario: int) -> int:
    """Placement scenario_mask bit for a 1-based game ScenarioNo.

    Placement masks are 0-based (bit 0 is ScenarioNo 1) while the game and
    every recorded measurement report a 1-based ScenarioNo. Getting this wrong
    selects a neighbouring scenario's placements silently, so the conversion
    lives in one place.
    """

    return 1 << (scenario - 1) if scenario >= 1 else 0


@dataclass(frozen=True)
class SpawnRestriction:
    """One audited "do not place entity X here" exception.

    Keyed on the physical stage and the 1-based ScenarioNo the failure was
    observed in, never widened to the entity as a whole. A row is an exception;
    no row means the placement is allowed by compatibility data.
    """

    stage: str
    scenario: int
    entity: int
    source_mask: int
    max_instances: int
    reason: str
    status: str

    @property
    def confirmed(self) -> bool:
        return self.status == "confirmed"

    @property
    def scenario_bit(self) -> int:
        return scenario_mask_bit(self.scenario)

    def covers(self, source_mask: int) -> bool:
        return bool(self.source_mask & source_mask)

    def applies_to(self, placement: "SwapPlacement") -> bool:
        """Whether this placement is inside the restricted stage+scenario.

        A restriction names the physical stage, but placements inside an
        embedded zone carry the zone as ``stage`` and the physical stage only
        as ``logic_stage`` -- CityWorldFactory01Zone/obj632 is in
        CityWorldFactoryStage. Both have to match the recorded stage or the
        restriction covers nothing.
        """

        if placement.stage != self.stage and placement.logic_stage != self.stage:
            return False
        return bool(placement.scenario_mask & self.scenario_bit)


@dataclass(frozen=True)
class MemoryUsage:
    stage: str
    scenario: int
    vanilla_resource_bytes: int
    resolved_resource_bytes: int
    vanilla_scene_bytes: int
    resolved_scene_bytes: int

    @property
    def extra_resource_bytes(self) -> int:
        return self.resolved_resource_bytes - self.vanilla_resource_bytes

    @property
    def extra_scene_bytes(self) -> int:
        return self.resolved_scene_bytes - self.vanilla_scene_bytes


@dataclass(frozen=True)
class ResolvedAssignment:
    entities: tuple[int, ...]
    witness_gate_ids: tuple[str, ...]


def reliable_capture_tokens(entities: Iterable[SwapEntity]) -> set[str]:
    producible: set[str] = set()
    swappable: set[str] = set()
    for entity in entities:
        if not entity.capture_token:
            continue
        producible.add(entity.capture_token)
        if entity.swap_out:
            swappable.add(entity.capture_token)
    return producible - swappable


@dataclass
class SwapConfig:
    entities: list[SwapEntity]
    placements: list[SwapPlacement]
    rules: list[SwapRule]
    gates: list[CaptureGate]
    any_pool: list[int]
    menu_pool: list[int]
    npc_pool: list[int]
    prop_pool: list[int]
    two_d_pool: list[int]
    entity_by_name: dict[str, int]
    placement_by_key: dict[tuple[str, str], int]
    gates_by_edge_token: dict[tuple[str, str, str], list[int]]
    route_constraints: list[RouteCaptureConstraint]
    reliable_captures: set[str] = field(default_factory=set)
    # (logic_stage, capture_token) -> placements pinned vanilla because logic
    # names the capture and no gate opts that stage into randomization.
    pinned_captures: dict[tuple[str, str], int] = field(default_factory=dict)
    memory_costs: dict[int, EntityMemoryCost] = field(default_factory=dict)
    stage_memory_budgets: tuple[StageMemoryBudget, ...] = ()
    memory_data_available: bool = False
    spawn_restrictions: tuple[SpawnRestriction, ...] = ()

    def prop_spatial_compatible(
        self, source_entity_index: int, target_entity_index: int
    ) -> bool:
        """Whether a prop target preserves the source's structural scale.

        The runtime uses the same source placement transform for the target,
        so comparing the actors' unscaled dimensions also compares their
        effective dimensions: the placement scale cancels from both ratios.
        Combat candidates in the explicit WithCombat mode are intentionally
        outside this prop-only safety contract.
        """

        if not (0 <= source_entity_index < len(self.entities)) \
                or not (0 <= target_entity_index < len(self.entities)):
            return False
        if source_entity_index == target_entity_index:
            return True
        source = self.entities[source_entity_index]
        target = self.entities[target_entity_index]
        if source.category != "prop" or target.category != "prop":
            return True
        if not source.prop_spatial_known or not target.prop_spatial_known:
            return False
        if source.prop_solid != target.prop_solid:
            return False

        def within_factor_two(left: int, right: int) -> bool:
            return left <= right * 2 and right <= left * 2

        return (
            within_factor_two(source.prop_footprint, target.prop_footprint)
            and within_factor_two(source.prop_height, target.prop_height)
        )

    def restrictions_for(
        self, placement: SwapPlacement, entity_index: int,
        source_mask: int = SOURCE_MASK_BOTH,
    ) -> list[SpawnRestriction]:
        """Every restriction that would govern this entity at this placement.

        Mirrors ``EnemySwapRando::spawnRestricted``. Keep the two in step: a
        divergence silently desynchronises host and runtime assignments.
        """

        return [
            restriction for restriction in self.spawn_restrictions
            if restriction.entity == entity_index
            and restriction.covers(source_mask)
            and restriction.applies_to(placement)
        ]

    def spawn_restriction_violations(
        self, resolved_entities: Sequence[int]
    ) -> list[tuple[SpawnRestriction, int]]:
        """Every restriction the resolved assignment exceeds, with its count."""

        if not self.spawn_restrictions:
            return []
        violations: list[tuple[SpawnRestriction, int]] = []
        for restriction in self.spawn_restrictions:
            count = sum(
                1 for placement in self.placements
                if restriction.applies_to(placement)
                and resolved_entities[placement.index] == restriction.entity
            )
            if count > restriction.max_instances:
                violations.append((restriction, count))
        return violations

    def _memory_instance_bytes(self, entity_index: int,
                               count: int) -> int:
        if count <= 0:
            return 0
        cost = self.memory_costs.get(entity_index)
        if cost is None:
            return 0
        entity = self.entities[entity_index]
        if entity.category == "generator":
            return cost.generator_peak_bytes * count
        return cost.first_instance_bytes + (
            max(0, count - 1) * cost.additional_instance_bytes
        )

    def _memory_usage_for(self, values: Sequence[int], stage: str,
                          scenario: int) -> tuple[int, int]:
        counts = [0] * len(self.entities)
        resources: dict[str, int] = {}
        for placement in self.placements:
            if placement.stage != stage:
                continue
            if not (placement.scenario_mask & (1 << scenario)):
                continue
            entity_index = values[placement.index]
            if entity_index < 0 or entity_index >= len(self.entities):
                continue
            cost = self.memory_costs.get(entity_index)
            if cost is None:
                continue
            counts[entity_index] += 1
            for key, size in zip(cost.resource_keys, cost.resource_key_bytes):
                resources[key] = max(resources.get(key, 0), size)
        resource_bytes = sum(resources.values())
        scene_bytes = sum(
            self._memory_instance_bytes(entity_index, count)
            for entity_index, count in enumerate(counts)
        )
        return resource_bytes, scene_bytes

    def memory_usage(self, resolved_entities: Sequence[int], stage: str,
                     scenario: int) -> MemoryUsage:
        vanilla = [placement.entity for placement in self.placements]
        vanilla_resource, vanilla_scene = self._memory_usage_for(
            vanilla, stage, scenario
        )
        resolved_resource, resolved_scene = self._memory_usage_for(
            resolved_entities, stage, scenario
        )
        return MemoryUsage(
            stage=stage,
            scenario=scenario,
            vanilla_resource_bytes=vanilla_resource,
            resolved_resource_bytes=resolved_resource,
            vanilla_scene_bytes=vanilla_scene,
            resolved_scene_bytes=resolved_scene,
        )

    def memory_budget_violations(
        self, resolved_entities: Sequence[int]
    ) -> list[tuple[StageMemoryBudget, MemoryUsage]]:
        if not self.memory_data_available or not self.stage_memory_budgets:
            return []
        violations = []
        for budget in self.stage_memory_budgets:
            usage = self.memory_usage(
                resolved_entities, budget.stage, budget.scenario
            )
            if (
                usage.extra_resource_bytes + budget.reserve_resource_bytes
                > budget.max_extra_resource_bytes
                or usage.extra_scene_bytes + budget.reserve_scene_bytes
                > budget.max_extra_scene_bytes
            ):
                violations.append((budget, usage))
        return violations

    def validate_spawn_restrictions(self, resolved_entities: Sequence[int],
                                    seed: int, attempt: int) -> None:
        violations = self.spawn_restriction_violations(resolved_entities)
        if not violations:
            return
        restriction, count = violations[0]
        raise SpawnRestrictionViolated(
            f"seed {seed} attempt {attempt}: "
            f"{self.entities[restriction.entity].name} appears {count} time(s) "
            f"in {restriction.stage} scenario {restriction.scenario}, "
            f"but the audit allows {restriction.max_instances} "
            f"({restriction.status}: {restriction.reason})",
            stage=restriction.stage,
            scenario=restriction.scenario,
            entity=restriction.entity,
            count=count,
            max_instances=restriction.max_instances,
        )

    def validate_memory_assignment(self, resolved_entities: Sequence[int],
                                   seed: int, attempt: int) -> None:
        violations = self.memory_budget_violations(resolved_entities)
        if not violations:
            return
        budget, usage = violations[0]
        raise MemoryBudgetExceeded(
            f"seed {seed} attempt {attempt}: memory budget exceeded for "
            f"{budget.stage} scenario {budget.scenario} "
            f"(resource +{usage.extra_resource_bytes}/"
            f"{budget.max_extra_resource_bytes}, scene +"
            f"{usage.extra_scene_bytes}/{budget.max_extra_scene_bytes})",
            stage=budget.stage,
            scenario=budget.scenario,
            extra_resource_bytes=usage.extra_resource_bytes,
            max_extra_resource_bytes=budget.max_extra_resource_bytes,
            extra_scene_bytes=usage.extra_scene_bytes,
            max_extra_scene_bytes=budget.max_extra_scene_bytes,
        )

    def placement_replacement(self, seed: int, attempt: int,
                              placement: SwapPlacement,
                              pool: list[int] | tuple[int, ...] | None = None) -> int:
        candidates = self.any_pool if pool is None else pool
        if not candidates:
            return -1
        base = swap_base(seed, attempt)
        pick = mix(base ^ fnv1a(placement.stage)
                   ^ ((fnv1a(placement.object_id) * TOKEN_MIX) & U32)
                   ^ ANY_SALT)
        return candidates[pick % len(candidates)]

    def build_stage_token_remap(self, seed: int,
                                attempt: int) -> dict[tuple[str, str], str]:
        # Edge-aware gates supersede global stage-token remapping. This remains
        # an identity mapping for older non-route call sites.
        del seed, attempt
        return {}

    def gate_matches(self, gate: CaptureGate,
                     resolved_entities: Sequence[int],
                     scenario: int | None = None) -> bool:
        if not gate.members:
            return False
        active = 0
        matching = 0
        scenario_index = scenario - 1 if scenario is not None and scenario > 0 else scenario
        for placement_index in gate.members:
            if placement_index < 0 or placement_index >= len(self.placements):
                return False
            placement = self.placements[placement_index]
            is_active = scenario_index is None or not (0 <= scenario_index < 32) \
                or bool(placement.scenario_mask & (1 << scenario_index))
            if not is_active:
                if gate.match_mode == MATCH_ALL:
                    return False
                continue
            active += 1
            entity_index = resolved_entities[placement_index]
            if 0 <= entity_index < len(self.entities) \
                    and self.entities[entity_index].capture_token \
                    == gate.replacement_capture_token:
                matching += 1
        if gate.match_mode == MATCH_ALL:
            return active == len(gate.members) and matching == active
        if gate.match_mode == MATCH_ANY:
            return matching >= 1
        if gate.match_mode == MATCH_AT_MOST:
            return 1 <= matching <= gate.minimum_count
        return matching >= gate.minimum_count

    def build_assignment(self, seed: int, attempt: int, *,
                         enemy_enabled: bool = True,
                         npc_mode: int = SWAP_MODE_OFF,
                         prop_mode: int = SWAP_MODE_OFF,
                         prop_chance: int = 0,
                         two_d_enabled: bool = False,
                         logic_difficulty: str | int = "casual",
                         keep_critical_captures: bool = False
                         ) -> ResolvedAssignment:
        """Construct a deterministic assignment satisfying every gate group.

        Gate groups are keyed by (edge, source capture). Each group chooses one
        OR alternative. This is the shared reference used by the host logic and
        mirrored by EnemySwapRando.cpp.
        """
        if npc_mode not in NPC_SWAP_MODES or prop_mode not in PROP_SWAP_MODES:
            raise ValueError(
                f"unknown swap mode: npc={npc_mode} prop={prop_mode}")
        resolved_npc_mode = npc_mode
        resolved_prop_mode = prop_mode

        def mode_domain(category: str) -> list[int]:
            """Targets a source of this category may become, per its mode.

            SameCategory keeps the swap inside the source's own pool; the
            combat pool is added on top of it for WithCombat rather than
            replacing it, so widening a mode never removes a target that the
            narrower mode allowed. WithDeletion keeps that same widened pool
            and separately allows deterministic prop suppression.
            """

            if category == "enemy2d":
                # 2D actors only ever become other 2D actors. There is no
                # wider mode: a 3D enemy dropped into a flat section has no
                # 2D behaviour to fall back on, and the section's camera and
                # collision are built for the plane it would ignore.
                return list(self.two_d_pool) if two_d_enabled else []
            if category == "npc":
                mode, own = resolved_npc_mode, self.npc_pool
            elif category == "prop":
                mode, own = resolved_prop_mode, self.prop_pool
            else:
                return list(self.any_pool)
            if mode == SWAP_MODE_OFF:
                return []
            if mode == SWAP_MODE_SAME_CATEGORY:
                return list(own)
            return list(own) + list(self.any_pool)

        resolved = [placement.entity for placement in self.placements]
        forced_tokens: list[str | None] = [None] * len(self.placements)
        witnesses: list[int] = []
        witness_scenarios: list[int] = []
        rule_by_placement = {rule.placement: rule for rule in self.rules}
        producers_by_token: dict[str, list[int]] = {}
        critical_tokens = {
            gate.source_capture_token for gate in self.gates
        }
        for entity in self.entities:
            if entity.combat_target and entity.capture_token:
                producers_by_token.setdefault(entity.capture_token, []).append(entity.index)

        def is_bowser_replacement(
            placement_index: int, entity_index: int
        ) -> bool:
            return (
                0 <= placement_index < len(self.placements)
                and 0 <= entity_index < len(self.entities)
                and entity_index != self.placements[placement_index].entity
                and self.entities[entity_index].name == "Koopa"
            )

        def stage_has_bowser_replacement(
            placement_index: int, *, ignore: int | None = None
        ) -> bool:
            placement = self.placements[placement_index]
            stage = placement.logic_stage or placement.stage
            return any(
                index != ignore
                and (candidate.logic_stage or candidate.stage) == stage
                and is_bowser_replacement(index, resolved[index])
                for index, candidate in enumerate(self.placements)
            )

        def category_enabled(placement: SwapPlacement) -> bool:
            category = placement.category
            if keep_critical_captures \
                    and placement.capture_token in critical_tokens:
                return False
            if category in {"capture", "enemy", "generator"}:
                return enemy_enabled
            if category == "npc":
                return resolved_npc_mode != SWAP_MODE_OFF
            if category == "enemy2d":
                return two_d_enabled
            if category == "prop":
                if resolved_prop_mode == SWAP_MODE_OFF:
                    return False
                if prop_chance >= 100:
                    return True
                if prop_chance <= 0:
                    return False
                roll = mix(
                    swap_base(seed, attempt)
                    ^ fnv1a(placement.stage)
                    ^ ((fnv1a(placement.object_id) * TOKEN_MIX) & U32)
                    ^ PROP_SALT
                )
                return roll % 100 < prop_chance
            return False

        def should_delete_prop(placement: SwapPlacement) -> bool:
            if resolved_prop_mode != SWAP_MODE_WITH_DELETION:
                return False
            roll = mix(
                swap_base(seed, attempt)
                ^ fnv1a(placement.stage)
                ^ ((fnv1a(placement.object_id) * TOKEN_MIX) & U32)
                ^ PROP_DELETE_SALT
            )
            return roll % 100 < PROP_DELETE_CHANCE_PERCENT

        def can_force(placement_index: int, capture_token: str) -> bool:
            existing_token = forced_tokens[placement_index]
            if existing_token and existing_token != capture_token:
                return False
            placement = self.placements[placement_index]
            if placement.entity >= 0 \
                    and self.entities[placement.entity].capture_token == capture_token:
                return True
            rule = rule_by_placement.get(placement_index)
            return bool(
                rule
                and rule.policy == POLICY_RANDOMIZE
                and category_enabled(placement)
                and producers_by_token.get(capture_token)
            )

        def producer_for(placement_index: int, capture_token: str,
                         gate_id: str) -> int:
            producers = producers_by_token.get(capture_token, [])
            if not producers:
                return -1
            placement = self.placements[placement_index]
            pick = mix(
                swap_base(seed, attempt)
                ^ fnv1a(gate_id)
                ^ fnv1a(placement.stage)
                ^ ((fnv1a(placement.object_id) * TOKEN_MIX) & U32)
            )
            return producers[pick % len(producers)]

        if isinstance(logic_difficulty, str):
            logic_rank = DIFFICULTY_RANK.get(
                logic_difficulty.strip().lower(), 0
            )
        else:
            logic_rank = max(0, min(MAX_DIFFICULTY_RANK, int(logic_difficulty)))

        def forcing_options(
            gate: CaptureGate, scenario: int
        ) -> list[tuple[int, ...]]:
            scenario_index = scenario - 1
            active_members = [
                member for member in gate.members
                if scenario_index < 0
                or scenario_index >= 32
                or (
                    self.placements[member].scenario_mask
                    & (1 << scenario_index)
                )
            ]
            eligible = [
                member for member in active_members
                if can_force(member, gate.replacement_capture_token)
            ]
            required = gate.required_count()
            if gate.match_mode == MATCH_ALL:
                return (
                    [gate.members]
                    if len(active_members) == len(gate.members)
                    and len(eligible) == len(gate.members)
                    else []
                )
            if required < 1 or len(eligible) < required:
                return []
            rotation = mix(
                swap_base(seed, attempt)
                ^ fnv1a(gate.gate_id)
                ^ ((scenario * TOKEN_MIX) & U32)
            ) % len(eligible)
            ordered = eligible[rotation:] + eligible[:rotation]
            return list(itertools.combinations(ordered, required))

        def matching_members(gate: CaptureGate, scenario: int) -> list[int]:
            """Members explicitly forced (by an earlier placement-scenario
            pass, or an earlier OR-alternative gate sharing this member) to
            this exact gate's replacement token. Deliberately checks
            forced_tokens rather than each member's raw resolved entity, so
            a member that merely still sits at its unmodified vanilla value
            (e.g. every member before any gate has been decided) is never
            mistaken for an earlier deliberate choice -- that would bias an
            identity (source == replacement) gate to always win instead of
            being chosen through the same fair rotation as any other
            alternative."""
            scenario_index = scenario - 1
            matched: list[int] = []
            for member in gate.members:
                if not (
                    scenario_index < 0
                    or scenario_index >= 32
                    or (
                        self.placements[member].scenario_mask
                        & (1 << scenario_index)
                    )
                ):
                    continue
                if forced_tokens[member] == gate.replacement_capture_token:
                    matched.append(member)
            return matched

        constraint_instances: list[
            tuple[RouteCaptureConstraint, int]
        ] = []
        for constraint in self.route_constraints:
            for scenario in range(1, MAX_PLACEMENT_SCENARIO + 1):
                if any(
                    (option.difficulty <= logic_rank or option.is_capture_only)
                    and (
                        option.scenario_mask == 0
                        or scenario > 5
                        or option.scenario_mask & (1 << scenario)
                    )
                    for option in constraint.options
                ):
                    constraint_instances.append((constraint, scenario))

        member_frequency: dict[int, int] = {}
        for constraint, _ in constraint_instances:
            for option in constraint.options:
                for source_token in option.source_capture_tokens:
                    if keep_critical_captures \
                            and source_token in critical_tokens:
                        continue
                    for gate_index in self.gates_by_edge_token.get(
                        (constraint.kingdom, constraint.edge_id, source_token), []
                    ):
                        if self.gates[gate_index].difficulty > logic_rank:
                            continue
                        for member in self.gates[gate_index].members:
                            member_frequency[member] = (
                                min(
                                    0xFFFF,
                                    member_frequency.get(member, 0) + 1,
                                )
                            )

        def constraint_key(
            item: tuple[RouteCaptureConstraint, int]
        ) -> tuple[int, int, str, int]:
            constraint, scenario = item
            candidate_count = 0
            overlap = 0
            for option in constraint.options:
                if option.difficulty > logic_rank:
                    continue
                if option.scenario_mask \
                        and scenario <= 5 \
                        and not option.scenario_mask & (1 << scenario):
                    continue
                option_candidates = 1
                for source_token in option.source_capture_tokens:
                    if keep_critical_captures \
                            and source_token in critical_tokens:
                        continue
                    gate_indices = [
                        index for index in self.gates_by_edge_token.get(
                            (constraint.kingdom, constraint.edge_id, source_token), []
                        )
                        if self.gates[index].difficulty <= logic_rank
                    ]
                    option_candidates *= max(1, len(gate_indices))
                    option_candidates = min(
                        0xFFFF, option_candidates
                    )
                    for gate_index in gate_indices:
                        overlap = (
                            overlap + sum(
                                member_frequency.get(member, 0)
                                for member in self.gates[
                                    gate_index
                                ].members
                            )
                        ) & U32
                candidate_count += option_candidates
                candidate_count = min(0xFFFF, candidate_count)
            return (
                candidate_count,
                -overlap,
                constraint.edge_id,
                scenario,
            )

        constraint_instances.sort(key=constraint_key)

        def apply_members(
            gate: CaptureGate, members: tuple[int, ...]
        ) -> tuple[bool, list[tuple[int, int, str | None]]]:
            changes: list[tuple[int, int, str | None]] = []
            for placement_index in members:
                existing_token = forced_tokens[placement_index]
                if existing_token \
                        and existing_token != gate.replacement_capture_token:
                    return False, changes
                current_entity = resolved[placement_index]
                current_token = (
                    self.entities[current_entity].capture_token
                    if 0 <= current_entity < len(self.entities) else ""
                )
                if current_token == gate.replacement_capture_token:
                    if not existing_token:
                        changes.append(
                            (placement_index, current_entity, existing_token)
                        )
                        forced_tokens[placement_index] = (
                            gate.replacement_capture_token
                        )
                    continue
                producer = producer_for(
                    placement_index,
                    gate.replacement_capture_token,
                    gate.gate_id,
                )
                if producer < 0:
                    return False, changes
                if is_bowser_replacement(placement_index, producer) \
                        and stage_has_bowser_replacement(
                            placement_index, ignore=placement_index
                        ):
                    return False, changes
                changes.append(
                    (placement_index, current_entity,
                     forced_tokens[placement_index])
                )
                resolved[placement_index] = producer
                forced_tokens[placement_index] = (
                    gate.replacement_capture_token
                )
            return True, changes

        def rollback(
            changes: list[tuple[int, int, str | None]]
        ) -> None:
            for placement_index, entity_index, forced_token \
                    in reversed(changes):
                resolved[placement_index] = entity_index
                forced_tokens[placement_index] = forced_token

        witness_descriptions: list[str] = []

        def solve_sources(
            constraint_index: int,
            constraint: RouteCaptureConstraint,
            scenario: int,
            source_tokens: tuple[str, ...],
            source_index: int,
        ) -> bool:
            if source_index >= len(source_tokens):
                return solve(constraint_index + 1)
            source_token = source_tokens[source_index]
            gate_indices = sorted(
                (
                    index for index in self.gates_by_edge_token.get(
                        (constraint.kingdom, constraint.edge_id, source_token), []
                    )
                    if self.gates[index].difficulty <= logic_rank
                ),
                key=lambda index: (
                    self.gates[index].required_count(),
                    self.gates[index].gate_id,
                ),
            )
            if not gate_indices:
                return False
            rotation = mix(
                swap_base(seed, attempt)
                ^ fnv1a(constraint.edge_id)
                ^ fnv1a(source_token)
                ^ ((scenario * TOKEN_MIX) & U32)
            ) % len(gate_indices)
            gate_indices = (
                gate_indices[rotation:] + gate_indices[:rotation]
            )

            def try_gate(gate_index: int, members: tuple[int, ...]) -> bool:
                gate = self.gates[gate_index]
                compatible, changes = apply_members(gate, members)
                if compatible:
                    witnesses.append(gate_index)
                    witness_scenarios.append(scenario)
                    witness_descriptions.append(
                        f"{constraint.edge_id}@{scenario}:{gate.gate_id}"
                    )
                    if solve_sources(
                        constraint_index, constraint, scenario,
                        source_tokens, source_index + 1,
                    ):
                        return True
                    witness_descriptions.pop()
                    witness_scenarios.pop()
                    witnesses.pop()
                rollback(changes)
                return False

            # This edge/source-token requirement is solved once per
            # placement scenario (see constraint_instances above). If an
            # earlier scenario pass (or an earlier OR-alternative sharing a
            # member) already forced enough members to satisfy one of these
            # alternative gates, reusing that exact alternative is always
            # solvable -- but always reusing collapses many distinct
            # requirements onto whatever was decided first. So reuse_first()
            # and fresh_first() below are tried in a seed-random order
            # instead of a fixed one, with the loser always attempted as a
            # fallback so the coin flip can never turn a solvable seed
            # unsolvable.
            def reuse_first() -> bool:
                for gate_index in gate_indices:
                    gate = self.gates[gate_index]
                    matched = matching_members(gate, scenario)
                    if len(matched) < gate.required_count():
                        continue
                    if try_gate(gate_index, tuple(matched[:gate.required_count()])):
                        return True
                return False

            def fresh_first() -> bool:
                # A fair seed-random choice among every OR-alternative
                # (including identity gates) via the same rotation used
                # above.
                for gate_index in gate_indices:
                    gate = self.gates[gate_index]
                    for members in forcing_options(gate, scenario):
                        if try_gate(gate_index, members):
                            return True
                return False

            skip_reuse = mix(
                swap_base(seed, attempt)
                ^ fnv1a(constraint.edge_id)
                ^ fnv1a(source_token)
                ^ ((scenario * TOKEN_MIX) & U32)
                ^ SKIP_SALT
            ) % 100 < 50
            if skip_reuse:
                return fresh_first() or reuse_first()
            return reuse_first() or fresh_first()

        def solve(constraint_index: int) -> bool:
            if constraint_index >= len(constraint_instances):
                return True
            constraint, scenario = (
                constraint_instances[constraint_index]
            )

            def scenario_allowed(option: RouteCaptureOption) -> bool:
                return (
                    option.scenario_mask == 0
                    or scenario > 5
                    or option.scenario_mask & (1 << scenario)
                )

            # A capture-only alternative (nothing but capture token(s), e.g.
            # "Frog" alone rather than "Frog & WallJump") existed as a
            # solution before randomization. At least one such alternative
            # must always stay satisfiable, independent of its own tagged
            # difficulty or the selected logic difficulty -- Enemy/Capture
            # Chaos must never take away a capture-only solution that
            # existed in the original logic. This is a hard requirement: if
            # no capture-only alternative can be witnessed, the constraint
            # fails rather than silently falling back to an ability route.
            capture_only_options = [
                option for option in constraint.options
                if option.is_capture_only and scenario_allowed(option)
            ]
            if capture_only_options:
                rotation = mix(
                    swap_base(seed, attempt)
                    ^ fnv1a(constraint.edge_id)
                    ^ fnv1a("capture_only")
                    ^ ((scenario * TOKEN_MIX) & U32)
                ) % len(capture_only_options)
                rotated = (
                    capture_only_options[rotation:]
                    + capture_only_options[:rotation]
                )
                for option in rotated:
                    source_tokens = tuple(
                        token for token in option.source_capture_tokens
                        if not keep_critical_captures
                        or token not in critical_tokens
                    )
                    if solve_sources(
                        constraint_index, constraint, scenario,
                        source_tokens, 0,
                    ):
                        return True
                return False

            options = [
                option for option in constraint.options
                if option.difficulty <= logic_rank
                and scenario_allowed(option)
            ]
            if not options:
                return solve(constraint_index + 1)
            rotation = mix(
                swap_base(seed, attempt)
                ^ fnv1a(constraint.edge_id)
                ^ ((scenario * TOKEN_MIX) & U32)
            ) % len(options)
            options = options[rotation:] + options[:rotation]
            for option in options:
                source_tokens = tuple(
                    token for token in option.source_capture_tokens
                    if not keep_critical_captures
                    or token not in critical_tokens
                )
                if solve_sources(
                    constraint_index,
                    constraint,
                    scenario,
                    source_tokens,
                    0,
                ):
                    return True
            return False

        # Route capture remapping is active only with Enemy/Capture Chaos.
        # NPC/prop-only configurations still build their deterministic
        # placement assignments, but preserve vanilla route-token behavior,
        # matching EnemySwapRando::buildRemapTable.
        if enemy_enabled and not solve(0):
            raise UnsatisfiedCaptureGates(
                f"seed {seed} attempt {attempt}: capture gates are unsatisfiable",
                FAILURE_UNSATISFIED_CONSTRAINTS,
            )

        # The constructive solver above needs rollback-aware scans, but the
        # remaining fill is monotonic.  Track occupied Bowser stages directly
        # here so broad prop participation does not make the fill O(N^2).
        bowser_stages = {
            (placement.logic_stage or placement.stage)
            for placement in self.placements
            if is_bowser_replacement(
                placement.index, resolved[placement.index]
            )
        }

        # MATCH_AT_MOST gates cap how many of their own members may resolve
        # to the replacement token. Once a gate's cap is already met by
        # forced (witness) members, exclude its replacement token from the
        # random-fill domain of the gate's remaining, still-unforced members
        # so pure chance can't push the count past the declared cap.
        capped_exclusions: dict[int, set[str]] = {}
        for gate in self.gates:
            if gate.match_mode != MATCH_AT_MOST:
                continue
            cap = gate.maximum_count()
            if cap is None:
                continue
            forced_count = sum(
                1 for member in gate.members
                if forced_tokens[member] == gate.replacement_capture_token
            )
            if forced_count < cap:
                continue
            for member in gate.members:
                if forced_tokens[member] == gate.replacement_capture_token:
                    continue
                capped_exclusions.setdefault(member, set()).add(
                    gate.replacement_capture_token
                )

        # Audited spawn restrictions. A count is kept per restriction rather
        # than per scenario: one placement is live in several scenarios at
        # once, and each restriction names exactly one stage+scenario, so the
        # restriction itself is the right granularity. Counts start from the
        # vanilla/forced assignment, so an entity the stage already contains
        # consumes its own allowance. Mirrored by
        # EnemySwapRando::spawnRestricted / countResolvedInScenario.
        restrictions_by_entity: dict[int, list[tuple[int, SpawnRestriction]]] = {}
        for index, restriction in enumerate(self.spawn_restrictions):
            restrictions_by_entity.setdefault(restriction.entity, []).append(
                (index, restriction))
        spawn_counts: dict[int, int] = {}

        def adjust_spawn_count(placement: SwapPlacement, entity_index: int,
                               delta: int) -> None:
            for index, restriction in restrictions_by_entity.get(entity_index, ()):
                if restriction.applies_to(placement):
                    spawn_counts[index] = spawn_counts.get(index, 0) + delta

        if self.spawn_restrictions:
            for placement in self.placements:
                adjust_spawn_count(placement, resolved[placement.index], 1)

        def spawn_restricted(placement: SwapPlacement, entity_index: int) -> bool:
            for index, restriction in restrictions_by_entity.get(entity_index, ()):
                if not restriction.applies_to(placement):
                    continue
                held = spawn_counts.get(index, 0)
                # This placement's own current entity is about to be replaced,
                # so it must not count against the candidate taking its slot.
                if resolved[placement.index] == entity_index:
                    held -= 1
                if held >= restriction.max_instances:
                    return True
            return False

        def note_spawn(placement: SwapPlacement, old_entity: int,
                       new_entity: int) -> None:
            adjust_spawn_count(placement, old_entity, -1)
            adjust_spawn_count(placement, new_entity, 1)

        for rule in self.rules:
            if rule.policy != POLICY_RANDOMIZE or forced_tokens[rule.placement]:
                continue
            placement = self.placements[rule.placement]
            if placement.entity < 0 or not category_enabled(placement):
                continue
            if placement.category == "prop" and should_delete_prop(placement):
                if self.spawn_restrictions:
                    note_spawn(
                        placement, resolved[placement.index],
                        DELETED_PLACEMENT_ENTITY,
                    )
                resolved[placement.index] = DELETED_PLACEMENT_ENTITY
                continue
            if rule.pool:
                domain = list(rule.pool)
            else:
                domain = mode_domain(placement.category)
            # An NPC or prop may only appear as a target for a source of its
            # own category, and only because that category's mode asked for it.
            # Combat placements never gain them, and an authored rule pool
            # cannot reintroduce them -- rule pools predate the modes, so a
            # pool naming an NPC was written when NPCs could not be targets.
            if placement.category not in {"npc", "prop", "enemy2d"}:
                domain = [
                    entity_index for entity_index in domain
                    if self.entities[entity_index].category
                    not in {"npc", "prop", "enemy2d"}
                ]
            if placement.category == "prop":
                domain = [
                    entity_index for entity_index in domain
                    if self.entities[entity_index].category != "prop"
                    or self.prop_spatial_compatible(
                        placement.entity, entity_index
                    )
                ]
            excluded_tokens = capped_exclusions.get(rule.placement)
            if excluded_tokens:
                domain = [
                    entity_index for entity_index in domain
                    if self.entities[entity_index].capture_token
                    not in excluded_tokens
                ]
            stage_key = placement.logic_stage or placement.stage
            if stage_key in bowser_stages:
                domain = [
                    entity_index for entity_index in domain
                    if self.entities[entity_index].name != "Koopa"
                ]
            if self.spawn_restrictions:
                domain = [
                    entity_index for entity_index in domain
                    if not spawn_restricted(placement, entity_index)
                ]
            replacement = self.placement_replacement(
                seed, attempt, placement, domain
            )
            if replacement >= 0:
                if self.spawn_restrictions:
                    note_spawn(placement, resolved[placement.index], replacement)
                resolved[placement.index] = replacement
                if is_bowser_replacement(placement.index, replacement):
                    bowser_stages.add(stage_key)

        for gate_index, scenario in zip(
                witnesses, witness_scenarios):
            if not self.gate_matches(
                    self.gates[gate_index], resolved, scenario):
                raise UnsatisfiedCaptureGates(
                    f"seed {seed} attempt {attempt}: witness "
                    f"{self.gates[gate_index].gate_id} failed verification",
                    FAILURE_WITNESS_VERIFICATION,
                )
        self.validate_spawn_restrictions(resolved, seed, attempt)
        self.validate_memory_assignment(resolved, seed, attempt)
        return ResolvedAssignment(
            entities=tuple(resolved),
            witness_gate_ids=tuple(witness_descriptions),
        )


def _parse_pool(spec: str, entities: list[SwapEntity],
                entity_by_name: dict[str, int],
                any_pool: list[int],
                warn: Callable[[str], None],
                strict: bool = False) -> tuple[int, ...]:
    spec = (spec or "").strip()
    if not spec:
        return ()
    capture_pool = [entity.index for entity in entities
                    if entity.combat_target and entity.capture_token]
    first_for_token: dict[str, int] = {}
    for entity in entities:
        if entity.combat_target and entity.capture_token:
            first_for_token.setdefault(entity.capture_token, entity.index)
    result: list[int] = []
    for item in spec.split("|"):
        item = item.strip()
        if not item:
            continue
        if item.lower() == "any":
            result.extend(capture_pool)
        elif item.lower() == "all":
            result.extend(any_pool)
        elif item in entity_by_name:
            entity_index = entity_by_name[item]
            entity = entities[entity_index]
            if not entity.any_pool or entity.category not in {
                "capture", "enemy", "generator", "npc",
            }:
                message = f"unsafe pool target '{item}'"
                if strict:
                    raise ValueError(message)
                warn(message)
                continue
            result.append(entity_index)
        elif item in first_for_token:
            result.append(first_for_token[item])
        else:
            warn(f"unknown pool entry '{item}'")
    return tuple(dict.fromkeys(result))


def _parse_memory_int(row: dict[str, str], field_name: str,
                      default: int) -> int:
    raw = (row.get(field_name) or "").strip()
    if not raw:
        return default
    try:
        return max(0, int(raw, 0))
    except ValueError:
        return default


def _load_memory_data(
    config_dir: Path, entities: list[SwapEntity],
    warn: Callable[[str], None], strict: bool,
) -> tuple[dict[int, EntityMemoryCost], tuple[StageMemoryBudget, ...], bool]:
    """Read the versioned cost/budget tables.

    The checked-in cost table predates live-actor measurement. Its archive
    columns remain accepted, but missing live fields receive conservative
    values and are marked estimated. This makes an incomplete measurement
    explicit without reintroducing a zero-cost target.
    """
    rows = _read_csv(config_dir / "enemy_swap_costs.csv")
    if not rows:
        warn("enemy_swap_costs.csv is absent; memory admission is unavailable")
        return {}, (), False

    by_name: dict[str, dict[str, str]] = {}
    for row in rows:
        name = (row.get("unit_config_name") or "").strip()
        if not name:
            warn("memory cost row has no unit_config_name")
            continue
        if name in by_name:
            message = f"duplicate memory cost row '{name}'"
            if strict:
                raise ValueError(message)
            warn(message)
            continue
        by_name[name] = row

    entity_names = {entity.name for entity in entities}
    unknown_rows = sorted(set(by_name) - entity_names)
    if unknown_rows:
        message = "memory costs contain unknown entities: " + ", ".join(
            unknown_rows
        )
        if strict:
            raise ValueError(message)
        warn(message)

    # Source-only scenery entries are never constructed as replacements, so
    # runtime admission never prices them.  Requiring cost rows for thousands
    # of broad prop sources would add data without affecting a single
    # admission decision; only full-actor targets need a complete cost model.
    required_cost_entities = [
        entity for entity in entities if entity.full_actor_target
    ]
    missing = [
        entity.name for entity in required_cost_entities
        if entity.name not in by_name
    ]
    if missing:
        message = "memory costs missing entities: " + ", ".join(missing)
        if strict:
            raise ValueError(message)
        warn(message)

    costs: dict[int, EntityMemoryCost] = {}
    for entity in entities:
        row = by_name.get(entity.name)
        if row is None:
            continue
        defaults = DEFAULT_INSTANCE_BYTES.get(
            entity.category, DEFAULT_INSTANCE_BYTES["enemy"]
        )
        gpu_defaults = DEFAULT_GPU_BYTES.get(
            entity.category, DEFAULT_GPU_BYTES["enemy"]
        )
        resource_bytes = _parse_memory_int(
            row, "first_resource_bytes",
            _parse_memory_int(row, "resident_bytes", UNKNOWN_RESOURCE_BYTES),
        )
        if resource_bytes <= 0:
            resource_bytes = UNKNOWN_RESOURCE_BYTES
        raw_keys = (row.get("resource_keys") or "").strip()
        if not raw_keys:
            raw_keys = (row.get("archive") or "").strip()
        if not raw_keys or raw_keys in {"(estimated)", "(none)"}:
            keys = (f"unknown:{entity.name}",)
            key_bytes = (resource_bytes,)
        else:
            parsed_keys = tuple(
                item.strip() for item in raw_keys.replace("|", ";").split(";")
                if item.strip()
            )
            raw_sizes = (row.get("resource_key_bytes") or "").strip()
            parsed_sizes: tuple[int, ...] = ()
            if raw_sizes:
                try:
                    parsed_sizes = tuple(
                        max(0, int(item.strip(), 0))
                        for item in raw_sizes.replace("|", ";").split(";")
                        if item.strip()
                    )
                except ValueError:
                    warn(
                        f"invalid resource_key_bytes for '{entity.name}'; "
                        "using the combined resource key"
                    )
            if parsed_sizes and len(parsed_sizes) == len(parsed_keys):
                keys = parsed_keys
                key_bytes = parsed_sizes
                resource_bytes = sum(key_bytes)
            else:
                # Legacy archive values such as "Koopa+KoopaHack" are a
                # single deduplication identity until the runtime audit emits
                # per-archive sizes.
                keys = ("+".join(parsed_keys),)
                key_bytes = (resource_bytes,)

        first_instance = _parse_memory_int(
            row, "first_instance_bytes", defaults[0]
        ) or defaults[0]
        additional_instance = _parse_memory_int(
            row, "additional_instance_bytes", defaults[1]
        ) or defaults[1]
        generator_peak = _parse_memory_int(
            row, "generator_peak_bytes",
            DEFAULT_GENERATOR_PEAK_BYTES
            if entity.category == "generator"
            else first_instance + additional_instance,
        )
        if generator_peak <= 0:
            generator_peak = (
                DEFAULT_GENERATOR_PEAK_BYTES
                if entity.category == "generator"
                else first_instance + additional_instance
            )
        model_ubo_bytes = _parse_memory_int(
            row, "model_ubo_bytes", gpu_defaults[0]
        ) or gpu_defaults[0]
        shader_option_ubo_bytes = _parse_memory_int(
            row, "shader_option_ubo_bytes", gpu_defaults[1]
        ) or gpu_defaults[1]
        explicit_live_fields = any(
            (row.get(field) or "").strip()
            for field in (
                "first_resource_bytes", "first_instance_bytes",
                "additional_instance_bytes", "generator_peak_bytes",
                "model_ubo_bytes", "shader_option_ubo_bytes",
                "resource_keys", "measurement_status", "confidence",
                "measured",
            )
        )
        status = (
            (row.get("measurement_status") or "").strip().lower()
            or ("estimated" if not explicit_live_fields else "estimated")
        )
        confidence = (
            (row.get("confidence") or "").strip().lower()
            or "conservative"
        )
        measured = _parse_bool(row.get("measured") or "false")
        if not measured and status == "measured":
            status = "estimated"
        costs[entity.index] = EntityMemoryCost(
            first_resource_bytes=resource_bytes,
            first_instance_bytes=first_instance,
            additional_instance_bytes=additional_instance,
            generator_peak_bytes=generator_peak,
            resource_keys=keys,
            resource_key_bytes=key_bytes,
            heap_domain=(row.get("heap_domain") or "scene_resource+scene").strip(),
            measurement_status=status,
            confidence=confidence,
            measured=measured,
            model_ubo_bytes=model_ubo_bytes,
            shader_option_ubo_bytes=shader_option_ubo_bytes,
        )
        if not costs[entity.index].bounded:
            warn(f"memory cost for '{entity.name}' is not bounded")

    budget_rows = _read_csv(config_dir / "enemy_swap_stage_budgets.csv")
    budgets: list[StageMemoryBudget] = []
    seen_budgets: set[tuple[str, int]] = set()
    for row in budget_rows:
        stage = (row.get("stage") or "").strip()
        if not stage:
            warn("stage memory budget has no stage")
            continue
        try:
            scenario = int((row.get("scenario") or "0").strip(), 0)
        except ValueError:
            scenario = -1
        if not 0 <= scenario < MAX_PLACEMENT_SCENARIO:
            message = f"invalid memory budget scenario for '{stage}'"
            if strict:
                raise ValueError(message)
            warn(message)
            continue
        key = stage, scenario
        if key in seen_budgets:
            message = f"duplicate stage memory budget {stage}/{scenario}"
            if strict:
                raise ValueError(message)
            warn(message)
            continue
        seen_budgets.add(key)
        budgets.append(StageMemoryBudget(
            stage=stage,
            scenario=scenario,
            max_extra_resource_bytes=_parse_memory_int(
                row, "max_extra_resource_bytes", 0
            ),
            max_extra_scene_bytes=_parse_memory_int(
                row, "max_extra_scene_bytes", 0
            ),
            reserve_resource_bytes=_parse_memory_int(
                row, "reserve_resource_bytes", 0
            ),
            reserve_scene_bytes=_parse_memory_int(
                row, "reserve_scene_bytes", 0
            ),
        ))
    return costs, tuple(sorted(budgets, key=lambda item: (item.stage, item.scenario))), \
        all(entity.index in costs for entity in required_cost_entities)


SPAWN_RESTRICTION_STATUSES = frozenset({"suspected", "confirmed"})

# Zero means "no reviewed compatibility restrictions are present", which is a
# different statement from "every placement is allowed". Bump it whenever the
# emitted restriction table changes so a save can tell which data it was built
# against.
COMPATIBILITY_DATA_VERSION_NONE = 0
COMPATIBILITY_DATA_VERSION_REVIEWED = 1


def compatibility_data_version(
    restrictions: Sequence[SpawnRestriction],
) -> int:
    return (COMPATIBILITY_DATA_VERSION_REVIEWED if restrictions
            else COMPATIBILITY_DATA_VERSION_NONE)


def _spatial_axes(
    row: dict[str, str],
    prefix: str,
    name: str,
    warn: Callable[[str], None],
    strict: bool,
) -> tuple[int, int, int]:
    """Read a ``<prefix>_x/y/z`` extent triple, treating absent columns as 0."""

    axes: list[int] = []
    for axis in ("x", "y", "z"):
        raw = (row.get(f"{prefix}_{axis}") or "0").strip() or "0"
        try:
            axes.append(max(0, min(U32, int(float(raw)))))
        except ValueError:
            message = (
                f"prop spatial row '{name}' has invalid {prefix}_{axis}"
            )
            if strict:
                raise ValueError(message)
            warn(message)
            return (0, 0, 0)
    return (axes[0], axes[1], axes[2])


def _load_prop_spatial_data(
    config_dir: Path,
    entities: Sequence[SwapEntity],
    warn: Callable[[str], None],
    strict: bool,
) -> list[SwapEntity]:
    """Attach generated physical dimensions to prop entity records."""

    path = config_dir / "enemy_swap_spatial.csv"
    rows = _read_csv(path)
    if not rows:
        message = "enemy_swap_spatial.csv is absent; prop size matching is unavailable"
        if strict and any(
            entity.category == "prop" and entity.any_pool
            for entity in entities
        ):
            raise ValueError(message)
        warn(message)
        return list(entities)

    by_name: dict[str, dict[str, str]] = {}
    for row in rows:
        name = (row.get("unit_config_name") or "").strip()
        if not name:
            warn("prop spatial row has no unit_config_name")
            continue
        if name in by_name:
            message = f"duplicate prop spatial row '{name}'"
            if strict:
                raise ValueError(message)
            warn(message)
            continue
        by_name[name] = row

    known_names = {entity.name for entity in entities}
    for name in sorted(set(by_name) - known_names):
        warn(f"prop spatial row names unknown entity '{name}'")

    result: list[SwapEntity] = []
    missing_targets: list[str] = []
    for entity in entities:
        row = by_name.get(entity.name)
        if entity.category != "prop":
            result.append(entity)
            continue
        footprint = height = 0
        solid = False
        collision_axes = (0, 0, 0)
        model_axes = (0, 0, 0)
        if row is not None:
            try:
                footprint = max(
                    0, min(U32, int(float((row.get("footprint") or "0").strip())))
                )
                height = max(
                    0, min(U32, int(float((row.get("height") or "0").strip())))
                )
            except ValueError:
                message = f"prop spatial row '{entity.name}' has invalid dimensions"
                if strict:
                    raise ValueError(message)
                warn(message)
                footprint = height = 0
            raw_solid = (row.get("solid") or "false").strip().lower()
            if raw_solid not in {"0", "1", "false", "true", "no", "yes", "n", "y"}:
                message = f"prop spatial row '{entity.name}' has invalid solid flag"
                if strict:
                    raise ValueError(message)
                warn(message)
            solid = _parse_bool(raw_solid)
            # Absent on rows written before the axis columns existed, and on
            # hand-maintained ones. Zero means "unmeasured", which every shape
            # rule already reads as "no opinion".
            collision_axes = _spatial_axes(row, "collision", entity.name, warn, strict)
            model_axes = _spatial_axes(row, "model", entity.name, warn, strict)
        if entity.any_pool and (footprint <= 0 or height <= 0):
            missing_targets.append(entity.name)
        result.append(replace(
            entity,
            prop_footprint=footprint,
            prop_height=height,
            prop_solid=solid,
            prop_collision_axes=collision_axes,
            prop_model_axes=model_axes,
        ))

    if missing_targets:
        message = (
            "prop targets lack bounded spatial metadata: "
            + ", ".join(missing_targets)
        )
        if strict:
            raise ValueError(message)
        warn(message)
    return result


def _load_spawn_restrictions(
    config_dir: Path,
    entities: Sequence[SwapEntity],
    warn: Callable[[str], None],
    strict: bool,
    *,
    enforce_suspected: bool = False,
) -> tuple[SpawnRestriction, ...]:
    """Read config/enemy_spawn_restrictions.csv.

    Only ``confirmed`` rows are enforced unless ``enforce_suspected`` is set.
    ``suspected`` rows come from a single inferred reboot, which the audit
    cannot distinguish from an unrelated crash, so promoting them by default
    would remove entities from the pool on one-off evidence.
    """

    path = config_dir / "enemy_spawn_restrictions.csv"
    if not path.exists():
        return ()
    restrictions: list[SpawnRestriction] = []
    seen: set[tuple[str, int, int, int]] = set()
    for row in _read_csv(path):
        stage = (row.get("stage") or "").strip()
        if not stage:
            warn("spawn restriction has no stage")
            continue

        def fail(message: str) -> None:
            if strict:
                raise ValueError(message)
            warn(message)

        try:
            scenario = int((row.get("scenario") or "").strip(), 0)
            entity = int((row.get("target_entity") or "").strip(), 0)
            source_mask = int((row.get("source_mask") or "").strip(), 0)
            max_instances = int((row.get("max_instances") or "").strip(), 0)
        except ValueError:
            fail(f"spawn restriction for '{stage}' has a non-numeric field")
            continue
        if not 0 <= scenario < MAX_PLACEMENT_SCENARIO:
            fail(f"invalid spawn restriction scenario for '{stage}'")
            continue
        if not 0 <= entity < len(entities):
            fail(f"spawn restriction for '{stage}' names unknown entity {entity}")
            continue
        # target_name is redundant with the index but catches the dangerous
        # case: the entity table shifting under a hand-edited table.
        expected = (row.get("target_name") or "").strip()
        if expected and expected != entities[entity].name:
            fail(
                f"spawn restriction for '{stage}' has target_entity {entity} "
                f"('{entities[entity].name}') but target_name '{expected}'"
            )
            continue
        if not 1 <= source_mask <= 3:
            fail(f"invalid spawn restriction source_mask for '{stage}'")
            continue
        if not 0 <= max_instances <= 2:
            fail(f"invalid spawn restriction max_instances for '{stage}'")
            continue
        status = (row.get("status") or "").strip().lower()
        if status not in SPAWN_RESTRICTION_STATUSES:
            fail(f"invalid spawn restriction status '{status}' for '{stage}'")
            continue
        key = stage, scenario, entity, source_mask
        if key in seen:
            fail(f"duplicate spawn restriction {stage}/{scenario}/{entity}")
            continue
        seen.add(key)
        if status != "confirmed" and not enforce_suspected:
            continue
        restrictions.append(SpawnRestriction(
            stage=stage,
            scenario=scenario,
            entity=entity,
            source_mask=source_mask,
            max_instances=max_instances,
            reason=(row.get("reason") or "").strip(),
            status=status,
        ))
    return tuple(sorted(
        restrictions,
        key=lambda item: (item.stage, item.scenario, item.entity, item.source_mask),
    ))


def load_swap_config(config_dir: Path | None = None,
                     templates_dir: Path | None = None,
                     warn: Callable[[str], None] = lambda message: None,
                     strict: bool = False,
                     enforce_suspected: bool = False) -> SwapConfig:
    config_dir = config_dir or CONFIG_DIR
    templates_dir = templates_dir or (
        TEMPLATES_DIR if config_dir == CONFIG_DIR else config_dir
    )

    entities: list[SwapEntity] = []
    entity_by_name: dict[str, int] = {}
    for row in _read_csv(config_dir / "enemy_swap_entities.csv"):
        name = (row.get("unit_config_name") or "").strip()
        if not name:
            continue
        if name in entity_by_name:
            message = f"duplicate entity '{name}'"
            if strict:
                raise ValueError(message)
            warn(message)
            continue
        friendly_name = (row.get("friendly_name") or "").strip()
        if not friendly_name:
            friendly_name = (row.get("capture_token") or "").strip() or name
            message = (
                f"entity '{name}' has no friendly_name; "
                f"using '{friendly_name}'"
            )
            if strict:
                raise ValueError(message)
            warn(message)
        entity = SwapEntity(
            index=len(entities),
            name=name,
            friendly_name=friendly_name,
            class_name=(row.get("class_name") or "").strip() or name,
            category=(row.get("category") or "enemy").strip().lower(),
            capture_token=(row.get("capture_token") or "").strip(),
            swap_out=_parse_bool(row.get("swap_out") or "false"),
            any_pool=_parse_bool(row.get("any_pool") or "false"),
        )
        entity_by_name[name] = entity.index
        entities.append(entity)

    entities = _load_prop_spatial_data(config_dir, entities, warn, strict)
    any_pool = [entity.index for entity in entities if entity.combat_target]
    menu_pool = [entity.index for entity in entities if entity.menu_target]
    npc_pool = [entity.index for entity in entities
                if entity.any_pool and entity.category == "npc"]
    prop_pool = [entity.index for entity in entities
                 if entity.full_actor_target and entity.category == "prop"]
    two_d_pool = [entity.index for entity in entities
                  if entity.any_pool and entity.category == "enemy2d"]
    memory_costs, stage_memory_budgets, memory_data_available = (
        _load_memory_data(config_dir, entities, warn, strict)
    )
    spawn_restrictions = _load_spawn_restrictions(
        config_dir, entities, warn, strict, enforce_suspected=enforce_suspected
    )

    placements: list[SwapPlacement] = []
    seen_placements: set[tuple[str, str]] = set()
    for row in _read_csv(config_dir / "enemy_capture_map.csv"):
        stage = (row.get("stage") or "").strip()
        object_id = (row.get("object_id") or "").strip()
        if not stage or not object_id:
            continue
        key = stage, object_id
        if key in seen_placements:
            message = f"duplicate placement {stage}/{object_id}"
            if strict:
                raise ValueError(message)
            warn(message)
            continue
        seen_placements.add(key)
        unit_name = (row.get("unit_config_name") or "").strip()
        entity_index = entity_by_name.get(unit_name, -1)
        entity = entities[entity_index] if entity_index >= 0 else None
        placements.append(SwapPlacement(
            index=-1,
            stage=stage,
            logic_stage=(row.get("logic_stage") or "").strip() or stage,
            object_id=object_id,
            unit_config_name=unit_name,
            entity=entity_index,
            category=entity.category if entity else (
                (row.get("category") or "other").strip().lower() or "other"
            ),
            capture_token=entity.capture_token if entity else "",
            scenario_mask=_parse_scenario_mask(row.get("scenarios") or ""),
            list_name=(row.get("list_name") or "").strip(),
            layer=(row.get("layer") or "").strip(),
            x=(row.get("x") or "").strip(),
            y=(row.get("y") or "").strip(),
            z=(row.get("z") or "").strip(),
            event_name=(row.get("event_name") or "").strip(),
        ))
    placements.sort(key=lambda placement: (placement.stage, placement.object_id))
    placements = [
        SwapPlacement(index=index, **{
            field_name: getattr(placement, field_name)
            for field_name in SwapPlacement.__dataclass_fields__
            if field_name != "index"
        })
        for index, placement in enumerate(placements)
    ]
    placement_by_key = {placement.key: placement.index
                        for placement in placements}

    rules: list[SwapRule] = []
    rule_by_placement: dict[int, int] = {}
    for row in _read_csv(config_dir / "enemy_swap_rules.csv"):
        stage = (row.get("stage") or "").strip()
        object_id = (row.get("object_id") or "").strip()
        placement_index = placement_by_key.get((stage, object_id), -1)
        raw_policy = (row.get("policy") or POLICY_KEEP).strip().lower()
        if raw_policy in LEGACY_RANDOMIZE_POLICIES:
            policy = POLICY_RANDOMIZE
            if raw_policy != POLICY_RANDOMIZE:
                warn(f"{stage}/{object_id}: legacy policy '{raw_policy}' "
                     f"treated as '{POLICY_RANDOMIZE}'")
        elif raw_policy == POLICY_KEEP:
            policy = POLICY_KEEP
        else:
            warn(f"{stage}/{object_id}: unknown policy '{raw_policy}', keeping")
            policy = POLICY_KEEP
        if placement_index < 0:
            if policy != POLICY_KEEP:
                warn(f"{stage}/{object_id}: rule has no extracted placement")
            continue
        if placement_index in rule_by_placement:
            message = f"{stage}/{object_id}: duplicate rule; keeping first"
            if strict:
                raise ValueError(message)
            warn(message)
            continue
        placement = placements[placement_index]
        if placement.entity < 0 and policy != POLICY_KEEP:
            warn(f"{stage}/{object_id}: unknown source entity; keeping")
            policy = POLICY_KEEP
        if (
            placement.entity >= 0
            and policy == POLICY_RANDOMIZE
            and not entities[placement.entity].swap_out
        ):
            message = (
                f"{stage}/{object_id}: source entity "
                f"'{entities[placement.entity].name}' is not swap-out safe"
            )
            if strict:
                raise ValueError(message)
            warn(f"{message}; keeping")
            policy = POLICY_KEEP
        if policy == POLICY_RANDOMIZE and not placement.runtime_source_safe:
            warn(
                f"{stage}/{object_id}: event-driven NPC is not runtime-safe; "
                "keeping"
            )
            policy = POLICY_KEEP
        pool = _parse_pool(
            row.get("pool") or "",
            entities,
            entity_by_name,
            any_pool,
            warn,
            strict,
        )
        invalid_domain = [
            entity_index for entity_index in pool
            if not entities[entity_index].combat_target
        ]
        if invalid_domain:
            names = ", ".join(
                entities[index].name for index in invalid_domain
            )
            message = (
                f"{stage}/{object_id}: pool violates the "
                f"{placement.category} compatibility domain: {names}"
            )
            if strict:
                raise ValueError(message)
            warn(message)
            pool = tuple(
                index for index in pool
                if index not in invalid_domain
            )
        rule_index = len(rules)
        rule_by_placement[placement_index] = rule_index
        rules.append(SwapRule(
            index=rule_index,
            placement=placement_index,
            policy=policy,
            pool=pool,
        ))

    gate_rows = _read_csv(templates_dir / "capture_gates.csv")
    member_rows = _read_csv(templates_dir / "capture_gate_objects.csv")
    member_keys: dict[str, list[tuple[str, str]]] = {}
    for row in member_rows:
        gate_id = (row.get("gate_id") or "").strip()
        stage = (row.get("placement_stage") or "").strip()
        object_id = (row.get("object_id") or "").strip()
        if not gate_id or not stage or not object_id:
            message = (
                "capture gate object row is missing gate_id or placement key"
            )
            if strict:
                raise ValueError(message)
            warn(message)
            continue
        key = stage, object_id
        values = member_keys.setdefault(gate_id, [])
        if key in values:
            message = (
                f"gate '{gate_id}' repeats member {stage}/{object_id}"
            )
            if strict:
                raise ValueError(message)
            warn(message)
            continue
        values.append(key)

    gates: list[CaptureGate] = []
    seen_gate_ids: set[str] = set()
    gates_by_edge_token: dict[tuple[str, str, str], list[int]] = {}
    safe_capture_tokens = {
        entity.capture_token for entity in entities
        if entity.combat_target and entity.capture_token
    }
    # Alternatives skipped because their producer left the pool, keyed the
    # same way gates_by_edge_token is, so the check after the loop can ask
    # whether a group lost every alternative it had.
    dropped_alternatives: dict[tuple[str, str, str], list[str]] = {}

    for row in gate_rows:
        gate_id = (row.get("gate_id") or "").strip()
        if not gate_id:
            message = "capture gate row has an empty gate_id"
            if strict:
                raise ValueError(message)
            warn(message)
            continue
        if gate_id in seen_gate_ids:
            message = f"duplicate capture gate '{gate_id}'"
            if strict:
                raise ValueError(message)
            warn(message)
            continue
        seen_gate_ids.add(gate_id)
        mode = (row.get("match_mode") or MATCH_ANY).strip().lower()
        if mode not in MATCH_MODES:
            message = (
                f"gate '{gate_id}' has invalid match_mode '{mode}'"
            )
            if strict:
                raise ValueError(message)
            warn(message)
            continue
        try:
            minimum = int((row.get("minimum_count") or "0").strip() or "0")
        except ValueError:
            message = f"gate '{gate_id}' has invalid minimum_count"
            if strict:
                raise ValueError(message)
            warn(message)
            continue
        replacement_token = (
            row.get("replacement_capture_token") or ""
        ).strip()
        if replacement_token not in safe_capture_tokens:
            # Dropped, not fatal. Gates are authored as a set of OR
            # alternatives on one (edge, source token), and the pool they draw
            # from is configuration that legitimately shrinks -- an entity the
            # audit proves never constructs gets removed from it. Failing the
            # build for one dead alternative would mean any evidence-driven
            # pool reduction breaks the build, even when five alternatives
            # remain. What actually matters is that the group keeps at least
            # one, and that is checked after the loop.
            warn(f"gate '{gate_id}' replacement '{replacement_token}' "
                 "has no vetted producer; dropping this alternative")
            dropped_alternatives.setdefault(
                (
                    (row.get("kingdom") or "").strip(),
                    (row.get("edge_id") or "").strip(),
                    (row.get("source_capture_token") or "").strip(),
                ),
                [],
            ).append(replacement_token)
            continue
        difficulty_name = (row.get("difficulty") or "casual").strip().lower()
        difficulty = DIFFICULTY_RANK.get(difficulty_name)
        if difficulty is None:
            warn(
                f"gate '{gate_id}' has invalid difficulty "
                f"'{difficulty_name}'"
            )
            if strict:
                raise ValueError(f"invalid capture gate difficulty: {gate_id}")
            continue
        members: list[int] = []
        logical_stages: set[str] = set()
        missing_member = False
        for key in member_keys.get(gate_id, []):
            placement_index = placement_by_key.get(key, -1)
            if placement_index < 0:
                warn(f"gate '{gate_id}' references missing placement "
                     f"{key[0]}/{key[1]}")
                missing_member = True
                continue
            # NPC members are kept: whether they can actually serve as a
            # witness is a runtime toggle (npc_enabled), decided later by
            # can_force()/category_enabled(), not a load-time fact.
            members.append(placement_index)
            logical_stages.add(placements[placement_index].logic_stage)
        logic_stage = (row.get("logic_stage") or "").strip()
        if not members:
            warn(f"gate '{gate_id}' has no valid members")
            if strict:
                raise ValueError(f"empty capture gate: {gate_id}")
            continue
        if missing_member and strict:
            raise ValueError(f"missing capture gate member: {gate_id}")
        if any(stage != logic_stage for stage in logical_stages):
            warn(f"gate '{gate_id}' contains an object outside {logic_stage}")
            if strict:
                raise ValueError(f"cross-map capture gate: {gate_id}")
            continue
        if mode in (MATCH_AT_LEAST, MATCH_AT_MOST) \
                and not (1 <= minimum <= len(members)):
            warn(f"gate '{gate_id}' minimum_count must be within its members")
            if strict:
                raise ValueError(f"invalid capture gate threshold: {gate_id}")
            continue
        gate = CaptureGate(
            index=len(gates),
            gate_id=gate_id,
            kingdom=(row.get("kingdom") or "").strip(),
            logic_stage=logic_stage,
            edge_id=(row.get("edge_id") or "").strip(),
            source_capture_token=(
                row.get("source_capture_token") or ""
            ).strip(),
            replacement_capture_token=replacement_token,
            difficulty=difficulty,
            match_mode=mode,
            minimum_count=(
                minimum if mode in (MATCH_AT_LEAST, MATCH_AT_MOST) else 0
            ),
            members=tuple(members),
            notes=(row.get("notes") or "").strip(),
        )
        gates_by_edge_token.setdefault(
            (gate.kingdom, gate.edge_id, gate.source_capture_token), []
        ).append(gate.index)
        gates.append(gate)

    # A gate group with no surviving alternative is unsatisfiable, and unlike
    # a single dead alternative that is a genuine defect: the edge claims a
    # capture route the game can no longer provide by any means.
    for key, tokens in sorted(dropped_alternatives.items()):
        if gates_by_edge_token.get(key):
            continue
        message = (
            f"capture gate group {key[1]}/{key[2]} in {key[0] or 'unknown'} "
            f"lost every alternative ({', '.join(sorted(tokens))}); no "
            "producer remains in the pool"
        )
        if strict:
            raise ValueError(message)
        warn(message)

    authored_gate_ids = {
        (row.get("gate_id") or "").strip()
        for row in gate_rows
        if (row.get("gate_id") or "").strip()
    }
    for member_gate_id in member_keys:
        if member_gate_id not in authored_gate_ids:
            message = (
                f"capture gate objects reference unknown gate "
                f"'{member_gate_id}'"
            )
            if strict:
                raise ValueError(message)
            warn(message)

    # Route ownership and completeness are validated here, rather than being
    # inferred into replacement rules. This keeps graph logic authoritative.
    route_rows = _read_csv(templates_dir / "route_edges.csv")
    token_rows = _read_csv(templates_dir / "logic_tokens.csv")
    node_rows = _read_csv(templates_dir / "logic_nodes.csv")
    capture_tokens = {
        (row.get("token") or "").strip()
        for row in token_rows
        if (row.get("token_type") or "").strip() == "capture"
    }

    # Moon capture requirements use internal self-edge IDs. They are consumed
    # by the same gate solver as route constraints while remaining invisible to
    # normal graph traversal; see generate_runtime_logic.py.
    def moon_requirement_has_capture(expression: str) -> bool:
        try:
            alternatives = _requirement_dnf(expression)
        except ValueError:
            return False
        return any(
            token in capture_tokens
            for alternative in alternatives
            for token in alternative
        )

    for moon in _read_csv(templates_dir / "moon_access.csv"):
        uid = (moon.get("uid") or "").strip()
        stage = (moon.get("stage") or "").strip()
        if not uid or not stage:
            continue
        from_node = (moon.get("from_node_id") or "").strip()
        if not from_node:
            from_node = f"{stage}::start"
        for kind, field, option_scenarios, difficulty in (
            ("state", "state_requirement_expr", "", ""),
            (
                "local", "requirement_expr",
                moon.get("option_scenarios") or "",
                moon.get("difficulty") or "",
            ),
        ):
            requirement = moon.get(field) or ""
            if not moon_requirement_has_capture(requirement):
                continue
            route_rows.append({
                "edge_id": f"moon_{kind}_{uid}",
                "kingdom": (moon.get("kingdom") or "").strip(),
                "from_node_id": from_node,
                "to_node_id": from_node,
                "requirement_expr": requirement,
                "difficulty": difficulty,
                "option_scenarios": option_scenarios,
                "bidirectional": "true",
                "_moon_gate": "true",
            })
    # edge_id is only unique within a kingdom (the graph editor numbers each
    # kingdom's routes independently), so routes are keyed by (kingdom, edge_id).
    route_by_id: dict[tuple[str, str], dict[str, str]] = {}
    for row in route_rows:
        edge_id = (row.get("edge_id") or "").strip()
        if not edge_id:
            continue
        kingdom = (row.get("kingdom") or "").strip()
        key = (kingdom, edge_id)
        if key in route_by_id:
            message = f"duplicate route edge '{edge_id}' in '{kingdom}'"
            if strict:
                raise ValueError(message)
            warn(message)
            continue
        route_by_id[key] = row
    node_by_id: dict[str, dict[str, str]] = {}
    for row in node_rows:
        node_id = (row.get("node_id") or "").strip()
        if not node_id:
            continue
        if node_id in node_by_id:
            message = f"duplicate logic node '{node_id}'"
            if strict:
                raise ValueError(message)
            warn(message)
            continue
        node_by_id[node_id] = row
    identifier_pattern = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")

    def route_capture_tokens(row: dict[str, str]) -> set[str]:
        return {
            token
            for token in identifier_pattern.findall(
                row.get("requirement_expr") or ""
            )
            if token in capture_tokens
        }

    for gate in gates:
        edge = route_by_id.get((gate.kingdom, gate.edge_id))
        if not edge:
            message = (
                f"gate '{gate.gate_id}' references missing edge "
                f"'{gate.edge_id}'"
            )
            if strict:
                raise ValueError(message)
            warn(message)
            continue
        if gate.source_capture_token not in route_capture_tokens(edge):
            message = (
                f"gate '{gate.gate_id}' source capture "
                f"'{gate.source_capture_token}' is not in edge "
                f"'{gate.edge_id}'"
            )
            if strict:
                raise ValueError(message)
            warn(message)
        if gate.kingdom != (edge.get("kingdom") or "").strip():
            message = (
                f"gate '{gate.gate_id}' kingdom does not own "
                f"edge '{gate.edge_id}'"
            )
            if strict:
                raise ValueError(message)
            warn(message)
        from_node = node_by_id.get(
            (edge.get("from_node_id") or "").strip()
        )
        edge_stage = (
            (from_node.get("stage") or "").strip()
            if from_node else
            (edge.get("from_node_id") or "").split("::", 1)[0]
        )
        if edge_stage and gate.logic_stage != edge_stage:
            message = (
                f"gate '{gate.gate_id}' belongs to "
                f"'{gate.logic_stage}', but edge '{gate.edge_id}' "
                f"starts in '{edge_stage}'"
            )
            if strict:
                raise ValueError(message)
            warn(message)

        required = gate.required_count()
        scenario_possible = False
        for scenario_index in range(32):
            active = sum(
                1 for member in gate.members
                if placements[member].scenario_mask
                & (1 << scenario_index)
            )
            if gate.match_mode == MATCH_ALL:
                possible = active == len(gate.members)
            else:
                possible = active >= required
            if possible:
                scenario_possible = True
                break
        if not scenario_possible:
            message = (
                f"gate '{gate.gate_id}' has no scenario in which "
                f"its {gate.match_mode} threshold can be met"
            )
            if strict:
                raise ValueError(message)
            warn(message)

    reliable_captures = reliable_capture_tokens(entities)

    # Default behaviour: a capture named by a logic route stays vanilla in its
    # own physical stage until an authored gate exists for that (stage, token)
    # pair. Authoring a gate for a token in one stage opts only that stage's
    # placements of the token into randomization; the same species elsewhere,
    # with no gate of its own, stays pinned vanilla. Both runtimes consume this
    # via the emitted reliable-token set plus the gate table itself (already
    # stage-scoped), so the behaviour stays mirrored with no bespoke C++ data.
    gated_stage_tokens = {
        (gate.logic_stage, gate.source_capture_token) for gate in gates
    }

    def stage_capture_reliable(stage: str, token: str) -> bool:
        if token in reliable_captures:
            return True
        return (stage, token) not in gated_stage_tokens

    def edge_logic_stage(edge: dict[str, str]) -> str:
        from_node = node_by_id.get((edge.get("from_node_id") or "").strip())
        return (
            (from_node.get("stage") or "").strip()
            if from_node else
            (edge.get("from_node_id") or "").split("::", 1)[0]
        )

    # Only (stage, token) pairs actually named by a route/moon requirement are
    # eligible for the automatic vanilla-pin below; a placement whose capture
    # is never named by logic (or has no capture token at all) is untouched
    # and keeps its configured policy.
    logic_referenced_stage_tokens = {
        (edge_logic_stage(edge), source_token)
        for edge in route_by_id.values()
        for source_token in route_capture_tokens(edge)
    }

    def logic_pins_rule(rule: SwapRule) -> bool:
        placement = placements[rule.placement]
        key = placement.logic_stage, placement.capture_token
        return (
            rule.policy == POLICY_RANDOMIZE
            and key in logic_referenced_stage_tokens
            and stage_capture_reliable(*key)
        )

    # Record what the pin actually covered so the operator can see which
    # (stage, token) pairs are waiting on an authored gate. Authoring a gate is
    # the only way to opt a logic-referenced capture into randomization, so
    # this doubles as the to-do list for widening the pool.
    pinned_captures: dict[tuple[str, str], int] = {}
    for rule in rules:
        if logic_pins_rule(rule):
            placement = placements[rule.placement]
            key = placement.logic_stage, placement.capture_token
            pinned_captures[key] = pinned_captures.get(key, 0) + 1

    rules = [
        replace(rule, policy=POLICY_KEEP) if logic_pins_rule(rule) else rule
        for rule in rules
    ]

    for (kingdom, edge_id), edge in route_by_id.items():
        from_node = node_by_id.get(
            (edge.get("from_node_id") or "").strip()
        )
        logic_stage = (
            (from_node.get("stage") or "").strip()
            if from_node else
            (edge.get("from_node_id") or "").split("::", 1)[0]
        )
        for source_token in route_capture_tokens(edge):
            if (
                stage_capture_reliable(logic_stage, source_token)
                or is_stage_locked_capture(logic_stage, source_token)
            ):
                continue
            if (kingdom, edge_id, source_token) not in gates_by_edge_token:
                message = (
                    f"edge '{edge_id}' capture '{source_token}' "
                    "has no authored physical gate"
                )
                if strict:
                    raise ValueError(message)
                warn(message)

    route_constraints: list[RouteCaptureConstraint] = []
    for (kingdom, edge_id), edge in route_by_id.items():
        try:
            alternatives = _requirement_dnf(
                edge.get("requirement_expr") or ""
            )
        except ValueError as exc:
            if strict:
                raise ValueError(
                    f"edge '{edge_id}' has malformed requirement: {exc}"
                ) from exc
            continue
        if not alternatives:
            continue
        masks = _option_scenario_masks(
            edge.get("option_scenarios") or "",
            len(alternatives),
        )
        difficulties = _option_difficulties(
            edge.get("difficulty") or "",
            len(alternatives),
        )
        from_node = node_by_id.get(
            (edge.get("from_node_id") or "").strip()
        )
        logic_stage = (
            (from_node.get("stage") or "").strip()
            if from_node else
            (edge.get("from_node_id") or "").split("::", 1)[0]
        )
        options = tuple(
            RouteCaptureOption(
                source_capture_tokens=tuple(
                    token for token in alternative
                    if token in capture_tokens
                    and not stage_capture_reliable(logic_stage, token)
                    and not is_stage_locked_capture(
                        logic_stage, token
                    )
                ),
                scenario_mask=masks[index],
                difficulty=difficulties[index],
                is_capture_only=bool(alternative) and all(
                    token in capture_tokens for token in alternative
                ),
            )
            for index, alternative in enumerate(alternatives)
        )
        if not any(option.source_capture_tokens for option in options):
            continue
        route_constraints.append(RouteCaptureConstraint(
            edge_id=edge_id,
            kingdom=kingdom,
            logic_stage=logic_stage,
            options=options,
        ))

    result = SwapConfig(
        entities=entities,
        placements=placements,
        rules=rules,
        gates=gates,
        any_pool=any_pool,
        menu_pool=menu_pool,
        npc_pool=npc_pool,
        prop_pool=prop_pool,
        two_d_pool=two_d_pool,
        entity_by_name=entity_by_name,
        placement_by_key=placement_by_key,
        gates_by_edge_token=gates_by_edge_token,
        route_constraints=route_constraints,
        reliable_captures=reliable_captures,
        pinned_captures=pinned_captures,
        memory_costs=memory_costs,
        stage_memory_budgets=stage_memory_budgets,
        spawn_restrictions=spawn_restrictions,
        memory_data_available=memory_data_available,
    )
    if strict and gates:
        # Every mode combination a player can select, not just on/off: the
        # narrow modes change which targets a gate can be satisfied by, so a
        # solver that only holds for the widest pool is not good enough.
        settings_combinations = (
            ("enemy", SWAP_MODE_OFF, SWAP_MODE_OFF, 0),
            ("enemy+npc", SWAP_MODE_SAME_CATEGORY, SWAP_MODE_OFF, 0),
            ("enemy+npc-combat", SWAP_MODE_WITH_COMBAT, SWAP_MODE_OFF, 0),
            ("enemy+prop", SWAP_MODE_OFF, SWAP_MODE_SAME_CATEGORY, 30),
            ("enemy+prop-combat", SWAP_MODE_OFF, SWAP_MODE_WITH_COMBAT, 30),
            ("enemy+prop-delete", SWAP_MODE_OFF,
             SWAP_MODE_WITH_DELETION, 30),
            ("enemy+npc+prop",
             SWAP_MODE_WITH_COMBAT, SWAP_MODE_WITH_COMBAT, 30),
        )
        for logic_difficulty in DIFFICULTY_RANK:
            for label, npc_mode, prop_mode, prop_chance \
                    in settings_combinations:
                try:
                    result.build_assignment(
                        0, 0,
                        enemy_enabled=True,
                        npc_mode=npc_mode,
                        prop_mode=prop_mode,
                        prop_chance=prop_chance,
                        logic_difficulty=logic_difficulty,
                    )
                except UnsatisfiedCaptureGates as exc:
                    raise ValueError(
                        "global capture-gate solver validation failed "
                        f"for {logic_difficulty}/{label}, "
                        f"code {exc.code}: {exc}"
                    ) from exc
    return result
