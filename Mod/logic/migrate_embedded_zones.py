"""Flatten embedded map zones across every graph-editor kingdom save.

Run after ``generate_logic_templates.py`` so ``embedded_zones.csv`` and the
flattened scenario metadata are current.  The migration is idempotent and also
repairs saves previously processed by the old moon-pocket migration.
"""

from __future__ import annotations

import csv
import json
import sys
from pathlib import Path

from embedded_zones import flatten_embedded_zones

ROOT = Path(__file__).resolve().parent
TEMPLATES = ROOT / "templates"
SAVES = ROOT / "graph-editor" / "saves" / "kingdoms"


def read_csv(name: str) -> list[dict[str, str]]:
    with (TEMPLATES / name).open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def scenario_key(row: dict) -> tuple[str, str, str, str]:
    return (
        row.get("stage", ""),
        row.get("scenario_no", ""),
        row.get("source_archive", ""),
        row.get("source_file", ""),
    )


def main() -> int:
    saves = sorted(SAVES.glob("*.json"))
    if not saves:
        print(f"No kingdom saves found in {SAVES}", file=sys.stderr)
        return 1

    mappings = read_csv("embedded_zones.csv")
    template_scenarios = read_csv("scenario_metadata.csv")
    total_merged = 0

    for path in saves:
        data = json.loads(path.read_text(encoding="utf-8"))
        workspace = data.get("workspace", data)
        result = flatten_embedded_zones(
            workspace.get("logicNodes", []),
            workspace.get("routeEdges", []),
            workspace.get("moonAccess", []),
            workspace.get("rooms", []),
            workspace.get("scenarioMetadata", []),
            mappings,
            workspace.get("nodePositions", {}),
        )

        workspace["logicNodes"] = result["nodes"]
        workspace["routeEdges"] = result["routes"]
        workspace["moonAccess"] = result["moons"]
        workspace["rooms"] = result["rooms"]
        workspace["scenarioMetadata"] = result["scenario_metadata"] or []

        kingdom = data.get("kingdom") or next(
            (
                row.get("kingdom", "")
                for row in workspace["rooms"]
                if row.get("kingdom")
            ),
            "",
        )
        existing_scenarios = {
            scenario_key(row) for row in workspace["scenarioMetadata"]
        }
        added_scenarios = 0
        for row in template_scenarios:
            if row.get("kingdom") != kingdom or scenario_key(row) in existing_scenarios:
                continue
            workspace["scenarioMetadata"].append(dict(row))
            existing_scenarios.add(scenario_key(row))
            added_scenarios += 1

        merged = result["merged"]
        if not merged and added_scenarios == 0:
            continue
        path.write_text(
            json.dumps(data, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
        total_merged += len(merged)
        pairs = ", ".join(
            f"{stage}->{parent}" for stage, parent in sorted(merged.items())
        )
        print(
            f"{path.name}: merged {len(merged)} embedded zone(s)"
            f"{f': {pairs}' if pairs else ''}; restored {added_scenarios} scenario row(s)"
        )

    print(f"\nDone. Merged {total_merged} zone mappings across {len(saves)} saves.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
