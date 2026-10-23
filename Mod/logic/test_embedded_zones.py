from __future__ import annotations

import copy
import unittest

from embedded_zones import flatten_embedded_zones, transform_point


def mapping() -> dict[str, str]:
    return {
        "embedded_stage": "UnitZone",
        "parent_stage": "UnitHomeStage",
        "source_parent_stage": "UnitHomeStage",
        "translation_x": "100.000000",
        "translation_y": "10.000000",
        "translation_z": "200.000000",
        "rotation_x": "0.000000",
        "rotation_y": "90.000000",
        "rotation_z": "0.000000",
        "scale_x": "1.000000",
        "scale_y": "1.000000",
        "scale_z": "1.000000",
        "matrix_00": "0.000000",
        "matrix_01": "0.000000",
        "matrix_02": "1.000000",
        "matrix_03": "100.000000",
        "matrix_10": "0.000000",
        "matrix_11": "1.000000",
        "matrix_12": "0.000000",
        "matrix_13": "10.000000",
        "matrix_20": "-1.000000",
        "matrix_21": "0.000000",
        "matrix_22": "0.000000",
        "matrix_23": "200.000000",
        "scenario_indices": "0;1",
        "status": "active",
        "notes": "",
    }


class EmbeddedZoneTests(unittest.TestCase):
    def test_transform_point_applies_rotation_then_translation(self) -> None:
        matrix = (
            0.0, 0.0, 1.0, 100.0,
            0.0, 1.0, 0.0, 10.0,
            -1.0, 0.0, 0.0, 200.0,
        )
        self.assertEqual(transform_point(matrix, 2.0, 3.0, 4.0), (104.0, 13.0, 198.0))

    def test_flattens_zone_and_preserves_stable_ids(self) -> None:
        rooms = [
            {
                "stage": "UnitHomeStage",
                "kingdom": "Unit Kingdom",
                "stage_display_name": "Unit Home",
                "stage_start_node_id": "UnitHomeStage::start",
                "area_scope": "overworld",
                "has_shine_db_moons": "false",
                "has_raw_stage_data_points": "false",
                "active_entrance_link_count": "0",
                "excluded_entrance_link_count": "0",
                "notes": "",
            },
            {
                "stage": "UnitZone",
                "kingdom": "Unit Kingdom",
                "stage_display_name": "Unit Zone",
                "stage_start_node_id": "UnitZone::start",
                "area_scope": "subarea",
                "has_shine_db_moons": "true",
                "has_raw_stage_data_points": "true",
                "active_entrance_link_count": "0",
                "excluded_entrance_link_count": "0",
                "notes": "",
            },
        ]
        nodes = [
            {
                "node_id": "UnitHomeStage::start",
                "kingdom": "Unit Kingdom",
                "stage": "UnitHomeStage",
                "stage_display_name": "Unit Home",
                "area_scope": "overworld",
                "node_type": "stage_start",
                "source": "generated",
                "x": "",
                "y": "",
                "z": "",
            },
            {
                "node_id": "UnitZone::start",
                "kingdom": "Unit Kingdom",
                "stage": "UnitZone",
                "stage_display_name": "Unit Zone",
                "area_scope": "subarea",
                "node_type": "stage_start",
                "source": "generated",
                "display_name": "Default start",
                "x": "",
                "y": "",
                "z": "",
            },
            {
                "node_id": "UnitZone::checkpoint::obj1",
                "kingdom": "Unit Kingdom",
                "stage": "UnitZone",
                "stage_display_name": "Unit Zone",
                "area_scope": "subarea",
                "node_type": "checkpoint",
                "source": "StageData",
                "x": "2",
                "y": "3",
                "z": "4",
            },
        ]
        routes = [
            {
                "edge_id": "unit_edge",
                "kingdom": "Unit Kingdom",
                "area_scope": "subarea",
                "from_node_id": "UnitZone::start",
                "to_node_id": "UnitZone::checkpoint::obj1",
            }
        ]
        moons = [
            {
                "moon_id": "UnitZone:1:2",
                "runtime_shine_key": "UnitZone:1:2",
                "kingdom": "Unit Kingdom",
                "stage": "UnitZone",
                "stage_display_name": "Unit Zone",
                "area_scope": "subarea",
                "from_node_id": "UnitZone::checkpoint::obj1",
                "placement_x": "2",
                "placement_y": "3",
                "placement_z": "4",
            }
        ]

        result = flatten_embedded_zones(
            nodes,
            routes,
            moons,
            rooms,
            [],
            [mapping()],
            {},
        )

        self.assertEqual([room["stage"] for room in result["rooms"]], ["UnitHomeStage"])
        landmark = next(node for node in result["nodes"] if node["node_id"] == "UnitZone::start")
        checkpoint = next(
            node for node in result["nodes"] if node["node_id"] == "UnitZone::checkpoint::obj1"
        )
        self.assertEqual(landmark["node_type"], "custom_place")
        self.assertEqual(landmark["stage"], "UnitHomeStage")
        self.assertEqual(
            (checkpoint["x"], checkpoint["y"], checkpoint["z"]),
            ("104.000", "13.000", "198.000"),
        )
        self.assertEqual(result["moons"][0]["stage"], "UnitHomeStage")
        self.assertEqual(result["moons"][0]["runtime_shine_key"], "UnitZone:1:2")
        self.assertEqual(result["routes"][0]["area_scope"], "overworld")

    def test_repairs_old_pocket_merge_and_is_idempotent(self) -> None:
        rooms = [
            {
                "stage": "UnitHomeStage",
                "kingdom": "Unit Kingdom",
                "stage_display_name": "Unit Home",
                "stage_start_node_id": "UnitHomeStage::start",
                "area_scope": "overworld",
                "has_shine_db_moons": "true",
                "has_raw_stage_data_points": "false",
                "active_entrance_link_count": "0",
                "excluded_entrance_link_count": "0",
                "notes": "",
            }
        ]
        nodes = [
            {
                "node_id": "UnitHomeStage::start",
                "kingdom": "Unit Kingdom",
                "stage": "UnitHomeStage",
                "stage_display_name": "Unit Home",
                "area_scope": "overworld",
                "node_type": "stage_start",
                "source": "generated",
                "x": "",
                "y": "",
                "z": "",
            }
        ]
        moons = [
            {
                "moon_id": "UnitZone:1:2",
                "runtime_shine_key": "UnitZone:1:2",
                "kingdom": "Unit Kingdom",
                "stage": "UnitHomeStage",
                "stage_display_name": "Unit Home",
                "area_scope": "overworld",
                "from_node_id": "UnitHomeStage::start",
                "placement_x": "2",
                "placement_y": "3",
                "placement_z": "4",
            }
        ]

        first = flatten_embedded_zones(nodes, [], moons, rooms, [], [mapping()], {})
        first_snapshot = copy.deepcopy(first)
        second = flatten_embedded_zones(
            first["nodes"],
            first["routes"],
            first["moons"],
            first["rooms"],
            first["scenario_metadata"],
            [mapping()],
            {},
        )

        self.assertEqual(first["moons"][0]["from_node_id"], "UnitZone::start")
        self.assertEqual(
            (
                first["moons"][0]["placement_x"],
                first["moons"][0]["placement_y"],
                first["moons"][0]["placement_z"],
            ),
            ("104.000", "13.000", "198.000"),
        )
        self.assertEqual(second["merged"], {})
        for key in ("nodes", "routes", "moons", "rooms", "scenario_metadata"):
            self.assertEqual(second[key], first_snapshot[key])


if __name__ == "__main__":
    unittest.main()
