#!/usr/bin/env python3
"""Host-side reader for include/Randomizer/EntranceFallbacks.h.

When an overworld loading zone does not exist in its kingdom's current
scenario, EntranceHooks does not simply let the spawn lookup fail and drop
Mario at the stage's default start. EntranceFallbacks.h authors, per entrance
and per scenario, what happens instead: an explicit vanilla override, forced
coordinates, a generated fake pipe or moon pipe standing at that spot, or a
genuine hand-off to the Odyssey.

Only the Odyssey case costs the player the entrance's node. The other modes
leave Mario standing on it, so the logic has to credit that node rather than
assuming an Odyssey warp - which would otherwise understate reachability and
add a phantom Odyssey hop to spoiler routes.

The header stays the single source of truth: it is hand-authored and carries
tuned camera parameters and per-entrance commentary that a generated file
would lose. This module reads only the four fields logic cares about (stage,
ChangeStageId, scenario mask, mode) and ignores everything else.

Mirrors the C++ helpers at the bottom of EntranceFallbacks.h
(findHomeFallback / modePlacesPlayerAtEntrance / arrivalPlacesPlayerAtEntrance).
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path


HEADER_PATH = (
    Path(__file__).resolve().parent.parent / "include" / "Randomizer" / "EntranceFallbacks.h"
)

SCENARIO_ANY = 0
SCENARIO_MIN = 1
SCENARIO_MAX = 15
SCENARIO_ALL = sum(1 << i for i in range(SCENARIO_MAX))

# Mirrors EntranceFallbacks::modePlacesPlayerAtEntrance. Mode_Vanilla keeps the
# authored spawn, Mode_Coordinates force-spawns on it, and both pipe modes put a
# generated pipe at that exact spot.
MODES_PLACING_PLAYER_AT_ENTRANCE = frozenset(
    {"Mode_Vanilla", "Mode_Coordinates", "Mode_Pipe", "Mode_Moon_Pipe"}
)

# Every fallback(...) / coordinateFallback(...) / pipeOffsetFallback(...) row
# starts with the same four arguments regardless of which trailing camera or
# offset overload it uses, so only those four are matched here.
_ROW_RE = re.compile(
    r"""\b\w*[fF]allback\s*\(\s*
        "(?P<stage>[^"]+)"\s*,\s*
        "(?P<change_id>[^"]+)"\s*,\s*
        (?P<mask>[^,]+?)\s*,\s*
        (?P<mode>Mode_\w+)""",
    re.VERBOSE,
)

_TABLE_RE = re.compile(
    r"sHomeFallbacks\s*\[\s*\]\s*=\s*\{(?P<body>.*?)^\s*\};",
    re.DOTALL | re.MULTILINE,
)

_LINE_COMMENT_RE = re.compile(r"//.*?$", re.MULTILINE)
_BLOCK_COMMENT_RE = re.compile(r"/\*.*?\*/", re.DOTALL)


@dataclass(frozen=True)
class HomeFallback:
    stage: str
    change_id: str
    scenario_mask: int
    mode: str

    @property
    def places_player_at_entrance(self) -> bool:
        return self.mode in MODES_PLACING_PLAYER_AT_ENTRANCE


class FallbackParseError(RuntimeError):
    """Raised when the header is present but cannot be understood."""


def _evaluate_scenario_mask(expression: str) -> int:
    """Evaluate a Scenario_* mask expression from the header.

    Handles every form the table uses: Scenario_Any, Scenario_All,
    ``Scenario_All & ~(Scenario_2 | Scenario_3)`` and plain
    ``Scenario_1 | Scenario_7`` unions.
    """
    text = expression.strip()

    def substitute(match: re.Match[str]) -> str:
        name = match.group(0)
        if name == "Scenario_Any":
            return str(SCENARIO_ANY)
        if name == "Scenario_All":
            return str(SCENARIO_ALL)
        index = int(name.rsplit("_", 1)[1])
        if not SCENARIO_MIN <= index <= SCENARIO_MAX:
            raise FallbackParseError(f"scenario index out of range: {name}")
        return str(1 << (index - 1))

    substituted = re.sub(r"Scenario_(?:Any|All|\d+)", substitute, text)

    # Only the operators the table actually uses survive; anything else is a
    # sign the header grew a form this reader does not model yet.
    if not re.fullmatch(r"[0-9\s()&|~^]+", substituted):
        raise FallbackParseError(f"unsupported scenario mask expression: {expression!r}")

    value = eval(substituted, {"__builtins__": {}}, {})  # noqa: S307 - digits/operators only
    return value & SCENARIO_ALL


def _parse(header_text: str) -> tuple[HomeFallback, ...]:
    table = _TABLE_RE.search(header_text)
    if not table:
        raise FallbackParseError("could not locate the sHomeFallbacks table")

    body = _BLOCK_COMMENT_RE.sub("", table.group("body"))
    body = _LINE_COMMENT_RE.sub("", body)

    rows = []
    for match in _ROW_RE.finditer(body):
        rows.append(
            HomeFallback(
                stage=match.group("stage"),
                change_id=match.group("change_id"),
                scenario_mask=_evaluate_scenario_mask(match.group("mask")),
                mode=match.group("mode"),
            )
        )
    return tuple(rows)


@lru_cache(maxsize=None)
def load_home_fallbacks(path: Path | None = None) -> tuple[HomeFallback, ...]:
    """Parse the fallback table, or return empty if the header is missing.

    A missing header is not fatal: every lookup then reports "no fallback",
    which is exactly the Odyssey-warp assumption the logic used before this
    table existed.
    """
    header = path or HEADER_PATH
    try:
        text = header.read_text(encoding="utf-8")
    except OSError:
        return ()
    return _parse(text)


def scenario_mask_contains(mask: int, scenario: int) -> bool:
    if not SCENARIO_MIN <= scenario <= SCENARIO_MAX:
        return False
    return bool(mask & (1 << (scenario - 1)))


def find_home_fallback(stage: str, change_id: str, scenario: int) -> HomeFallback | None:
    """Last matching row wins, so a specific scenario row placed after a
    generic one overrides it - same as the backward walk in the C++ lookup."""
    if not stage or not change_id:
        return None
    for row in reversed(load_home_fallbacks()):
        if row.stage != stage or row.change_id != change_id:
            continue
        if row.scenario_mask == SCENARIO_ANY or scenario_mask_contains(row.scenario_mask, scenario):
            return row
    return None


def arrival_places_player_at_entrance(stage: str, change_id: str, scenario: int) -> bool:
    """Whether an arrival at an entrance that is absent this scenario still
    leaves Mario standing on it. A missing row counts as Odyssey, because that
    is how EntranceHooks resolves it."""
    fallback = find_home_fallback(stage, change_id, scenario)
    return bool(fallback and fallback.places_player_at_entrance)


if __name__ == "__main__":
    import collections

    fallbacks = load_home_fallbacks()
    print(f"{HEADER_PATH}")
    print(f"parsed {len(fallbacks)} home fallback rows")
    by_mode = collections.Counter(row.mode for row in fallbacks)
    for mode, count in sorted(by_mode.items()):
        placed = "places player at entrance" if mode in MODES_PLACING_PLAYER_AT_ENTRANCE else "warps to Odyssey"
        print(f"  {mode:20s} {count:3d}  ({placed})")
    by_stage = collections.Counter(row.stage for row in fallbacks)
    print("\nrows per stage:")
    for stage, count in sorted(by_stage.items()):
        print(f"  {stage:32s} {count}")
