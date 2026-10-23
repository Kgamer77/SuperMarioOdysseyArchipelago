"""Guards for the host-side reader of include/Randomizer/EntranceFallbacks.h.

The header is hand-authored C++ and this module parses it, so the risk worth
testing is drift: a new row shape, a new Mode_, or a scenario expression the
regex silently skips would quietly downgrade entrances back to the old
"assume an Odyssey warp" behaviour without anything failing loudly.
"""

from __future__ import annotations

import re
import unittest

import entrance_fallbacks as ef


def table_body() -> str:
    text = ef.HEADER_PATH.read_text(encoding="utf-8")
    match = ef._TABLE_RE.search(text)
    assert match, "sHomeFallbacks table not found"
    body = ef._BLOCK_COMMENT_RE.sub("", match.group("body"))
    return ef._LINE_COMMENT_RE.sub("", body)


class EntranceFallbackParserTests(unittest.TestCase):
    def test_header_is_present_and_parses(self):
        rows = ef.load_home_fallbacks()
        self.assertTrue(rows, "no fallback rows parsed - header missing or unreadable")

    def test_every_table_row_is_parsed(self):
        """The count of parsed rows must equal the number of fallback calls in
        the table. A row shape the regex cannot read would otherwise be dropped
        silently, and that entrance would fall back to the Odyssey in logic
        while the runtime still places the player at it."""
        calls = re.findall(r"\b\w*[fF]allback\s*\(", table_body())
        self.assertEqual(len(calls), len(ef.load_home_fallbacks()))

    def test_all_modes_are_known(self):
        """A mode added to the header that this module does not classify would
        default to 'warps to Odyssey' by omission. Fail instead."""
        known = ef.MODES_PLACING_PLAYER_AT_ENTRANCE | {"Mode_Odyssey"}
        for row in ef.load_home_fallbacks():
            self.assertIn(row.mode, known, f"unclassified mode on {row.stage}/{row.change_id}")

    def test_scenario_masks_are_in_range(self):
        for row in ef.load_home_fallbacks():
            self.assertEqual(row.scenario_mask & ~ef.SCENARIO_ALL, 0)


class ScenarioMaskTests(unittest.TestCase):
    def test_any_and_all(self):
        self.assertEqual(ef._evaluate_scenario_mask("Scenario_Any"), 0)
        self.assertEqual(ef._evaluate_scenario_mask("Scenario_All"), ef.SCENARIO_ALL)

    def test_single_and_union(self):
        self.assertEqual(ef._evaluate_scenario_mask("Scenario_1"), 0b1)
        self.assertEqual(ef._evaluate_scenario_mask("Scenario_1 | Scenario_3"), 0b101)

    def test_subtraction_form(self):
        mask = ef._evaluate_scenario_mask("Scenario_All & ~(Scenario_4)")
        self.assertFalse(ef.scenario_mask_contains(mask, 4))
        self.assertTrue(ef.scenario_mask_contains(mask, 5))

        mask = ef._evaluate_scenario_mask("Scenario_All & ~(Scenario_2 | Scenario_3)")
        self.assertFalse(ef.scenario_mask_contains(mask, 2))
        self.assertFalse(ef.scenario_mask_contains(mask, 3))
        self.assertTrue(ef.scenario_mask_contains(mask, 1))

    def test_scenario_zero_and_overflow_are_not_members(self):
        self.assertFalse(ef.scenario_mask_contains(ef.SCENARIO_ALL, 0))
        self.assertFalse(ef.scenario_mask_contains(ef.SCENARIO_ALL, 16))

    def test_rejects_unmodelled_expression(self):
        with self.assertRaises(ef.FallbackParseError):
            ef._evaluate_scenario_mask("someFunction(Scenario_1)")


class ArrivalPlacementTests(unittest.TestCase):
    def test_odyssey_row_does_not_place_player_at_entrance(self):
        # CityWorldHomeStage/gunsyu is authored Mode_Odyssey.
        self.assertFalse(
            ef.arrival_places_player_at_entrance("CityWorldHomeStage", "gunsyu", 1)
        )

    def test_pipe_row_places_player_at_entrance(self):
        # CapWorldHomeStage/rollingstart is Mode_Moon_Pipe for every scenario but 4.
        self.assertTrue(
            ef.arrival_places_player_at_entrance("CapWorldHomeStage", "rollingstart", 1)
        )

    def test_coordinate_row_places_player_at_entrance(self):
        self.assertTrue(
            ef.arrival_places_player_at_entrance("ForestWorldHomeStage", "EXCloud", 1)
        )

    def test_row_does_not_apply_outside_its_scenario_mask(self):
        # rollingstart's mask is Scenario_All & ~(Scenario_4), so scenario 4 has
        # no matching row and resolves as Odyssey.
        self.assertFalse(
            ef.arrival_places_player_at_entrance("CapWorldHomeStage", "rollingstart", 4)
        )

    def test_unauthored_entrance_resolves_as_odyssey(self):
        self.assertFalse(ef.arrival_places_player_at_entrance("SandWorldHomeStage", "aaa", 1))
        self.assertFalse(ef.arrival_places_player_at_entrance("NoSuchStage", "nope", 1))

    def test_empty_arguments_are_safe(self):
        self.assertFalse(ef.arrival_places_player_at_entrance("", "", 1))


class DarkerSideOdysseyCoordinateTests(unittest.TestCase):
    """The Darker Side Odyssey fallback used to be the stage's own default
    start, which is exactly where the ship parks - so every arrival that fell
    back to it spawned Mario inside the Odyssey."""

    SHIP_SPAWN = (-7150.0, -1028.001, 1500.0)

    def test_darker_side_no_longer_uses_the_ship_spawn(self):
        text = ef.HEADER_PATH.read_text(encoding="utf-8")
        match = re.search(
            r'odyssey\("Special2WorldHomeStage",\s*'
            r"(-?[\d.]+)f,\s*(-?[\d.]+)f,\s*(-?[\d.]+)f\)",
            text,
        )
        self.assertIsNotNone(match, "Darker Side odyssey fallback row not found")
        coords = tuple(float(match.group(i)) for i in (1, 2, 3))
        self.assertNotEqual(coords, self.SHIP_SPAWN)


if __name__ == "__main__":
    unittest.main()
