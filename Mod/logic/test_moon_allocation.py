"""Targeted host-side parity coverage for Packet 7 Moon Allocation.

Run directly with ``python logic/test_moon_allocation.py``.  These tests use
the generated logic templates as the stable moon-order fixture; they do not
build the game or invoke the repository's integration test harness.
"""

from __future__ import annotations

import copy
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

from moon_allocation import (  # noqa: E402
    AllocationConstraintKind,
    BASE_MOON_REQUIREMENT,
    KINGDOM_BY_ID,
    KINGDOM_COUNT,
    allocate_moons,
    compute_reward_multi_values,
    is_moon_coin,
    MAX_SHINE_LIST_ROWS,
    required_ability_slots,
    required_capture_slots,
    TIGHT_EXCESS_MAX,
    VANILLA_EXCESS,
    _Item,
    _recompute_state,
)
from randomizer_logic import LogicSettings, load_world  # noqa: E402


TEMPLATES = ROOT / "templates"
_TEMPLATE_WORLD = None


def fresh_world():
    global _TEMPLATE_WORLD
    if _TEMPLATE_WORLD is None:
        _TEMPLATE_WORLD = load_world(TEMPLATES, None)
    return copy.deepcopy(_TEMPLATE_WORLD)


def active_settings(seed: int, **overrides):
    values = {
        "moon_reallocation_enabled": True,
        "randomize_moon_allocation": True,
        "moon_allocation_difficulty": "vanilla",
        "moon_coin_policy": "vanilla",
    }
    values.update(overrides)
    return LogicSettings(seed=seed, **values)


def run_allocation(seed: int, **overrides):
    world = fresh_world()
    settings = active_settings(seed, **overrides)
    compute_reward_multi_values(
        world, seed, settings.randomize_triple_moons
    )
    result = allocate_moons(world, settings, seed=seed)
    return world, settings, result


def allocation_values(result) -> tuple[int, ...]:
    return tuple(
        result.summaries[kingdom].assigned_value
        for kingdom in KINGDOM_BY_ID.values()
    )


# These are compact golden fixtures for the C++/Python contract.  The role
# layout hash covers every item in stable table order; totals, profile roles, and
# special capacities make a mismatch explainable without storing 775 rows here.
PARITY_FIXTURES = (
    {
        "name": "allocation-off-randomized-triple",
        "seed": 0x13579BDF,
        "settings": {
            "moon_reallocation_enabled": False,
            "randomize_moon_allocation": False,
            "randomize_triple_moons": True,
        },
        "role_layout_hash": 533757119,
        "triple_count": 22,
        "total_value": 819,
        "totals": None,
        "concentration_kingdoms": (),
        "target_tight_kingdoms": (),
        "special": {},
    },
    {
        "name": "vanilla-all-progression-locks",
        "seed": 0x2468ACE0,
        "settings": {
            "moon_reallocation_enabled": True,
            "randomize_moon_allocation": True,
            "cloud_kingdom_moons_unlock_paintings": True,
        },
        "role_layout_hash": 3676308779,
        "triple_count": 22,
        "total_value": 819,
        "totals": (55, 31, 81, 19, 17, 56, 48, 45, 74, 19, 49, 80, 44, 54, 63, 37, 47),
        "concentration_kingdoms": (),
        "target_tight_kingdoms": (
            "Wooded Kingdom",
            "Lake Kingdom",
            "Snow Kingdom",
        ),
        "special": {
            "Cloud Kingdom": (56, 10, "painting-entries"),
            "Mushroom Kingdom": (63, 50, "capture-slots"),
            "Dark Side": (84, 19, "ability-slots-combined"),
        },
    },
    {
        "name": "hard-standard-plentiful-equipment",
        "seed": 0x0BADF00D,
        "settings": {
            "moon_reallocation_enabled": True,
            "randomize_moon_allocation": True,
            "moon_allocation_difficulty": "hard",
            "moon_coin_policy": "standard",
            "starting_equipment": "plentiful",
        },
        "role_layout_hash": 2025656831,
        "triple_count": 22,
        "total_value": 819,
        "totals": (77, 70, 25, 34, 91, 26, 66, 51, 13, 19, 65, 58, 33, 54, 64, 37, 36),
        "concentration_kingdoms": ("Lake Kingdom",),
        "target_tight_kingdoms": (
            "Sand Kingdom",
            "Wooded Kingdom",
            "Seaside Kingdom",
            "Snow Kingdom",
        ),
        "special": {
            "Mushroom Kingdom": (64, 48, "capture-slots"),
            "Dark Side": (73, 18, "ability-slots-combined"),
        },
    },
)


class MoonAllocationTests(unittest.TestCase):
    def test_defaults_leave_identity_assignment(self):
        world = fresh_world()
        settings = LogicSettings(seed=7)
        compute_reward_multi_values(world, settings.seed, False)
        result = allocate_moons(world, settings, seed=settings.seed)

        self.assertTrue(result.success)
        self.assertFalse(result.active)
        self.assertFalse(result.assignment_active)
        for moon in world.moons.values():
            self.assertIsNone(moon.assigned_kingdom)
            self.assertEqual(moon.credit_kingdom, moon.kingdom)

    def test_same_seed_is_identical_and_different_seed_changes_layout(self):
        first_world, _, first = run_allocation(0x10203040)
        second_world, _, second = run_allocation(0x10203040)
        _, _, different = run_allocation(0x10203041)

        self.assertTrue(first.success)
        self.assertEqual(first.assignments, second.assignments)
        self.assertEqual(first.role_layout_hash, second.role_layout_hash)
        self.assertNotEqual(first.assignments, different.assignments)
        self.assertTrue(all(moon.assigned_kingdom for moon in first_world.moons.values()))
        self.assertTrue(all(moon.assigned_kingdom for moon in second_world.moons.values()))

    def test_rerun_clears_previous_assignments(self):
        world = fresh_world()
        first_settings = active_settings(17)
        compute_reward_multi_values(world, 17, False)
        first = allocate_moons(world, first_settings, seed=17)
        first_assignments = dict(first.assignments)

        second_settings = active_settings(18)
        compute_reward_multi_values(world, 18, False)
        second = allocate_moons(world, second_settings, seed=18)

        self.assertTrue(first.success)
        self.assertTrue(second.success)
        self.assertNotEqual(first_assignments, second.assignments)
        self.assertEqual(
            set(second.assignments),
            {moon.moon_id for moon in world.moons.values()},
        )
        self.assertTrue(all(moon.assigned_kingdom for moon in world.moons.values()))

    def test_value_is_conserved_and_all_ordinary_floors_are_met(self):
        world, _, result = run_allocation(12345)

        self.assertTrue(result.success)
        total = sum(moon.moon_value for moon in world.moons.values())
        self.assertEqual(total, sum(summary.assigned_value for summary in result.summaries.values()))
        for kingdom, summary in result.summaries.items():
            self.assertGreaterEqual(
                summary.assigned_value,
                summary.minimum_value,
                kingdom,
            )

    def test_cloud_gets_ten_painting_credits_when_required(self):
        _, _, result = run_allocation(
            12345,
            cloud_kingdom_moons_unlock_paintings=True,
        )

        self.assertTrue(result.success)
        summary = result.summaries["Cloud Kingdom"]
        self.assertEqual(summary.special_kind, AllocationConstraintKind.PaintingEntries)
        self.assertGreaterEqual(summary.special_capacity, 10)

    def test_cloud_triple_is_worth_three_painting_credits(self):
        items = [
            _Item(5, 1, 1 << 5, True, False, True, True, True, False),
            _Item(5, 3, 1 << 5, True, False, True, True, True, False),
        ]
        (
            _values,
            _entries,
            _capture_value,
            _ability_values,
            painting_credits,
        ) = _recompute_state([5, 5], items)

        self.assertEqual(painting_credits, 4)

    def test_mushroom_capture_and_dark_ability_capacity(self):
        _, settings, result = run_allocation(12345)

        self.assertTrue(result.success)
        mushroom = result.summaries["Mushroom Kingdom"]
        dark = result.summaries["Dark Side"]
        self.assertEqual(mushroom.special_kind, AllocationConstraintKind.CaptureSlots)
        self.assertEqual(dark.special_kind, AllocationConstraintKind.AbilitySlotsCombined)
        self.assertGreaterEqual(
            mushroom.special_capacity,
            required_capture_slots(settings),
        )
        self.assertGreaterEqual(
            dark.special_capacity,
            required_ability_slots(settings),
        )

    def test_starting_equipment_reduces_only_matching_requirements(self):
        expected = {
            "scarce": (52, 20),
            "normal": (50, 19),
            "plentiful": (48, 18),
        }
        for equipment, requirements in expected.items():
            settings = active_settings(1, starting_equipment=equipment)
            self.assertEqual(
                required_capture_slots(settings), requirements[0]
            )
            self.assertEqual(
                required_ability_slots(settings), requirements[1]
            )

    def test_vanilla_coins_stay_home(self):
        world, settings, result = run_allocation(12345)

        self.assertTrue(result.success)
        coins = [moon for moon in world.moons.values() if is_moon_coin(moon, settings)]
        self.assertTrue(coins)
        self.assertTrue(all(moon.credit_kingdom == moon.kingdom for moon in coins))

    def test_standard_coins_avoid_special_destinations(self):
        world, settings, result = run_allocation(
            12345,
            moon_coin_policy="standard",
        )

        self.assertTrue(result.success)
        coins = [moon for moon in world.moons.values() if is_moon_coin(moon, settings)]
        self.assertTrue(coins)
        for moon in coins:
            self.assertNotIn(
                moon.credit_kingdom,
                {"Mushroom Kingdom", "Dark Side", "Darker Side"},
            )

    def test_ability_rush_allows_coin_destinations_everywhere(self):
        world, settings, result = run_allocation(
            2,
            capture_rando=False,
            ability_rando=False,
            moon_coin_policy="ability-rush",
        )

        self.assertTrue(result.success)
        coins = [moon for moon in world.moons.values() if is_moon_coin(moon, settings)]
        self.assertTrue(coins)
        self.assertTrue(
            any(
                moon.credit_kingdom in {"Mushroom Kingdom", "Dark Side", "Darker Side"}
                for moon in coins
            )
        )

    def test_easy_is_more_even_than_max_for_representative_seeds(self):
        for seed in (1, 2, 3):
            _, _, easy = run_allocation(seed, moon_allocation_difficulty="easy")
            _, _, maximum = run_allocation(seed, moon_allocation_difficulty="max")
            self.assertTrue(easy.success)
            self.assertTrue(maximum.success)
            easy_values = allocation_values(easy)
            max_values = allocation_values(maximum)
            self.assertLessEqual(
                max(easy_values) - min(easy_values),
                max(max_values) - min(max_values),
            )

    def test_vanilla_randomizes_which_kingdom_gets_each_excess_bucket(self):
        observed = set()
        for seed in (1, 2, 3):
            _, _, vanilla = run_allocation(seed, moon_allocation_difficulty="vanilla")
            self.assertTrue(vanilla.success)
            excess = tuple(
                value - requirement
                for value, requirement in zip(
                    allocation_values(vanilla), BASE_MOON_REQUIREMENT
                )
            )
            self.assertEqual(sorted(excess), sorted(VANILLA_EXCESS))
            observed.add(excess)
        self.assertEqual(len(observed), 3)

    def test_vanilla_preserves_and_randomizes_vanilla_excess_multiset(self):
        _, _, result = run_allocation(
            0x2468ACE0,
            moon_allocation_difficulty="vanilla",
            cloud_kingdom_moons_unlock_paintings=True,
        )

        self.assertTrue(result.success)
        new_totals = allocation_values(result)
        new_excess = tuple(
            value - requirement
            for value, requirement in zip(new_totals, BASE_MOON_REQUIREMENT)
        )
        self.assertEqual(sorted(new_excess), sorted(VANILLA_EXCESS))
        self.assertNotEqual(new_excess, VANILLA_EXCESS)
        self.assertEqual(
            len(result.target_tight_kingdoms),
            sum(excess <= TIGHT_EXCESS_MAX for excess in VANILLA_EXCESS),
        )

    def test_vanilla_ruined_rows_distinguish_entries_from_moon_value(self):
        world, _, result = run_allocation(
            27, moon_allocation_difficulty="vanilla"
        )

        self.assertTrue(result.success)
        ruined = result.summaries["Ruined Kingdom"]
        self.assertEqual(ruined.assigned_entries, 4)
        self.assertEqual(ruined.assigned_value, 6)
        self.assertEqual(ruined.target_value, 6)
        self.assertEqual(ruined.excess_value, 3)
        self.assertEqual(
            sorted(
                moon.moon_value
                for moon in world.moons.values()
                if moon.credit_kingdom == "Ruined Kingdom"
            ),
            [1, 1, 1, 3],
        )

    def test_hard_and_expert_progressively_tighten_excess_kingdoms(self):
        for seed in (1, 2, 3, 12345):
            _, _, vanilla = run_allocation(
                seed, moon_allocation_difficulty="vanilla"
            )
            _, _, hard = run_allocation(
                seed, moon_allocation_difficulty="hard"
            )
            _, _, expert = run_allocation(
                seed, moon_allocation_difficulty="expert"
            )
            self.assertTrue(vanilla.success)
            self.assertTrue(hard.success)
            self.assertTrue(expert.success)
            vanilla_tight = set(vanilla.target_tight_kingdoms)
            hard_tight = set(hard.target_tight_kingdoms)
            expert_tight = set(expert.target_tight_kingdoms)
            self.assertIn(len(hard_tight) - len(vanilla_tight), (1, 2))
            self.assertIn(
                len(expert_tight) - len(hard_tight),
                (1, 2),
            )
            self.assertLessEqual(vanilla_tight, hard_tight)
            self.assertLessEqual(hard_tight, expert_tight)
            for result in (hard, expert):
                self.assertTrue(result.concentration_kingdoms)
                for kingdom in result.target_tight_kingdoms:
                    self.assertLessEqual(
                        result.summaries[kingdom].excess_value,
                        TIGHT_EXCESS_MAX,
                    )
                    self.assertGreaterEqual(result.summaries[kingdom].excess_value, 0)

    def test_max_randomly_tightens_between_one_and_seven_kingdoms(self):
        observed = set()
        for seed in (1, 2, 3, 0x0BADF00D, 0x2468ACE0):
            _, _, vanilla = run_allocation(
                seed, moon_allocation_difficulty="vanilla"
            )
            _, _, result = run_allocation(
                seed, moon_allocation_difficulty="max"
            )
            self.assertTrue(vanilla.success)
            self.assertTrue(result.success)
            tight_count = (
                len(result.target_tight_kingdoms)
                - len(vanilla.target_tight_kingdoms)
            )
            self.assertGreaterEqual(tight_count, 1)
            self.assertLessEqual(tight_count, 7)
            observed.add(tight_count)
        self.assertIn(1, observed)
        self.assertIn(7, observed)

    def test_every_profile_fits_the_native_shine_list(self):
        for seed in (1, 2, 3):
            for difficulty in ("easy", "vanilla", "hard", "expert", "max"):
                _, _, result = run_allocation(
                    seed,
                    moon_allocation_difficulty=difficulty,
                    moon_coin_policy="standard",
                )
                self.assertTrue(result.success)
                self.assertTrue(
                    all(
                        summary.assigned_entries <= MAX_SHINE_LIST_ROWS
                        for summary in result.summaries.values()
                    )
                )

    def test_impossible_synthetic_pool_returns_capacity_failure(self):
        moon = SimpleNamespace(
            moon_id="fixed",
            kingdom="Cap Kingdom",
            flags="",
            uid=0,
            obj_id=-1,
            reward_is_multi=False,
            assigned_kingdom=None,
        )
        settings = active_settings(
            1,
            ability_rando=False,
            required_capture_slots=1,
        )
        result = allocate_moons(
            [moon],
            settings,
            seed=1,
            minimum_values=(0,) * KINGDOM_COUNT,
        )

        self.assertFalse(result.success)
        self.assertIsNotNone(result.failure)
        self.assertEqual(result.failure.constraint, "capture-slots")

    def test_triple_valued_items_remain_indivisible(self):
        moon = SimpleNamespace(
            moon_id="triple",
            kingdom="Cap Kingdom",
            flags="",
            uid=1,
            obj_id=1,
            reward_is_multi=True,
            assigned_kingdom=None,
        )
        settings = active_settings(
            1,
            capture_rando=False,
            ability_rando=False,
        )
        result = allocate_moons(
            [moon],
            settings,
            seed=1,
            minimum_values=(0,) * KINGDOM_COUNT,
        )

        self.assertTrue(result.success)
        self.assertEqual(sum(summary.assigned_value for summary in result.summaries.values()), 3)
        self.assertEqual(result.assignments[moon.moon_id], moon.assigned_kingdom)

    def test_cpp_python_parity_fixtures(self):
        for fixture in PARITY_FIXTURES:
            with self.subTest(fixture=fixture["name"]):
                world = fresh_world()
                settings = LogicSettings(seed=fixture["seed"], **fixture["settings"])
                compute_reward_multi_values(
                    world, fixture["seed"], settings.randomize_triple_moons
                )
                result = allocate_moons(world, settings, seed=fixture["seed"])

                self.assertTrue(result.success)
                self.assertEqual(result.role_layout_hash, fixture["role_layout_hash"])
                self.assertEqual(
                    sum(moon.is_reward_multi for moon in world.moons.values()),
                    fixture["triple_count"],
                )
                self.assertEqual(
                    sum(moon.moon_value for moon in world.moons.values()),
                    fixture["total_value"],
                )
                self.assertEqual(
                    result.concentration_kingdoms,
                    fixture["concentration_kingdoms"],
                )
                self.assertEqual(
                    result.target_tight_kingdoms,
                    fixture["target_tight_kingdoms"],
                )
                if fixture["totals"] is not None:
                    self.assertEqual(allocation_values(result), fixture["totals"])
                actual_special = {
                    kingdom: (
                        summary.special_capacity,
                        summary.special_minimum,
                        str(summary.special_kind),
                    )
                    for kingdom, summary in result.summaries.items()
                    if summary.special_kind != AllocationConstraintKind.None_
                }
                self.assertEqual(actual_special, fixture["special"])


if __name__ == "__main__":
    unittest.main()
