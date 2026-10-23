"""Subarea Keys: which entrance kinds a kingdom's optional sub-areas use.

This is the offline mirror of include/Randomizer/SubareaKeyDB.h. The C++ runtime
is what actually generates seeds, so this module exists so the audit scripts and
the Python reachability model agree with it rather than guessing.

The classification itself is NOT recomputed here - it is read straight out of the
generated ``EntranceDB2.h`` (``LegacyLink::subareaType``), so the two sides
cannot drift. See tools/generate_entrance_db2.py for how a kind is derived.

Run ``python logic/subarea_keys.py --dump`` to print the per-kingdom key matrix.
"""

from __future__ import annotations

import argparse
import csv
import re
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ENTRANCE_DB2 = ROOT / "include" / "EntranceDB2.h"
ENTRANCE_LINKS = ROOT / "logic" / "templates" / "entrance_links.csv"

# Kinds a key can gate. Spark Pylons are left free (one-way launches with no
# matching door to bounce back out of) and Paintings belong to PaintingRando.
GATED_TYPES = (
    "WarpPipe",
    "MoonPipe",
    "OpenPassage",
    "Doorway",
    "CappyDoor",
    "Beanstalk",
)

TYPE_DISPLAY_NAMES = {
    "WarpPipe": "Warp Pipe",
    "MoonPipe": "Moon Pipe",
    "OpenPassage": "Open Passage",
    "Doorway": "Doorway",
    "CappyDoor": "Scarecrow Door",
    "Beanstalk": "Beanstalk",
    "SparkPylon": "Spark Pylon",
    "Painting": "Painting",
}

MODE_UNLOCKED = "unlocked"
MODE_PER_TYPE = "per_type"
MODE_PER_KINGDOM = "per_kingdom"
MODE_PER_KINGDOM_AND_TYPE = "per_kingdom_and_type"
MODES = (MODE_UNLOCKED, MODE_PER_TYPE, MODE_PER_KINGDOM, MODE_PER_KINGDOM_AND_TYPE)

# Moons each host can carry keys on, matching SubareaKeyDB's constants.
HOST_CAPACITY = {"cap": 31, "moon": 38, "cap_and_moon": 69}


@dataclass(frozen=True)
class SubareaLink:
    link_index: int
    kingdom: str
    home_stage: str
    home_entrance_in: str
    sub_stage: str
    status: str  # active / vanilla / excluded
    area_scope: str  # overworld / subarea
    subarea_type: str  # WarpPipe / MoonPipe / ... / None

    @property
    def is_gated(self) -> bool:
        """Whether Subarea Keys apply to this link at all.

        Story routes are already marked vanilla (they come from
        config/entrance_db2_report_ignores.csv), excluded rows never reach the
        runtime's entrance resolution, and nested sub-area doors are left alone
        so only a kingdom's own overworld entrances are gated.
        """
        return (
            self.status == "active"
            and self.area_scope == "overworld"
            and self.subarea_type in GATED_TYPES
        )


_LINK_RE = re.compile(
    r'\{\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)",'
    r"\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),\s*(-?\d+),"
    r"\s*DoorSideBehavior_\w+,\s*DoorSideBehavior_\w+,"
    r"\s*RandoStatus_(\w+),\s*(?:true|false),\s*SubareaType_(\w+)"
)


def _read_generated_links(path: Path = ENTRANCE_DB2) -> list[tuple[str, ...]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    match = re.search(r"sLegacyLinks\[\]\s*=\s*\{(.*?)\n\s*\};", text, re.S)
    if not match:
        raise SystemExit(f"sLegacyLinks table not found in {path}")
    rows = []
    for line in match.group(1).splitlines():
        found = _LINK_RE.search(line)
        if found:
            rows.append(
                tuple(found.group(index) for index in range(1, 7))
                + (found.group(11).lower(), found.group(12))
            )
    return rows


def load_links(
    db_path: Path = ENTRANCE_DB2, links_path: Path = ENTRANCE_LINKS
) -> list[SubareaLink]:
    """Every curated entrance link, with its generated kind and its kingdom.

    sLegacyLinks[i] is index-aligned with the logic template's row i (both are
    built from the same curated CSV), which is what lets the kingdom and scope
    columns be joined on position.
    """
    generated = _read_generated_links(db_path)
    with links_path.open(newline="", encoding="utf-8-sig") as handle:
        template = list(csv.DictReader(handle))
    if len(generated) != len(template):
        raise SystemExit(
            f"link count mismatch: {len(generated)} in {db_path.name} vs "
            f"{len(template)} in {links_path.name} - regenerate EntranceDB2.h"
        )

    links = []
    for index, (generated_row, row) in enumerate(zip(generated, template)):
        expected = tuple((row.get(field, "") or "").strip() for field in (
            "home_stage",
            "home_entrance_in",
            "home_entrance_out",
            "sub_stage",
            "sub_entrance_in",
            "sub_entrance_out",
        )) + ((row.get("status", "") or "unknown").strip().lower(),)
        if generated_row[:7] != expected:
            raise SystemExit(
                f"entrance row {index} mismatch between {db_path.name} and "
                f"{links_path.name}: generated={generated_row[:7]!r}, "
                f"logic={expected!r} - regenerate EntranceDB2.h"
            )
        home_stage, home_in, _, sub_stage, _, _, status, kind = generated_row
        links.append(
            SubareaLink(
                link_index=index,
                kingdom=row["kingdom"],
                home_stage=home_stage,
                home_entrance_in=home_in,
                sub_stage=sub_stage,
                status=status,
                area_scope=row["area_scope"],
                subarea_type=kind,
            )
        )
    return links


class SubareaKeyDB:
    """Which (kingdom, kind) pairs exist, and how a mode cuts them into keys."""

    def __init__(self, links: list[SubareaLink] | None = None) -> None:
        self.links = links if links is not None else load_links()
        self.by_index = {link.link_index: link for link in self.links}
        self.types_by_kingdom: dict[str, set[str]] = {}
        for link in self.links:
            if link.is_gated:
                self.types_by_kingdom.setdefault(link.kingdom, set()).add(link.subarea_type)

    def kingdoms(self) -> list[str]:
        """Kingdoms with at least one gateable door, in the template's order."""
        seen: list[str] = []
        for link in self.links:
            if link.is_gated and link.kingdom not in seen:
                seen.append(link.kingdom)
        return seen

    def types(self) -> list[str]:
        present = {t for types in self.types_by_kingdom.values() for t in types}
        return [t for t in GATED_TYPES if t in present]

    def kingdom_has_type(self, kingdom: str, subarea_type: str) -> bool:
        return subarea_type in self.types_by_kingdom.get(kingdom, ())

    def key_slots(self, mode: str) -> list[tuple[str | None, str | None]]:
        """One entry per key. ``None`` means "every value" for that half."""
        if mode == MODE_PER_TYPE:
            return [(None, t) for t in self.types()]
        if mode == MODE_PER_KINGDOM:
            return [(k, None) for k in self.kingdoms()]
        if mode == MODE_PER_KINGDOM_AND_TYPE:
            return [
                (k, t)
                for k in self.kingdoms()
                for t in GATED_TYPES
                if self.kingdom_has_type(k, t)
            ]
        return []

    def find_key_slot(self, mode: str, kingdom: str, subarea_type: str) -> int:
        """Slot governing this pair, or -1 when nothing gates it."""
        if not self.kingdom_has_type(kingdom, subarea_type):
            return -1
        for index, (slot_kingdom, slot_type) in enumerate(self.key_slots(mode)):
            if (slot_kingdom is None or slot_kingdom == kingdom) and (
                slot_type is None or slot_type == subarea_type
            ):
                return index
        return -1

    def slot_for_link(self, mode: str, link_index: int) -> int:
        """Slot gating this link, keyed on its HOME side (the door touched)."""
        link = self.by_index.get(link_index)
        if link is None or not link.is_gated:
            return -1
        return self.find_key_slot(mode, link.kingdom, link.subarea_type)

    def key_count(self, mode: str) -> int:
        return len(self.key_slots(mode))

    def slots_relevant_to_kingdoms(
        self, mode: str, accessible_kingdoms: set[str]
    ) -> set[int]:
        """Keys worth considering while one of their home kingdoms is open.

        The runtime's progression bundle search applies this same frontier
        filter. A key outside this set cannot change the current topology; if a
        different unlock exposes its kingdom, that unlock advances the frontier
        first and the key joins the following search.
        """
        relevant: set[int] = set()
        for kingdom in accessible_kingdoms:
            for subarea_type in self.types_by_kingdom.get(kingdom, ()):
                slot = self.find_key_slot(mode, kingdom, subarea_type)
                if slot >= 0:
                    relevant.add(slot)
        return relevant

    def host_fits(self, mode: str, host: str) -> bool:
        if mode == MODE_UNLOCKED:
            return True
        return self.key_count(mode) <= HOST_CAPACITY[host]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dump", action="store_true", help="print the per-kingdom key matrix")
    args = parser.parse_args()

    db = SubareaKeyDB()
    unclassified = [link for link in db.links if link.subarea_type == "None"]
    if unclassified:
        for link in unclassified:
            print(f"  UNCLASSIFIED {link.link_index} {link.home_stage}/{link.home_entrance_in}")
        raise SystemExit(f"{len(unclassified)} link(s) have no subarea type")

    if args.dump:
        for kingdom in db.kingdoms():
            names = [
                TYPE_DISPLAY_NAMES[t] for t in GATED_TYPES if db.kingdom_has_type(kingdom, t)
            ]
            print(f"  {kingdom:22s} {len(names)}  " + ", ".join(names))
        print()
        for mode in MODES:
            hosts = [h for h in HOST_CAPACITY if db.host_fits(mode, h)]
            print(f"  {mode:22s} keys={db.key_count(mode):3d}  hosts={','.join(hosts)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
