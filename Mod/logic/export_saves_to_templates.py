#!/usr/bin/env python3
"""Export graph-editor kingdom saves -> editable logic templates.

The graph editor (logic/graph-editor) stores authored logic per kingdom in
``graph-editor/saves/kingdoms/*.json``. The runtime generator
(``generate_runtime_logic.py``) reads the editable templates
``logic/templates/{logic_nodes,route_edges,moon_access}.csv``.

This script bridges the two: it merges every kingdom save and writes the
editable graph and capture-gate CSVs, mirroring the editor's download action
(``exportEditableCsvs`` in ``graph-editor/src/lib/workspace.ts``). The saved rows
are already normalized at save time (per-kingdom ``bidirectional`` computed,
``source == 'generated'`` rows dropped), so merging is concatenation by kingdom.

Run from anywhere, then regenerate the runtime header:

    python logic/export_saves_to_templates.py
    python logic/generate_runtime_logic.py
"""
from __future__ import annotations

import csv
import json
import sys
from pathlib import Path

from fixed_stage_links import (
    canonicalize_fixed_room_routes,
    fixed_transition_edges,
    semanticize_fixed_transition_nodes,
)
from generate_logic_templates import (
    make_runtime_anchors,
    write_area_graphs,
    write_csv,
    write_kingdom_splits,
)

ROOT = Path(__file__).resolve().parent
SAVES = ROOT / "graph-editor" / "saves" / "kingdoms"
OUT = ROOT / "templates"

# graph-editor workspace table -> editable template filename
TABLES = {
    "logicNodes": "logic_nodes.csv",
    "routeEdges": "route_edges.csv",
    "moonAccess": "moon_access.csv",
    "captureGates": "capture_gates.csv",
    "captureGateObjects": "capture_gate_objects.csv",
}


def main() -> int:
    saves = sorted(SAVES.glob("*.json"))
    if not saves:
        print(f"No kingdom saves found in {SAVES}", file=sys.stderr)
        return 1

    merged: dict[str, list[dict]] = {table: [] for table in TABLES}
    headers: dict[str, list[str]] = {table: [] for table in TABLES}

    for path in saves:
        project = json.loads(path.read_text(encoding="utf-8"))
        workspace = project.get("workspace", {})
        csv_headers = workspace.get("csvHeaders", {})
        for table in TABLES:
            for header in csv_headers.get(table, []):
                if header and header not in headers[table]:
                    headers[table].append(header)
            for row in workspace.get(table, []):
                # Mirror normalizeRouteRows: drop generator-authored route rows.
                if table == "routeEdges" and row.get("source") == "generated":
                    continue
                merged[table].append(row)

    semanticize_fixed_transition_nodes(merged["logicNodes"])
    merged["routeEdges"] = canonicalize_fixed_room_routes(
        merged["logicNodes"],
        merged["routeEdges"],
    )
    fixed_edge_ids = {row["edge_id"] for row in fixed_transition_edges()}
    merged["routeEdges"] = [
        row for row in merged["routeEdges"]
        if row.get("edge_id") not in fixed_edge_ids
    ]
    merged["routeEdges"].extend(fixed_transition_edges())

    for table, filename in TABLES.items():
        columns = list(headers[table])
        existing_path = OUT / filename
        if not columns and existing_path.exists():
            with existing_path.open(newline="", encoding="utf-8-sig") as handle:
                columns.extend(csv.DictReader(handle).fieldnames or [])
        for row in merged[table]:
            for key in row:
                if key.startswith("__"):  # editor-internal (e.g. __editor_id)
                    continue
                if key not in columns:
                    columns.append(key)

        out_path = OUT / filename
        with out_path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=columns, extrasaction="ignore")
            writer.writeheader()
            for row in merged[table]:
                writer.writerow({col: row.get(col, "") for col in columns})
        print(f"wrote {len(merged[table]):>5} rows, {len(columns):>3} cols -> {out_path}")

    def load_template(filename: str) -> tuple[list[str], list[dict[str, str]]]:
        with (OUT / filename).open(newline="", encoding="utf-8") as handle:
            reader = csv.DictReader(handle)
            return list(reader.fieldnames or []), list(reader)

    runtime_headers, _ = load_template("runtime_anchors.csv")
    runtime_anchors = make_runtime_anchors(merged["logicNodes"])
    write_csv(OUT / "runtime_anchors.csv", runtime_anchors, runtime_headers)

    split_files = [
        "rooms.csv",
        "progression_events.csv",
        "scenario_metadata.csv",
        "entrance_links.csv",
        "logic_nodes.csv",
        "route_edges.csv",
        "moon_access.csv",
        "runtime_anchors.csv",
        "capture_gates.csv",
    ]
    fields_by_file: dict[str, list[str]] = {}
    data_by_file: dict[str, list[dict[str, str]]] = {}
    for filename in split_files:
        fields_by_file[filename], data_by_file[filename] = load_template(filename)

    write_kingdom_splits(OUT, fields_by_file, data_by_file)
    write_area_graphs(
        OUT,
        data_by_file["rooms.csv"],
        data_by_file["logic_nodes.csv"],
        data_by_file["route_edges.csv"],
        data_by_file["moon_access.csv"],
        runtime_anchors,
        data_by_file["progression_events.csv"],
    )

    print(f"merged {len(saves)} kingdom saves")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
