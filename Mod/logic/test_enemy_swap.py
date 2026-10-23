from __future__ import annotations

import csv
import tempfile
import unittest
from pathlib import Path

from enemy_swap import (
    ASSIGNMENT_DATA_VERSION,
    SWAP_MODE_OFF,
    SWAP_MODE_SAME_CATEGORY,
    SWAP_MODE_WITH_COMBAT,
    CaptureGate,
    FAILURE_MEMORY_BUDGET,
    FAILURE_SPAWN_RESTRICTED,
    FAILURE_UNSATISFIED_CONSTRAINTS,
    MATCH_ALL,
    MATCH_ANY,
    MATCH_AT_LEAST,
    MemoryBudgetExceeded,
    PROP_SHAPE_FILTER_EXCEPTIONS,
    SpawnRestrictionViolated,
    UnsatisfiedCaptureGates,
    fnv1a,
    load_swap_config,
    mix,
)


def write_csv(
    path: Path, fields: list[str], rows: list[dict[str, str]]
) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


ENTITY_FIELDS = [
    "unit_config_name", "friendly_name", "class_name", "category",
    "capture_token", "swap_out", "any_pool", "notes",
]
PLACEMENT_FIELDS = [
    "stage", "object_id", "unit_config_name", "list_name", "layer",
    "scenarios", "x", "y", "z", "logic_stage", "model_name",
    "event_name", "event_wait_action_name", "balloon_action_name", "comment",
]
RULE_FIELDS = [
    "stage", "object_id", "unit_config_name", "policy", "pool",
    "logic_stage", "notes",
]
GATE_FIELDS = [
    "gate_id", "kingdom", "logic_stage", "edge_id",
    "source_capture_token", "replacement_capture_token",
    "match_mode", "minimum_count", "notes",
]
MEMBER_FIELDS = ["gate_id", "placement_stage", "object_id"]
TOKEN_FIELDS = [
    "token", "token_type", "display_name", "category", "prerequisite",
    "always_unlocked", "requires_any_cap_throw", "can_start_with_seed",
    "aliases", "notes",
]
NODE_FIELDS = [
    "node_id", "kingdom", "stage", "display_name", "node_type", "side",
    "entrance_id", "obj_id", "x", "y", "z", "scenario_mask",
    "scenario_floor", "source", "notes",
]
ROUTE_FIELDS = [
    "edge_id", "kingdom", "area_scope", "from_node_id", "to_node_id",
    "requirement_expr", "requirement_notes", "edge_type", "difficulty",
    "bidirectional", "source", "notes", "option_scenarios", "status",
]
MEMORY_FIELDS = [
    "unit_config_name", "first_resource_bytes", "first_instance_bytes",
    "additional_instance_bytes", "generator_peak_bytes", "resource_keys",
    "resource_key_bytes", "heap_domain", "measurement_status", "confidence",
    "measured",
]
SPATIAL_FIELDS = [
    "unit_config_name", "footprint", "height", "solid",
    "measurement", "confidence", "notes",
]
# The axis columns are optional by design, so PropSpatialCompatibilityTest
# keeps writing rows without them.
SPATIAL_AXIS_FIELDS = SPATIAL_FIELDS + [
    "collision_x", "collision_y", "collision_z",
    "model_x", "model_y", "model_z",
]
BUDGET_FIELDS = [
    "stage", "scenario", "max_extra_resource_bytes",
    "max_extra_scene_bytes", "reserve_resource_bytes", "reserve_scene_bytes",
]

RESTRICTION_FIELDS = [
    "stage", "scenario", "target_entity", "target_name",
    "source_mask", "max_instances", "reason", "status", "evidence",
]

ALL_SCENARIOS = ";".join(str(index) for index in range(15))


def entity(
    name: str, token: str, category: str = "capture",
    *, swap_out: bool = True, target: bool = True
) -> dict[str, str]:
    return {
        "unit_config_name": name,
        "friendly_name": name,
        "class_name": name,
        "category": category,
        "capture_token": token,
        "swap_out": str(swap_out).lower(),
        "any_pool": str(target).lower(),
        "notes": "",
    }


def placement(
    object_id: str, actor: str, category: str,
    token: str = "", scenarios: str = ALL_SCENARIOS,
    stage: str = "TestStage"
) -> dict[str, str]:
    return {
        "stage": stage,
        "object_id": object_id,
        "unit_config_name": actor,
        "list_name": "ObjectList",
        "layer": "Common",
        "scenarios": scenarios,
        "x": "0", "y": "0", "z": "0",
        "logic_stage": "TestStage",
        "model_name": "",
        "event_name": "",
        "event_wait_action_name": "",
        "balloon_action_name": "",
        "comment": "",
    }


def rule(object_id: str, actor: str, policy: str = "randomize") -> dict[str, str]:
    return {
        "stage": "TestStage",
        "object_id": object_id,
        "unit_config_name": actor,
        "policy": policy,
        "pool": "",
        "logic_stage": "",
        "notes": "",
    }


class Fixture:
    def __init__(
        self,
        *,
        placements: list[dict[str, str]],
        rules: list[dict[str, str]],
        routes: list[dict[str, str]],
        gates: list[dict[str, str]],
        members: list[dict[str, str]],
        memory: bool = False,
    ) -> None:
        self.directory = Path(tempfile.mkdtemp())
        write_csv(self.directory / "enemy_swap_entities.csv", ENTITY_FIELDS, [
            entity("FrogActor", "Frog"),
            entity("GoombaActor", "Kuribo"),
            entity("EnemyActor", "", "enemy"),
            entity("NpcActor", "", "npc"),
            entity("PropActor", "", "prop"),
            entity(
                "WireActor", "ElectricWire",
                swap_out=False, target=False,
            ),
        ])
        write_csv(
            self.directory / "enemy_swap_spatial.csv",
            SPATIAL_FIELDS,
            [{
                "unit_config_name": "PropActor",
                "footprint": "100",
                "height": "100",
                "solid": "false",
                "measurement": "manual",
                "confidence": "high",
                "notes": "test fixture",
            }],
        )
        write_csv(
            self.directory / "enemy_capture_map.csv",
            PLACEMENT_FIELDS, placements,
        )
        write_csv(
            self.directory / "enemy_swap_rules.csv",
            RULE_FIELDS, rules,
        )
        write_csv(
            self.directory / "capture_gates.csv",
            GATE_FIELDS, gates,
        )
        write_csv(
            self.directory / "capture_gate_objects.csv",
            MEMBER_FIELDS, members,
        )
        write_csv(self.directory / "logic_tokens.csv", TOKEN_FIELDS, [
            {
                "token": "Frog", "token_type": "capture",
                "display_name": "Frog", "category": "Capture",
                "prerequisite": "", "always_unlocked": "false",
                "requires_any_cap_throw": "false",
                "can_start_with_seed": "false", "aliases": "", "notes": "",
            },
            {
                "token": "Kuribo", "token_type": "capture",
                "display_name": "Goomba", "category": "Capture",
                "prerequisite": "", "always_unlocked": "false",
                "requires_any_cap_throw": "false",
                "can_start_with_seed": "false", "aliases": "", "notes": "",
            },
            {
                "token": "ElectricWire", "token_type": "capture",
                "display_name": "Wire", "category": "Capture",
                "prerequisite": "", "always_unlocked": "false",
                "requires_any_cap_throw": "false",
                "can_start_with_seed": "false", "aliases": "", "notes": "",
            },
            {
                "token": "WallJump", "token_type": "ability",
                "display_name": "Wall Jump", "category": "Movement",
                "prerequisite": "", "always_unlocked": "false",
                "requires_any_cap_throw": "false",
                "can_start_with_seed": "false", "aliases": "", "notes": "",
            },
        ])
        write_csv(self.directory / "logic_nodes.csv", NODE_FIELDS, [
            {
                "node_id": "TestStage::a", "kingdom": "Test Kingdom",
                "stage": "TestStage", "display_name": "A",
                "node_type": "custom_place", "side": "",
                "entrance_id": "", "obj_id": "", "x": "0", "y": "0",
                "z": "0", "scenario_mask": "", "scenario_floor": "",
                "source": "test", "notes": "",
            },
            {
                "node_id": "TestStage::b", "kingdom": "Test Kingdom",
                "stage": "TestStage", "display_name": "B",
                "node_type": "custom_place", "side": "",
                "entrance_id": "", "obj_id": "", "x": "1", "y": "0",
                "z": "0", "scenario_mask": "", "scenario_floor": "",
                "source": "test", "notes": "",
            },
        ])
        write_csv(
            self.directory / "route_edges.csv",
            ROUTE_FIELDS, routes,
        )
        if memory:
            memory_rows = []
            for index, name in enumerate((
                "FrogActor", "GoombaActor", "EnemyActor", "NpcActor",
                "PropActor", "WireActor",
            )):
                shared = name in {"FrogActor", "GoombaActor"}
                memory_rows.append({
                    "unit_config_name": name,
                    "first_resource_bytes": str(100 + index * 10),
                    "first_instance_bytes": "20",
                    "additional_instance_bytes": "5",
                    "generator_peak_bytes": "40",
                    "resource_keys": "shared" if shared else name,
                    "resource_key_bytes": "100" if shared else str(100 + index * 10),
                    "heap_domain": "scene_resource+scene",
                    "measurement_status": "measured",
                    "confidence": "verified",
                    "measured": "true",
                })
            write_csv(
                self.directory / "enemy_swap_costs.csv",
                MEMORY_FIELDS, memory_rows,
            )

    def write_budget(self, *, max_resource: int,
                     max_scene: int = 100000) -> None:
        write_csv(
            self.directory / "enemy_swap_stage_budgets.csv",
            BUDGET_FIELDS, [{
                "stage": "TestStage", "scenario": "0",
                "max_extra_resource_bytes": str(max_resource),
                "max_extra_scene_bytes": str(max_scene),
                "reserve_resource_bytes": "0",
                "reserve_scene_bytes": "0",
            }],
        )

    def write_restrictions(self, rows: list[dict[str, str]]) -> None:
        write_csv(
            self.directory / "enemy_spawn_restrictions.csv",
            RESTRICTION_FIELDS, rows,
        )

    def load(self, strict: bool = False, enforce_suspected: bool = False):
        return load_swap_config(
            self.directory,
            templates_dir=self.directory,
            strict=strict,
            enforce_suspected=enforce_suspected,
        )


def route(edge_id: str, requirement: str) -> dict[str, str]:
    return {
        "edge_id": edge_id,
        "kingdom": "Test Kingdom",
        "area_scope": "overworld",
        "from_node_id": "TestStage::a",
        "to_node_id": "TestStage::b",
        "requirement_expr": requirement,
        "requirement_notes": "",
        "edge_type": "human_route",
        "difficulty": "casual",
        "bidirectional": "false",
        "source": "test",
        "notes": "",
        "option_scenarios": "",
        "status": "",
    }


def gate(
    gate_id: str, edge_id: str, source: str, replacement: str,
    mode: str = MATCH_ANY, minimum: str = ""
) -> dict[str, str]:
    return {
        "gate_id": gate_id,
        "kingdom": "Test Kingdom",
        "logic_stage": "TestStage",
        "edge_id": edge_id,
        "source_capture_token": source,
        "replacement_capture_token": replacement,
        "match_mode": mode,
        "minimum_count": minimum,
        "notes": "",
    }


def member(gate_id: str, object_id: str) -> dict[str, str]:
    return {
        "gate_id": gate_id,
        "placement_stage": "TestStage",
        "object_id": object_id,
    }


class HashContractTest(unittest.TestCase):
    def test_hash_and_version_contract(self) -> None:
        self.assertEqual(ASSIGNMENT_DATA_VERSION, 12)
        self.assertEqual(fnv1a(""), 2166136261)
        self.assertEqual(fnv1a("KuriboWing"), 4209863504)
        self.assertEqual(mix(0), 2462723854)
        self.assertEqual(mix(0xDEADBEEF), 3553530007)


class DataContractTest(unittest.TestCase):
    def test_complete_inventory_keeps_unknown_actors(self) -> None:
        fixture = Fixture(
            placements=[
                placement("obj1", "FrogActor", "capture", "Frog"),
                placement("obj2", "UnknownActor", "other"),
            ],
            rules=[rule("obj1", "FrogActor")],
            routes=[],
            gates=[],
            members=[],
        )
        config = fixture.load()
        self.assertEqual(len(config.placements), 2)
        self.assertEqual(config.placements[1].entity, -1)
        self.assertEqual(config.placements[1].category, "other")

    def test_legacy_policy_warns_and_normalizes(self) -> None:
        warnings: list[str] = []
        fixture = Fixture(
            placements=[placement(
                "obj1", "EnemyActor", "enemy"
            )],
            rules=[rule("obj1", "EnemyActor", "logic")],
            routes=[],
            gates=[],
            members=[],
        )
        config = load_swap_config(
            fixture.directory,
            templates_dir=fixture.directory,
            warn=warnings.append,
        )
        self.assertEqual(config.rules[0].policy, "randomize")
        self.assertTrue(any("legacy policy" in item for item in warnings))

    def test_duplicate_physical_key_is_strict_error(self) -> None:
        duplicate = placement(
            "obj1", "EnemyActor", "enemy"
        )
        fixture = Fixture(
            placements=[duplicate, dict(duplicate)],
            rules=[],
            routes=[],
            gates=[],
            members=[],
        )
        with self.assertRaisesRegex(ValueError, "duplicate placement"):
            fixture.load(strict=True)

    def test_event_driven_npc_rule_is_forced_to_keep(self) -> None:
        npc = placement("obj1", "NpcActor", "npc")
        npc["event_name"] = "SimpleMessage"
        fixture = Fixture(
            placements=[npc],
            rules=[rule("obj1", "NpcActor")],
            routes=[],
            gates=[],
            members=[],
        )
        warnings: list[str] = []
        config = load_swap_config(
            fixture.directory,
            templates_dir=fixture.directory,
            warn=warnings.append,
        )
        self.assertEqual(config.rules[0].policy, "keep")
        self.assertTrue(any(
            "event-driven NPC" in warning for warning in warnings
        ))

    def test_strict_rules_reject_unsafe_explicit_targets(self) -> None:
        unsafe_rule = rule("obj1", "EnemyActor")
        unsafe_rule["pool"] = "WireActor"
        fixture = Fixture(
            placements=[placement("obj1", "EnemyActor", "enemy")],
            rules=[unsafe_rule],
            routes=[],
            gates=[],
            members=[],
        )
        with self.assertRaisesRegex(ValueError, "unsafe pool target"):
            fixture.load(strict=True)

    def test_rule_cannot_override_catalog_source_safety(self) -> None:
        fixture = Fixture(
            placements=[placement("obj1", "WireActor", "capture")],
            rules=[rule("obj1", "WireActor")],
            routes=[],
            gates=[],
            members=[],
        )
        with self.assertRaisesRegex(ValueError, "not swap-out safe"):
            fixture.load(strict=True)


class GateSemanticsTest(unittest.TestCase):
    def make_quantifier_config(self):
        fixture = Fixture(
            placements=[
                placement("obj1", "EnemyActor", "enemy"),
                placement("obj2", "EnemyActor", "enemy"),
                placement("obj3", "EnemyActor", "enemy"),
            ],
            rules=[
                rule("obj1", "EnemyActor"),
                rule("obj2", "EnemyActor"),
                rule("obj3", "EnemyActor"),
            ],
            routes=[route("edge", "Frog")],
            gates=[gate(
                "g", "edge", "Frog", "Frog",
                MATCH_AT_LEAST, "2",
            )],
            members=[
                member("g", "obj1"),
                member("g", "obj2"),
                member("g", "obj3"),
            ],
        )
        return fixture.load()

    def test_all_any_and_at_least_boundaries(self) -> None:
        config = self.make_quantifier_config()
        frog = config.entity_by_name["FrogActor"]
        enemy = config.entity_by_name["EnemyActor"]
        base = config.gates[0]
        resolved = [frog, frog, enemy]
        self.assertTrue(config.gate_matches(base, resolved, 1))
        self.assertFalse(config.gate_matches(
            CaptureGate(**{
                **base.__dict__,
                "match_mode": MATCH_ALL,
                "minimum_count": 0,
            }),
            resolved,
            1,
        ))
        self.assertTrue(config.gate_matches(
            CaptureGate(**{
                **base.__dict__,
                "match_mode": MATCH_ANY,
                "minimum_count": 0,
            }),
            resolved,
            1,
        ))

    def test_empty_and_malformed_threshold_are_strict_errors(self) -> None:
        fixture = Fixture(
            placements=[placement(
                "obj1", "EnemyActor", "enemy"
            )],
            rules=[rule("obj1", "EnemyActor")],
            routes=[route("edge", "Frog")],
            gates=[gate(
                "g", "edge", "Frog", "Frog",
                MATCH_AT_LEAST, "2",
            )],
            members=[member("g", "obj1")],
        )
        with self.assertRaisesRegex(ValueError, "threshold"):
            fixture.load(strict=True)


class MemoryBudgetTest(unittest.TestCase):
    def test_shared_resource_is_charged_once_and_instances_are_separate(self) -> None:
        fixture = Fixture(
            placements=[
                placement("obj1", "EnemyActor", "enemy"),
                placement("obj2", "EnemyActor", "enemy"),
            ],
            rules=[], routes=[], gates=[], members=[], memory=True,
        )
        config = fixture.load(strict=True)
        frog = config.entity_by_name["FrogActor"]
        usage = config.memory_usage([frog, frog], "TestStage", 0)
        self.assertEqual(usage.resolved_resource_bytes, 100)
        self.assertEqual(usage.resolved_scene_bytes, 25)

    def test_over_budget_assignment_has_deterministic_failure_code(self) -> None:
        fixture = Fixture(
            placements=[placement("obj1", "FrogActor", "capture")],
            rules=[rule("obj1", "FrogActor")],
            routes=[], gates=[], members=[], memory=True,
        )
        fixture.write_budget(max_resource=10)
        with (fixture.directory / "enemy_swap_rules.csv").open(
                encoding="utf-8") as handle:
            rules = list(csv.DictReader(handle))
        rules[0]["pool"] = "EnemyActor"
        write_csv(fixture.directory / "enemy_swap_rules.csv",
                  RULE_FIELDS, rules)
        config = fixture.load(strict=True)
        with self.assertRaises(MemoryBudgetExceeded) as raised:
            config.build_assignment(123, 0)
        self.assertEqual(raised.exception.code, FAILURE_MEMORY_BUDGET)
        self.assertEqual(raised.exception.stage, "TestStage")
        self.assertEqual(raised.exception.scenario, 0)


class LogicCapturePinTest(unittest.TestCase):
    """A capture the logic graph depends on stays vanilla until gated."""

    def build(self, *, gates, members):
        return Fixture(
            placements=[
                placement("obj1", "FrogActor", "capture", "Frog"),
                placement("obj2", "FrogActor", "capture", "Frog"),
            ],
            rules=[rule("obj1", "FrogActor"), rule("obj2", "FrogActor")],
            routes=[route("edge1", "Frog")],
            gates=gates, members=members,
        )

    def test_ungated_logic_capture_is_pinned_and_reported(self) -> None:
        config = self.build(gates=[], members=[]).load()

        self.assertEqual(config.pinned_captures, {("TestStage", "Frog"): 2})
        for swap_rule in config.rules:
            self.assertNotEqual(swap_rule.policy, "randomize")
        frog = config.entity_by_name["FrogActor"]
        resolved = config.build_assignment(7, 0).entities
        self.assertEqual(list(resolved), [frog, frog])

    def test_authoring_a_gate_opts_that_stage_in(self) -> None:
        config = self.build(
            gates=[gate("g1", "edge1", "Frog", "Kuribo", MATCH_ALL)],
            members=[member("g1", "obj1"), member("g1", "obj2")],
        ).load(strict=True)

        self.assertEqual(config.pinned_captures, {})
        self.assertTrue(
            any(r.policy == "randomize" for r in config.rules),
            "an authored gate must make the gated stage randomizable",
        )


class SpawnRestrictionTest(unittest.TestCase):
    def fixture(self) -> Fixture:
        return Fixture(
            placements=[
                placement("obj1", "EnemyActor", "enemy"),
                placement("obj2", "EnemyActor", "enemy"),
                placement("obj3", "EnemyActor", "enemy"),
            ],
            rules=[
                rule("obj1", "EnemyActor"),
                rule("obj2", "EnemyActor"),
                rule("obj3", "EnemyActor"),
            ],
            routes=[], gates=[], members=[],
        )

    def restriction(self, config_entities, name: str, **overrides
                    ) -> dict[str, str]:
        index = next(i for i, e in enumerate(config_entities)
                     if e.name == name)
        row = {
            "stage": "TestStage", "scenario": "1",
            "target_entity": str(index), "target_name": name,
            "source_mask": "3", "max_instances": "0",
            "reason": "creation_failed", "status": "confirmed",
            "evidence": "test",
        }
        row.update(overrides)
        return row

    def entities_of(self, fixture: Fixture):
        return fixture.load().entities

    def test_suspected_rows_are_inert_until_explicitly_enforced(self) -> None:
        fixture = self.fixture()
        entities = self.entities_of(fixture)
        fixture.write_restrictions([
            self.restriction(entities, "FrogActor", status="suspected"),
        ])

        self.assertEqual(len(fixture.load().spawn_restrictions), 0)
        self.assertEqual(
            len(fixture.load(enforce_suspected=True).spawn_restrictions), 1)

    def test_confirmed_restriction_removes_the_entity_from_the_domain(self) -> None:
        fixture = self.fixture()
        entities = self.entities_of(fixture)
        fixture.write_restrictions([
            self.restriction(entities, "FrogActor"),
        ])
        config = fixture.load(strict=True)
        frog = config.entity_by_name["FrogActor"]

        resolved = config.build_assignment(123, 0).entities

        self.assertNotIn(frog, resolved)
        self.assertEqual(config.spawn_restriction_violations(resolved), [])

    def test_cap_of_one_allows_exactly_one_instance(self) -> None:
        fixture = self.fixture()
        entities = self.entities_of(fixture)
        # Narrow the pool so Frog is a likely pick and its cap is what stops a
        # second one. EnemyActor stays legal: it is the vanilla entity here,
        # and a restriction that forbids the placement's own vanilla actor
        # leaves the solver nothing to fall back to.
        rows = [self.restriction(entities, name) for name in
                ("GoombaActor", "PropActor")]
        rows.append(self.restriction(entities, "FrogActor", max_instances="1"))
        fixture.write_restrictions(rows)
        config = fixture.load(strict=True)
        frog = config.entity_by_name["FrogActor"]

        resolved = config.build_assignment(123, 0).entities

        self.assertLessEqual(sum(1 for e in resolved if e == frog), 1)
        self.assertEqual(config.spawn_restriction_violations(resolved), [])

    def test_scenario_is_one_based_against_the_zero_based_mask(self) -> None:
        # scenarios="0" is ScenarioNo 1, so a restriction naming scenario 1
        # must cover it and a restriction naming scenario 2 must not.
        fixture = Fixture(
            placements=[placement("obj1", "EnemyActor", "enemy", scenarios="0")],
            rules=[rule("obj1", "EnemyActor")],
            routes=[], gates=[], members=[],
        )
        entities = self.entities_of(fixture)

        fixture.write_restrictions([
            self.restriction(entities, "FrogActor", scenario="1")])
        covering = fixture.load(strict=True)
        fixture.write_restrictions([
            self.restriction(entities, "FrogActor", scenario="2")])
        adjacent = fixture.load(strict=True)

        target = covering.placements[0]
        frog = covering.entity_by_name["FrogActor"]
        self.assertTrue(covering.spawn_restrictions[0].applies_to(target))
        self.assertFalse(adjacent.spawn_restrictions[0].applies_to(target))
        self.assertNotIn(frog, covering.build_assignment(123, 0).entities)

    def test_restriction_reaches_placements_inside_an_embedded_zone(self) -> None:
        # The audit names the physical stage; the placement carries the zone.
        zone = placement("obj9", "EnemyActor", "enemy", stage="TestZone")
        fixture = Fixture(
            placements=[zone],
            rules=[dict(rule("obj9", "EnemyActor"), stage="TestZone")],
            routes=[], gates=[], members=[],
        )
        entities = self.entities_of(fixture)
        fixture.write_restrictions([self.restriction(entities, "FrogActor")])
        config = fixture.load(strict=True)

        self.assertEqual(config.placements[0].stage, "TestZone")
        self.assertEqual(config.placements[0].logic_stage, "TestStage")
        self.assertTrue(
            config.spawn_restrictions[0].applies_to(config.placements[0]))
        self.assertNotIn(
            config.entity_by_name["FrogActor"],
            config.build_assignment(123, 0).entities)

    def test_violation_raises_with_a_deterministic_failure_code(self) -> None:
        fixture = self.fixture()
        entities = self.entities_of(fixture)
        config = fixture.load(strict=True)
        frog = config.entity_by_name["FrogActor"]
        # Bypass the domain filter to prove the post-assignment check also
        # catches a violation the candidate filter somehow let through.
        fixture.write_restrictions([self.restriction(entities, "FrogActor")])
        config = fixture.load(strict=True)

        violations = config.spawn_restriction_violations(
            [frog] * len(config.placements))

        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0][1], len(config.placements))
        with self.assertRaises(SpawnRestrictionViolated) as raised:
            config.validate_spawn_restrictions(
                [frog] * len(config.placements), 1, 0)
        self.assertEqual(raised.exception.code, FAILURE_SPAWN_RESTRICTED)
        self.assertEqual(raised.exception.max_instances, 0)

    def test_target_name_mismatch_is_a_strict_error(self) -> None:
        fixture = self.fixture()
        entities = self.entities_of(fixture)
        fixture.write_restrictions([
            self.restriction(entities, "FrogActor", target_name="GoombaActor"),
        ])
        with self.assertRaises(ValueError):
            fixture.load(strict=True)


class AssignmentSolverTest(unittest.TestCase):
    def test_multi_capture_requirement_has_two_witnesses(self) -> None:
        fixture = Fixture(
            placements=[
                placement("obj1", "EnemyActor", "enemy"),
                placement("obj2", "EnemyActor", "enemy"),
            ],
            rules=[
                rule("obj1", "EnemyActor"),
                rule("obj2", "EnemyActor"),
            ],
            routes=[route("edge", "Frog & Kuribo")],
            gates=[
                gate("frog", "edge", "Frog", "Frog"),
                gate("goomba", "edge", "Kuribo", "Kuribo"),
            ],
            members=[
                member("frog", "obj1"),
                member("goomba", "obj2"),
            ],
        )
        config = fixture.load(strict=True)
        result = config.build_assignment(1234, 0)
        self.assertEqual(len(result.witness_gate_ids), 30)
        self.assertTrue(all("@"
                            in witness for witness in result.witness_gate_ids))

    def test_conflicting_single_member_gates_are_unsatisfiable(self) -> None:
        fixture = Fixture(
            placements=[placement(
                "obj1", "EnemyActor", "enemy"
            )],
            rules=[rule("obj1", "EnemyActor")],
            routes=[
                route("frog_edge", "Frog"),
                route("goomba_edge", "Kuribo"),
            ],
            gates=[
                gate("frog", "frog_edge", "Frog", "Frog"),
                gate(
                    "goomba", "goomba_edge",
                    "Kuribo", "Kuribo",
                ),
            ],
            members=[
                member("frog", "obj1"),
                member("goomba", "obj1"),
            ],
        )
        config = fixture.load()
        with self.assertRaises(UnsatisfiedCaptureGates) as raised:
            config.build_assignment(1, 0)
        self.assertEqual(
            raised.exception.code,
            FAILURE_UNSATISFIED_CONSTRAINTS,
        )

    def test_or_gate_alternative_backtracks_to_compatible_member(self) -> None:
        fixture = Fixture(
            placements=[
                placement("obj1", "EnemyActor", "enemy"),
                placement("obj2", "EnemyActor", "enemy"),
            ],
            rules=[
                rule("obj1", "EnemyActor"),
                rule("obj2", "EnemyActor"),
            ],
            routes=[
                route("frog_edge", "Frog"),
                route("goomba_edge", "Kuribo"),
            ],
            gates=[
                gate("frog_a", "frog_edge", "Frog", "Frog"),
                gate("frog_b", "frog_edge", "Frog", "Frog"),
                gate(
                    "goomba", "goomba_edge",
                    "Kuribo", "Kuribo",
                ),
            ],
            members=[
                member("frog_a", "obj1"),
                member("frog_b", "obj2"),
                member("goomba", "obj1"),
            ],
        )
        config = fixture.load(strict=True)
        result = config.build_assignment(99, 0)
        frog = config.entity_by_name["FrogActor"]
        goomba = config.entity_by_name["GoombaActor"]
        self.assertEqual(result.entities[0], goomba)
        self.assertEqual(result.entities[1], frog)

    def test_disabled_npc_category_cannot_be_a_phantom_witness(self) -> None:
        fixture = Fixture(
            placements=[placement(
                "obj1", "NpcActor", "npc"
            )],
            rules=[rule("obj1", "NpcActor")],
            routes=[route("edge", "Frog")],
            gates=[gate("g", "edge", "Frog", "Frog")],
            members=[member("g", "obj1")],
        )
        config = fixture.load()
        with self.assertRaises(UnsatisfiedCaptureGates):
            config.build_assignment(7, 0, npc_mode=SWAP_MODE_OFF)
        self.assertTrue(
            config.build_assignment(
                7, 0, npc_mode=SWAP_MODE_WITH_COMBAT
            ).witness_gate_ids
        )

    def test_npc_only_assignment_preserves_vanilla_route_logic(self) -> None:
        fixture = Fixture(
            placements=[placement(
                "obj1", "NpcActor", "npc"
            )],
            rules=[rule("obj1", "NpcActor")],
            routes=[route("edge", "Frog")],
            gates=[gate("g", "edge", "Frog", "Frog")],
            members=[member("g", "obj1")],
        )
        config = fixture.load()
        result = config.build_assignment(
            7, 0, enemy_enabled=False, npc_mode=SWAP_MODE_WITH_COMBAT
        )
        self.assertEqual(result.witness_gate_ids, ())

    def test_npc_explicit_pool_takes_precedence_over_default_domain(self) -> None:
        explicit_rule = rule("obj1", "NpcActor")
        explicit_rule["pool"] = "FrogActor"
        fixture = Fixture(
            placements=[placement("obj1", "NpcActor", "npc")],
            rules=[explicit_rule],
            routes=[],
            gates=[],
            members=[],
        )
        config = fixture.load(strict=True)
        result = config.build_assignment(
            11, 0, enemy_enabled=False, npc_mode=SWAP_MODE_WITH_COMBAT
        )
        self.assertEqual(
            result.entities[0],
            config.entity_by_name["FrogActor"],
        )
        self.assertNotIn(
            config.entity_by_name["NpcActor"], config.menu_pool
        )

    def test_npc_target_is_rejected_from_authored_pool(self) -> None:
        explicit_rule = rule("obj1", "EnemyActor")
        explicit_rule["pool"] = "NpcActor"
        fixture = Fixture(
            placements=[placement("obj1", "EnemyActor", "enemy")],
            rules=[explicit_rule],
            routes=[],
            gates=[],
            members=[],
        )
        with self.assertRaisesRegex(ValueError, "compatibility domain"):
            fixture.load(strict=True)


if __name__ == "__main__":
    unittest.main()


class ChaosSwapModeTest(unittest.TestCase):
    """The three-way NPC and prop modes, against the real config."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.config = load_swap_config(Path(__file__).resolve().parents[1] / "config")

    def swapped_pairs(self, **kwargs) -> dict[tuple[str, str], int]:
        """Map (source category, target category) -> count of changed placements."""

        result = self.config.build_assignment(
            1234, 0, enemy_enabled=True, **kwargs)
        pairs: dict[tuple[str, str], int] = {}
        for placement, entity in zip(self.config.placements, result.entities):
            if entity == placement.entity:
                continue
            key = (placement.category, self.config.entities[entity].category)
            pairs[key] = pairs.get(key, 0) + 1
        return pairs

    def test_an_unknown_mode_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            self.config.build_assignment(1234, 0, npc_mode=9)

    def test_broad_prop_sources_keep_moon_hosts_vanilla(self) -> None:
        config_dir = Path(__file__).resolve().parents[1] / "config"
        with (config_dir / "enemy_swap_entities.csv").open(
            newline="", encoding="utf-8-sig"
        ) as handle:
            entity_rows = list(csv.DictReader(handle))
        prop_names = {
            row["unit_config_name"]
            for row in entity_rows
            if row.get("category", "").lower() == "prop"
        }
        self.assertGreaterEqual(len(prop_names), 100)

        with (config_dir / "enemy_capture_map.csv").open(
            newline="", encoding="utf-8-sig"
        ) as handle:
            placement_rows = list(csv.DictReader(handle))
        with (config_dir / "enemy_swap_rules.csv").open(
            newline="", encoding="utf-8-sig"
        ) as handle:
            rule_rows = list(csv.DictReader(handle))
        rules = {
            (row["stage"], row["object_id"]): row
            for row in rule_rows
        }
        broad_placements = [
            row for row in placement_rows
            if row["unit_config_name"] in prop_names
            and row.get("parameter_config_name", "")
        ]
        self.assertGreaterEqual(len(broad_placements), 5000)
        moon_hosts = [
            row for row in broad_placements
            if row.get("moon_related", "").lower() == "true"
        ]
        self.assertGreaterEqual(len(moon_hosts), 100)
        self.assertTrue(all(
            rules[(row["stage"], row["object_id"])]["policy"] == "keep"
            for row in moon_hosts
        ))

    def test_combat_sources_never_receive_an_npc_or_prop(self) -> None:
        # Whatever the other categories are set to, an enemy placement must
        # not become scenery or a villager: only its own pool is eligible.
        for label, kwargs in (
            ("off", {}),
            ("npc same", {"npc_mode": SWAP_MODE_SAME_CATEGORY}),
            ("npc combat", {"npc_mode": SWAP_MODE_WITH_COMBAT}),
            ("prop same",
             {"prop_mode": SWAP_MODE_SAME_CATEGORY, "prop_chance": 100}),
            ("prop combat",
             {"prop_mode": SWAP_MODE_WITH_COMBAT, "prop_chance": 100}),
        ):
            with self.subTest(label):
                for (source, target) in self.swapped_pairs(**kwargs):
                    if source in {"capture", "enemy", "generator"}:
                        self.assertNotIn(target, {"npc", "prop"})

    def test_off_leaves_the_category_vanilla(self) -> None:
        pairs = self.swapped_pairs(
            npc_mode=SWAP_MODE_OFF, prop_mode=SWAP_MODE_OFF)
        self.assertFalse([k for k in pairs if k[0] in {"npc", "prop"}])

    def test_same_category_stays_inside_its_own_pool(self) -> None:
        npc = self.swapped_pairs(npc_mode=SWAP_MODE_SAME_CATEGORY)
        self.assertEqual(
            {target for source, target in npc if source == "npc"}, {"npc"})
        prop = self.swapped_pairs(
            prop_mode=SWAP_MODE_SAME_CATEGORY, prop_chance=100)
        self.assertEqual(
            {target for source, target in prop if source == "prop"}, {"prop"})

    def test_same_category_props_preserve_spatial_class(self) -> None:
        result = self.config.build_assignment(
            1234, 0, enemy_enabled=True,
            prop_mode=SWAP_MODE_SAME_CATEGORY, prop_chance=100,
        )
        for placement, target in zip(
            self.config.placements, result.entities
        ):
            if placement.category != "prop" or target == placement.entity:
                continue
            self.assertEqual(self.config.entities[target].category, "prop")
            self.assertTrue(
                self.config.prop_spatial_compatible(
                    placement.entity, target
                ),
                f"{self.config.entities[placement.entity].name} -> "
                f"{self.config.entities[target].name}",
            )

    def test_with_combat_widens_rather_than_replaces(self) -> None:
        # The wider mode must keep the narrow mode's own-category targets
        # available, not swap one pool for the other.
        npc = self.swapped_pairs(npc_mode=SWAP_MODE_WITH_COMBAT)
        npc_targets = {target for source, target in npc if source == "npc"}
        self.assertIn("npc", npc_targets)
        self.assertTrue(npc_targets & {"capture", "enemy", "generator"})
        prop = self.swapped_pairs(
            prop_mode=SWAP_MODE_WITH_COMBAT, prop_chance=100)
        prop_targets = {target for source, target in prop if source == "prop"}
        self.assertIn("prop", prop_targets)
        self.assertTrue(prop_targets & {"capture", "enemy", "generator"})

    def test_two_d_enemies_only_become_two_d_enemies(self) -> None:
        # The one hard rule of the 2D pool. A 3D actor in a flat section has
        # no 2D behaviour, and the section's camera and collision are built
        # for a plane it would ignore, so leakage in either direction is a bug
        # rather than a variety choice.
        pairs = self.swapped_pairs(two_d_enabled=True)
        self.assertTrue(
            [k for k in pairs if k[0] == "enemy2d"],
            "2D swapping produced nothing at all",
        )
        for source, target in pairs:
            if source == "enemy2d":
                self.assertEqual(target, "enemy2d")
            else:
                self.assertNotEqual(target, "enemy2d")

    def test_two_d_off_leaves_two_d_vanilla(self) -> None:
        pairs = self.swapped_pairs(two_d_enabled=False)
        self.assertFalse([k for k in pairs if "enemy2d" in k])

    def test_the_two_categories_are_independent(self) -> None:
        # Turning props on must not change what NPCs become, and vice versa.
        both = self.swapped_pairs(
            npc_mode=SWAP_MODE_SAME_CATEGORY,
            prop_mode=SWAP_MODE_WITH_COMBAT, prop_chance=100)
        npc_only = self.swapped_pairs(npc_mode=SWAP_MODE_SAME_CATEGORY)
        self.assertEqual(
            {k: v for k, v in both.items() if k[0] == "npc"},
            {k: v for k, v in npc_only.items() if k[0] == "npc"},
        )


class PropSpatialCompatibilityTest(unittest.TestCase):
    @staticmethod
    def write_prop_catalog(
        fixture: Fixture,
        entities: list[dict[str, str]],
        spatial_rows: list[dict[str, str]],
    ) -> None:
        write_csv(
            fixture.directory / "enemy_swap_entities.csv",
            ENTITY_FIELDS,
            entities,
        )
        write_csv(
            fixture.directory / "enemy_swap_spatial.csv",
            SPATIAL_FIELDS,
            spatial_rows,
        )

    @staticmethod
    def spatial(
        name: str, footprint: int, height: int, *, solid: bool
    ) -> dict[str, str]:
        return {
            "unit_config_name": name,
            "footprint": str(footprint),
            "height": str(height),
            "solid": str(solid).lower(),
            "measurement": "manual",
            "confidence": "high",
            "notes": "test fixture",
        }

    def test_prop_assignment_filters_size_and_solidity(self) -> None:
        fixture = Fixture(
            placements=[placement("obj1", "SmallPropA", "prop")],
            rules=[rule("obj1", "SmallPropA")],
            routes=[], gates=[], members=[],
        )
        props = [
            entity("SmallPropA", "", "prop"),
            entity("SmallPropB", "", "prop"),
            entity("LargeProp", "", "prop"),
            entity("SolidSmallProp", "", "prop"),
        ]
        self.write_prop_catalog(fixture, props, [
            self.spatial("SmallPropA", 100, 100, solid=False),
            self.spatial("SmallPropB", 180, 160, solid=False),
            self.spatial("LargeProp", 500, 600, solid=False),
            self.spatial("SolidSmallProp", 120, 130, solid=True),
        ])
        config = fixture.load(strict=True)
        allowed = {
            config.entity_by_name["SmallPropA"],
            config.entity_by_name["SmallPropB"],
        }
        seen = {
            config.build_assignment(
                seed, 0, enemy_enabled=False,
                prop_mode=SWAP_MODE_SAME_CATEGORY, prop_chance=100,
            ).entities[0]
            for seed in range(64)
        }
        self.assertTrue(seen <= allowed)
        self.assertIn(config.entity_by_name["SmallPropB"], seen)

    def test_unknown_size_source_stays_vanilla(self) -> None:
        fixture = Fixture(
            placements=[placement("obj1", "UnknownProp", "prop")],
            rules=[rule("obj1", "UnknownProp")],
            routes=[], gates=[], members=[],
        )
        self.write_prop_catalog(fixture, [
            entity("UnknownProp", "", "prop", target=False),
            entity("KnownTarget", "", "prop"),
        ], [
            self.spatial("KnownTarget", 100, 100, solid=False),
        ])
        config = fixture.load(strict=True)
        result = config.build_assignment(
            7, 0, enemy_enabled=False,
            prop_mode=SWAP_MODE_SAME_CATEGORY, prop_chance=100,
        )
        self.assertEqual(
            result.entities[0], config.entity_by_name["UnknownProp"]
        )

    def test_strict_load_rejects_unmeasured_prop_target(self) -> None:
        fixture = Fixture(
            placements=[], rules=[], routes=[], gates=[], members=[],
        )
        self.write_prop_catalog(
            fixture,
            [entity("UnknownTarget", "", "prop")],
            [],
        )
        with self.assertRaisesRegex(
            ValueError, "spatial.*unavailable|lack bounded spatial metadata"
        ):
            fixture.load(strict=True)


class PropShapeFilterTest(unittest.TestCase):
    """The shape rules that keep decorations out of the target pool."""

    @staticmethod
    def spatial(
        name: str, footprint: int, height: int, *, solid: bool,
        collision: tuple[int, int, int] = (0, 0, 0),
        model: tuple[int, int, int] = (0, 0, 0),
    ) -> dict[str, str]:
        return {
            "unit_config_name": name,
            "footprint": str(footprint),
            "height": str(height),
            "solid": str(solid).lower(),
            "collision_x": str(collision[0]),
            "collision_y": str(collision[1]),
            "collision_z": str(collision[2]),
            "model_x": str(model[0]),
            "model_y": str(model[1]),
            "model_z": str(model[2]),
            "measurement": "manual",
            "confidence": "high",
            "notes": "test fixture",
        }

    def eligibility(self, rows: list[dict[str, str]]) -> dict[str, bool]:
        fixture = Fixture(
            placements=[], rules=[], routes=[], gates=[], members=[],
        )
        write_csv(
            fixture.directory / "enemy_swap_entities.csv",
            ENTITY_FIELDS,
            [entity(row["unit_config_name"], "", "prop") for row in rows],
        )
        write_csv(
            fixture.directory / "enemy_swap_spatial.csv",
            SPATIAL_AXIS_FIELDS,
            rows,
        )
        config = fixture.load(strict=True)
        pool = set(config.prop_pool)
        return {
            row["unit_config_name"]:
                config.entity_by_name[row["unit_config_name"]] in pool
            for row in rows
        }

    def test_collision_that_does_not_describe_the_model_is_rejected(self) -> None:
        # PeachWorldHomeFountain: a 2280-unit model behind a 65-unit box.
        verdicts = self.eligibility([
            self.spatial(
                "Fountain", 65, 54, solid=True,
                collision=(65, 54, 61), model=(2280, 793, 1950),
            ),
            self.spatial(
                "Crate", 150, 100, solid=True,
                collision=(100, 100, 150), model=(19, 100, 153),
            ),
        ])
        self.assertFalse(verdicts["Fountain"])
        self.assertTrue(verdicts["Crate"])

    def test_bare_model_is_rejected_only_once_it_is_large(self) -> None:
        verdicts = self.eligibility([
            self.spatial(
                "BigBush", 624, 624, solid=False, model=(505, 70, 375),
            ),
            self.spatial(
                "KickStone", 140, 140, solid=False, model=(141, 146, 139),
            ),
        ])
        self.assertFalse(verdicts["BigBush"])
        self.assertTrue(verdicts["KickStone"])

    def test_ground_plate_is_rejected_but_upright_panel_is_not(self) -> None:
        verdicts = self.eligibility([
            # A manhole cover: knee-high collision over a wide disc.
            self.spatial(
                "Manhole", 459, 47, solid=True,
                collision=(459, 47, 459), model=(401, 9, 401),
            ),
            # A sign board is just as thin, standing on its edge.
            self.spatial(
                "SignBoard", 216, 254, solid=True,
                collision=(216, 254, 50), model=(239, 362, 71),
            ),
            # No collision mesh, so the model carries the shape.
            self.spatial(
                "FlowerPatch", 600, 600, solid=False, model=(94, 35, 271),
            ),
        ])
        self.assertFalse(verdicts["Manhole"])
        self.assertTrue(verdicts["SignBoard"])
        self.assertFalse(verdicts["FlowerPatch"])

    def test_named_exception_keeps_its_deliberate_flat_shape(self) -> None:
        name = sorted(PROP_SHAPE_FILTER_EXCEPTIONS)[0]
        verdicts = self.eligibility([
            self.spatial(
                name, 3366, 56, solid=True,
                collision=(3360, 56, 3366), model=(1538, 95, 1901),
            ),
            self.spatial(
                "PlainSlab", 3366, 56, solid=True,
                collision=(3360, 56, 3366), model=(1538, 95, 1901),
            ),
        ])
        self.assertTrue(verdicts[name])
        self.assertFalse(verdicts["PlainSlab"])
