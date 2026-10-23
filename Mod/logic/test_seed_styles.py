from __future__ import annotations

import unittest

import randomizer_logic as rl


class SeedStyleTests(unittest.TestCase):
    def test_standard_preserves_all_standard_axes(self) -> None:
        settings = rl.LogicSettings(seed=12345)
        self.assertEqual(settings.resolved_unlock_timing(), "standard")
        self.assertEqual(settings.resolved_route_breadth(), "standard")
        self.assertEqual(settings.resolved_progression_depth(), "standard")

    def test_named_style_profiles(self) -> None:
        rush = rl.LogicSettings(seed=1, seed_style="ability-rush")
        self.assertEqual(
            (
                rush.resolved_unlock_timing(),
                rush.resolved_route_breadth(),
                rush.resolved_progression_depth(),
            ),
            ("early", "standard", "standard"),
        )

        focused = rl.LogicSettings(seed=1, seed_style="focused-route")
        self.assertEqual(
            (
                focused.resolved_unlock_timing(),
                focused.resolved_route_breadth(),
                focused.resolved_progression_depth(),
            ),
            ("standard", "focused", "shallow"),
        )

        explorer = rl.LogicSettings(seed=1, seed_style="explorer")
        self.assertEqual(
            (
                explorer.resolved_unlock_timing(),
                explorer.resolved_route_breadth(),
                explorer.resolved_progression_depth(),
            ),
            ("standard", "expansive", "deep"),
        )

    def test_mystery_is_stable_for_known_seed(self) -> None:
        settings = rl.LogicSettings(seed=12345, seed_style="mystery")
        self.assertEqual(
            (
                settings.resolved_unlock_timing(),
                settings.resolved_route_breadth(),
                settings.resolved_progression_depth(),
            ),
            ("standard", "focused", "standard"),
        )


if __name__ == "__main__":
    unittest.main()
