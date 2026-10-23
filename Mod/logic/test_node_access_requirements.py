"""Node-access requirements: `@<node_id>` atoms inside requirement expressions.

Covers the parse split (tokens vs nodes), the reachability fixed point they force
(an edge can open only after another node is reached), and the moon gate.
"""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import randomizer_logic as rl

KINGDOM = "Cap Kingdom"
STAGE = "CapWorldHomeStage"


def node_row(node_id: str, node_type: str = "custom_place") -> dict[str, str]:
    return {
        "node_id": node_id,
        "kingdom": KINGDOM,
        "stage": STAGE,
        "stage_display_name": "Cap Kingdom",
        "area_scope": "overworld",
        "node_type": node_type,
        "source": "editor",
        "side": "",
        "scenario_indices": "",
        "status": "",
    }


def edge_row(edge_id: str, from_node: str, to_node: str, expr: str) -> dict[str, str]:
    return {
        "edge_id": edge_id,
        "kingdom": KINGDOM,
        "area_scope": "overworld",
        "from_node_id": from_node,
        "to_node_id": to_node,
        "requirement_expr": expr,
        "difficulty": "casual",
        "option_scenarios": "",
        "bidirectional": "false",
        "source": "editor",
        "status": "",
    }


def moon_row(moon_id: str, from_node: str, expr: str) -> dict[str, str]:
    return {
        "moon_id": moon_id,
        "kingdom": KINGDOM,
        "stage": STAGE,
        "stage_display_name": "Cap Kingdom",
        "area_scope": "overworld",
        "moon_name": moon_id,
        "obj_id": "1",
        "uid": "9001",
        "flags": "ShineFlag_None",
        "from_node_id": from_node,
        "state_requirement_expr": "",
        "requirement_expr": expr,
        "difficulty": "casual",
        "option_scenarios": "",
        "shine_info_available_scenarios": "",
        "scenario_move_to": "",
        "effort_level": "unrated",
        "status": "",
    }


def build_world(edges: list[dict[str, str]], moons: list[dict[str, str]]) -> rl.LogicWorld:
    return rl.build_world_from_rows({
        "logic_tokens": [
            {
                "token": "Frog",
                "token_type": "capture",
                "display_name": "Frog",
                "category": "capture",
                "prerequisite": "",
                "always_unlocked": "false",
                "requires_any_cap_throw": "false",
                "can_start_with_seed": "false",
            },
        ],
        "rooms": [{
            "stage": STAGE,
            "kingdom": KINGDOM,
            "stage_display_name": "Cap Kingdom",
            "area_scope": "overworld",
        }],
        "logic_nodes": [
            node_row(f"{STAGE}::start", "stage_start"),
            node_row(f"{STAGE}::switch"),
            node_row(f"{STAGE}::gated"),
            node_row(f"{STAGE}::deep"),
        ],
        "route_edges": edges,
        "moon_access": moons,
        "entrance_links": [],
        "painting_links": [],
    })


def seeded_state(world: rl.LogicWorld) -> tuple[rl.LogicRandomizer, rl.LogicState]:
    settings = rl.LogicSettings(seed=1, entrance_rando=False)
    randomizer = rl.LogicRandomizer(world, settings)
    state = rl.LogicState(settings, world)
    state.accessible_stages.add(STAGE)
    return randomizer, state


class NodeAccessParseTests(unittest.TestCase):
    def test_splits_node_atoms_from_token_atoms(self) -> None:
        requirement = rl.parse_requirement(
            "Frog & @A::b | Frog", {"Frog"}, {"A::b"}
        )
        self.assertEqual("custom", requirement.mode)
        self.assertEqual(
            [frozenset({"Frog"}), frozenset({"Frog"})], requirement.alternatives
        )
        self.assertEqual(
            [frozenset({"A::b"}), frozenset()], requirement.node_alternatives
        )
        self.assertTrue(requirement.has_node_requirements)

    def test_node_only_option_keeps_an_empty_token_set(self) -> None:
        requirement = rl.parse_requirement("@A::b", {"Frog"}, {"A::b"})
        self.assertEqual([frozenset()], requirement.alternatives)
        self.assertEqual([frozenset({"A::b"})], requirement.node_alternatives)

    def test_unknown_node_degrades_to_raw(self) -> None:
        requirement = rl.parse_requirement("@Nope::start", {"Frog"}, {"A::b"})
        self.assertEqual("raw", requirement.mode)

    def test_node_atoms_pass_unchecked_without_a_node_set(self) -> None:
        requirement = rl.parse_requirement("@Nope::start", {"Frog"})
        self.assertEqual("custom", requirement.mode)
        self.assertEqual([frozenset({"Nope::start"})], requirement.node_alternatives)

    def test_bare_at_sign_is_rejected(self) -> None:
        self.assertEqual("raw", rl.parse_requirement("@", {"Frog"}).mode)


class NodeAccessReachabilityTests(unittest.TestCase):
    def test_edge_opens_only_once_the_required_node_is_reached(self) -> None:
        # start -> gated needs @switch, and switch is only reachable from start.
        world = build_world(
            [
                edge_row("e_switch", f"{STAGE}::start", f"{STAGE}::switch", "free"),
                edge_row(
                    "e_gated", f"{STAGE}::start", f"{STAGE}::gated",
                    f"@{STAGE}::switch",
                ),
            ],
            [],
        )
        randomizer, state = seeded_state(world)
        self.assertEqual(
            {f"{STAGE}::start", f"{STAGE}::switch", f"{STAGE}::gated"},
            randomizer.reachable_nodes(state),
        )

    def test_unreachable_required_node_keeps_the_edge_shut(self) -> None:
        # Nothing leads to switch, so the gated edge never opens.
        world = build_world(
            [
                edge_row(
                    "e_gated", f"{STAGE}::start", f"{STAGE}::gated",
                    f"@{STAGE}::switch",
                ),
            ],
            [],
        )
        randomizer, state = seeded_state(world)
        self.assertEqual({f"{STAGE}::start"}, randomizer.reachable_nodes(state))

    def test_chained_node_requirements_settle_in_one_sweep(self) -> None:
        # gated needs switch; deep needs gated. Both must open in the same call,
        # in whatever order the edges happen to be visited.
        world = build_world(
            [
                edge_row(
                    "e_deep", f"{STAGE}::start", f"{STAGE}::deep",
                    f"@{STAGE}::gated",
                ),
                edge_row(
                    "e_gated", f"{STAGE}::start", f"{STAGE}::gated",
                    f"@{STAGE}::switch",
                ),
                edge_row("e_switch", f"{STAGE}::start", f"{STAGE}::switch", "free"),
            ],
            [],
        )
        randomizer, state = seeded_state(world)
        self.assertIn(f"{STAGE}::deep", randomizer.reachable_nodes(state))

    def test_token_and_node_requirements_are_both_needed(self) -> None:
        world = build_world(
            [
                edge_row("e_switch", f"{STAGE}::start", f"{STAGE}::switch", "free"),
                edge_row(
                    "e_gated", f"{STAGE}::start", f"{STAGE}::gated",
                    f"Frog & @{STAGE}::switch",
                ),
            ],
            [],
        )
        randomizer, state = seeded_state(world)
        self.assertNotIn(f"{STAGE}::gated", randomizer.reachable_nodes(state))

        state.tokens.add("Frog")
        self.assertIn(f"{STAGE}::gated", randomizer.reachable_nodes(state))


class NodeAccessMoonTests(unittest.TestCase):
    def test_moon_requirement_honours_node_access(self) -> None:
        world = build_world(
            [
                edge_row("e_switch", f"{STAGE}::start", f"{STAGE}::switch", "free"),
            ],
            [moon_row("moon_gated", f"{STAGE}::start", f"@{STAGE}::deep")],
        )
        randomizer, state = seeded_state(world)
        moon = world.moons["moon_gated"]
        reachable = randomizer.reachable_nodes(state)

        # deep has no incoming edge, so the moon stays locked...
        self.assertNotIn(f"{STAGE}::deep", reachable)
        self.assertFalse(randomizer.is_location_reachable(moon, state, reachable))

        # ...and unlocks once the player is standing on it.
        state.accessible_nodes.add(f"{STAGE}::deep")
        reachable = randomizer.reachable_nodes(state)
        self.assertTrue(randomizer.is_location_reachable(moon, state, reachable))


if __name__ == "__main__":
    unittest.main()
