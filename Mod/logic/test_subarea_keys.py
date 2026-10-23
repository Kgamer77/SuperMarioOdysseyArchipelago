"""Pins the Subarea Key classification and the mode/host budget arithmetic.

The classification itself lives in the generated EntranceDB2.h, so these tests
are really guarding the join between that table and the logic templates: if the
two ever fall out of index alignment, or a regenerated header loses a type, the
gate would silently start locking (or freeing) the wrong doors.
"""

from __future__ import annotations

import sys
import unittest
import csv
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import subarea_keys


class SubareaKeyClassificationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.db = subarea_keys.SubareaKeyDB()

    def test_every_link_is_classified(self):
        unclassified = [link for link in self.db.links if link.subarea_type == "None"]
        self.assertEqual(unclassified, [], "regenerate EntranceDB2.h")

    def test_every_generated_row_is_index_aligned_with_logic_templates(self):
        generated = subarea_keys._read_generated_links()
        with subarea_keys.ENTRANCE_LINKS.open(newline="", encoding="utf-8-sig") as handle:
            template = list(csv.DictReader(handle))

        self.assertEqual(len(generated), len(template))
        for index, (generated_row, logic_row) in enumerate(zip(generated, template)):
            expected = tuple(
                (logic_row.get(field, "") or "").strip()
                for field in (
                    "home_stage",
                    "home_entrance_in",
                    "home_entrance_out",
                    "sub_stage",
                    "sub_entrance_in",
                    "sub_entrance_out",
                )
            ) + ((logic_row.get("status", "") or "unknown").strip().lower(),)
            self.assertEqual(generated_row[:7], expected, f"row {index}")

        cap = [l for l in self.db.links if l.home_entrance_in == "PushBlockExStageEnt"]
        self.assertEqual(len(cap), 1)
        self.assertEqual(cap[0].home_stage, "CapWorldHomeStage")
        self.assertEqual(cap[0].kingdom, "Cap Kingdom")

    def test_cap_kingdom_has_doorways_not_open_passages(self):
        # Cap's three sub-area entrances are hat-shaped arches: ChangeStageArea
        # triggers with a DoorAreaChange actor sitting on them.
        self.assertTrue(self.db.kingdom_has_type("Cap Kingdom", "Doorway"))
        self.assertFalse(self.db.kingdom_has_type("Cap Kingdom", "OpenPassage"))
        self.assertFalse(self.db.kingdom_has_type("Cap Kingdom", "WarpPipe"))

    def test_moon_pipes_are_the_post_moon_rock_entrances(self):
        moon_pipes = {
            (l.home_stage, l.home_entrance_in)
            for l in self.db.links
            if l.subarea_type == "MoonPipe"
        }
        for expected in (
            ("CapWorldHomeStage", "rollingstart"),
            ("CityWorldHomeStage", "gragra"),
            ("ForestWorldHomeStage", "KillerRoad"),
        ):
            self.assertIn(expected, moon_pipes)

    def test_story_routes_and_nested_doors_are_never_gated(self):
        for link in self.db.links:
            if link.status == "vanilla" or link.area_scope != "overworld":
                self.assertFalse(link.is_gated, f"{link.home_stage}/{link.home_entrance_in}")

    def test_excluded_links_are_never_gated(self):
        # Notably the four Wooded beanstalks, which are reference-only rows.
        excluded = [l for l in self.db.links if l.status == "excluded"]
        self.assertTrue(excluded)
        for link in excluded:
            self.assertFalse(link.is_gated)

    def test_spark_pylons_and_paintings_stay_free(self):
        for link in self.db.links:
            if link.subarea_type in ("SparkPylon", "Painting"):
                self.assertFalse(link.is_gated)


class SubareaKeyBudgetTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.db = subarea_keys.SubareaKeyDB()

    def test_key_counts_per_mode(self):
        self.assertEqual(self.db.key_count(subarea_keys.MODE_UNLOCKED), 0)
        # Six kinds, not five: Wooded, Snow and Bowser's each gate a beanstalk
        # (see SUBAREA_TYPE_OVERRIDES). Snow's converted from an open passage
        # rather than adding one, so the per-kingdom total moves 46 -> 48.
        self.assertEqual(self.db.key_count(subarea_keys.MODE_PER_TYPE), 6)
        self.assertEqual(self.db.key_count(subarea_keys.MODE_PER_KINGDOM), 16)
        self.assertEqual(self.db.key_count(subarea_keys.MODE_PER_KINGDOM_AND_TYPE), 48)

    def test_only_both_hosts_can_carry_the_finest_mode(self):
        finest = subarea_keys.MODE_PER_KINGDOM_AND_TYPE
        self.assertFalse(self.db.host_fits(finest, "cap"))
        self.assertFalse(self.db.host_fits(finest, "moon"))
        self.assertTrue(self.db.host_fits(finest, "cap_and_moon"))

    def test_coarser_modes_fit_every_host(self):
        for mode in (subarea_keys.MODE_PER_TYPE, subarea_keys.MODE_PER_KINGDOM):
            for host in subarea_keys.HOST_CAPACITY:
                self.assertTrue(self.db.host_fits(mode, host), f"{mode}/{host}")

    def test_coarser_modes_collapse_pairs_onto_one_slot(self):
        # Per-kingdom: every kind in a kingdom shares that kingdom's slot.
        per_kingdom = subarea_keys.MODE_PER_KINGDOM
        sand_pipe = self.db.find_key_slot(per_kingdom, "Sand Kingdom", "WarpPipe")
        sand_door = self.db.find_key_slot(per_kingdom, "Sand Kingdom", "Doorway")
        self.assertGreaterEqual(sand_pipe, 0)
        self.assertEqual(sand_pipe, sand_door)

        # Per-type: the same kind in different kingdoms shares one slot.
        per_type = subarea_keys.MODE_PER_TYPE
        sand = self.db.find_key_slot(per_type, "Sand Kingdom", "WarpPipe")
        metro = self.db.find_key_slot(per_type, "Metro Kingdom", "WarpPipe")
        self.assertGreaterEqual(sand, 0)
        self.assertEqual(sand, metro)

        # Finest: the two are distinct keys.
        finest = subarea_keys.MODE_PER_KINGDOM_AND_TYPE
        self.assertNotEqual(
            self.db.find_key_slot(finest, "Sand Kingdom", "WarpPipe"),
            self.db.find_key_slot(finest, "Sand Kingdom", "Doorway"),
        )

    def test_pairs_without_gateable_doors_have_no_slot(self):
        finest = subarea_keys.MODE_PER_KINGDOM_AND_TYPE
        # Cap has no plain pipes and Cloud has no doors at all.
        self.assertEqual(self.db.find_key_slot(finest, "Cap Kingdom", "WarpPipe"), -1)
        self.assertEqual(self.db.find_key_slot(finest, "Cloud Kingdom", "Doorway"), -1)

    def test_slot_for_link_uses_the_home_side(self):
        finest = subarea_keys.MODE_PER_KINGDOM_AND_TYPE
        for link in self.db.links:
            slot = self.db.slot_for_link(finest, link.link_index)
            if not link.is_gated:
                self.assertEqual(slot, -1)
                continue
            self.assertEqual(
                slot, self.db.find_key_slot(finest, link.kingdom, link.subarea_type)
            )

    def test_progression_search_only_considers_keys_on_the_open_frontier(self):
        finest = subarea_keys.MODE_PER_KINGDOM_AND_TYPE
        cap_slots = self.db.slots_relevant_to_kingdoms(finest, {"Cap Kingdom"})
        self.assertEqual(len(cap_slots), 2)
        self.assertLess(len(cap_slots), self.db.key_count(finest))

        all_slots = self.db.slots_relevant_to_kingdoms(
            finest, set(self.db.kingdoms())
        )
        self.assertEqual(len(all_slots), self.db.key_count(finest))

    def test_frontier_filter_preserves_collapsed_key_modes(self):
        per_type = subarea_keys.MODE_PER_TYPE
        sand_and_metro = self.db.slots_relevant_to_kingdoms(
            per_type, {"Sand Kingdom", "Metro Kingdom"}
        )
        expected = {
            self.db.find_key_slot(per_type, "Sand Kingdom", subarea_type)
            for subarea_type in self.db.types_by_kingdom["Sand Kingdom"]
        }
        self.assertEqual(sand_and_metro, expected)

    def test_tracker_scopes_match_key_slot_shapes(self):
        per_type = self.db.key_slots(subarea_keys.MODE_PER_TYPE)
        self.assertTrue(per_type)
        self.assertTrue(all(kingdom is None and kind is not None
                            for kingdom, kind in per_type))
        self.assertEqual(len({kind for _, kind in per_type}), len(per_type))

        per_kingdom = self.db.key_slots(subarea_keys.MODE_PER_KINGDOM)
        self.assertTrue(all(kingdom is not None and kind is None
                            for kingdom, kind in per_kingdom))

        finest = self.db.key_slots(subarea_keys.MODE_PER_KINGDOM_AND_TYPE)
        self.assertTrue(all(kingdom is not None and kind is not None
                            for kingdom, kind in finest))
        self.assertEqual(
            set(finest),
            {
                (kingdom, kind)
                for kingdom in self.db.kingdoms()
                for kind in subarea_keys.GATED_TYPES
                if self.db.kingdom_has_type(kingdom, kind)
            },
        )


if __name__ == "__main__":
    unittest.main()
