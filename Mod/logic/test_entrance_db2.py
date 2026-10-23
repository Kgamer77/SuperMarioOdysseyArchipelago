"""Regression tests for the curated EntranceDB2 generator contract."""

from __future__ import annotations

import csv
import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GENERATOR_PATH = ROOT / "tools" / "generate_entrance_db2.py"
SPEC = importlib.util.spec_from_file_location("generate_entrance_db2", GENERATOR_PATH)
assert SPEC and SPEC.loader
generate_entrance_db2 = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = generate_entrance_db2
SPEC.loader.exec_module(generate_entrance_db2)


def endpoint(
    stage: str,
    change_id: str,
    target_stage: str,
    *,
    exit_only: bool = False,
) -> object:
    return generate_entrance_db2.Endpoint(
        stage=stage,
        change_id=change_id,
        target_stage=target_stage,
        unit_config_name="DokanStageChange",
        is_exit_only=exit_only,
        is_entry_only=not bool(target_stage),
        is_return_prev_stage=False,
        scenario_mask=1,
        x=0.0,
        y=0.0,
        z=0.0,
        listed_stage=False,
        main_stage=False,
    )


class EndpointMatchingTests(unittest.TestCase):
    def test_exact_stage_id_fallback_handles_developer_target(self) -> None:
        endpoints = [
            endpoint("SnowWorldTownStage", "RaceEntrance", "SnowWorldLobby000Stage"),
            endpoint("SnowWorldTownStage", "RaceTrackExit", "TestOkamoto027Stage", exit_only=True),
        ]

        index, status = generate_entrance_db2.match_legacy_endpoint(
            [0],
            [0, 1],
            endpoints,
            "RaceTrackExit",
            "home_out",
            "SnowWorldLobby000Stage",
            "SnowWorldTownStage",
        )

        self.assertEqual(index, 1)
        self.assertIsNone(status)

    def test_normal_area_target_match_has_priority(self) -> None:
        endpoints = [
            endpoint("SnowWorldTownStage", "RaceTrackExit", "SnowWorldLobby000Stage"),
            endpoint("SnowWorldTownStage", "RaceTrackExit", "TestOkamoto027Stage", exit_only=True),
        ]

        index, status = generate_entrance_db2.match_legacy_endpoint(
            [0, 1],
            [0, 1],
            endpoints,
            "RaceTrackExit",
            "home_out",
            "SnowWorldLobby000Stage",
            "SnowWorldTownStage",
        )

        self.assertEqual(index, 0)
        self.assertIsNone(status)

    def test_fallback_does_not_accept_another_stage(self) -> None:
        endpoints = [
            endpoint("SnowWorldLobby000Stage", "RaceTrackExit", "SnowWorldTownStage"),
        ]

        index, status = generate_entrance_db2.match_legacy_endpoint(
            [],
            [0],
            endpoints,
            "RaceTrackExit",
            "home_out",
            "SnowWorldLobby000Stage",
            "SnowWorldTownStage",
        )

        self.assertEqual(index, -1)
        self.assertEqual(status, "missing")


class GeneratorValidationTests(unittest.TestCase):
    def test_alignment_validation_reports_missing_rows(self) -> None:
        fields = [
            "link_index",
            "home_stage",
            "home_entrance_in",
            "home_entrance_out",
            "sub_stage",
            "sub_entrance_in",
            "sub_entrance_out",
            "status",
        ]
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "entrance_links.csv"
            with source.open("w", newline="", encoding="utf-8") as handle:
                writer = csv.DictWriter(handle, fieldnames=fields)
                writer.writeheader()
                writer.writerow(
                    {
                        "link_index": "0",
                        "home_stage": "Home",
                        "home_entrance_in": "in",
                        "home_entrance_out": "out",
                        "sub_stage": "Sub",
                        "sub_entrance_in": "near",
                        "sub_entrance_out": "far",
                        "status": "active",
                    }
                )

            with self.assertRaises(SystemExit) as raised:
                generate_entrance_db2.validate_legacy_alignment([], source)

        self.assertIn("row 0: runtime row missing", str(raised.exception))

    def test_alignment_validation_reports_field_drift(self) -> None:
        fields = [
            "link_index",
            "home_stage",
            "home_entrance_in",
            "home_entrance_out",
            "sub_stage",
            "sub_entrance_in",
            "sub_entrance_out",
            "status",
        ]
        row = {
            "home_stage": "Home",
            "home_entrance_in": "in",
            "home_entrance_out": "out",
            "sub_stage": "Sub",
            "sub_entrance_in": "near",
            "sub_entrance_out": "far",
            "status": "active",
        }
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "entrance_links.csv"
            with source.open("w", newline="", encoding="utf-8") as handle:
                writer = csv.DictWriter(handle, fieldnames=fields)
                writer.writeheader()
                writer.writerow({"link_index": "0", **row, "status": "vanilla"})

            with self.assertRaises(SystemExit) as raised:
                generate_entrance_db2.validate_legacy_alignment([row], source)

        self.assertIn("row 0 status", str(raised.exception))

    def test_stage_archive_validation_rejects_partial_data(self) -> None:
        rows = [
            {"home_stage": "Home", "sub_stage": "Sub"},
        ]
        with tempfile.TemporaryDirectory() as temp:
            stage_dir = Path(temp)
            with self.assertRaises(SystemExit) as raised:
                generate_entrance_db2.validate_stage_archives(stage_dir, rows)

        self.assertIn("Sub", str(raised.exception))


class GeneratedHeaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.header_lines = (ROOT / "include" / "EntranceDB2.h").read_text(
            encoding="utf-8", errors="replace"
        ).splitlines()

    def _line_starting_with(self, prefix: str) -> str:
        return next(line for line in self.header_lines if line.startswith(prefix))

    def test_ice_cave_primary_and_alias_are_restored(self) -> None:
        primary = self._line_starting_with(
            '        { "SandWorldHomeStage", "arijigoku", "arijigoku2", '
            '"SandWorldPressExStage", "arijigoku", "arijigoku2",'
        )
        alias = self._line_starting_with(
            '        { "SandWorldHomeStage", "arijigoku", "arijigoku1", '
            '"SandWorldPressExStage", "arijigoku", "arijigoku1",'
        )
        self.assertIn("RandoStatus_Active", primary)
        self.assertIn("RandoStatus_Vanilla", alias)

    def test_shiveria_race_exit_has_authored_endpoint(self) -> None:
        endpoint = self._line_starting_with(
            '        { "SnowWorldTownStage", "RaceTrackExit", '
            '"TestOkamoto027Stage", "DokanStageChange"'
        )
        self.assertIn("-950.0f, -4800.0f, 7600.0f", endpoint)

        link = self._line_starting_with(
            '        { "SnowWorldTownStage", "RaceEntrance", "RaceTrackExit", '
            '"SnowWorldLobby000Stage"'
        )
        self.assertIn("DoorSideBehavior_ReturnSpawn", link)
        self.assertNotIn("home_out=missing", link)


if __name__ == "__main__":
    unittest.main(verbosity=2)
