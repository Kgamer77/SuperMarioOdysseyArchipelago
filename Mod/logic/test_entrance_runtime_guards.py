from __future__ import annotations

import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
PREFIX = "Randomized_"
MAX_CHANGE_STAGE_INFO_STRING = 0x80
PAINTING_FALLBACK_CHANGE_IDS = ("Come", "Go")

ENDPOINT_RE = re.compile(
    r'\{\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)",\s*'
    r"\d+,\s*0x[0-9A-Fa-f]+u,"
)
ENDPOINT_COUNT_RE = re.compile(r"static constexpr s16 sEndpointCount = (\d+);")


def entrance_db_text() -> str:
    return (ROOT / "include" / "EntranceDB2.h").read_text(encoding="utf-8")


def entrance_hooks_text() -> str:
    return (ROOT / "src" / "Randomizer" / "EntranceHooks.cpp").read_text(
        encoding="utf-8"
    )


def entrance_fallbacks_text() -> str:
    return (ROOT / "include" / "Randomizer" / "EntranceFallbacks.h").read_text(
        encoding="utf-8"
    )


def moon_rando_text() -> str:
    return (ROOT / "src" / "Randomizer" / "MoonRando.cpp").read_text(
        encoding="utf-8"
    )


def fake_pipe_patcher_text() -> str:
    return (ROOT / "tools" / "patch_fake_exit_pipes.py").read_text(encoding="utf-8")


def endpoint_change_ids() -> list[str]:
    return [match.group(2) for match in ENDPOINT_RE.finditer(entrance_db_text())]


def routed_change_ids() -> list[str]:
    return endpoint_change_ids() + list(PAINTING_FALLBACK_CHANGE_IDS)


class EntranceRuntimeGuardTests(unittest.TestCase):
    def test_endpoint_parser_matches_generated_count(self) -> None:
        text = entrance_db_text()
        count_match = ENDPOINT_COUNT_RE.search(text)
        self.assertIsNotNone(count_match)
        self.assertEqual(len(endpoint_change_ids()), int(count_match.group(1)))

    def test_randomized_prefix_is_reserved_and_fits_fixed_string(self) -> None:
        change_ids = routed_change_ids()
        self.assertGreater(len(change_ids), 400)

        prefixed = [change_id for change_id in change_ids if change_id.startswith(PREFIX)]
        self.assertEqual(prefixed, [])

        too_long = [
            change_id
            for change_id in change_ids
            if len(PREFIX) + len(change_id) >= MAX_CHANGE_STAGE_INFO_STRING
        ]
        self.assertEqual(too_long, [])

    def test_synthetic_player_start_keeps_backing_placement_info(self) -> None:
        text = entrance_hooks_text()

        self.assertIn("placementInfoFromPlayerStartInfo(backingInfo)", text)
        self.assertIn("ctor(sBuffer, backingPlacementInfo", text)
        self.assertNotIn("ctor(sBuffer, nullptr", text)

    def test_fake_pipe_patch_preserves_pipe_start_behavior(self) -> None:
        text = fake_pipe_patcher_text()
        make_pipe = re.search(
            r"def make_fake_exit_pipe\(.*?\n\n\ndef add_pipe_to_scenario",
            text,
            re.DOTALL,
        )
        source_path = re.search(
            r"def source_path\(.*?\n\n\ndef template_source_path",
            text,
            re.DOTALL,
        )
        template_source_path = re.search(
            r"def template_source_path\(.*?\n\n\ndef object_id",
            text,
            re.DOTALL,
        )
        self.assertIsNotNone(make_pipe)
        self.assertIsNotNone(source_path)
        self.assertIsNotNone(template_source_path)
        self.assertIn("AREA_PIPE_SPECS", text)
        self.assertIn('"RexPoppunEx"', text)
        self.assertIn('"EXCloud"', text)
        self.assertIn('FAKE_PIPE_MODEL = "Dokan"', text)
        self.assertIn('"--prefer-extraction"', text)
        self.assertIn("deprecated compatibility flag", text)
        self.assertIn("if override.exists():\n        return override", source_path.group(0))
        self.assertIn("if output.exists():\n        return output", template_source_path.group(0))
        self.assertNotIn("if prefer_extraction and extracted.exists()", source_path.group(0))
        self.assertIn("def is_rando_fake_pipe_clone", text)
        self.assertIn("include_rando_objects=False", text)
        self.assertIn('node["Links"] = oead.byml.Hash()', make_pipe.group(0))
        self.assertIn('node["IsExitOnly"] = True', make_pipe.group(0))
        self.assertIn('node["ModelName"] = FAKE_PIPE_MODEL', make_pipe.group(0))
        self.assertIn('node["PlacementFileName"] = template.stage', make_pipe.group(0))

    def test_fake_pipe_arrivals_use_hidden_actor_or_odyssey_fallback(self) -> None:
        text = entrance_hooks_text()
        fallbacks = entrance_fallbacks_text()
        moon_hooks = (ROOT / "src" / "Randomizer" / "MoonHooks.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn('#include "Randomizer/EntranceFallbacks.h"', text)
        self.assertIn("namespace EntranceFallbacks", fallbacks)
        self.assertIn("Mode_Vanilla", fallbacks)
        self.assertIn("Mode_Coordinates", fallbacks)
        self.assertIn("Mode_Pipe", fallbacks)
        self.assertIn("Mode_Odyssey", fallbacks)
        self.assertIn("coordinateFallback(", fallbacks)
        self.assertIn("odysseyForScenario(", fallbacks)
        self.assertIn("static constexpr HomeFallback sHomeFallbacks[]", fallbacks)
        self.assertIn("static constexpr OdysseyFallback sOdysseyFallbacks[]", fallbacks)
        self.assertIn("// Cascade Kingdom", fallbacks)
        self.assertIn("Candidate home loading zones not currently overridden", fallbacks)
        self.assertIn('fallback("CityWorldHomeStage", "shop_corect", AnyScenario, Mode_Odyssey)', fallbacks)
        self.assertIn('fallback("PeachWorldHomeStage", "BossKnuckleA", AnyScenario, Mode_Odyssey)', fallbacks)
        self.assertIn('fallback("WaterfallWorldHomeStage", "RexPoppunEx", AnyScenario, Mode_Pipe)', fallbacks)
        self.assertIn('fallback("ForestWorldHomeStage", "EXCloud", AnyScenario, Mode_Pipe)', fallbacks)
        self.assertIn('fallback("CityWorldHomeStage", "Bonus", AnyScenario, Mode_Odyssey)', fallbacks)
        self.assertIn('odyssey("WaterfallWorldHomeStage"', fallbacks)
        self.assertNotIn("static constexpr HomeFallbackEntry sHomeFallbacks[]", text)
        self.assertIn("hasFakeExitPipe(dest.homeStage, arrival)", text)
        self.assertIn("HomeFallback_Vanilla", text)
        self.assertIn("HomeFallback_Coordinates", text)
        self.assertIn("HomeFallback_Pipe", text)
        self.assertIn("HomeFallback_Odyssey", text)
        self.assertIn("bool useFakeExitPipe = false;", text)
        self.assertIn("bool useCoordinateFallback = false;", text)
        self.assertIn("bool useOdysseyFallback = false;", text)
        self.assertIn("findHomeFallbackEntry(dest.homeStage, arrival, scenario)", text)
        self.assertIn("if (useFakeExitPipe) {", text)
        self.assertIn("armFakeExitPipeArrival(dest.homeStage, arrival, scenario)", text)
        self.assertIn("sFallbackToOdysseyOnMissingStart = true;", text)
        self.assertIn("kOdysseyEntranceName", text)
        self.assertIn("ER fake pipe missing: id=%s -> Odyssey coords", text)
        self.assertIn("shouldHideRandoFakeExitPipeActor", moon_hooks)
        self.assertIn("actor->makeActorDead()", moon_hooks)
        self.assertIn("armPendingArrival(patched.mChangeStageId.cstr(), endpoint)", text)
        self.assertIn('std::strcmp(model, "Dokan") != 0', text)

    def test_home_arrivals_never_force_endpoint_scenario(self) -> None:
        text = entrance_hooks_text()
        scenario_text = (ROOT / "src" / "Randomizer" / "ScenarioPatcher.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn("ER home unavailable fallback %s:", text)
        self.assertIn("arrival = kOdysseyEntranceName;", text)
        self.assertIn("armOdysseyFallbackArrival(dest.homeStage, scenario, patched.mChangeStageId.cstr())", text)
        self.assertIn("findOdysseyFallbackEntry(stage, scenario)", text)
        self.assertIn("armFixedCoordinateArrival", text)
        self.assertIn("tryBuildForcedPendingSynthetic(info, startId)", text)
        self.assertNotIn("armHomeTransArrival", text)
        self.assertNotIn("kGetHomeTransOffset", text)
        self.assertNotIn("firstScenarioInMask(endpoint->scenarioMask)", text)
        self.assertNotIn("setEntranceScenarioOverride", text)
        self.assertNotIn("clearEntranceScenarioOverride", text)
        self.assertNotIn("Scenario ER override:", scenario_text)
        self.assertNotIn("Scenario ER override arm:", scenario_text)

    def test_spoiler_travel_renders_home_arrivals_as_home_stages(self) -> None:
        text = moon_rando_text()

        self.assertIn("destKind == EntranceRando::ConnKind_HomeIn", text)
        self.assertIn("destKind == EntranceRando::ConnKind_HomeOut", text)
        self.assertIn("destinationStage = dest.homeStage", text)
        self.assertIn("destinationDoor = dest.homeEntranceOut", text)

    def test_forward_validator_rejects_unrooted_kingdom_access(self) -> None:
        text = moon_rando_text()

        self.assertIn("stageAccessIsRooted", text)
        self.assertIn("visiting[stage]", text)
        self.assertIn("allAccessibleKingdomsHaveRootedAccess", text)
        self.assertIn("Unrooted kingdom access:", text)
        self.assertGreaterEqual(
            text.count("if (!allAccessibleKingdomsHaveRootedAccess(state))"),
            3,
        )

    def test_buffer_only_uses_rooted_location_frontiers(self) -> None:
        text = moon_rando_text()

        location_reachability = re.search(
            r"static s32 reachableLocations\(.*?\n    \}",
            text,
            re.DOTALL,
        )
        trace_update = re.search(
            r"static void setStageTrace\(.*?\n    \}",
            text,
            re.DOTALL,
        )
        self.assertIsNotNone(location_reachability)
        self.assertIsNotNone(trace_update)
        self.assertIn("computeRootedStageMap", location_reachability.group(0))
        self.assertIn("!rootedStages[stage]", location_reachability.group(0))
        self.assertIn("candidateRooted && !currentRooted", trace_update.group(0))
        self.assertIn("kind == StageTrace_Painting", trace_update.group(0))

    def test_change_stage_area_linked_restarts_use_parent_start_id(self) -> None:
        text = entrance_hooks_text()

        self.assertIn("kIsPlayerStartPlacementOffset = 0x00528410", text)
        self.assertIn("isPlayerStartPlacementHook.installAtMainOffset", text)
        self.assertIn(
            "initInfo.placementInfo && isRoutedPlayerStartPlacement(accessor, *initInfo.placementInfo)",
            text,
        )


if __name__ == "__main__":
    unittest.main()
