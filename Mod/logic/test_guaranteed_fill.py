from __future__ import annotations

import sys
import unittest
from collections import Counter
from dataclasses import replace
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import randomizer_logic as rl


class GuaranteedFillDataTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.world = rl.load_world(Path(__file__).parent / "templates", None)

    def test_mushroom_rematch_multis_are_anchored_and_connected(self) -> None:
        rematches = [
            moon
            for moon in self.world.moons.values()
            if moon.stage.startswith("Revenge") and moon.is_multi
        ]
        self.assertEqual(6, len(rematches))
        self.assertTrue(all(moon.from_node_id in self.world.nodes for moon in rematches))

        destinations = {edge.to_node_id for edge in self.world.edges}
        self.assertTrue(all(f"{moon.stage}::start" in destinations for moon in rematches))
        fixed_edges = [
            edge for edge in self.world.edges
            if edge.edge_id.startswith("fixed_mushroom_")
        ]
        self.assertEqual(6, len(fixed_edges))
        self.assertTrue(
            all(
                self.world.nodes[edge.from_node_id].node_type == "fixed_transition"
                for edge in fixed_edges
            )
        )
        settings = rl.LogicSettings(
            seed=1,
            starting_kingdoms=["Mushroom Kingdom"],
            entrance_rando=False,
        )
        randomizer = rl.LogicRandomizer(self.world, settings)
        for edge in fixed_edges:
            state = rl.LogicState(settings, self.world)
            source_stage = self.world.nodes[edge.from_node_id].stage
            state.accessible_stages.add(source_stage)
            state.accessible_nodes.add(edge.from_node_id)
            self.assertIn(edge.to_node_id, randomizer.reachable_nodes(state))

    def test_only_first_reward_copy_is_mandatory(self) -> None:
        randomizer = rl.LogicRandomizer(self.world, rl.LogicSettings(seed=7))
        randomizer.assign_unlocks()

        reward_counts: Counter[str] = Counter()
        reward_moons: set[str] = set()
        for moon in self.world.moons.values():
            for token in [*moon.capture_unlocks, *moon.ability_unlocks]:
                reward_counts[token] += 1
                reward_moons.add(moon.moon_id)
        for token in reward_counts:
            self.assertTrue(
                any(
                    moon.moon_id in randomizer.progression_item_ids
                    and token in [*moon.capture_unlocks, *moon.ability_unlocks]
                    for moon in self.world.moons.values()
                )
            )
        self.assertLessEqual(len(randomizer.progression_item_ids), len(reward_counts))
        self.assertLess(len(randomizer.progression_item_ids), len(reward_moons))
        self.assertTrue(any(count > 1 for count in reward_counts.values()))

    def test_darker_side_multi_moon_grants_three_abilities(self) -> None:
        randomizer = rl.LogicRandomizer(self.world, rl.LogicSettings(seed=7))
        randomizer.assign_unlocks()

        moon = self.world.moons["Special2WorldHomeStage:55:1061"]
        self.assertTrue(moon.is_multi)
        self.assertEqual(3, len(moon.ability_unlocks))

    def test_no_logic_does_not_open_orphan_topology(self) -> None:
        settings = rl.LogicSettings(seed=1, no_logic=True, entrance_rando=False)
        state = rl.LogicState(settings, self.world)
        randomizer = rl.LogicRandomizer(self.world, settings)
        moon = next(
            moon for moon in self.world.moons.values()
            if moon.stage == "RevengeForestBossStage" and moon.is_multi
        )
        reachable = randomizer.reachable_nodes(state)
        self.assertFalse(randomizer.has_stage_access_for_location(moon, state, reachable))

    def test_single_story_location_does_not_advance_scenario(self) -> None:
        source = next(moon for moon in self.world.moons.values() if moon.is_story and not moon.is_multi)
        settings = rl.LogicSettings(seed=1)
        state = rl.LogicState(settings, self.world)
        before = state.kingdom_scenarios.get(source.kingdom, 1)
        moon = replace(source, scenario_move_to=before + 2)
        state.collect_location(moon, None, self.world)
        self.assertEqual(before, state.kingdom_scenarios.get(moon.kingdom, 1))

    def test_option_scenario_parser_preserves_alternative_boundaries(self) -> None:
        # Semicolons still separate requirement alternatives and an alternative
        # with no group of its own stays unrestricted (0). Each group's highest
        # checked scenario is open-ended - see extend_top_scenario - so the raw
        # bits are the sticky-topped ones, not the literal ticks.
        masks = rl.option_scenario_masks("1;2,3", 3)
        self.assertEqual(
            (
                rl.extend_top_scenario(1 << 1),
                rl.extend_top_scenario((1 << 2) | (1 << 3)),
                0,
            ),
            masks,
        )
        # scenario_mask drives node/moon presence, which is separately floored
        # and is deliberately NOT sticky-topped here.
        self.assertEqual((1 << 2) | (1 << 3), rl.scenario_mask("2,3"))

    def test_option_scenario_top_is_open_ended_but_gaps_survive(self) -> None:
        # "available at N implies available after N" applies to the highest
        # ticked scenario only; a gap below it is deliberate authoring (Metro
        # shuts routes during its scenario-3 festival) and must be preserved.
        mask = rl.option_scenario_masks("1,3,4,5", 1)[0]
        self.assertFalse(rl.scenario_mask_contains(mask, 2))
        for scenario in (1, 3, 4, 5, 6, 8):
            self.assertTrue(rl.scenario_mask_contains(mask, scenario), scenario)
        # An empty column stays fully unrestricted.
        self.assertEqual(0, rl.option_scenario_masks("", 1)[0])

    def test_buffer_expansion_only_requests_active_scenario_alternative(self) -> None:
        settings = rl.LogicSettings(seed=1)
        state = rl.LogicState(settings, self.world)
        state.tokens.discard("Wanwan")
        state.tokens.discard("Frog")
        requirement = rl.Requirement(
            "custom",
            [frozenset({"Wanwan"}), frozenset({"Frog"})],
        )
        masks = rl.option_scenario_masks("1;2", 2)

        # Scenario 1: only the first alternative is in scope, so the buffer must
        # not ask for Frog.
        self.assertEqual(
            {"Wanwan"},
            rl.missing_requirement_tokens_for_scenario(
                requirement, masks, 1, state, self.world
            ),
        )
        # Scenario 2: the first alternative's top is open-ended, so it is still
        # usable here alongside the second. Both are legitimately in scope - the
        # point of this test is that scope follows the scenario masks, not that
        # exactly one alternative ever qualifies.
        self.assertEqual(
            {"Wanwan", "Frog"},
            rl.missing_requirement_tokens_for_scenario(
                requirement, masks, 2, state, self.world
            ),
        )

    def test_cascade_start_cannot_skip_chain_chomp_gate(self) -> None:
        target = "WaterfallWorldHomeStage::checkpoint::obj1724"
        settings = rl.LogicSettings(seed=1, entrance_rando=False)
        state = rl.LogicState(settings, self.world)
        state.kingdom_scenarios["Cascade Kingdom"] = 1
        state.tokens.discard("Wanwan")
        randomizer = rl.LogicRandomizer(self.world, settings)

        self.assertNotIn(target, randomizer.reachable_nodes(state))

        state.kingdom_scenarios["Cascade Kingdom"] = 2
        self.assertIn(target, randomizer.reachable_nodes(state))

        direct = next(edge for edge in self.world.edges if edge.edge_id == "edge_user_0187")
        self.assertFalse(rl.any_option_scenario_allowed(direct.option_scenario_masks, 1))
        self.assertTrue(rl.any_option_scenario_allowed(direct.option_scenario_masks, 2))

    def test_final_objective_requires_wedding_hall_checkpoint(self) -> None:
        settings = rl.LogicSettings(
            seed=1,
            starting_kingdoms=["Moon Kingdom"],
            entrance_rando=False,
        )
        state = rl.LogicState(settings, self.world)
        randomizer = rl.LogicRandomizer(self.world, settings)

        self.assertIn(rl.FINAL_OBJECTIVE_NODE_ID, self.world.nodes)
        self.assertNotIn(
            rl.FINAL_OBJECTIVE_NODE_ID,
            randomizer.reachable_nodes(state),
        )
        self.assertFalse(randomizer.is_final_objective_reachable(state, True))

        state.tokens.update(self.world.tokens)
        self.assertIn(
            rl.FINAL_OBJECTIVE_NODE_ID,
            randomizer.reachable_nodes(state),
        )
        self.assertTrue(randomizer.is_final_objective_reachable(state, True))

    def test_required_picker_uses_one_multi_for_three_moons(self) -> None:
        settings = rl.LogicSettings(seed=11, entrance_rando=False)
        settings.moon_requirements["Sand Kingdom"] = 3
        state = rl.LogicState(settings, self.world)
        randomizer = rl.LogicRandomizer(self.world, settings)
        items = [
            moon for moon in self.world.moons.values()
            if moon.kingdom == "Sand Kingdom"
        ]
        multi = next(moon for moon in items if moon.is_multi)
        singles = [moon for moon in items if not moon.is_multi][:3]

        picked = randomizer.pick_required_moon_for_kingdom(
            state,
            "Sand Kingdom",
            {multi.moon_id, *(moon.moon_id for moon in singles)},
            [multi, *singles],
        )

        self.assertIsNotNone(picked)
        self.assertTrue(picked.is_multi)

    def test_frontier_sphere_only_advances_for_new_locations(self) -> None:
        settings = rl.LogicSettings(seed=12, entrance_rando=False)
        state = rl.LogicState(settings, self.world)
        randomizer = rl.LogicRandomizer(self.world, settings)
        entrances = randomizer.build_entrance_mapping()
        lookup = {placement.source_index: placement for placement in entrances}
        unfilled = {
            moon.moon_id for moon in self.world.location_pool(settings)
        }
        frontier: list[int | bool] = [0, False]

        first = randomizer.update_frontier_spheres(
            state, unfilled, lookup, frontier
        )
        self.assertTrue(first)
        self.assertEqual(0, frontier[0])
        first_spheres = dict(randomizer.location_spheres)

        randomizer.update_frontier_spheres(state, unfilled, lookup, frontier)
        self.assertEqual(0, frontier[0])
        self.assertEqual(first_spheres, randomizer.location_spheres)

        randomizer.unlock_kingdom(state, "Sand Kingdom")
        randomizer.update_frontier_spheres(state, unfilled, lookup, frontier)
        self.assertEqual(1, frontier[0])
        self.assertGreater(len(randomizer.location_spheres), len(first_spheres))

    def test_even_picker_avoids_existing_native_kingdom_clump(self) -> None:
        settings = rl.LogicSettings(seed=13, entrance_rando=False)
        randomizer = rl.LogicRandomizer(self.world, settings)
        item = next(
            moon for moon in self.world.moons.values()
            if moon.kingdom == "Sand Kingdom" and not moon.is_multi
        )
        cap_locations = [
            moon for moon in self.world.moons.values()
            if moon.kingdom == "Cap Kingdom" and not moon.is_multi
        ][:3]
        cascade = next(
            moon for moon in self.world.moons.values()
            if moon.kingdom == "Cascade Kingdom" and not moon.is_multi
        )
        sand_items = [
            moon for moon in self.world.moons.values()
            if moon.kingdom == "Sand Kingdom" and not moon.is_multi
        ][:2]
        placements = [
            rl.Placement(cap_locations[index].moon_id, sand_items[index].moon_id)
            for index in range(2)
        ]

        picked = randomizer.pick_even_location(
            item, [cap_locations[2], cascade], placements
        )

        self.assertIsNotNone(picked)
        self.assertEqual("Cascade Kingdom", picked.kingdom)

    def test_bowser_route_gate_requires_story_completion(self) -> None:
        settings = rl.LogicSettings(seed=14, entrance_rando=False)
        state = rl.LogicState(settings, self.world)
        randomizer = rl.LogicRandomizer(self.world, settings)
        kingdom = "Bowser's Kingdom"
        randomizer.unlock_kingdom(state, kingdom)
        state.moon_counts[kingdom] = settings.moon_requirements[kingdom]

        self.assertFalse(randomizer.is_kingdom_requirement_met(state, kingdom))
        state.story_counts[kingdom] = self.world.story_totals[kingdom]
        self.assertTrue(randomizer.is_kingdom_requirement_met(state, kingdom))


if __name__ == "__main__":
    unittest.main()
