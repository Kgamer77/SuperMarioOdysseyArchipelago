from __future__ import annotations

import unittest

from apply_moon_scenario_report import Change, update_row
from audit_moon_scenarios import (
    ShineInfoEvidence,
    StageMapEvidence,
    decode_progress_bit_flag,
    effective_is_moon_rock,
    make_record,
    parse_runtime_shine_key,
    stage_map_relation,
)
from generate_logic_templates import scenarios_from_progress_bit_flag


class MoonScenarioAuditTests(unittest.TestCase):
    def test_decodes_progress_bits_to_one_based_scenarios(self) -> None:
        self.assertEqual(decode_progress_bit_flag(8), [4])
        self.assertEqual(decode_progress_bit_flag(63), [1, 2, 3, 4, 5, 6])
        self.assertEqual(decode_progress_bit_flag(0), [])
        self.assertEqual(scenarios_from_progress_bit_flag(975), "1;2;3;4;7;8;9;10")

    def test_parses_runtime_key(self) -> None:
        self.assertEqual(
            parse_runtime_shine_key("SnowWorldIcicleZone:412:699"),
            ("SnowWorldIcicleZone", "obj412", 699),
        )
        self.assertIsNone(parse_runtime_shine_key("not-a-key"))

    def test_game_derives_scenario_only_moon_rock(self) -> None:
        self.assertTrue(effective_is_moon_rock(False, 8, 4, "Snow"))
        self.assertFalse(effective_is_moon_rock(False, 12, 4, "Snow"))
        self.assertFalse(effective_is_moon_rock(True, 8, 4, "Peach"))

    def test_stage_map_relation(self) -> None:
        self.assertEqual(stage_map_relation([4], [4]), "exact")
        self.assertEqual(stage_map_relation([4], [1, 2, 3, 4]), "superset")
        self.assertEqual(stage_map_relation([4], [5]), "disjoint")
        self.assertEqual(stage_map_relation([4], []), "unavailable")

    def test_incorrect_record_keeps_reusable_patch(self) -> None:
        row = {
            "moon_id": "SnowWorldIcicleZone:412:699",
            "runtime_shine_key": "SnowWorldIcicleZone:412:699",
            "moon_name": "Stacked-Up Ice Climb",
            "kingdom": "Snow Kingdom",
            "stage": "SnowWorldTownStage",
            "shine_info_available_scenarios": "1;2;3;4;5;6",
            "shine_info_is_moon_rock": "false",
            "shine_info_progress_bit_flag": "8",
            "placement_stage_map_indices": "3",
            "placement_unit_config_names": "ShineAddByRockAddHeight",
        }
        shine = ShineInfoEvidence(
            source_file="ShineList_SnowWorldHomeStage.byml",
            world_home_stage="SnowWorldHomeStage",
            stage_name="SnowWorldIcicleZone",
            obj_id="obj412",
            uid=699,
            progress_bit_flag=8,
            is_moon_rock=False,
            main_scenario_no=-1,
            hint_idx=38,
            object_name="StageSwitch",
            scenario_name="Stacked-Up Ice Climb",
        )
        placement = StageMapEvidence(
            scenario_indices={3},
            unit_config_names={"ShineAddByRockAddHeight"},
            layer_config_names={"B"},
            scenario_no_values={"-1"},
            placement_count=1,
        )

        record = make_record(
            2,
            row,
            [shine],
            {699: [1, 2, 3, 4, 5, 6]},
            {
                "SnowWorldHomeStage": {
                    "world_name": "Snow",
                    "moon_rock_scenario": 4,
                }
            },
            {("SnowWorldIcicleZone", "obj412"): placement},
        )

        self.assertEqual(record["status"], "incorrect")
        self.assertEqual(
            record["authoritative"]["available_scenarios"], [4]
        )
        self.assertEqual(record["cross_checks"]["stage_map_relation"], "exact")
        self.assertEqual(
            record["proposed_change"]["replacement_fields"],
            {
                "shine_info_available_scenarios": "4",
                "shine_info_is_moon_rock": "true",
            },
        )

    def test_report_apply_changes_only_selected_fields(self) -> None:
        row = {
            "moon_id": "SkyWorldTreasureStage:95:999",
            "from_node_id": "SkyWorldTreasureStage::entrance::shop_dress::sub_near",
            "shine_info_available_scenarios": "",
            "requirement_expr": "free",
        }
        conflicts: list[str] = []
        changed = update_row(
            row,
            Change(
                moon_id="SkyWorldTreasureStage:95:999",
                expected_fields={"shine_info_available_scenarios": ""},
                replacement_fields={
                    "shine_info_available_scenarios": "2;3;4;5;6"
                },
            ),
            __import__("pathlib").Path("moon_access.csv"),
            conflicts,
        )

        self.assertTrue(changed)
        self.assertEqual(conflicts, [])
        self.assertEqual(
            row["from_node_id"],
            "SkyWorldTreasureStage::entrance::shop_dress::sub_near",
        )
        self.assertEqual(row["requirement_expr"], "free")
        self.assertEqual(row["shine_info_available_scenarios"], "2;3;4;5;6")


if __name__ == "__main__":
    unittest.main()
