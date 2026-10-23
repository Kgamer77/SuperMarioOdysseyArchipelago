#!/usr/bin/env python3
"""Audit extracted moon scenario availability against the game's raw data.

The report is deliberately self-contained.  A later fixer can consume
``proposed_changes`` without opening or extracting the game archives again.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INPUT = ROOT / "logic" / "templates" / "moon_access.csv"
DEFAULT_SYSTEM_DATA = ROOT / "extraction" / "SystemData"
DEFAULT_STAGE_DATA = ROOT / "extraction" / "StageData"
DEFAULT_JSON_OUTPUT = ROOT / "logic" / "reports" / "moon_scenario_audit.json"
DEFAULT_CSV_OUTPUT = ROOT / "logic" / "reports" / "moon_scenario_audit.csv"

REPORT_SCHEMA_VERSION = 1
MAX_SCENARIO_BITS = 32


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_int(value: Any) -> int | None:
    if value is None or str(value).strip() == "":
        return None
    try:
        return int(value)
    except (TypeError, ValueError):
        return None


def parse_scenario_list(value: Any) -> list[int]:
    scenarios: set[int] = set()
    for part in str(value or "").replace(",", ";").split(";"):
        number = parse_int(part.strip())
        if number is not None:
            scenarios.add(number)
    return sorted(scenarios)


def format_scenario_list(scenarios: Iterable[int]) -> str:
    return ";".join(str(scenario) for scenario in sorted(set(scenarios)))


def decode_progress_bit_flag(value: int) -> list[int]:
    """Decode the game's zero-based bit positions into one-based ScenarioNo values."""
    if value < 0:
        raise ValueError("ProgressBitFlag cannot be negative")
    return [
        bit + 1
        for bit in range(MAX_SCENARIO_BITS)
        if value & (1 << bit)
    ]


def parse_runtime_shine_key(value: str) -> tuple[str, str, int] | None:
    parts = value.rsplit(":", 2)
    if len(parts) != 3:
        return None
    stage, obj_id, uid_text = parts
    uid = parse_int(uid_text)
    if not stage or uid is None:
        return None
    if not obj_id.startswith("obj"):
        obj_id = f"obj{obj_id}"
    return stage, obj_id, uid


def require_oead():
    try:
        import oead  # type: ignore
    except ImportError as exc:  # pragma: no cover - environment failure
        raise SystemExit(
            "The oead package is required to read the in-game SZS/BYML data."
        ) from exc
    return oead


def hget(value: Any, key: str, default: Any = None) -> Any:
    return value[key] if key in value else default


def scalar_text(value: Any) -> str:
    return "" if value is None else str(value)


def iter_stage_objects(value: Any, oead: Any):
    if isinstance(value, oead.byml.Hash):
        if "Id" in value and "UnitConfigName" in value:
            yield value
        for key, child in value.items():
            if str(key) in {"Links", "UnitConfig"}:
                continue
            yield from iter_stage_objects(child, oead)
    elif isinstance(value, oead.byml.Array):
        for child in value:
            yield from iter_stage_objects(child, oead)


@dataclass
class ShineInfoEvidence:
    source_file: str
    world_home_stage: str
    stage_name: str
    obj_id: str
    uid: int
    progress_bit_flag: int | None
    is_moon_rock: bool
    main_scenario_no: int | None
    hint_idx: int | None
    object_name: str
    scenario_name: str


@dataclass
class StageMapEvidence:
    scenario_indices: set[int] = field(default_factory=set)
    unit_config_names: set[str] = field(default_factory=set)
    layer_config_names: set[str] = field(default_factory=set)
    scenario_no_values: set[str] = field(default_factory=set)
    source_archives: set[str] = field(default_factory=set)
    source_files: set[str] = field(default_factory=set)
    placement_count: int = 0


def load_shine_info(
    path: Path,
) -> tuple[
    dict[tuple[str, str, int], list[ShineInfoEvidence]],
    dict[int, list[int]],
]:
    oead = require_oead()
    archive = oead.Sarc(oead.yaz0.decompress(path.read_bytes()))
    shine_info: dict[tuple[str, str, int], list[ShineInfoEvidence]] = defaultdict(list)

    for file in archive.get_files():
        if not file.name.startswith("ShineList_") or not file.name.endswith(".byml"):
            continue
        world_home_stage = file.name[len("ShineList_") : -len(".byml")]
        root = oead.byml.from_binary(file.data)
        for row in hget(root, "ShineList", []) or []:
            stage_name = scalar_text(hget(row, "StageName"))
            obj_id = scalar_text(hget(row, "ObjId"))
            uid = parse_int(hget(row, "UniqueId"))
            if not stage_name or not obj_id or uid is None:
                continue
            key = (stage_name, obj_id, uid)
            shine_info[key].append(
                ShineInfoEvidence(
                    source_file=file.name,
                    world_home_stage=world_home_stage,
                    stage_name=stage_name,
                    obj_id=obj_id,
                    uid=uid,
                    progress_bit_flag=parse_int(hget(row, "ProgressBitFlag")),
                    is_moon_rock=bool(hget(row, "IsMoonRock", False)),
                    main_scenario_no=parse_int(hget(row, "MainScenarioNo")),
                    hint_idx=parse_int(hget(row, "HintIdx")),
                    object_name=scalar_text(hget(row, "ObjectName")),
                    scenario_name=scalar_text(hget(row, "ScenarioName")),
                )
            )

    shine_positions: dict[int, list[int]] = {}
    pos_file = archive.get_file("ShinePosList.byml")
    if pos_file:
        root = oead.byml.from_binary(pos_file.data)
        for row in root:
            uid = parse_int(hget(row, "UniqueId"))
            if uid is None:
                continue
            shine_positions[uid] = sorted(
                int(str(key))
                for key, position in row.items()
                if str(key).isdigit()
                and hasattr(position, "__contains__")
                and hget(position, "X") is not None
            )
    return dict(shine_info), shine_positions


def load_world_list(path: Path) -> dict[str, dict[str, Any]]:
    oead = require_oead()
    archive = oead.Sarc(oead.yaz0.decompress(path.read_bytes()))
    file = archive.get_file("WorldList.byml")
    if not file:
        raise ValueError(f"{path} does not contain WorldList.byml")
    root = oead.byml.from_binary(file.data)
    result: dict[str, dict[str, Any]] = {}
    for row in root:
        home_stage = scalar_text(hget(row, "Name"))
        if not home_stage:
            continue
        result[home_stage] = {
            "world_name": scalar_text(hget(row, "WorldName")),
            "home_stage": home_stage,
            "scenario_count": parse_int(hget(row, "ScenarioNum")),
            "moon_rock_scenario": parse_int(hget(row, "MoonRockScenario")),
            "after_ending_scenario": parse_int(hget(row, "AfterEndingScenario")),
            "clear_main_scenario": parse_int(hget(row, "ClearMainScenario")),
        }
    return result


def scan_stage_maps(
    stage_data_dir: Path,
    inventory_keys: set[tuple[str, str]],
) -> tuple[dict[tuple[str, str], StageMapEvidence], str, int]:
    oead = require_oead()
    evidence: dict[tuple[str, str], StageMapEvidence] = {}
    fingerprint = hashlib.sha256()
    archive_count = 0

    for path in sorted(stage_data_dir.rglob("*Map.szs")):
        archive_count += 1
        relative_name = path.relative_to(stage_data_dir).as_posix()
        raw_archive = path.read_bytes()
        fingerprint.update(relative_name.encode("utf-8"))
        fingerprint.update(b"\0")
        fingerprint.update(hashlib.sha256(raw_archive).digest())
        try:
            archive = oead.Sarc(oead.yaz0.decompress(raw_archive))
        except Exception:
            continue
        archive_stage = path.name[: -len("Map.szs")]

        for file in archive.get_files():
            if not file.name.endswith("Map.byml"):
                continue
            try:
                root = oead.byml.from_binary(file.data)
            except Exception:
                continue
            scenario_roots = list(root) if isinstance(root, oead.byml.Array) else [root]
            for scenario_index, scenario_root in enumerate(scenario_roots):
                for obj in iter_stage_objects(scenario_root, oead):
                    stage = scalar_text(hget(obj, "PlacementFileName")) or archive_stage
                    obj_id = scalar_text(hget(obj, "Id"))
                    key = (stage, obj_id)
                    if key not in inventory_keys:
                        continue
                    row = evidence.setdefault(key, StageMapEvidence())
                    row.scenario_indices.add(scenario_index)
                    row.placement_count += 1
                    row.source_archives.add(path.name)
                    row.source_files.add(file.name)
                    for target, field_name in [
                        (row.unit_config_names, "UnitConfigName"),
                        (row.layer_config_names, "LayerConfigName"),
                        (row.scenario_no_values, "ScenarioNo"),
                    ]:
                        value = hget(obj, field_name)
                        if value is not None and str(value) != "":
                            target.add(str(value))

    return evidence, fingerprint.hexdigest(), archive_count


def effective_is_moon_rock(
    raw_value: bool,
    progress_bit_flag: int | None,
    moon_rock_scenario: int | None,
    world_name: str,
) -> bool:
    """Mirror the game's derived moon-rock classification for scenario-only moons."""
    if world_name == "Peach":
        return False
    if raw_value:
        return True
    if progress_bit_flag is None or moon_rock_scenario is None or moon_rock_scenario < 1:
        return False
    return progress_bit_flag == (1 << (moon_rock_scenario - 1))


def stage_map_relation(
    authoritative: list[int],
    stage_map_scenarios: list[int],
) -> str:
    if not stage_map_scenarios:
        return "unavailable"
    authoritative_set = set(authoritative)
    stage_map_set = set(stage_map_scenarios)
    if authoritative_set == stage_map_set:
        return "exact"
    if authoritative_set and authoritative_set < stage_map_set:
        return "superset"
    if authoritative_set & stage_map_set:
        return "overlap"
    return "disjoint"


def make_record(
    row_number: int,
    row: dict[str, str],
    shine_candidates: list[ShineInfoEvidence],
    shine_positions: dict[int, list[int]],
    world_list: dict[str, dict[str, Any]],
    stage_maps: dict[tuple[str, str], StageMapEvidence],
) -> dict[str, Any]:
    runtime_key_text = row.get("runtime_shine_key") or row.get("moon_id", "")
    runtime_key = parse_runtime_shine_key(runtime_key_text)
    current_scenarios = parse_scenario_list(
        row.get("shine_info_available_scenarios", "")
    )
    current_text = format_scenario_list(current_scenarios)
    current_is_moon_rock = str(
        row.get("shine_info_is_moon_rock", "")
    ).strip().lower() == "true"

    status = "needs_review"
    reasons: list[str] = []
    authoritative_scenarios: list[int] = []
    progress_bit_flag: int | None = None
    info: ShineInfoEvidence | None = None

    if runtime_key is None:
        reasons.append("runtime_shine_key is not parseable")
    elif len(shine_candidates) == 0:
        reasons.append("no exact ShineList entry was found")
    elif len(shine_candidates) > 1:
        reasons.append("multiple exact ShineList entries were found")
    else:
        info = shine_candidates[0]
        progress_bit_flag = info.progress_bit_flag
        if progress_bit_flag is None:
            reasons.append("ShineList entry has no ProgressBitFlag")
        else:
            authoritative_scenarios = decode_progress_bit_flag(progress_bit_flag)
            if not authoritative_scenarios:
                reasons.append("ProgressBitFlag has no scenario bits set")
            elif current_scenarios == authoritative_scenarios:
                status = "verified"
                reasons.append("current scenarios match ProgressBitFlag")
            else:
                status = "incorrect"
                reasons.append("current scenarios do not match ProgressBitFlag")

    map_evidence = (
        stage_maps.get((runtime_key[0], runtime_key[1]))
        if runtime_key is not None
        else None
    )
    map_indices = sorted(map_evidence.scenario_indices) if map_evidence else []
    map_scenarios = [index + 1 for index in map_indices]
    relation = stage_map_relation(authoritative_scenarios, map_scenarios)
    if status == "incorrect" and relation == "exact":
        reasons.append("StageMap placement independently confirms ProgressBitFlag")

    uid = runtime_key[2] if runtime_key is not None else parse_int(row.get("uid"))
    position_scenarios = shine_positions.get(uid, []) if uid is not None else []
    world = world_list.get(info.world_home_stage, {}) if info else {}
    effective_moon_rock = (
        effective_is_moon_rock(
            info.is_moon_rock,
            progress_bit_flag,
            world.get("moon_rock_scenario"),
            world.get("world_name", ""),
        )
        if info
        else current_is_moon_rock
    )

    proposed_fields: dict[str, str] = {}
    expected_fields: dict[str, str] = {}
    if status == "incorrect":
        expected_fields["shine_info_available_scenarios"] = row.get(
            "shine_info_available_scenarios", ""
        )
        proposed_fields["shine_info_available_scenarios"] = format_scenario_list(
            authoritative_scenarios
        )
    if info and current_is_moon_rock != effective_moon_rock:
        expected_fields["shine_info_is_moon_rock"] = row.get(
            "shine_info_is_moon_rock", ""
        )
        proposed_fields["shine_info_is_moon_rock"] = str(
            effective_moon_rock
        ).lower()

    return {
        "row_number": row_number,
        "moon_id": row.get("moon_id", ""),
        "runtime_shine_key": runtime_key_text,
        "moon_name": row.get("moon_name", ""),
        "kingdom": row.get("kingdom", ""),
        "stage": row.get("stage", ""),
        "status": status,
        "reasons": reasons,
        "current": {
            "available_scenarios": current_scenarios,
            "available_scenarios_text": current_text,
            "is_moon_rock": current_is_moon_rock,
        },
        "authoritative": {
            "source": "ShineList.ProgressBitFlag" if progress_bit_flag is not None else "",
            "progress_bit_flag": progress_bit_flag,
            "available_scenarios": authoritative_scenarios,
            "available_scenarios_text": format_scenario_list(
                authoritative_scenarios
            ),
            "raw_is_moon_rock": info.is_moon_rock if info else None,
            "effective_is_moon_rock": effective_moon_rock if info else None,
            "moon_rock_scenario": world.get("moon_rock_scenario"),
            "world_name": world.get("world_name", ""),
            "source_file": info.source_file if info else "",
        },
        "cross_checks": {
            "shine_pos_scenarios": position_scenarios,
            "stage_map_indices": map_indices,
            "stage_map_scenarios": map_scenarios,
            "stage_map_relation": relation,
            "stage_map_placement_count": map_evidence.placement_count
            if map_evidence
            else 0,
            "stage_map_unit_config_names": sorted(
                map_evidence.unit_config_names
            )
            if map_evidence
            else [],
            "stage_map_layer_config_names": sorted(
                map_evidence.layer_config_names
            )
            if map_evidence
            else [],
            "stage_map_scenario_no_values": sorted(
                map_evidence.scenario_no_values
            )
            if map_evidence
            else [],
            "stage_map_source_archives": sorted(
                map_evidence.source_archives
            )
            if map_evidence
            else [],
            "stage_map_source_files": sorted(map_evidence.source_files)
            if map_evidence
            else [],
        },
        "source_row": {
            "shine_info_progress_bit_flag": row.get(
                "shine_info_progress_bit_flag", ""
            ),
            "shine_info_available_scenarios": row.get(
                "shine_info_available_scenarios", ""
            ),
            "placement_stage_map_indices": row.get(
                "placement_stage_map_indices", ""
            ),
            "placement_unit_config_names": row.get(
                "placement_unit_config_names", ""
            ),
        },
        "proposed_change": {
            "can_apply_without_game_data": bool(proposed_fields),
            "expected_fields": expected_fields,
            "replacement_fields": proposed_fields,
        },
    }


def load_moon_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def build_report(
    input_path: Path,
    shine_info_path: Path,
    world_list_path: Path,
    stage_data_dir: Path,
) -> dict[str, Any]:
    rows = load_moon_rows(input_path)
    shine_info, shine_positions = load_shine_info(shine_info_path)
    world_list = load_world_list(world_list_path)

    row_runtime_keys = [
        parse_runtime_shine_key(row.get("runtime_shine_key") or row.get("moon_id", ""))
        for row in rows
    ]
    inventory_keys = {
        (key[0], key[1])
        for key in row_runtime_keys
        if key is not None
    }
    stage_maps, stage_fingerprint, archive_count = scan_stage_maps(
        stage_data_dir, inventory_keys
    )

    records: list[dict[str, Any]] = []
    for row_number, (row, runtime_key) in enumerate(
        zip(rows, row_runtime_keys), start=2
    ):
        candidates = shine_info.get(runtime_key, []) if runtime_key else []
        records.append(
            make_record(
                row_number,
                row,
                candidates,
                shine_positions,
                world_list,
                stage_maps,
            )
        )

    status_counts = Counter(record["status"] for record in records)
    incorrect_by_kingdom = Counter(
        record["kingdom"]
        for record in records
        if record["status"] == "incorrect"
    )
    moon_rock_metadata_mismatches = sum(
        1
        for record in records
        if "shine_info_is_moon_rock"
        in record["proposed_change"]["replacement_fields"]
    )
    proposed_changes = [
        {
            "moon_id": record["moon_id"],
            "runtime_shine_key": record["runtime_shine_key"],
            "moon_name": record["moon_name"],
            "kingdom": record["kingdom"],
            **record["proposed_change"],
        }
        for record in records
        if record["proposed_change"]["can_apply_without_game_data"]
    ]

    snapshot_payload = {
        "moon_access_sha256": file_sha256(input_path),
        "shine_info_sha256": file_sha256(shine_info_path),
        "world_list_sha256": file_sha256(world_list_path),
        "stage_data_sha256": stage_fingerprint,
    }
    snapshot_id = hashlib.sha256(
        json.dumps(snapshot_payload, sort_keys=True).encode("utf-8")
    ).hexdigest()

    return {
        "schema_version": REPORT_SCHEMA_VERSION,
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "snapshot_id": snapshot_id,
        "sources": {
            "moon_access_csv": str(input_path.resolve()),
            "shine_info_archive": str(shine_info_path.resolve()),
            "world_list_archive": str(world_list_path.resolve()),
            "stage_data_directory": str(stage_data_dir.resolve()),
            "stage_map_archives_scanned": archive_count,
            **snapshot_payload,
        },
        "method": {
            "authoritative_scenario_source": "ShineList.ProgressBitFlag",
            "scenario_decode": "bit N maps to ScenarioNo N+1",
            "shine_pos_role": "cross-check only; coordinates are not spawn availability",
            "stage_map_role": "independent placement cross-check; indices are zero-based",
            "apply_note": (
                "Use proposed_changes as the input to a later fixer. "
                "No game archives need to be opened again."
            ),
        },
        "summary": {
            "total_moons": len(records),
            "verified": status_counts["verified"],
            "incorrect": status_counts["incorrect"],
            "needs_review": status_counts["needs_review"],
            "moon_rock_metadata_mismatches": moon_rock_metadata_mismatches,
            "proposed_change_count": len(proposed_changes),
            "incorrect_by_kingdom": dict(sorted(incorrect_by_kingdom.items())),
        },
        "proposed_changes": proposed_changes,
        "records": records,
    }


CSV_FIELDS = [
    "status",
    "kingdom",
    "moon_name",
    "moon_id",
    "runtime_shine_key",
    "current_scenarios",
    "authoritative_scenarios",
    "progress_bit_flag",
    "shine_pos_scenarios",
    "stage_map_indices",
    "stage_map_scenarios",
    "stage_map_relation",
    "current_is_moon_rock",
    "effective_is_moon_rock",
    "moon_rock_scenario",
    "proposed_scenarios",
    "proposed_is_moon_rock",
    "reason",
]


def write_csv_report(path: Path, records: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=CSV_FIELDS)
        writer.writeheader()
        for record in records:
            authoritative = record["authoritative"]
            cross_checks = record["cross_checks"]
            replacements = record["proposed_change"]["replacement_fields"]
            writer.writerow(
                {
                    "status": record["status"],
                    "kingdom": record["kingdom"],
                    "moon_name": record["moon_name"],
                    "moon_id": record["moon_id"],
                    "runtime_shine_key": record["runtime_shine_key"],
                    "current_scenarios": record["current"][
                        "available_scenarios_text"
                    ],
                    "authoritative_scenarios": authoritative[
                        "available_scenarios_text"
                    ],
                    "progress_bit_flag": authoritative["progress_bit_flag"],
                    "shine_pos_scenarios": format_scenario_list(
                        cross_checks["shine_pos_scenarios"]
                    ),
                    "stage_map_indices": format_scenario_list(
                        cross_checks["stage_map_indices"]
                    ),
                    "stage_map_scenarios": format_scenario_list(
                        cross_checks["stage_map_scenarios"]
                    ),
                    "stage_map_relation": cross_checks["stage_map_relation"],
                    "current_is_moon_rock": str(
                        record["current"]["is_moon_rock"]
                    ).lower(),
                    "effective_is_moon_rock": (
                        ""
                        if authoritative["effective_is_moon_rock"] is None
                        else str(
                            authoritative["effective_is_moon_rock"]
                        ).lower()
                    ),
                    "moon_rock_scenario": authoritative["moon_rock_scenario"],
                    "proposed_scenarios": replacements.get(
                        "shine_info_available_scenarios", ""
                    ),
                    "proposed_is_moon_rock": replacements.get(
                        "shine_info_is_moon_rock", ""
                    ),
                    "reason": " | ".join(record["reasons"]),
                }
            )


def write_json_report(path: Path, report: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Audit every moon's extracted scenario availability."
    )
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument(
        "--system-data-dir", type=Path, default=DEFAULT_SYSTEM_DATA
    )
    parser.add_argument(
        "--stage-data-dir", type=Path, default=DEFAULT_STAGE_DATA
    )
    parser.add_argument("--json-output", type=Path, default=DEFAULT_JSON_OUTPUT)
    parser.add_argument("--csv-output", type=Path, default=DEFAULT_CSV_OUTPUT)
    args = parser.parse_args()

    shine_info_path = args.system_data_dir / "ShineInfo.szs"
    world_list_path = args.system_data_dir / "WorldList.szs"
    for required in [
        args.input,
        shine_info_path,
        world_list_path,
        args.stage_data_dir,
    ]:
        if not required.exists():
            parser.error(f"required input does not exist: {required}")

    report = build_report(
        args.input,
        shine_info_path,
        world_list_path,
        args.stage_data_dir,
    )
    write_json_report(args.json_output, report)
    write_csv_report(args.csv_output, report["records"])

    summary = report["summary"]
    print(f"Audited {summary['total_moons']} moons.")
    print(
        f"Verified: {summary['verified']}; "
        f"incorrect: {summary['incorrect']}; "
        f"needs review: {summary['needs_review']}."
    )
    print(
        "Reusable proposed changes: "
        f"{summary['proposed_change_count']} "
        f"(moon-rock metadata mismatches: "
        f"{summary['moon_rock_metadata_mismatches']})."
    )
    print(f"JSON report: {args.json_output}")
    print(f"CSV report: {args.csv_output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
