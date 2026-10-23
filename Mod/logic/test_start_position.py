#!/usr/bin/env python3
"""Randomized Starting Position: candidate selection and the transient-access model.

Run: python logic/test_start_position.py
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

from randomizer_logic import (  # noqa: E402
    MAIN_STAGE_BY_KINGDOM,
    LogicRandomizer,
    LogicSettings,
    LogicState,
    Rng,
    load_world,
)
from start_position import (  # noqa: E402
    collect_candidates,
    load_node_anchors,
    normalize_start_position_mode,
    shuffled_candidates,
)

TEMPLATES = ROOT / "templates"

_world = None


def world():
    global _world
    if _world is None:
        _world = load_world(TEMPLATES, None)
    return _world


def make_randomizer(mode: str, seed: int = 1234, attempt: int = 0):
    settings = LogicSettings(
        seed=seed, start_position_mode=mode, start_position_attempt=attempt
    )
    rando = LogicRandomizer(world(), settings)
    rando.assign_unlocks()
    entrances = rando.build_entrance_mapping()
    lookup = {placement.source_index: placement for placement in entrances}
    return rando, lookup


class ModeNormalization(unittest.TestCase):
    def test_known_modes_pass_through(self):
        for mode in ("vanilla", "overworld", "max", "odyssey"):
            self.assertEqual(normalize_start_position_mode(mode), mode)

    def test_unknown_and_empty_fall_back_to_vanilla(self):
        self.assertEqual(normalize_start_position_mode("nonsense"), "vanilla")
        self.assertEqual(normalize_start_position_mode(""), "vanilla")
        self.assertEqual(normalize_start_position_mode(None), "vanilla")

    def test_settings_normalize_on_construction(self):
        self.assertEqual(
            LogicSettings(seed=1, start_position_mode="MAX").start_position_mode, "max"
        )
        self.assertEqual(
            LogicSettings(seed=1, start_position_mode="bogus").start_position_mode,
            "vanilla",
        )


class Anchors(unittest.TestCase):
    def test_every_anchor_has_a_full_coordinate_triple(self):
        anchors = load_node_anchors()
        self.assertTrue(anchors)
        for anchor in anchors.values():
            for value in (anchor.x, anchor.y, anchor.z):
                self.assertIsInstance(value, float)

    def test_loading_zone_kinds_are_flagged(self):
        anchors = load_node_anchors()
        kinds = {anchor.kind for anchor in anchors.values()}
        self.assertIn("entrance_side", kinds)
        self.assertIn("checkpoint_obj", kinds)
        for anchor in anchors.values():
            expected = anchor.kind in ("entrance_side", "change_stage_id")
            self.assertEqual(anchor.is_loading_zone, expected, anchor.node_id)


class CandidatePools(unittest.TestCase):
    def setUp(self):
        self.anchors = load_node_anchors()
        self.main_stages = set(MAIN_STAGE_BY_KINGDOM.values())
        self.main_stage = MAIN_STAGE_BY_KINGDOM["Cap Kingdom"]
        self.rando, self.lookup = make_randomizer("max")
        self.linked = self.rando.stages_linked_from_kingdom("Cap Kingdom", self.lookup)

    def collect(self, mode):
        return collect_candidates(
            world(), self.anchors, mode, self.main_stage, self.main_stages, self.linked
        )

    def nodes_only(self, mode):
        return [node_id for node_id in self.collect(mode) if node_id is not None]

    def test_vanilla_has_no_candidates(self):
        self.assertEqual(self.collect("vanilla"), [])

    def test_odyssey_has_no_candidates(self):
        # Inside the Odyssey is a fixed start with no logic node: the ship exits
        # to the vanilla drop-off, so there is nothing to roll and nothing to
        # prove. Mirrors the early return in StartPositionRando::rebuild.
        self.assertEqual(self.collect("odyssey"), [])

    def test_odyssey_start_leaves_the_logic_at_vanilla(self):
        rando, lookup = make_randomizer("odyssey")
        self.assertIsNone(rando.choose_start_position(lookup))
        self.assertIsNone(rando.start_node)
        self.assertEqual(rando.warnings, [])

    def test_odyssey_dropoff_is_always_a_candidate(self):
        for mode in ("overworld", "max"):
            self.assertIn(None, self.collect(mode))

    def test_overworld_pool_is_all_on_the_starting_main_stage(self):
        candidates = self.nodes_only("overworld")
        self.assertTrue(candidates)
        for node_id in candidates:
            self.assertEqual(world().nodes[node_id].stage, self.main_stage)

    def test_overworld_pool_contains_only_home_out_nodes(self):
        home_out = {
            link.home_out_node_id
            for link in world().entrances
            if link.status in ("active", "vanilla")
            and link.home_out_node_id
            and link.home_entrance_out
        }
        home_in = {
            link.home_in_node_id
            for link in world().entrances
            if link.status in ("active", "vanilla")
            and link.home_in_node_id
        }
        for main_stage in MAIN_STAGE_BY_KINGDOM.values():
            candidates = set(
                collect_candidates(
                    world(), self.anchors, "overworld", main_stage,
                    self.main_stages, set()
                )
            )
            candidates.discard(None)
            self.assertTrue(candidates, main_stage)
            self.assertTrue(candidates.issubset(home_out), main_stage)
            self.assertTrue(candidates.isdisjoint(home_in), main_stage)
            for node_id in candidates:
                self.assertEqual(self.anchors[node_id].kind, "entrance_side", node_id)
                self.assertTrue(self.anchors[node_id].change_stage_id, node_id)

    def test_max_pool_contains_the_overworld_pool(self):
        overworld = set(self.nodes_only("overworld"))
        self.assertTrue(overworld.issubset(set(self.nodes_only("max"))))

    def test_max_pool_adds_only_non_main_stages(self):
        extra = set(self.nodes_only("max")) - set(self.nodes_only("overworld"))
        self.assertTrue(extra)
        for node_id in extra:
            self.assertNotIn(world().nodes[node_id].stage, self.main_stages)

    def test_max_pool_is_bounded_by_stages_linked_from_the_starting_overworld(self):
        # The whole point of the forward check: a sub-area that merely happens to
        # sit inside the starting kingdom is NOT eligible unless this seed's
        # shuffle actually hangs it off that kingdom's own doors.
        extra = set(self.nodes_only("max")) - set(self.nodes_only("overworld"))
        for node_id in extra:
            self.assertIn(world().nodes[node_id].stage, self.linked)

        unlinked = {
            node_id
            for node_id, node in world().nodes.items()
            if node_id in self.anchors
            and node.stage
            and node.stage not in self.main_stages
            and node.stage not in self.linked
        }
        self.assertTrue(unlinked, "expected some sub-areas to be off the starting chain")
        self.assertEqual(set(), unlinked & extra)

    def test_pools_exclude_nodes_without_coordinates(self):
        for node_id in self.nodes_only("max"):
            self.assertIn(node_id, self.anchors)

    def test_pool_order_matches_the_generated_sNodes_table(self):
        # Both runtimes shuffle the same pool, so they only agree if the
        # pre-shuffle order agrees. world.nodes preserves logic_nodes.csv row
        # order, and generate_runtime_logic.py emits sNodes from those same rows
        # in the same order - this guards that chain.
        import csv

        with (TEMPLATES / "logic_nodes.csv").open(newline="", encoding="utf-8-sig") as handle:
            csv_order = [row["node_id"] for row in csv.DictReader(handle)]
        self.assertEqual(csv_order, list(world().nodes))

        # The Odyssey sentinel leads, matching StartPositionRando's array.
        candidates = self.collect("max")
        self.assertIsNone(candidates[0])
        positions = [csv_order.index(node_id) for node_id in candidates[1:]]
        self.assertEqual(positions, sorted(positions))

    def test_shuffle_is_reproducible_and_a_permutation(self):
        candidates = self.collect("max")
        first = shuffled_candidates(candidates, 1234, 0, Rng)
        again = shuffled_candidates(candidates, 1234, 0, Rng)
        self.assertEqual(first, again)
        # The pool mixes the Odyssey sentinel (None) with node ids, so sort on a
        # key rather than on the values themselves.
        key = lambda value: (value is not None, value or "")
        self.assertEqual(sorted(first, key=key), sorted(candidates, key=key))

    def test_attempt_salts_the_order(self):
        candidates = self.collect("max")
        self.assertNotEqual(
            shuffled_candidates(candidates, 1234, 0, Rng),
            shuffled_candidates(candidates, 1234, 1, Rng),
        )


class Selection(unittest.TestCase):
    def test_vanilla_selects_nothing(self):
        rando, lookup = make_randomizer("vanilla")
        self.assertIsNone(rando.choose_start_position(lookup))
        self.assertIsNone(rando.start_node)

    def test_selection_is_stable_for_a_seed_and_attempt(self):
        first, first_lookup = make_randomizer("max", seed=99, attempt=2)
        second, second_lookup = make_randomizer("max", seed=99, attempt=2)
        self.assertEqual(
            first.choose_start_position(first_lookup),
            second.choose_start_position(second_lookup),
        )

    def test_selected_node_proves_its_way_back_to_the_overworld(self):
        for mode in ("overworld", "max"):
            for seed in (1, 1234, 60606):
                rando, lookup = make_randomizer(mode, seed=seed)
                node_id = rando.choose_start_position(lookup)
                if node_id is None:
                    continue  # a seed with no usable candidate falls back to vanilla
                main_stage = MAIN_STAGE_BY_KINGDOM[rando.settings.kingdom_order[0]]
                self.assertTrue(
                    rando.can_start_at_node(node_id, lookup, main_stage),
                    f"{mode}/{seed}: {node_id} cannot reach {main_stage}",
                )

    def test_walk_back_check_is_not_vacuous(self):
        # Regression: can_start_at_node used to inherit the starting kingdom from
        # the LogicState constructor, so the required stage was already
        # accessible before the start node was seeded and EVERY candidate passed.
        # A node with no way out must be rejected.
        rando, lookup = make_randomizer("max")
        main_stage = MAIN_STAGE_BY_KINGDOM[rando.settings.kingdom_order[0]]
        anchors = load_node_anchors()
        linked = rando.stages_linked_from_kingdom(
            rando.settings.kingdom_order[0], lookup
        )
        pool = [
            node_id
            for node_id in collect_candidates(
                world(), anchors, "max", main_stage,
                set(MAIN_STAGE_BY_KINGDOM.values()), linked,
            )
            if node_id is not None
        ]
        rejected = [
            node_id
            for node_id in pool
            if not rando.can_start_at_node(node_id, lookup, main_stage)
        ]
        self.assertTrue(
            rejected,
            "no candidate was rejected - the walk-back check is passing everything",
        )

    def test_raw_stage_change_nodes_have_no_route_edges(self):
        # The seed-2248857319 start was WaterTubeExStage's `::raw` stage_change
        # node. Raw nodes carry coordinates but no authored route edges, so
        # standing on one connects to nothing inside its own stage - the only way
        # out is whatever seed_start_node opens up. Documents why the walk-back
        # check has to do the work rather than the graph.
        raw = [
            node_id
            for node_id, node in world().nodes.items()
            if node.stage == "WaterTubeExStage" and node.side == "raw"
        ]
        self.assertTrue(raw)
        touched = {
            edge.from_node_id for edge in world().edges
        } | {edge.to_node_id for edge in world().edges}
        for node_id in raw:
            self.assertNotIn(node_id, touched)

    def test_overworld_mode_never_starts_in_a_sub_area(self):
        for seed in (1, 1234, 60606):
            rando, lookup = make_randomizer("overworld", seed=seed)
            node_id = rando.choose_start_position(lookup)
            if node_id is None:
                continue
            main_stage = MAIN_STAGE_BY_KINGDOM[rando.settings.kingdom_order[0]]
            self.assertEqual(world().nodes[node_id].stage, main_stage)


class StartNodeAccess(unittest.TestCase):
    """The start node is permanently reachable - the map's Return to Start warp
    can always put the player back on it, like a checkpoint flag."""

    def setUp(self):
        self.rando, self.lookup = make_randomizer("max", seed=1234)
        self.node_id = self.rando.choose_start_position(self.lookup)
        if self.node_id is None:
            self.skipTest("seed 1234 rolled the Odyssey drop-off")

    def test_start_node_stays_accessible(self):
        state = LogicState(self.rando.settings, self.rando.world)
        self.rando.apply_start_position(state, self.lookup)
        self.assertIn(self.node_id, state.accessible_nodes)

    def test_start_stage_becomes_accessible(self):
        state = LogicState(self.rando.settings, self.rando.world)
        self.rando.apply_start_position(state, self.lookup)
        self.assertIn(self.rando.world.nodes[self.node_id].stage, state.accessible_stages)

    def test_both_sub_area_doors_are_available_as_exits(self):
        state = LogicState(self.rando.settings, self.rando.world)
        self.rando.apply_start_position(state, self.lookup)
        stage = self.rando.world.nodes[self.node_id].stage
        for link in self.rando.world.entrances:
            if link.sub_stage == stage:
                self.assertIn((link.link_index, "near"), state.accessible_sub_entries)
                self.assertIn((link.link_index, "far"), state.accessible_sub_entries)

    def test_a_sub_area_start_does_not_hand_over_its_kingdom(self):
        # Standing inside a sub-area is not the same as reaching the kingdom it
        # lives in: its exits are shuffled. Granting that would over-estimate
        # reachability and let the fill build unbeatable seeds.
        state = LogicState(self.rando.settings, self.rando.world)
        node = self.rando.world.nodes[self.node_id]
        self.rando.seed_start_node(state, self.node_id)

        room = self.rando.world.rooms.get(node.stage)
        if not room or node.stage in MAIN_STAGE_BY_KINGDOM.values():
            self.skipTest("chosen start is not inside a sub-area")
        host_main_stage = MAIN_STAGE_BY_KINGDOM.get(room.kingdom)
        if host_main_stage:
            self.assertNotIn(host_main_stage, state.accessible_stages)


class Additivity(unittest.TestCase):
    """The model is purely additive, so it can never shrink reachability."""

    def test_seeding_only_adds(self):
        rando, lookup = make_randomizer("max", seed=1234)
        node_id = rando.choose_start_position(lookup)
        if node_id is None:
            self.skipTest("seed 1234 rolled the Odyssey drop-off")

        plain = LogicState(rando.settings, rando.world)
        seeded = LogicState(rando.settings, rando.world)
        rando.apply_start_position(seeded, lookup)

        self.assertTrue(plain.accessible_nodes.issubset(seeded.accessible_nodes))
        self.assertTrue(plain.accessible_stages.issubset(seeded.accessible_stages))
        self.assertTrue(
            plain.accessible_sub_entries.issubset(seeded.accessible_sub_entries)
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
