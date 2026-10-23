#!/usr/bin/env python3
"""Apply a saved moon scenario audit without reading game archives again."""

from __future__ import annotations

import argparse
import csv
import io
import json
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_REPORT = ROOT / "logic" / "reports" / "moon_scenario_audit.json"


@dataclass
class Change:
    moon_id: str
    expected_fields: dict[str, str]
    replacement_fields: dict[str, str]


@dataclass
class PendingWrite:
    path: Path
    content: str
    changed_rows: int


def load_changes(path: Path, include_moon_rock: bool) -> dict[str, Change]:
    report = json.loads(path.read_text(encoding="utf-8"))
    if report.get("schema_version") != 1:
        raise ValueError(f"unsupported report schema: {report.get('schema_version')}")

    changes: dict[str, Change] = {}
    for entry in report.get("proposed_changes", []):
        moon_id = str(entry.get("moon_id", ""))
        expected = dict(entry.get("expected_fields", {}))
        replacements = dict(entry.get("replacement_fields", {}))
        if not include_moon_rock:
            expected.pop("shine_info_is_moon_rock", None)
            replacements.pop("shine_info_is_moon_rock", None)
        if not moon_id or not replacements:
            continue
        if moon_id in changes:
            raise ValueError(f"duplicate proposed change for {moon_id}")
        changes[moon_id] = Change(moon_id, expected, replacements)
    return changes


def update_row(
    row: dict[str, Any],
    change: Change,
    path: Path,
    conflicts: list[str],
) -> bool:
    changed = False
    for field, replacement in change.replacement_fields.items():
        if field not in row:
            conflicts.append(f"{path}: {change.moon_id} has no field {field}")
            continue
        actual = str(row[field])
        expected = str(change.expected_fields.get(field, ""))
        if actual == replacement:
            continue
        if actual != expected:
            conflicts.append(
                f"{path}: {change.moon_id} {field} expected {expected!r}, found {actual!r}"
            )
            continue
        row[field] = replacement
        changed = True
    return changed


def prepare_csv(
    path: Path,
    changes: dict[str, Change],
    conflicts: list[str],
    occurrences: Counter[tuple[str, str]],
    scope: str,
) -> PendingWrite | None:
    raw = path.read_bytes()
    has_bom = raw.startswith(b"\xef\xbb\xbf")
    text = raw.decode("utf-8-sig")
    newline = "\r\n" if "\r\n" in text else "\n"
    reader = csv.DictReader(io.StringIO(text, newline=""))
    rows = list(reader)
    if reader.fieldnames is None:
        conflicts.append(f"{path}: CSV has no header")
        return None

    changed_rows = 0
    for row in rows:
        moon_id = row.get("moon_id", "")
        change = changes.get(moon_id)
        if not change:
            continue
        occurrences[(scope, moon_id)] += 1
        if update_row(row, change, path, conflicts):
            changed_rows += 1

    if changed_rows == 0:
        return None
    output = io.StringIO(newline="")
    writer = csv.DictWriter(
        output,
        fieldnames=reader.fieldnames,
        lineterminator=newline,
    )
    writer.writeheader()
    writer.writerows(rows)
    content = output.getvalue()
    if has_bom:
        content = "\ufeff" + content
    return PendingWrite(path, content, changed_rows)


def iter_moon_rows(value: Any) -> Iterable[dict[str, Any]]:
    if isinstance(value, dict):
        if "moon_id" in value:
            yield value
        for child in value.values():
            yield from iter_moon_rows(child)
    elif isinstance(value, list):
        for child in value:
            yield from iter_moon_rows(child)


def prepare_json(
    path: Path,
    changes: dict[str, Change],
    conflicts: list[str],
    occurrences: Counter[tuple[str, str]],
    scope: str,
) -> PendingWrite | None:
    original = path.read_text(encoding="utf-8")
    data = json.loads(original)
    changed_rows = 0
    for row in iter_moon_rows(data):
        moon_id = str(row.get("moon_id", ""))
        change = changes.get(moon_id)
        if not change:
            continue
        occurrences[(scope, moon_id)] += 1
        if update_row(row, change, path, conflicts):
            changed_rows += 1

    suffix = "\n" if original.endswith("\n") else ""
    # Preserve each JSON file's existing Unicode style. Most editor saves use
    # literal Japanese, while some snapshots intentionally use \u escapes.
    ensure_ascii = not any(ord(char) > 127 for char in original)
    content = json.dumps(data, ensure_ascii=ensure_ascii, indent=2) + suffix
    if content == original:
        return None
    return PendingWrite(path, content, changed_rows)


def target_groups() -> dict[str, list[Path]]:
    return {
        "master_csv": [ROOT / "logic" / "templates" / "moon_access.csv"],
        "kingdom_csv": sorted(
            (ROOT / "logic" / "templates" / "by_kingdom").glob(
                "*/moon_access.csv"
            )
        ),
        "area_graph": sorted(
            (ROOT / "logic" / "templates" / "area_graphs").glob("*/*.json")
        ),
        "editor_save": sorted(
            (ROOT / "logic" / "graph-editor" / "saves" / "kingdoms").glob(
                "*.json"
            )
        ),
    }


def validate_occurrences(
    changes: dict[str, Change],
    occurrences: Counter[tuple[str, str]],
    conflicts: list[str],
) -> None:
    for scope in ["master_csv", "kingdom_csv", "area_graph", "editor_save"]:
        for moon_id in changes:
            count = occurrences[(scope, moon_id)]
            if count != 1:
                conflicts.append(
                    f"{scope}: expected exactly one {moon_id} row, found {count}"
                )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Apply the reusable changes in a moon scenario audit report."
    )
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    parser.add_argument(
        "--skip-moon-rock",
        action="store_true",
        help="Apply scenario availability corrections only.",
    )
    parser.add_argument(
        "--apply",
        action="store_true",
        help="Write changes. Without this flag the command is a dry run.",
    )
    args = parser.parse_args()

    changes = load_changes(args.report, not args.skip_moon_rock)
    conflicts: list[str] = []
    occurrences: Counter[tuple[str, str]] = Counter()
    writes: list[PendingWrite] = []

    for scope, paths in target_groups().items():
        for path in paths:
            pending = (
                prepare_csv(path, changes, conflicts, occurrences, scope)
                if path.suffix == ".csv"
                else prepare_json(path, changes, conflicts, occurrences, scope)
            )
            if pending:
                writes.append(pending)

    validate_occurrences(changes, occurrences, conflicts)
    if conflicts:
        print("Refusing to apply due to validation conflicts:")
        for conflict in conflicts:
            print(f"  - {conflict}")
        return 1

    mode = "APPLY" if args.apply else "DRY RUN"
    print(f"{mode}: {len(changes)} moon records, {len(writes)} files")
    for pending in writes:
        print(f"  {pending.path}: {pending.changed_rows} row(s)")

    bowser_id = "SkyWorldTreasureStage:95:999"
    if bowser_id in changes:
        bowser = changes[bowser_id]
        print(
            "Bowser vault guard: "
            f"{bowser.expected_fields.get('shine_info_available_scenarios', '')!r} -> "
            f"{bowser.replacement_fields.get('shine_info_available_scenarios', '')!r}; "
            "no other fields selected"
        )

    if args.apply:
        for pending in writes:
            pending.path.write_text(pending.content, encoding="utf-8", newline="")
        print("Changes written.")
    else:
        print("No files written. Re-run with --apply after reviewing this output.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
