"""Deterministic host-side mirror of the runtime Moon Allocation engine.

The runtime implementation lives in src/Randomizer/MoonRando.cpp. This module
intentionally keeps the allocator independent of the graph solver: it only
reads moon metadata and settings, mutates the host moon objects with their
credited kingdom, and returns diagnostics for spoiler and parity tooling.
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
from typing import Any, Mapping, Sequence


UINT32_MASK = 0xFFFFFFFF

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
KINGDOM_BY_ID = {value: key for key, value in WORLD_IDS.items()}
KINGDOM_COUNT = len(WORLD_IDS)

# Vanilla credited totals in the LogicData/WorldMeta::WorldId_* order.  The
# historical NATIVE_TARGET name remains part of the host-tooling interface.
NATIVE_TARGET = (
    31, 42, 93, 80, 44, 9, 35, 85, 73,
    57, 72, 12, 64, 38, 55, 26, 3,
)
# Vanilla's local departure requirements.  Special/postgame kingdoms have no
# local moon gate, so their base is zero even though WorldMeta carries the
# unrelated 250/500 access thresholds for Dark and Darker Side.
BASE_MOON_REQUIREMENT = (
    0, 5, 16, 16, 8, 0, 10, 20, 10,
    10, 18, 3, 8, 0, 0, 0, 0,
)
VANILLA_EXCESS = tuple(
    total - requirement
    for total, requirement in zip(NATIVE_TARGET, BASE_MOON_REQUIREMENT)
)
MINIMUM_VALUE = (
    5, 15, 26, 26, 18, 5, 20, 30, 20,
    20, 28, 13, 18, 5, 5, 5, 5,
)

RNG_STREAM_SALTS = {
    "Moon": 0x4D6F6F6E,
    "MoonAllocation": 0x4D416C6C,
}
MOON_ALLOCATION_TARGET_SEED_SALT = 0x54415247
MOON_ALLOCATION_CARRIER_SEED_SALT = 0x43415252
MOON_ALLOCATION_ASSIGNMENT_SEED_SALT = 0x4153534E
MOON_ALLOCATION_PROFILE_SEED_SALT = 0x50524F46
TRIPLE_REWARD_SEED_SALT = 0x54524D33
LEGACY_TRIPLE_SEED_SALT = 0x4B494E47

MAX_FLOOR_RETRIES = 32
PROFILE_SHUFFLE_RETRIES = 512
CLOUD_CARRIER_MINIMUM = 10
MAX_SHINE_LIST_ROWS = 110
TIGHT_EXCESS_MAX = 20
CAPTURE_POOL_SIZE = 52
ABILITY_POOL_SIZE = 23
ABILITY_NON_ALWAYS_POOL_SIZE = 21


class MoonAllocationDifficulty(IntEnum):
    Easy = 0
    Vanilla = 1
    Hard = 2
    Expert = 3
    Max = 4


class MoonCoinPolicy(IntEnum):
    Vanilla = 0
    Standard = 1
    AbilityRush = 2


class AllocationConstraintKind(IntEnum):
    # The integer values intentionally match the C++ declaration order.
    None_ = 0
    CaptureSlots = 1
    PaintingEntries = 2
    AbilitySlotsCombined = 3

    def __str__(self) -> str:
        return {
            AllocationConstraintKind.None_: "none",
            AllocationConstraintKind.CaptureSlots: "capture-slots",
            AllocationConstraintKind.PaintingEntries: "painting-entries",
            AllocationConstraintKind.AbilitySlotsCombined: "ability-slots-combined",
        }[self]


@dataclass(frozen=True)
class CapacityFailure:
    constraint: str
    required: int
    available: int
    difficulty: MoonAllocationDifficulty
    coin_policy: MoonCoinPolicy
    lock_setting: str

    @property
    def message(self) -> str:
        return (
            "Moon allocation capacity failure: "
            f"constraint={self.constraint} required={self.required} "
            f"available={self.available} "
            f"difficulty={difficulty_name(self.difficulty)} "
            f"coinPolicy={coin_policy_name(self.coin_policy)} "
            f"lock={self.lock_setting}"
        )

    def __str__(self) -> str:
        return self.message


class MoonAllocationError(RuntimeError):
    """Raised when a requested allocation cannot satisfy its constraints."""

    def __init__(self, failure: CapacityFailure) -> None:
        self.failure = failure
        super().__init__(failure.message)


@dataclass
class KingdomAllocationSummary:
    kingdom: str
    assigned_entries: int = 0
    assigned_value: int = 0
    base_requirement: int = 0
    excess_value: int = 0
    minimum_value: int = 0
    special_capacity: int = 0
    special_minimum: int = 0
    special_kind: AllocationConstraintKind = AllocationConstraintKind.None_
    target_value: int = 0
    target_tight: bool = False
    concentration: bool = False

    def as_dict(self) -> dict[str, Any]:
        return {
            "kingdom": self.kingdom,
            "assigned_entries": self.assigned_entries,
            "assigned_value": self.assigned_value,
            "base_requirement": self.base_requirement,
            "excess_value": self.excess_value,
            "minimum_value": self.minimum_value,
            "special_capacity": self.special_capacity,
            "special_minimum": self.special_minimum,
            "special_kind": str(self.special_kind),
            "target_value": self.target_value,
            "target_tight": self.target_tight,
            "concentration": self.concentration,
        }


@dataclass
class MoonAllocationResult:
    success: bool
    active: bool
    assignment_active: bool
    assignments: dict[str, str]
    summaries: dict[str, KingdomAllocationSummary]
    concentration_kingdoms: tuple[str, ...]
    target_tight_kingdoms: tuple[str, ...]
    role_layout_hash: int
    failure: CapacityFailure | None = None

    def __bool__(self) -> bool:
        return self.success

    def as_dict(self) -> dict[str, Any]:
        return {
            "success": self.success,
            "active": self.active,
            "assignment_active": self.assignment_active,
            "assignments": dict(self.assignments),
            "summaries": {
                kingdom: summary.as_dict()
                for kingdom, summary in self.summaries.items()
            },
            "concentration_kingdoms": list(self.concentration_kingdoms),
            "target_tight_kingdoms": list(self.target_tight_kingdoms),
            "role_layout_hash": self.role_layout_hash,
            "failure": None if self.failure is None else {
                "constraint": self.failure.constraint,
                "required": self.failure.required,
                "available": self.failure.available,
                "difficulty": difficulty_name(self.failure.difficulty),
                "coin_policy": coin_policy_name(self.failure.coin_policy),
                "lock_setting": self.failure.lock_setting,
                "message": self.failure.message,
            },
        }


class _Rng:
    """The exact uint32 LCG used by Randomizer::Rng."""

    def __init__(self, seed: int, stream: str = "MoonAllocation") -> None:
        self.state = (
            (int(seed) & UINT32_MASK)
            ^ RNG_STREAM_SALTS.get(stream, 0)
        ) & UINT32_MASK

    def next(self) -> int:
        self.state = (
            self.state * 1664525 + 1013904223
        ) & UINT32_MASK
        return self.state

    def next_modulo(self, value: int) -> int:
        if value <= 0:
            return 0
        return self.next() % value

    def shuffle(self, values: list[Any]) -> None:
        for index in range(len(values) - 1, 0, -1):
            other = self.next_modulo(index + 1)
            values[index], values[other] = values[other], values[index]


def _make_allocation_rng(seed: int, salt: int) -> _Rng:
    # Mirrors makeMoonAllocationRng(seed, salt): makeRng(seed ^ salt,
    # RngStream::MoonAllocation).
    return _Rng((int(seed) ^ salt) & UINT32_MASK, "MoonAllocation")


def _setting(settings: Any, *names: str, default: Any = None) -> Any:
    for name in names:
        if hasattr(settings, name):
            value = getattr(settings, name)
            if value is not None:
                return value
    return default


def _as_bool(value: Any, default: bool = False) -> bool:
    if value is None:
        return default
    if isinstance(value, str):
        return value.strip().lower() in {"1", "true", "yes", "on"}
    return bool(value)


def _normalize_enum(
    value: Any,
    enum_type: type[IntEnum],
    names: Mapping[str, IntEnum],
    default: IntEnum,
) -> IntEnum:
    if isinstance(value, enum_type):
        return value
    if isinstance(value, IntEnum):
        try:
            return enum_type(int(value))
        except ValueError:
            return default
    if isinstance(value, int):
        try:
            return enum_type(value)
        except ValueError:
            return default
    normalized = str(value or "").strip().lower().replace("_", "-")
    return names.get(normalized, default)


def normalize_difficulty(value: Any) -> MoonAllocationDifficulty:
    return _normalize_enum(
        value,
        MoonAllocationDifficulty,
        {
            "easy": MoonAllocationDifficulty.Easy,
            "vanilla": MoonAllocationDifficulty.Vanilla,
            "standard": MoonAllocationDifficulty.Vanilla,
            "hard": MoonAllocationDifficulty.Hard,
            "expert": MoonAllocationDifficulty.Expert,
            "max": MoonAllocationDifficulty.Max,
        },
        MoonAllocationDifficulty.Vanilla,
    )


def normalize_coin_policy(value: Any) -> MoonCoinPolicy:
    return _normalize_enum(
        value,
        MoonCoinPolicy,
        {
            "vanilla": MoonCoinPolicy.Vanilla,
            "standard": MoonCoinPolicy.Standard,
            "ability-rush": MoonCoinPolicy.AbilityRush,
            "abilityrush": MoonCoinPolicy.AbilityRush,
        },
        MoonCoinPolicy.Vanilla,
    )


def difficulty_name(value: Any) -> str:
    return {
        MoonAllocationDifficulty.Easy: "Easy",
        MoonAllocationDifficulty.Vanilla: "Vanilla",
        MoonAllocationDifficulty.Hard: "Hard",
        MoonAllocationDifficulty.Expert: "Expert",
        MoonAllocationDifficulty.Max: "Max",
    }[normalize_difficulty(value)]


def coin_policy_name(value: Any) -> str:
    return {
        MoonCoinPolicy.Vanilla: "Vanilla",
        MoonCoinPolicy.Standard: "Standard",
        MoonCoinPolicy.AbilityRush: "Ability Rush",
    }[normalize_coin_policy(value)]


def _allocation_active(settings: Any) -> bool:
    return (
        _as_bool(
            _setting(
                settings,
                "moon_rando_enabled",
                "moon_randomization_enabled",
                default=True,
            ),
            True,
        )
        and _as_bool(
            _setting(
                settings,
                "moon_reallocation_enabled",
                "moon_reallocation",
                "enable_moon_reallocation",
                default=False,
            )
        )
        and _as_bool(
            _setting(
                settings,
                "randomize_moon_allocation",
                "moon_allocation_randomize",
                default=False,
            )
        )
    )


def _triple_randomized(settings: Any) -> bool:
    return _as_bool(
        _setting(
            settings,
            "randomize_triple_moons",
            "triple_moons_randomized",
            default=False,
        )
    )


def _capture_enabled(settings: Any) -> bool:
    return _as_bool(
        _setting(settings, "capture_rando", "capture_lock", default=True),
        True,
    )


def _ability_enabled(settings: Any) -> bool:
    return _as_bool(
        _setting(settings, "ability_rando", "ability_lock", default=True),
        True,
    )


def _kingdom_id(value: Any) -> int:
    if isinstance(value, int):
        return value if 0 <= value < KINGDOM_COUNT else -1
    return WORLD_IDS.get(str(value), -1)


def kingdom_name(value: Any) -> str:
    kingdom = _kingdom_id(value)
    return KINGDOM_BY_ID.get(kingdom, str(value))


def _moon_flags(moon: Any) -> str:
    return str(getattr(moon, "flags", "") or "")


def _is_story(moon: Any) -> bool:
    return "ShineFlag_Story" in _moon_flags(moon)


def _is_hint_art(moon: Any) -> bool:
    return "ShineFlag_HintArt" in _moon_flags(moon)


def _is_toadette(moon: Any) -> bool:
    return "ShineFlag_Toadette" in _moon_flags(moon)


def _physical_multi(moon: Any) -> bool:
    return "ShineFlag_Multi" in _moon_flags(moon)


def _pool_eligible(moon: Any) -> bool:
    uid = getattr(moon, "uid", -1)
    obj_id = getattr(moon, "obj_id", -1)
    try:
        uid = int(uid)
        obj_id = int(obj_id)
    except (TypeError, ValueError):
        return False
    return (
        uid != 0
        and obj_id >= 0
        and not _is_toadette(moon)
    )


def _reward_multi(moon: Any) -> bool:
    override = getattr(moon, "reward_is_multi", None)
    return _physical_multi(moon) if override is None else bool(override)


def reward_value(moon: Any) -> int:
    return 3 if _reward_multi(moon) else 1


def _home_kingdom(moon: Any) -> int:
    return _kingdom_id(getattr(moon, "kingdom", ""))


def _credited_kingdom(moon: Any) -> int:
    assigned = getattr(moon, "assigned_kingdom", None)
    return _kingdom_id(
        assigned if assigned is not None else getattr(moon, "kingdom", "")
    )


def _moon_sequence(source: Any) -> list[Any]:
    if hasattr(source, "moons"):
        source = getattr(source, "moons")
    if isinstance(source, Mapping):
        return list(source.values())
    return list(source)


def _set_assignment(moon: Any, kingdom: int | None) -> None:
    setattr(
        moon,
        "assigned_kingdom",
        None if kingdom is None else KINGDOM_BY_ID[kingdom],
    )


def clear_moon_assignments(source: Any) -> None:
    for moon in _moon_sequence(source):
        _set_assignment(moon, None)


def compute_reward_multi_values(
    source: Any,
    seed: int,
    randomized: bool = False,
) -> None:
    """Apply the C++ reward-triple prepass in stable moon-table order.

    Physical ShineFlag_Multi remains untouched. Only the item reward role is
    changed, matching the runtime distinction between multi locations and
    multi rewards.
    """

    moons = _moon_sequence(source)
    for moon in moons:
        setattr(moon, "reward_is_multi", _physical_multi(moon))

    if not randomized:
        return

    target = sum(_physical_multi(moon) for moon in moons)
    candidates = [
        index
        for index, moon in enumerate(moons)
        if _pool_eligible(moon) and not _is_story(moon) and not _is_hint_art(moon)
    ]
    # Once the C++ override is active, every item reads from the override table;
    # entries outside the eligible candidate pool therefore become single-value
    # rewards even if their physical location is a vanilla triple slot.
    for moon in moons:
        setattr(moon, "reward_is_multi", False)
    rng = _Rng(
        (int(seed) ^ TRIPLE_REWARD_SEED_SALT) & UINT32_MASK,
        "Moon",
    )
    rng.shuffle(candidates)
    for index in candidates[: min(target, len(candidates))]:
        setattr(moons[index], "reward_is_multi", True)
    for index in candidates[min(target, len(candidates)):]:
        setattr(moons[index], "reward_is_multi", False)


def _special_use(settings: Any, kingdom: int) -> str:
    names = {
        0: ("cap_kingdom_moon_use", "cap_moon_use"),
        5: ("cloud_kingdom_moon_use", "cloud_moon_use"),
        13: ("moon_kingdom_moon_use", "moon_moon_use"),
    }
    value = _setting(settings, *names.get(kingdom, ()), default="coins")
    if kingdom == 5 and _as_bool(
        _setting(settings, "cloud_kingdom_moons_unlock_paintings", default=False)
    ):
        # Preserve the older host setting as an alias for the current selector.
        value = "painting-unlock"
    if isinstance(value, int):
        value = {
            0: "coins",
            1: "abilities-or-captures",
            2: "kingdom-unlock",
            3: "painting-unlock",
            4: "abilities",
            5: "captures",
        }.get(value, "coins")
    normalized = (
        str(value or "coins")
        .strip()
        .lower()
        .replace("_", "-")
        .replace(" ", "-")
    )
    return {
        "coin": "coins",
        "coins": "coins",
        "ability": "abilities",
        "abilities": "abilities",
        "capture": "captures",
        "captures": "captures",
        "abilities-or-captures": "abilities-or-captures",
        "ability-or-capture": "abilities-or-captures",
        "kingdom-unlock": "kingdom-unlock",
        "painting-unlock": "painting-unlock",
    }.get(normalized, "coins")


def is_moon_coin(moon: Any, settings: Any) -> bool:
    """Classify once from the physical/home kingdom, before allocation."""

    if not _pool_eligible(moon) or _is_story(moon) or _is_hint_art(moon):
        return False
    home = _home_kingdom(moon)
    if home in (0, 5, 13):
        return _special_use(settings, home) == "coins"
    if home == 14:
        return not _capture_enabled(settings)
    if home in (15, 16):
        return not _ability_enabled(settings)
    return False


def _starting_equipment_rank(settings: Any) -> int:
    value = _setting(settings, "starting_equipment", default="normal")
    if isinstance(value, int):
        return max(0, min(2, value))
    normalized = (
        str(value or "normal")
        .strip()
        .lower()
        .replace("_", "-")
        .replace(" ", "-")
    )
    return {"scarce": 0, "normal": 1, "plentiful": 2}.get(normalized, 1)


def required_capture_slots(settings: Any, world: Any = None) -> int:
    override = _setting(settings, "required_capture_slots", default=None)
    if override is not None:
        return max(0, int(override))
    guaranteed = (0, 2, 4)[_starting_equipment_rank(settings)]
    return max(0, CAPTURE_POOL_SIZE - guaranteed)


def required_ability_slots(settings: Any, world: Any = None) -> int:
    override = _setting(settings, "required_ability_slots", default=None)
    if override is not None:
        return max(0, int(override))

    # The C++ ability table contains 23 entries, two of which are always
    # unlocked. The progression requirement is counted from the remaining 21
    # entries, then removes the starting cap throw and equipment grants.
    pool_size = ABILITY_NON_ALWAYS_POOL_SIZE
    tokens = getattr(world, "tokens", None) if world is not None else None
    if isinstance(tokens, Mapping):
        ability_tokens = [
            token
            for token in tokens.values()
            if getattr(token, "token_type", "") == "ability"
        ]
        if ability_tokens:
            pool_size = sum(
                not _as_bool(getattr(token, "always_unlocked", False))
                for token in ability_tokens
            )

    # One non-always-unlocked ability is the starting cap throw.
    pool_size = max(0, pool_size - 1)

    guaranteed_extra = (0, 1, 2)[_starting_equipment_rank(settings)]
    return max(0, pool_size - guaranteed_extra)


def _minimums(
    settings: Any,
    override: Sequence[int] | Mapping[Any, int] | None,
) -> tuple[int, ...]:
    values: list[int] = list(MINIMUM_VALUE)
    supplied = override
    if supplied is None:
        supplied = _setting(settings, "moon_allocation_minimums", default=None)
    if isinstance(supplied, Mapping):
        for key, value in supplied.items():
            kingdom = _kingdom_id(key)
            if kingdom >= 0:
                values[kingdom] = int(value)
    elif supplied is not None:
        supplied_values = list(supplied)
        if len(supplied_values) != KINGDOM_COUNT:
            raise ValueError("moon allocation minimums must contain 17 values")
        values = [int(value) for value in supplied_values]
    return tuple(values)


def _native_targets(settings: Any) -> tuple[int, ...]:
    supplied = _setting(settings, "moon_allocation_native_targets", default=None)
    if supplied is None:
        return NATIVE_TARGET
    if isinstance(supplied, Mapping):
        values = list(NATIVE_TARGET)
        for key, value in supplied.items():
            kingdom = _kingdom_id(key)
            if kingdom >= 0:
                values[kingdom] = int(value)
        return tuple(values)
    values = tuple(int(value) for value in supplied)
    if len(values) != KINGDOM_COUNT:
        raise ValueError("moon allocation native targets must contain 17 values")
    return values


def _mask(kingdom: int) -> int:
    return 1 << kingdom if 0 <= kingdom < KINGDOM_COUNT else 0


def _mask_width(mask: int) -> int:
    return mask.bit_count()


def _failure(
    moons: Sequence[Any],
    settings: Any,
    difficulty: MoonAllocationDifficulty,
    coin_policy: MoonCoinPolicy,
    constraint: str,
    required: int,
    available: int,
    lock_setting: str,
    *,
    active: bool,
    assignment_active: bool = False,
) -> MoonAllocationResult:
    clear_moon_assignments(moons)
    return MoonAllocationResult(
        success=False,
        active=active,
        assignment_active=assignment_active,
        assignments={
            str(getattr(moon, "moon_id", index)): getattr(moon, "kingdom", "")
            for index, moon in enumerate(moons)
        },
        summaries={},
        concentration_kingdoms=(),
        target_tight_kingdoms=(),
        # C++ clears the persisted role-layout hash on every rejected
        # allocation; do not expose the cleared identity layout as a valid
        # parity digest.
        role_layout_hash=0,
        failure=CapacityFailure(
            constraint,
            int(required),
            int(available),
            difficulty,
            coin_policy,
            lock_setting,
        ),
    )


def moon_role_layout_hash(source: Any) -> int:
    """Stable FNV-style digest matching calculateMoonRoleLayoutHash()."""

    moons = _moon_sequence(source)
    digest = (2166136261 ^ 0x4D524C59) & UINT32_MASK
    for moon in moons:
        kingdom = _credited_kingdom(moon)
        digest ^= (kingdom + 2) & UINT32_MASK
        digest = (digest * 16777619) & UINT32_MASK
        digest ^= 1 if reward_value(moon) == 3 else 0
        digest = (digest * 16777619) & UINT32_MASK
    return digest or 1


@dataclass
class _Item:
    home: int
    value: int
    destination_mask: int
    movable: bool
    fixed: bool
    capture_carrier: bool
    ability_carrier: bool
    painting_carrier: bool
    moon_coin: bool


def _recompute_state(
    assigned: Sequence[int],
    items: Sequence[_Item],
) -> tuple[list[int], list[int], int, list[int], int]:
    values = [0] * KINGDOM_COUNT
    entries = [0] * KINGDOM_COUNT
    capture_value = 0
    ability_values = [0] * KINGDOM_COUNT
    painting_entries = 0
    for index, destination in enumerate(assigned):
        if not 0 <= destination < KINGDOM_COUNT:
            continue
        item = items[index]
        values[destination] += item.value
        entries[destination] += 1
        if item.capture_carrier and destination == 14:
            capture_value += item.value
        if item.ability_carrier and destination in (15, 16):
            ability_values[destination] += item.value
        if item.painting_carrier and destination == 5:
            # Cloud painting capacity follows reward value: a randomized triple
            # reward opens three links, matching the runtime mapping.
            painting_entries += item.value
    return values, entries, capture_value, ability_values, painting_entries


def _has_enough_remaining_value(
    candidate: int,
    target: int,
    deficits: Sequence[int],
    items: Sequence[_Item],
    remaining_values: Sequence[int],
) -> bool:
    for kingdom in range(KINGDOM_COUNT):
        required = deficits[kingdom]
        if kingdom == target:
            required = max(0, required - items[candidate].value)
        available = remaining_values[kingdom]
        if items[candidate].destination_mask & _mask(kingdom):
            available -= items[candidate].value
        if available < required:
            return False
    return True


def _fallback_distributed_totals(
    baseline_assigned: Sequence[int],
    balance_values: Sequence[int],
    items: Sequence[_Item],
    optional_order: Sequence[int],
    native_target: Sequence[int],
    kingdom_tie_rank: Sequence[int],
) -> list[int]:
    """Build exact totals for noncanonical, synthetic host-side item pools."""

    assigned = list(baseline_assigned)
    values = list(balance_values)
    for index in optional_order:
        allowed = items[index].destination_mask
        has_positive_deficit = any(
            allowed & _mask(kingdom)
            and native_target[kingdom] - values[kingdom] > 0
            for kingdom in range(KINGDOM_COUNT)
        )
        destination = -1
        for kingdom in range(KINGDOM_COUNT):
            if not allowed & _mask(kingdom):
                continue
            difference = (
                native_target[kingdom] - values[kingdom]
                if has_positive_deficit
                else values[kingdom] - native_target[kingdom]
            )
            if destination < 0:
                destination = kingdom
                continue
            current_difference = (
                native_target[destination] - values[destination]
                if has_positive_deficit
                else values[destination] - native_target[destination]
            )
            if (
                difference > current_difference
                or (
                    difference == current_difference
                    and kingdom_tie_rank[kingdom]
                    < kingdom_tie_rank[destination]
                )
            ):
                destination = kingdom
        if destination < 0:
            return []
        assigned[index] = destination
        values[destination] += items[index].value
    return _recompute_state(assigned, items)[0]


def _fill_profile_group(
    counts: list[int],
    maximums: Sequence[int],
    deficits: Sequence[int],
    candidates: Sequence[int],
    wanted_total: int,
    kingdom_tie_rank: Sequence[int],
) -> bool:
    while sum(counts[kingdom] for kingdom in candidates) < wanted_total:
        destination = -1
        for kingdom in candidates:
            if counts[kingdom] >= maximums[kingdom]:
                continue
            if (
                destination < 0
                or deficits[kingdom] - 3 * counts[kingdom]
                > deficits[destination] - 3 * counts[destination]
                or (
                    deficits[kingdom] - 3 * counts[kingdom]
                    == deficits[destination] - 3 * counts[destination]
                    and kingdom_tie_rank[kingdom]
                    < kingdom_tie_rank[destination]
                )
            ):
                destination = kingdom
        if destination < 0:
            return False
        counts[destination] += 1
    return True


def _plan_exact_profile_assignment(
    profile_target: Sequence[int],
    baseline_assigned: Sequence[int],
    items: Sequence[_Item],
    optional_order: Sequence[int],
    kingdom_tie_rank: Sequence[int],
) -> list[int] | None:
    """Plan an exact 1/3-value fill without exceeding the native 110 rows."""

    (
        baseline_values,
        baseline_entries,
        _capture_value,
        _ability_values,
        _painting_entries,
    ) = _recompute_state(baseline_assigned, items)
    deficits = [
        int(profile_target[kingdom]) - baseline_values[kingdom]
        for kingdom in range(KINGDOM_COUNT)
    ]
    if any(deficit < 0 for deficit in deficits):
        return None

    optional = [index for index in optional_order if baseline_assigned[index] < 0]
    if any(items[index].value not in (1, 3) for index in optional):
        return None
    if sum(deficits) != sum(items[index].value for index in optional):
        return None

    special_mask = _mask(14) | _mask(15) | _mask(16)
    restricted_triples = [
        index
        for index in optional
        if items[index].value == 3
        and not items[index].destination_mask & special_mask
    ]
    unrestricted_triples = [
        index
        for index in optional
        if items[index].value == 3
        and items[index].destination_mask & special_mask
    ]
    restricted_singles = [
        index
        for index in optional
        if items[index].value == 1
        and not items[index].destination_mask & special_mask
    ]
    unrestricted_singles = [
        index
        for index in optional
        if items[index].value == 1
        and items[index].destination_mask & special_mask
    ]

    minimum_triples = [0] * KINGDOM_COUNT
    maximum_triples = [0] * KINGDOM_COUNT
    for kingdom in range(KINGDOM_COUNT):
        over_rows = baseline_entries[kingdom] + deficits[kingdom] - MAX_SHINE_LIST_ROWS
        minimum_triples[kingdom] = max(0, (over_rows + 1) // 2)
        maximum_triples[kingdom] = deficits[kingdom] // 3
        if minimum_triples[kingdom] > maximum_triples[kingdom]:
            return None

    ordinary = tuple(range(14))
    special = (14, 15, 16)
    total_triples = len(restricted_triples) + len(unrestricted_triples)
    ordinary_deficit = sum(deficits[kingdom] for kingdom in ordinary)
    ordinary_minimum = sum(minimum_triples[kingdom] for kingdom in ordinary)
    ordinary_maximum = sum(maximum_triples[kingdom] for kingdom in ordinary)
    special_minimum = sum(minimum_triples[kingdom] for kingdom in special)
    special_maximum = sum(maximum_triples[kingdom] for kingdom in special)
    ordinary_lower = max(
        len(restricted_triples),
        ordinary_minimum,
        total_triples - special_maximum,
    )
    ordinary_upper = min(
        ordinary_maximum,
        total_triples - special_minimum,
        (ordinary_deficit - len(restricted_singles)) // 3,
    )
    if ordinary_lower > ordinary_upper:
        return None

    # Use as many special-destination triples as possible.  This preserves
    # ordinary capacity for Standard-policy Moon Coin singles and naturally
    # lowers the row count in the three smallest native target kingdoms.
    ordinary_triple_total = ordinary_lower
    triple_counts = list(minimum_triples)
    if not _fill_profile_group(
        triple_counts,
        maximum_triples,
        deficits,
        ordinary,
        ordinary_triple_total,
        kingdom_tie_rank,
    ):
        return None
    if not _fill_profile_group(
        triple_counts,
        maximum_triples,
        deficits,
        special,
        total_triples - ordinary_triple_total,
        kingdom_tie_rank,
    ):
        return None

    assigned = list(baseline_assigned)
    remaining_triples = list(triple_counts)

    def take_destination(index: int, remaining: list[int], candidates: Sequence[int]) -> int:
        destination = -1
        for kingdom in candidates:
            if remaining[kingdom] <= 0 or not items[index].destination_mask & _mask(kingdom):
                continue
            if (
                destination < 0
                or remaining[kingdom] > remaining[destination]
                or (
                    remaining[kingdom] == remaining[destination]
                    and kingdom_tie_rank[kingdom]
                    < kingdom_tie_rank[destination]
                )
            ):
                destination = kingdom
        if destination >= 0:
            remaining[destination] -= 1
        return destination

    for index in restricted_triples:
        destination = take_destination(index, remaining_triples, ordinary)
        if destination < 0:
            return None
        assigned[index] = destination
    for index in unrestricted_triples:
        destination = take_destination(index, remaining_triples, tuple(range(KINGDOM_COUNT)))
        if destination < 0:
            return None
        assigned[index] = destination
    if any(remaining_triples):
        return None

    remaining_singles = [
        deficits[kingdom] - 3 * triple_counts[kingdom]
        for kingdom in range(KINGDOM_COUNT)
    ]
    for index in restricted_singles:
        destination = take_destination(index, remaining_singles, ordinary)
        if destination < 0:
            return None
        assigned[index] = destination
    for index in unrestricted_singles:
        destination = take_destination(index, remaining_singles, tuple(range(KINGDOM_COUNT)))
        if destination < 0:
            return None
        assigned[index] = destination
    if any(remaining_singles):
        return None

    final_values, final_entries, *_ = _recompute_state(assigned, items)
    if final_values != list(profile_target):
        return None
    if any(entries > MAX_SHINE_LIST_ROWS for entries in final_entries):
        return None
    return assigned


def _identity_result(
    moons: Sequence[Any],
    settings: Any,
    *,
    assignment_active: bool = False,
) -> MoonAllocationResult:
    active = _allocation_active(settings)
    clear_moon_assignments(moons)
    return MoonAllocationResult(
        success=True,
        active=active,
        assignment_active=assignment_active,
        assignments={
            str(getattr(moon, "moon_id", index)): getattr(moon, "kingdom", "")
            for index, moon in enumerate(moons)
        },
        summaries={},
        concentration_kingdoms=(),
        target_tight_kingdoms=(),
        role_layout_hash=moon_role_layout_hash(moons),
    )


def _legacy_triple_assignment(
    moons: Sequence[Any],
    settings: Any,
    seed: int,
) -> MoonAllocationResult:
    need = [
        sum(
            3 if _physical_multi(moon) else 1
            for moon in moons
            if _home_kingdom(moon) == kingdom
        )
        for kingdom in range(KINGDOM_COUNT)
    ]
    triples: list[int] = []
    singles: list[int] = []
    for index, moon in enumerate(moons):
        home = _home_kingdom(moon)
        if not 0 <= home < KINGDOM_COUNT:
            return _failure(
                moons,
                settings,
                MoonAllocationDifficulty.Vanilla,
                MoonCoinPolicy.Vanilla,
                "legacy-triple-home",
                1,
                0,
                "none",
                active=False,
            )
        if (
            not _pool_eligible(moon)
            or _is_story(moon)
            or _is_hint_art(moon)
        ):
            need[home] -= reward_value(moon)
        elif _reward_multi(moon):
            triples.append(index)
        else:
            singles.append(index)

    slots: list[int] = []
    for kingdom in range(KINGDOM_COUNT):
        slots.extend([kingdom] * max(0, need[kingdom] // 3))
    rng = _Rng(
        (int(seed) ^ LEGACY_TRIPLE_SEED_SALT) & UINT32_MASK,
        "Moon",
    )
    rng.shuffle(slots)
    rng.shuffle(triples)
    rng.shuffle(singles)

    assigned = [_home_kingdom(moon) for moon in moons]
    triples_per_kingdom = [0] * KINGDOM_COUNT
    for kingdom in slots[: min(len(triples), len(slots))]:
        triples_per_kingdom[kingdom] += 1
    triple_index = 0
    for kingdom in range(KINGDOM_COUNT):
        for _ in range(triples_per_kingdom[kingdom]):
            if triple_index >= len(triples):
                break
            assigned[triples[triple_index]] = kingdom
            triple_index += 1
            need[kingdom] -= 3
    single_index = 0
    for kingdom in range(KINGDOM_COUNT):
        while need[kingdom] > 0 and single_index < len(singles):
            assigned[singles[single_index]] = kingdom
            single_index += 1
            need[kingdom] -= 1

    expected = [0] * KINGDOM_COUNT
    actual = [0] * KINGDOM_COUNT
    total = 0
    for index, moon in enumerate(moons):
        home = _home_kingdom(moon)
        destination = assigned[index]
        value = reward_value(moon)
        total += value
        if not 0 <= home < KINGDOM_COUNT or not 0 <= destination < KINGDOM_COUNT:
            continue
        expected[home] += 3 if _physical_multi(moon) else 1
        actual[destination] += value
    if actual != expected:
        return _failure(
            moons,
            settings,
            MoonAllocationDifficulty.Vanilla,
            MoonCoinPolicy.Vanilla,
            "legacy-triple-total",
            total,
            total,
            "none",
            active=False,
        )
    for index, destination in enumerate(assigned):
        _set_assignment(moons[index], destination)
    return MoonAllocationResult(
        success=True,
        active=False,
        assignment_active=True,
        assignments={
            str(getattr(moon, "moon_id", index)): KINGDOM_BY_ID[assigned[index]]
            for index, moon in enumerate(moons)
        },
        summaries={},
        concentration_kingdoms=(),
        target_tight_kingdoms=(),
        role_layout_hash=moon_role_layout_hash(moons),
    )


def allocate_moons(
    source: Any,
    settings: Any,
    *,
    seed: int | None = None,
    minimum_values: Sequence[int] | Mapping[Any, int] | None = None,
) -> MoonAllocationResult:
    """Allocate credited kingdoms using the runtime algorithm.

    source may be a LogicWorld or an ordered iterable of Moon objects.
    Allocation is deliberately performed in source order; callers must pass
    the same stable moon order used by the runtime metadata table.
    """

    moons = _moon_sequence(source)
    world = source if hasattr(source, "tokens") else None
    if seed is None:
        seed = int(_setting(settings, "seed", default=0))
    difficulty = normalize_difficulty(
        _setting(
            settings,
            "moon_allocation_difficulty",
            "allocation_difficulty",
            default="vanilla",
        )
    )
    coin_policy = normalize_coin_policy(
        _setting(settings, "moon_coin_policy", "coin_policy", default="vanilla")
    )
    active = _allocation_active(settings)
    clear_moon_assignments(moons)

    if not active:
        if _triple_randomized(settings):
            return _legacy_triple_assignment(moons, settings, seed)
        return _identity_result(moons, settings)

    supplied_minimums = _setting(
        settings, "moon_allocation_minimums", default=None
    )
    has_minimum_override = minimum_values is not None or supplied_minimums is not None
    minimum = _minimums(settings, minimum_values)
    native_target = _native_targets(settings)
    items: list[_Item] = []
    base_assigned = [-1] * len(moons)
    baseline_assigned = [-1] * len(moons)
    working_assigned = [-1] * len(moons)

    for index, moon in enumerate(moons):
        home = _home_kingdom(moon)
        if not 0 <= home < KINGDOM_COUNT:
            return _failure(
                moons,
                settings,
                difficulty,
                coin_policy,
                "invalid-home",
                1,
                0,
                "none",
                active=active,
            )
        pool_eligible = _pool_eligible(moon)
        fixed = not pool_eligible or _is_story(moon) or _is_hint_art(moon)
        moon_coin = is_moon_coin(moon, settings)
        if moon_coin and coin_policy == MoonCoinPolicy.Vanilla:
            fixed = True
        value = reward_value(moon)
        if fixed:
            destination_mask = _mask(home)
            base_assigned[index] = home
        elif moon_coin and coin_policy == MoonCoinPolicy.Standard:
            destination_mask = (1 << 14) - 1
        else:
            destination_mask = (1 << KINGDOM_COUNT) - 1
        items.append(
            _Item(
                home=home,
                value=value,
                destination_mask=destination_mask,
                movable=not fixed,
                fixed=fixed,
                capture_carrier=pool_eligible,
                ability_carrier=pool_eligible,
                painting_carrier=pool_eligible,
                moon_coin=moon_coin,
            )
        )

    kingdom_order = list(range(KINGDOM_COUNT))
    target_rng = _make_allocation_rng(
        seed, MOON_ALLOCATION_TARGET_SEED_SALT
    )
    target_rng.shuffle(kingdom_order)
    kingdom_tie_rank = [0] * KINGDOM_COUNT
    for rank, kingdom in enumerate(kingdom_order):
        kingdom_tie_rank[kingdom] = rank

    carrier_order = [
        index
        for index, item in enumerate(items)
        if item.movable
        and (
            item.capture_carrier
            or item.ability_carrier
            or item.painting_carrier
        )
    ]
    carrier_rng = _make_allocation_rng(
        seed, MOON_ALLOCATION_CARRIER_SEED_SALT
    )
    carrier_rng.shuffle(carrier_order)

    fixed_capture_value = 0
    fixed_ability_value = [0] * KINGDOM_COUNT
    fixed_painting_entries = 0
    for index, destination in enumerate(base_assigned):
        if destination < 0:
            continue
        item = items[index]
        if item.capture_carrier and destination == 14:
            fixed_capture_value += item.value
        if item.ability_carrier and destination in (15, 16):
            fixed_ability_value[destination] += item.value
        if item.painting_carrier and destination == 5:
            fixed_painting_entries += item.value

    cloud_painting_active = _special_use(settings, 5) == "painting-unlock"
    if cloud_painting_active and fixed_painting_entries < CLOUD_CARRIER_MINIMUM:
        available = fixed_painting_entries + sum(
            items[index].value
            for index in carrier_order
            if base_assigned[index] < 0
            and items[index].painting_carrier
            and items[index].destination_mask & _mask(5)
        )
        if available < CLOUD_CARRIER_MINIMUM:
            return _failure(
                moons,
                settings,
                difficulty,
                coin_policy,
                "painting-entries",
                CLOUD_CARRIER_MINIMUM,
                available,
                "cloud-painting",
                active=active,
            )
        while fixed_painting_entries < CLOUD_CARRIER_MINIMUM:
            chosen = -1
            # The C++ loop deliberately visits value 1, then value 3.
            for value_class in (1, 3):
                for index in carrier_order:
                    item = items[index]
                    if (
                        base_assigned[index] < 0
                        and item.painting_carrier
                        and item.value == value_class
                        and item.destination_mask & _mask(5)
                    ):
                        chosen = index
                        break
                if chosen >= 0:
                    break
            if chosen < 0:
                return _failure(
                    moons,
                    settings,
                    difficulty,
                    coin_policy,
                    "painting-entries",
                    CLOUD_CARRIER_MINIMUM,
                    fixed_painting_entries,
                    "cloud-painting",
                    active=active,
                )
            base_assigned[chosen] = 5
            fixed_painting_entries += items[chosen].value

    if _capture_enabled(settings):
        required = required_capture_slots(settings, world)
        available = fixed_capture_value + sum(
            items[index].value
            for index in carrier_order
            if base_assigned[index] < 0
            and items[index].capture_carrier
            and items[index].destination_mask & _mask(14)
        )
        if available < required:
            return _failure(
                moons,
                settings,
                difficulty,
                coin_policy,
                "capture-slots",
                required,
                available,
                "capture-lock",
                active=active,
            )
        current = fixed_capture_value
        while current < required:
            preferred = 3 if required - current >= 3 else 1
            chosen = -1
            for value_class in (
                preferred,
                1 if preferred == 3 else 3,
            ):
                for index in carrier_order:
                    item = items[index]
                    if (
                        base_assigned[index] < 0
                        and item.capture_carrier
                        and item.value == value_class
                        and item.destination_mask & _mask(14)
                    ):
                        chosen = index
                        break
                if chosen >= 0:
                    break
            if chosen < 0:
                return _failure(
                    moons,
                    settings,
                    difficulty,
                    coin_policy,
                    "capture-slots",
                    required,
                    current,
                    "capture-lock",
                    active=active,
                )
            base_assigned[chosen] = 14
            current += items[chosen].value

    if _ability_enabled(settings):
        required = required_ability_slots(settings, world)
        available = (
            fixed_ability_value[15]
            + fixed_ability_value[16]
            + sum(
                items[index].value
                for index in carrier_order
                if base_assigned[index] < 0
                and items[index].ability_carrier
                and items[index].destination_mask & (_mask(15) | _mask(16))
            )
        )
        if available < required:
            return _failure(
                moons,
                settings,
                difficulty,
                coin_policy,
                "ability-slots-combined",
                required,
                available,
                "ability-lock",
                active=active,
            )
        current = fixed_ability_value[15] + fixed_ability_value[16]
        ability_value = list(fixed_ability_value)
        while current < required:
            chosen = -1
            destination = -1
            for index in carrier_order:
                item = items[index]
                if base_assigned[index] >= 0 or not item.ability_carrier:
                    continue
                can_dark = bool(item.destination_mask & _mask(15))
                can_darker = bool(item.destination_mask & _mask(16))
                if not can_dark and not can_darker:
                    continue
                if can_dark and can_darker:
                    destination = (
                        15
                        if ability_value[15] < ability_value[16]
                        or (
                            ability_value[15] == ability_value[16]
                            and kingdom_tie_rank[15] < kingdom_tie_rank[16]
                        )
                        else 16
                    )
                elif can_dark:
                    destination = 15
                else:
                    destination = 16
                chosen = index
                break
            if chosen < 0:
                return _failure(
                    moons,
                    settings,
                    difficulty,
                    coin_policy,
                    "ability-slots-combined",
                    required,
                    current,
                    "ability-lock",
                    active=active,
                )
            base_assigned[chosen] = destination
            ability_value[destination] += items[chosen].value
            current += items[chosen].value

    item_total_value = sum(item.value for item in items)
    uses_vanilla_excess_profile = (
        difficulty != MoonAllocationDifficulty.Easy
        and item_total_value == sum(native_target)
        and all(
            native_target[kingdom] >= BASE_MOON_REQUIREMENT[kingdom]
            for kingdom in range(KINGDOM_COUNT)
        )
    )
    floor_values = (
        tuple(BASE_MOON_REQUIREMENT)
        if uses_vanilla_excess_profile and not has_minimum_override
        else minimum
    )

    ordinary_order = [
        index
        for index, destination in enumerate(base_assigned)
        if destination < 0
    ]
    floor_succeeded = False
    assigned_values = [0] * KINGDOM_COUNT
    assigned_entries = [0] * KINGDOM_COUNT
    assigned_capture_value = 0
    assigned_ability_value = [0] * KINGDOM_COUNT
    assigned_painting_entries = 0

    for _retry in range(MAX_FLOOR_RETRIES):
        working_assigned = list(base_assigned)
        (
            assigned_values,
            assigned_entries,
            assigned_capture_value,
            assigned_ability_value,
            assigned_painting_entries,
        ) = _recompute_state(working_assigned, items)
        deficits = [
            max(0, floor_values[kingdom] - assigned_values[kingdom])
            for kingdom in range(KINGDOM_COUNT)
        ]
        remaining_values = [
            sum(
                items[index].value
                for index, destination in enumerate(base_assigned)
                if destination < 0
                and items[index].destination_mask & _mask(kingdom)
            )
            for kingdom in range(KINGDOM_COUNT)
        ]
        target_rng.shuffle(ordinary_order)
        remaining_deficits = sum(deficits)
        while remaining_deficits > 0:
            made_progress = False
            for target in kingdom_order:
                if deficits[target] <= 0:
                    continue
                chosen = -1
                chosen_mask_width = 0x7FFFFFFF
                chosen_value_rank = 0x7FFFFFFF
                chosen_order_rank = 0x7FFFFFFF
                for order_rank, index in enumerate(ordinary_order):
                    item = items[index]
                    if (
                        working_assigned[index] >= 0
                        or not item.destination_mask & _mask(target)
                    ):
                        continue
                    desired_value = 3 if deficits[target] >= 3 else 1
                    value_rank = 0 if item.value == desired_value else 1
                    if value_rank != 0 and item.value not in (1, 3):
                        continue
                    if not _has_enough_remaining_value(
                        index,
                        target,
                        deficits,
                        items,
                        remaining_values,
                    ):
                        continue
                    mask_width = _mask_width(item.destination_mask)
                    if (
                        chosen < 0
                        or mask_width < chosen_mask_width
                        or (
                            mask_width == chosen_mask_width
                            and value_rank < chosen_value_rank
                        )
                        or (
                            mask_width == chosen_mask_width
                            and value_rank == chosen_value_rank
                            and order_rank < chosen_order_rank
                        )
                    ):
                        chosen = index
                        chosen_mask_width = mask_width
                        chosen_value_rank = value_rank
                        chosen_order_rank = order_rank
                if chosen < 0:
                    continue
                working_assigned[chosen] = target
                for kingdom in range(KINGDOM_COUNT):
                    if items[chosen].destination_mask & _mask(kingdom):
                        remaining_values[kingdom] -= items[chosen].value
                before = deficits[target]
                deficits[target] = max(0, before - items[chosen].value)
                remaining_deficits -= before - deficits[target]
                made_progress = True
                break
            if not made_progress:
                break
        if remaining_deficits == 0:
            floor_succeeded = True
            break

    if not floor_succeeded:
        required = sum(floor_values)
        available = sum(
            items[index].value
            for index, destination in enumerate(base_assigned)
            if destination < 0
        )
        return _failure(
            moons,
            settings,
            difficulty,
            coin_policy,
            "ordinary-floors",
            required,
            available,
            "none",
            active=active,
        )

    baseline_assigned = list(working_assigned)
    optional_items = [
        index
        for index, destination in enumerate(working_assigned)
        if destination < 0
    ]
    assignment_rng = _make_allocation_rng(
        seed,
        MOON_ALLOCATION_ASSIGNMENT_SEED_SALT,
    )
    assignment_rng.shuffle(optional_items)

    profile_target = [0] * KINGDOM_COUNT
    target_tight = [False] * KINGDOM_COUNT
    concentration = [False] * KINGDOM_COUNT

    if difficulty == MoonAllocationDifficulty.Easy:
        # Preserve the original distributed allocation byte-for-byte.  A
        # separate row counter enforces the newly discovered native list limit
        # without changing the credited-value counters used for balancing.
        row_entries = _recompute_state(working_assigned, items)[1]
        for index in optional_items:
            allowed = items[index].destination_mask
            destination = -1
            for kingdom in range(KINGDOM_COUNT):
                if (
                    not allowed & _mask(kingdom)
                    or row_entries[kingdom] >= MAX_SHINE_LIST_ROWS
                ):
                    continue
                if (
                    destination < 0
                    or assigned_values[kingdom] < assigned_values[destination]
                    or (
                        assigned_values[kingdom] == assigned_values[destination]
                        and kingdom_tie_rank[kingdom]
                        < kingdom_tie_rank[destination]
                    )
                ):
                    destination = kingdom
            if not 0 <= destination < KINGDOM_COUNT:
                return _failure(
                    moons,
                    settings,
                    difficulty,
                    coin_policy,
                    "shine-list-rows",
                    len(optional_items),
                    sum(MAX_SHINE_LIST_ROWS - value for value in row_entries),
                    "none",
                    active=active,
                )
            working_assigned[index] = destination
            assigned_values[destination] += items[index].value
            assigned_entries[destination] += 1
            row_entries[destination] += 1
        profile_target = _recompute_state(working_assigned, items)[0]
    else:
        profile_rng = _make_allocation_rng(
            seed,
            MOON_ALLOCATION_PROFILE_SEED_SALT,
        )
        planned_assignment = None

        if uses_vanilla_excess_profile:
            shuffled_excess = [
                native_target[kingdom] - BASE_MOON_REQUIREMENT[kingdom]
                for kingdom in range(KINGDOM_COUNT)
            ]
            for _attempt in range(PROFILE_SHUFFLE_RETRIES):
                profile_rng.shuffle(shuffled_excess)
                candidate_target = [
                    BASE_MOON_REQUIREMENT[kingdom] + shuffled_excess[kingdom]
                    for kingdom in range(KINGDOM_COUNT)
                ]
                planned_assignment = _plan_exact_profile_assignment(
                    candidate_target,
                    baseline_assigned,
                    items,
                    optional_items,
                    kingdom_tie_rank,
                )
                if planned_assignment is not None:
                    profile_target = candidate_target
                    break

            # The native mapping is an exact member of the same excess
            # multiset.  Keep it as a constraint fallback for combinations such
            # as fixed Moon Coins; normal seeds use a shuffled feasible mapping.
            if planned_assignment is None:
                profile_target = list(native_target)
                planned_assignment = _plan_exact_profile_assignment(
                    profile_target,
                    baseline_assigned,
                    items,
                    optional_items,
                    kingdom_tie_rank,
                )
        else:
            # Small synthetic host fixtures do not contain the canonical
            # 819-value pool.  Preserve the old exact-total fallback for those
            # callers without weakening the runtime profile contract.
            canonical_totals = _fallback_distributed_totals(
                baseline_assigned,
                assigned_values,
                items,
                optional_items,
                native_target,
                kingdom_tie_rank,
            )
            if canonical_totals:
                profile_target = list(canonical_totals)
                planned_assignment = _plan_exact_profile_assignment(
                    profile_target,
                    baseline_assigned,
                    items,
                    optional_items,
                    kingdom_tie_rank,
                )

        if planned_assignment is None:
            return _failure(
                moons,
                settings,
                difficulty,
                coin_policy,
                "profile-target",
                item_total_value,
                sum(_recompute_state(baseline_assigned, items)[0]),
                "none",
                active=active,
            )

        if uses_vanilla_excess_profile:
            target_tight = [
                profile_target[kingdom] - BASE_MOON_REQUIREMENT[kingdom]
                <= TIGHT_EXCESS_MAX
                for kingdom in range(KINGDOM_COUNT)
            ]

            # Draw both cumulative tier sizes before changing the profile so
            # Expert's first steps are exactly the same steps Hard would use.
            hard_tighten_count = 1 + profile_rng.next_modulo(2)
            expert_extra_count = 1 + profile_rng.next_modulo(2)
            max_tighten_count = 1 + profile_rng.next_modulo(7)
            tighten_count = 0
            if difficulty == MoonAllocationDifficulty.Hard:
                tighten_count = hard_tighten_count
            elif difficulty == MoonAllocationDifficulty.Expert:
                tighten_count = hard_tighten_count + expert_extra_count
            elif difficulty == MoonAllocationDifficulty.Max:
                tighten_count = max_tighten_count

            completed_tighten_count = 0
            for _step in range(tighten_count):
                candidate_order = [
                    kingdom
                    for kingdom in range(KINGDOM_COUNT)
                    if not target_tight[kingdom]
                    and not concentration[kingdom]
                    and profile_target[kingdom]
                    - BASE_MOON_REQUIREMENT[kingdom]
                    > TIGHT_EXCESS_MAX
                ]
                profile_rng.shuffle(candidate_order)
                moved = False
                for candidate in candidate_order:
                    old_excess = (
                        profile_target[candidate]
                        - BASE_MOON_REQUIREMENT[candidate]
                    )
                    tight_start = profile_rng.next_modulo(TIGHT_EXCESS_MAX + 1)
                    sink_order = [
                        kingdom
                        for kingdom in range(KINGDOM_COUNT)
                        if kingdom != candidate and not target_tight[kingdom]
                    ]
                    # Reuse an existing outlier while its exact item/row
                    # capacity permits, then spill into the next seeded sink.
                    sink_order.sort(
                        key=lambda kingdom: (
                            0 if concentration[kingdom] else 1,
                            kingdom_tie_rank[kingdom],
                        )
                    )
                    for tight_offset in range(TIGHT_EXCESS_MAX + 1):
                        new_excess = (
                            tight_start + tight_offset
                        ) % (TIGHT_EXCESS_MAX + 1)
                        displaced = old_excess - new_excess
                        for sink in sink_order:
                            candidate_target = list(profile_target)
                            candidate_target[candidate] = (
                                BASE_MOON_REQUIREMENT[candidate] + new_excess
                            )
                            candidate_target[sink] += displaced
                            candidate_plan = _plan_exact_profile_assignment(
                                candidate_target,
                                baseline_assigned,
                                items,
                                optional_items,
                                kingdom_tie_rank,
                            )
                            if candidate_plan is None:
                                continue
                            profile_target = candidate_target
                            planned_assignment = candidate_plan
                            target_tight[candidate] = True
                            concentration[sink] = True
                            completed_tighten_count += 1
                            moved = True
                            break
                        if moved:
                            break
                    if moved:
                        break
                if not moved:
                    break

            if completed_tighten_count != tighten_count:
                return _failure(
                    moons,
                    settings,
                    difficulty,
                    coin_policy,
                    "tight-profile",
                    tighten_count,
                    completed_tighten_count,
                    "none",
                    active=active,
                )

        working_assigned = planned_assignment
        (
            assigned_values,
            assigned_entries,
            assigned_capture_value,
            assigned_ability_value,
            assigned_painting_entries,
        ) = _recompute_state(working_assigned, items)

    valid = True
    total_value = 0
    final_value = 0
    for index, item in enumerate(items):
        destination = working_assigned[index]
        total_value += item.value
        if (
            not 0 <= destination < KINGDOM_COUNT
            or (item.fixed and destination != item.home)
            or (
                item.moon_coin
                and coin_policy == MoonCoinPolicy.Standard
                and destination > 13
            )
        ):
            valid = False
        if 0 <= destination < KINGDOM_COUNT:
            final_value += item.value

    (
        assigned_values,
        assigned_entries,
        assigned_capture_value,
        assigned_ability_value,
        assigned_painting_entries,
    ) = _recompute_state(working_assigned, items)
    valid = valid and all(
        assigned_values[kingdom] >= floor_values[kingdom]
        for kingdom in range(KINGDOM_COUNT)
    )
    valid = valid and all(
        assigned_entries[kingdom] <= MAX_SHINE_LIST_ROWS
        for kingdom in range(KINGDOM_COUNT)
    )
    if _capture_enabled(settings):
        valid = valid and assigned_capture_value >= required_capture_slots(
            settings,
            world,
        )
    if _ability_enabled(settings):
        valid = valid and (
            assigned_ability_value[15] + assigned_ability_value[16]
            >= required_ability_slots(settings, world)
        )
    if cloud_painting_active:
        valid = valid and assigned_painting_entries >= CLOUD_CARRIER_MINIMUM
    valid = valid and final_value == total_value
    if not valid:
        return _failure(
            moons,
            settings,
            difficulty,
            coin_policy,
            "validation",
            total_value,
            final_value,
            "none",
            active=active,
        )

    for index, destination in enumerate(working_assigned):
        _set_assignment(moons[index], destination)

    summaries = {
        KINGDOM_BY_ID[kingdom]: KingdomAllocationSummary(
            kingdom=KINGDOM_BY_ID[kingdom],
            assigned_entries=assigned_entries[kingdom],
            assigned_value=assigned_values[kingdom],
            base_requirement=BASE_MOON_REQUIREMENT[kingdom],
            excess_value=(
                assigned_values[kingdom] - BASE_MOON_REQUIREMENT[kingdom]
            ),
            minimum_value=floor_values[kingdom],
            target_value=profile_target[kingdom],
            target_tight=target_tight[kingdom],
            concentration=concentration[kingdom],
        )
        for kingdom in range(KINGDOM_COUNT)
    }
    if _capture_enabled(settings):
        summary = summaries[KINGDOM_BY_ID[14]]
        summary.special_kind = AllocationConstraintKind.CaptureSlots
        summary.special_capacity = assigned_capture_value
        summary.special_minimum = required_capture_slots(settings, world)
    if cloud_painting_active:
        summary = summaries[KINGDOM_BY_ID[5]]
        summary.special_kind = AllocationConstraintKind.PaintingEntries
        summary.special_capacity = assigned_painting_entries
        summary.special_minimum = CLOUD_CARRIER_MINIMUM
    if _ability_enabled(settings):
        summary = summaries[KINGDOM_BY_ID[15]]
        summary.special_kind = AllocationConstraintKind.AbilitySlotsCombined
        summary.special_capacity = assigned_ability_value[15] + assigned_ability_value[16]
        summary.special_minimum = required_ability_slots(settings, world)

    return MoonAllocationResult(
        success=True,
        active=True,
        assignment_active=True,
        assignments={
            str(getattr(moon, "moon_id", index)): KINGDOM_BY_ID[working_assigned[index]]
            for index, moon in enumerate(moons)
        },
        summaries=summaries,
        concentration_kingdoms=tuple(
            KINGDOM_BY_ID[kingdom]
            for kingdom, is_concentration in enumerate(concentration)
            if is_concentration
        ),
        target_tight_kingdoms=tuple(
            KINGDOM_BY_ID[kingdom]
            for kingdom, is_tight in enumerate(target_tight)
            if is_tight
        ),
        role_layout_hash=moon_role_layout_hash(moons),
    )


# Descriptive aliases keep parity fixtures readable without duplicating code.
compute_moon_kingdom_assignment = allocate_moons
prepare_moon_allocation = allocate_moons
get_required_capture_progression_slot_count = required_capture_slots
get_required_ability_progression_slot_count = required_ability_slots
