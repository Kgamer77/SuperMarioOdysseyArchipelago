#!/usr/bin/env python3
"""Generate human-fillable logic mapping templates.

The generated CSV files are intentionally spreadsheet-friendly. They are not
game data yet; they are the source of truth humans can fill before a compiler
or converter turns the graph into runtime logic.
"""

from __future__ import annotations

import csv
import json
import re
from collections import defaultdict
from pathlib import Path

from fixed_stage_links import (
    fixed_transition_edges,
    semanticize_fixed_transition_nodes,
)
from embedded_zones import (
    EMBEDDED_ZONE_FIELDS,
    discover_embedded_zones,
    flatten_embedded_zones,
    validate_ambiguous_zones,
)

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "logic" / "templates"
STAGE_DATA_DIRS = [
    ROOT / "rawData" / "StageData",
    ROOT / "smoStageData",
    ROOT / "romfs" / "StageData",
    ROOT / "extraction" / "StageData",
]
SYSTEM_DATA_DIRS = [
    ROOT / "rawData" / "SystemData",
    ROOT / "romfs" / "SystemData",
    ROOT / "extraction" / "SystemData",
]

LOGIC_STAGE_OVERRIDES: dict[tuple[str, int, int, str], str] = {
    (
        "SkyWorldShopStage",
        36,
        360,
        "Bowser's Castle Treasure Vault",
    ): "SkyWorldTreasureStage",
}
def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def cpp_unescape(value: str) -> str:
    return value.encode("utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8")


def split_line_comments(text: str) -> str:
    return "\n".join(line.split("//", 1)[0] for line in text.splitlines())


def node_id(stage: str, kind: str, ident: str | None = None, side: str | None = None) -> str:
    parts = [stage, kind]
    if ident:
        parts.append(ident)
    if side:
        parts.append(side)
    return "::".join(parts)


def slugify(value: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "_", value.lower()).strip("_")
    return slug or "unknown"


def area_scope(stage: str, stage_world: dict[str, dict[str, str]]) -> str:
    world = stage_world.get(stage)
    if is_overworld_stage(stage, world):
        return "overworld"
    return "subarea"


def is_overworld_stage(stage: str, world: dict[str, str] | None = None) -> bool:
    return bool(world and stage == world.get("main_stage"))


def parse_worlds() -> tuple[dict[int, dict[str, str]], dict[str, dict[str, str]]]:
    text = read_text(ROOT / "include" / "Randomizer" / "WorldMeta.h")
    pattern = re.compile(
        r'\{\s*(?P<world_id>\d+),\s*(?P<scenario_world_id>\d+),\s*"(?P<develop>[^"]+)",\s*'
        r'"(?P<display>[^"]+)",\s*u"[^"]+",\s*"(?P<main_stage>[^"]+)"'
    )
    by_id: dict[int, dict[str, str]] = {}
    by_stage: dict[str, dict[str, str]] = {}
    for match in pattern.finditer(text):
        row = match.groupdict()
        row["world_id"] = int(row["world_id"])
        row["scenario_world_id"] = int(row["scenario_world_id"])
        by_id[row["world_id"]] = row
        by_stage[row["main_stage"]] = row
    return by_id, by_stage


def parse_stage_names() -> tuple[dict[str, str], list[dict[str, str]]]:
    text = read_text(ROOT / "src" / "stage_warp.cpp")
    names: dict[str, str] = {}
    entries: list[dict[str, str]] = []
    for stage, display in re.findall(r'\{\s*(?:u8)?"([^"]+)",\s*(?:u8)?"([^"]+)"\s*\}', text):
        display = cpp_unescape(display)
        names[stage] = display
        entries.append({"stage": stage, "stage_display_name": display})
    return names, entries


def parse_shines() -> list[dict[str, str]]:
    text = read_text(ROOT / "include" / "ShineDB.h")
    pattern = re.compile(
        r'\{\s*(?P<obj_id>-?\d+),\s*(?P<uid>-?\d+),\s*(?P<world_id>-?\d+),\s*'
        r'(?:u8)?"(?P<stage>[^"]+)",\s*(?:u8)?"(?P<name>(?:[^"\\]|\\.)*)",\s*(?P<flags>[^}]+?)\s*\},'
    )
    rows: list[dict[str, str]] = []
    for match in pattern.finditer(text):
        row = match.groupdict()
        row["obj_id"] = int(row["obj_id"])
        row["uid"] = int(row["uid"])
        row["world_id"] = int(row["world_id"])
        row["name"] = cpp_unescape(row["name"])
        row["flags"] = " ".join(row["flags"].split())
        row["source_index"] = len(rows)
        row["runtime_shine_key"] = f'{row["stage"]}:{row["obj_id"]}:{row["uid"]}'
        row["stage"] = LOGIC_STAGE_OVERRIDES.get(
            (row["stage"], row["obj_id"], row["uid"], row["name"]),
            row["stage"],
        )
        rows.append(row)
    counts: dict[str, int] = defaultdict(int)
    for row in rows:
        counts[f'{row["stage"]}:{row["obj_id"]}:{row["uid"]}'] += 1
    for row in rows:
        logic_key = f'{row["stage"]}:{row["obj_id"]}:{row["uid"]}'
        if counts[logic_key] == 1:
            row["moon_id"] = logic_key
        else:
            row["moon_id"] = f'{logic_key}:{slugify(row["name"])}'
    return rows


def parse_entrances() -> list[dict[str, str]]:
    path = ROOT / "config" / "entrance_db2_legacy_links.csv"
    with path.open(newline="", encoding="utf-8-sig") as handle:
        source_rows = list(csv.DictReader(handle))

    rows: list[dict[str, str]] = []
    for row in source_rows:
        rows.append(
            {
                "link_index": str(len(rows)),
                "home_stage": row.get("home_stage", "").strip(),
                "home_entrance_in": row.get("home_entrance_in", "").strip(),
                "home_entrance_out": row.get("home_entrance_out", "").strip(),
                "sub_stage": row.get("sub_stage", "").strip(),
                "sub_entrance_in": row.get("sub_entrance_in", "").strip(),
                "sub_entrance_out": row.get("sub_entrance_out", "").strip(),
                "rando_status": (row.get("status", "") or "unknown").strip() or "unknown",
                "notes": row.get("notes", "").strip(),
            }
        )
    return rows


def parse_abilities() -> list[dict[str, str]]:
    text = read_text(ROOT / "src" / "Randomizer" / "AbilityRando.cpp")
    pattern = re.compile(
        r'\{\s*AbilityId_(?P<id>[^,]+),\s*"(?P<key>[^"]+)",\s*"(?P<display>[^"]+)",\s*'
        r'AbilityCategory_(?P<category>[^,]+),\s*(?P<prereq>[^,]+),\s*'
        r'(?P<always>true|false),\s*(?P<requires_throw>true|false),\s*(?P<can_start>true|false)\s*\}'
    )
    rows: list[dict[str, str]] = []
    for match in pattern.finditer(text):
        row = match.groupdict()
        row["token_type"] = "ability"
        row["token"] = row["key"]
        row["aliases"] = row["display"]
        rows.append(row)
    return rows


def parse_captures() -> list[dict[str, str]]:
    text = read_text(ROOT / "src" / "Randomizer" / "CaptureRando.cpp")
    captures_block = text.split("static constexpr CaptureEntry sCaptures[]", 1)[1]
    captures_block = captures_block.split("};", 1)[0]
    alias_block = text.split("static constexpr CaptureAlias sCaptureAliases[]", 1)[1]
    alias_block = alias_block.split("};", 1)[0]

    aliases_by_hack: dict[str, list[str]] = defaultdict(list)
    for alias, hack in re.findall(r'\{\s*"([^"]+)",\s*"([^"]+)"\s*\}', alias_block):
        aliases_by_hack[hack].append(alias)

    rows: list[dict[str, str]] = []
    for hack_name, display in re.findall(r'\{\s*"([^"]+)",\s*"([^"]+)"\s*\}', captures_block):
        aliases = [display] + aliases_by_hack.get(hack_name, [])
        rows.append(
            {
                "token_type": "capture",
                "token": hack_name,
                "key": hack_name,
                "display": display,
                "category": "Capture",
                "prereq": "",
                "always": "false",
                "requires_throw": "false",
                "can_start": "false",
                "aliases": "; ".join(aliases),
            }
        )
    return rows


def parse_legacy_sheet(shines: list[dict[str, str]], worlds: dict[int, dict[str, str]]) -> dict[str, str]:
    path = ROOT / "MoonAbilityRequirements.csv"
    if not path.exists():
        return {}

    with path.open(newline="", encoding="utf-8-sig", errors="replace") as handle:
        rows = list(csv.reader(handle))

    legacy_by_suffix: dict[str, list[str]] = defaultdict(list)
    for index, row in enumerate(rows):
        if len(row) < 2 or row[0].strip().upper() != "TRUE" or not row[1].strip():
            continue
        full_name = row[1].strip()
        suffix = full_name.split(": ", 1)[-1].strip()
        cap_row = rows[index + 1] if index + 1 < len(rows) else []
        other_row = rows[index + 2] if index + 2 < len(rows) else []

        parts: list[str] = []
        if len(row) > 2 and row[2].strip():
            parts.append(f"captures={row[2].strip()}")
        for method_index, col in enumerate(range(4, 9), start=1):
            bits: list[str] = []
            jump = row[col].strip() if col < len(row) else ""
            cap_throw = cap_row[col].strip() if col < len(cap_row) else ""
            other = other_row[col].strip() if col < len(other_row) else ""
            if jump:
                bits.append(f"jump={jump}")
            if cap_throw:
                bits.append(f"cap_throw={cap_throw}")
            if other:
                bits.append(f"other={other}")
            if bits:
                parts.append(f"method_{method_index}({'; '.join(bits)})")
        if parts:
            legacy_by_suffix[suffix].append(" | ".join(parts))

    result: dict[str, str] = {}
    for shine in shines:
        kingdom = worlds.get(shine["world_id"], {}).get("display", "")
        full = f'{kingdom}: {shine["name"]}' if kingdom else shine["name"]
        candidates = legacy_by_suffix.get(shine["name"], [])
        result[moon_id(shine)] = " || ".join(dict.fromkeys(candidates))
        if not result[moon_id(shine)] and full in legacy_by_suffix:
            result[moon_id(shine)] = " || ".join(dict.fromkeys(legacy_by_suffix[full]))
    return result


def iter_stage_map_archives():
    seen: set[str] = set()
    for stage_dir in STAGE_DATA_DIRS:
        if not stage_dir.exists():
            continue
        for path in sorted(stage_dir.rglob("*Map.szs")):
            stage = path.name.rsplit("Map.szs", 1)[0]
            if stage in seen:
                continue
            seen.add(stage)
            yield stage, path


def system_data_archive_path(name: str) -> Path | None:
    for system_dir in SYSTEM_DATA_DIRS:
        path = system_dir / name
        if path.exists():
            return path
    return None


def moon_id(shine: dict[str, str]) -> str:
    return shine.get("moon_id") or f'{shine["stage"]}:{shine["obj_id"]}:{shine["uid"]}'


def maybe_import_oead():
    try:
        import oead  # type: ignore

        return oead
    except Exception:
        return None


def byml_to_plain(value):
    oead = maybe_import_oead()
    if oead and isinstance(value, oead.byml.Hash):
        return {str(key): byml_to_plain(val) for key, val in value.items()}
    if oead and isinstance(value, oead.byml.Array):
        return [byml_to_plain(item) for item in value]
    return value


def as_float_string(value) -> str:
    if value is None:
        return ""
    text = str(value)
    try:
        return f"{float(text):.3f}"
    except ValueError:
        return text


def as_int_string(value) -> str:
    if value is None:
        return ""
    try:
        return str(int(value))
    except (TypeError, ValueError):
        text = str(value)
        return text if text else ""


def as_bool_string(value) -> str:
    if value is None:
        return ""
    return str(bool(value)).lower()


def scenarios_from_progress_bit_flag(value) -> str:
    """Decode ShineInfo's zero-based availability bits into ScenarioNo values."""
    try:
        mask = int(value)
    except (TypeError, ValueError):
        return ""
    if mask < 0:
        return ""
    return ";".join(str(bit + 1) for bit in range(32) if mask & (1 << bit))


def hget(hash_obj, key: str, default=None):
    return hash_obj[key] if key in hash_obj else default


def merge_semicolon_values(left: str, right: str) -> str:
    values = [value for value in [*left.split(";"), *right.split(";")] if value]
    return ";".join(dict.fromkeys(values))


def blank_placement_row(x: str = "", y: str = "", z: str = "") -> dict[str, str]:
    return {
        "placement_found": "true",
        "placement_unit_config_names": "",
        "placement_layer_config_names": "",
        "placement_scenario_no_values": "",
        "placement_stage_map_indices": "",
        "placement_quest_no_values": "",
        "placement_shine_optional_ids": "",
        "placement_change_stage_ids": "",
        "placement_x": x,
        "placement_y": y,
        "placement_z": z,
        "placement_source_archives": "",
        "placement_source_files": "",
        "placement_count": "0",
        "shine_info_hint_idx": "",
        "shine_info_main_scenario_no": "",
        "shine_info_progress_bit_flag": "",
        "shine_info_is_grand": "",
        "shine_info_is_moon_rock": "",
        "shine_info_available_scenarios": "",
        "shine_info_object_name": "",
        "shine_info_scenario_name": "",
    }


def iter_stage_objects(value, scenario_index: int | None = None):
    oead = maybe_import_oead()
    if oead and isinstance(value, oead.byml.Hash):
        if "Id" in value and "UnitConfigName" in value:
            yield value, scenario_index
        for key, child in value.items():
            if str(key) in {"Links", "UnitConfig"}:
                continue
            yield from iter_stage_objects(child, scenario_index)
    elif oead and isinstance(value, oead.byml.Array):
        for child in value:
            yield from iter_stage_objects(child, scenario_index)


def parse_stage_points() -> list[dict[str, str]]:
    oead = maybe_import_oead()
    if not oead:
        return []

    points: dict[tuple[str, str, str, str], dict[str, str]] = {}
    for archive_stage, path in iter_stage_map_archives():
        try:
            archive_data = oead.yaz0.decompress(path.read_bytes())
            archive = oead.Sarc(archive_data)
        except Exception:
            continue

        for file in archive.get_files():
            if not file.name.endswith(".byml"):
                continue
            try:
                root = oead.byml.from_binary(file.data)
            except Exception:
                continue
            scenario_roots = list(root) if isinstance(root, oead.byml.Array) else [root]
            for scenario_index, scenario in enumerate(scenario_roots):
                for obj, obj_scenario in iter_stage_objects(scenario, scenario_index):
                    unit = str(hget(obj, "UnitConfigName", ""))
                    change_id = hget(obj, "ChangeStageId")
                    change_name = hget(obj, "ChangeStageName")
                    obj_id = str(hget(obj, "Id", ""))
                    stage = str(hget(obj, "PlacementFileName", "")) or archive_stage
                    if unit == "CheckpointFlag":
                        kind = "checkpoint"
                        ident = obj_id
                    elif change_id and "ForDevelop" not in unit and not str(change_id).startswith("Ghost"):
                        kind = "stage_change"
                        ident = str(change_id)
                    else:
                        continue

                    translate = hget(obj, "Translate", {}) or {}
                    key = (stage, kind, ident, obj_id)
                    row = points.setdefault(
                        key,
                        {
                            "stage": stage,
                            "point_type": kind,
                            "identifier": ident,
                            # Destination file (ChangeStageName). Kept so the logic graph can
                            # pair a loading zone with its far side directly, instead of leaning
                            # on the hand-curated entrance_db2_legacy_links.csv. Empty for
                            # checkpoints and any object without a destination.
                            "target_stage": str(change_name) if change_name and kind == "stage_change" else "",
                            "obj_id": obj_id,
                            "unit_config_name": unit,
                            "x": as_float_string(hget(translate, "X") if hasattr(translate, "__contains__") else None),
                            "y": as_float_string(hget(translate, "Y") if hasattr(translate, "__contains__") else None),
                            "z": as_float_string(hget(translate, "Z") if hasattr(translate, "__contains__") else None),
                            "scenario_indices": "",
                            "source_archive": path.name,
                            "source_file": file.name,
                        },
                    )
                    scenarios = set(filter(None, row["scenario_indices"].split(";")))
                    if obj_scenario is not None:
                        scenarios.add(str(obj_scenario))
                    row["scenario_indices"] = ";".join(sorted(scenarios, key=int))
    return list(points.values())


def parse_moon_placements(shines: list[dict[str, str]]) -> dict[str, dict[str, str]]:
    oead = maybe_import_oead()
    if not oead:
        return {}

    moon_ids_by_stage_obj: dict[tuple[str, str], list[str]] = defaultdict(list)
    for shine in shines:
        if int(shine["obj_id"]) < 0:
            continue
        moon_ids_by_stage_obj[(shine["stage"], f'obj{shine["obj_id"]}')].append(moon_id(shine))

    moon_keys = set(moon_ids_by_stage_obj)
    placements: dict[str, dict[str, str]] = {}
    set_fields: dict[str, dict[str, set[str]]] = defaultdict(lambda: defaultdict(set))

    for archive_stage, path in iter_stage_map_archives():
        try:
            archive_data = oead.yaz0.decompress(path.read_bytes())
            archive = oead.Sarc(archive_data)
        except Exception:
            continue

        for file in archive.get_files():
            if not file.name.endswith(".byml"):
                continue
            try:
                root = oead.byml.from_binary(file.data)
            except Exception:
                continue
            scenario_roots = list(root) if isinstance(root, oead.byml.Array) else [root]
            for scenario_index, scenario in enumerate(scenario_roots):
                for obj, obj_scenario in iter_stage_objects(scenario, scenario_index):
                    obj_id = str(hget(obj, "Id", ""))
                    stage = str(hget(obj, "PlacementFileName", "")) or archive_stage
                    if (stage, obj_id) not in moon_keys:
                        continue

                    translate = hget(obj, "Translate", {}) or {}
                    for placement_key in moon_ids_by_stage_obj[(stage, obj_id)]:
                        row = placements.setdefault(
                            placement_key,
                            blank_placement_row(
                                as_float_string(hget(translate, "X") if hasattr(translate, "__contains__") else None),
                                as_float_string(hget(translate, "Y") if hasattr(translate, "__contains__") else None),
                                as_float_string(hget(translate, "Z") if hasattr(translate, "__contains__") else None),
                            ),
                        )
                        row["placement_count"] = str(int(row["placement_count"]) + 1)

                        fields = set_fields[placement_key]
                        for field_name, value in [
                            ("placement_unit_config_names", hget(obj, "UnitConfigName")),
                            ("placement_layer_config_names", hget(obj, "LayerConfigName")),
                            ("placement_scenario_no_values", hget(obj, "ScenarioNo")),
                            ("placement_quest_no_values", hget(obj, "QuestNo")),
                            ("placement_shine_optional_ids", hget(obj, "ShineOptionalId")),
                            ("placement_change_stage_ids", hget(obj, "ChangeStageId")),
                        ]:
                            if value is not None and str(value) != "":
                                fields[field_name].add(str(value))
                        if obj_scenario is not None:
                            fields["placement_stage_map_indices"].add(str(obj_scenario))
                        fields["placement_source_archives"].add(path.name)
                        fields["placement_source_files"].add(file.name)

    for key, fields in set_fields.items():
        row = placements[key]
        for field_name, values in fields.items():
            row[field_name] = ";".join(sorted(values))

    for key, shine_row in parse_shine_info_placements(shines).items():
        row = placements.setdefault(key, blank_placement_row())
        row["placement_found"] = "true"
        for field_name in ["placement_x", "placement_y", "placement_z"]:
            if not row.get(field_name):
                row[field_name] = shine_row.get(field_name, "")
        for field_name in ["placement_source_archives", "placement_source_files"]:
            row[field_name] = merge_semicolon_values(row.get(field_name, ""), shine_row.get(field_name, ""))
        if row.get("placement_count", "0") == "0":
            row["placement_count"] = "1"
        for field_name, value in shine_row.items():
            if field_name.startswith("shine_info_"):
                row[field_name] = value
    return placements


def parse_shine_info_placements(shines: list[dict[str, str]]) -> dict[str, dict[str, str]]:
    oead = maybe_import_oead()
    path = system_data_archive_path("ShineInfo.szs")
    if not oead or not path:
        return {}

    try:
        archive = oead.Sarc(oead.yaz0.decompress(path.read_bytes()))
    except Exception:
        return {}

    shine_by_key: dict[tuple[str, str, str], list[dict[str, str]]] = defaultdict(list)
    for shine in shines:
        if int(shine["obj_id"]) >= 0:
            shine_by_key[(shine["stage"], f'obj{shine["obj_id"]}', str(shine["uid"]))].append(shine)

    # ShinePosList supplies per-scenario map/hint coordinates. Its keys are not
    # moon spawn availability: entries may exist for scenarios where the moon is
    # disabled. Keep only UID presence here for source provenance.
    shine_pos_uids: set[str] = set()
    try:
        shine_pos = oead.byml.from_binary(archive.get_file("ShinePosList.byml").data)
        for row in shine_pos:
            uid = as_int_string(hget(row, "UniqueId"))
            if uid:
                shine_pos_uids.add(uid)
    except Exception:
        pass

    world_info_by_home_stage: dict[str, dict[str, object]] = {}
    world_list_path = system_data_archive_path("WorldList.szs")
    if world_list_path:
        try:
            world_archive = oead.Sarc(oead.yaz0.decompress(world_list_path.read_bytes()))
            world_file = world_archive.get_file("WorldList.byml")
            world_list = oead.byml.from_binary(world_file.data)
            for world_info in world_list:
                home_stage = str(hget(world_info, "Name", ""))
                if home_stage:
                    world_info_by_home_stage[home_stage] = {
                        "world_name": str(hget(world_info, "WorldName", "")),
                        "moon_rock_scenario": hget(world_info, "MoonRockScenario"),
                    }
        except Exception:
            pass

    placements: dict[str, dict[str, str]] = {}
    for file in archive.get_files():
        if not file.name.startswith("ShineList_") or not file.name.endswith(".byml"):
            continue

        try:
            root = oead.byml.from_binary(file.data)
        except Exception:
            continue

        for shine_info in hget(root, "ShineList", []) or []:
            source_stage = str(hget(shine_info, "StageName", ""))
            obj_id = str(hget(shine_info, "ObjId", ""))
            uid = as_int_string(hget(shine_info, "UniqueId"))
            shine_candidates = shine_by_key.get((source_stage, obj_id, uid), [])
            shine = select_shine_info_candidate(
                shine_candidates,
                str(hget(shine_info, "ObjectName", "")),
                str(hget(shine_info, "ScenarioName", "")),
            )
            if not shine:
                continue

            progress_bit_flag = hget(shine_info, "ProgressBitFlag")
            available_scenarios = scenarios_from_progress_bit_flag(progress_bit_flag)
            raw_is_moon_rock = bool(hget(shine_info, "IsMoonRock", False))
            home_stage = file.name[len("ShineList_") : -len(".byml")]
            world_info = world_info_by_home_stage.get(home_stage, {})
            moon_rock_scenario = as_int_string(world_info.get("moon_rock_scenario"))
            effective_is_moon_rock = raw_is_moon_rock
            if world_info.get("world_name") == "Peach":
                effective_is_moon_rock = False
            elif moon_rock_scenario:
                moon_rock_mask = 1 << (int(moon_rock_scenario) - 1)
                try:
                    effective_is_moon_rock = effective_is_moon_rock or int(progress_bit_flag) == moon_rock_mask
                except (TypeError, ValueError):
                    pass

            translate = hget(shine_info, "Trans", {}) or {}
            placements[moon_id(shine)] = {
                **blank_placement_row(
                    as_float_string(hget(translate, "X") if hasattr(translate, "__contains__") else None),
                    as_float_string(hget(translate, "Y") if hasattr(translate, "__contains__") else None),
                    as_float_string(hget(translate, "Z") if hasattr(translate, "__contains__") else None),
                ),
                "placement_source_archives": path.name,
                "placement_source_files": f"{file.name};ShinePosList.byml" if uid in shine_pos_uids else file.name,
                "placement_count": "1",
                "shine_info_hint_idx": as_int_string(hget(shine_info, "HintIdx")),
                "shine_info_main_scenario_no": as_int_string(hget(shine_info, "MainScenarioNo")),
                "shine_info_progress_bit_flag": as_int_string(progress_bit_flag),
                "shine_info_is_grand": as_bool_string(hget(shine_info, "IsGrand")),
                "shine_info_is_moon_rock": as_bool_string(effective_is_moon_rock),
                "shine_info_available_scenarios": available_scenarios,
                "shine_info_object_name": str(hget(shine_info, "ObjectName", "")),
                "shine_info_scenario_name": str(hget(shine_info, "ScenarioName", "")),
            }
    return placements


def select_shine_info_candidate(
    candidates: list[dict[str, str]],
    object_name: str,
    scenario_name: str,
) -> dict[str, str] | None:
    if not candidates:
        return None
    if len(candidates) == 1:
        return candidates[0]

    context = f"{object_name} {scenario_name}"
    scored: list[tuple[int, int, dict[str, str]]] = []
    for index, candidate in enumerate(candidates):
        name = candidate["name"].lower()
        score = 0
        if "音符" in context and name.startswith("taking notes"):
            score += 20
        if "ショップ" in context or "お買い物" in context:
            if name.startswith("shopping"):
                score += 20
        if "宝箱" in context or "treasure" in context.lower():
            if "treasure" in name or "vault" in name:
                score += 20
        scored.append((score, -index, candidate))

    return max(scored, key=lambda row: (row[0], row[1]))[2]


def parse_scenario_metadata(stage_world: dict[str, dict[str, str]], stage_names: dict[str, str]) -> list[dict[str, str]]:
    oead = maybe_import_oead()
    if not oead:
        return []

    rows: list[dict[str, str]] = []
    for stage, path in iter_stage_map_archives():
        world = stage_world.get(stage, {})
        try:
            archive_data = oead.yaz0.decompress(path.read_bytes())
            archive = oead.Sarc(archive_data)
        except Exception:
            continue

        stage_map_indices: set[str] = set()
        scenario_info_rows: list[dict[str, str]] = []
        for file in archive.get_files():
            if not file.name.endswith(".byml"):
                continue
            try:
                root = oead.byml.from_binary(file.data)
            except Exception:
                continue
            if file.name.endswith("StageMap.byml"):
                if isinstance(root, oead.byml.Array):
                    stage_map_indices.update(str(i) for i in range(len(root)))
                else:
                    stage_map_indices.add("0")
            if file.name == "ScenarioInfo.byml":
                if isinstance(root, oead.byml.Hash):
                    for scenario_no, layers in root.items():
                        if isinstance(layers, oead.byml.Array):
                            active_layers = ";".join(str(layer) for layer in layers)
                        else:
                            active_layers = str(layers)
                        scenario_info_rows.append(
                            {
                                "scenario_no": str(scenario_no),
                                "active_layers": active_layers,
                                "source_file": file.name,
                                "status": "extracted",
                                "notes": "",
                            }
                        )
                else:
                    scenario_info_rows.append(
                        {
                            "scenario_no": "",
                            "active_layers": "",
                            "source_file": file.name,
                            "status": "raw",
                            "notes": f"ScenarioInfo type={type(root).__name__}; inspect manually if needed.",
                        }
                    )

        if not scenario_info_rows:
            scenario_info_rows.append(
                {
                    "scenario_no": "",
                    "active_layers": "",
                    "source_file": "",
                    "status": "missing",
                    "notes": "No ScenarioInfo.byml in archive.",
                }
            )

        for info in scenario_info_rows:
            rows.append(
                {
                    "stage": stage,
                    "kingdom": world.get("display", ""),
                    "stage_display_name": stage_names.get(stage, stage),
                    "area_scope": area_scope(stage, stage_world),
                    "scenario_no": info["scenario_no"],
                    "active_layers": info["active_layers"],
                    "stage_map_indices": ";".join(sorted(stage_map_indices, key=int)) if stage_map_indices else "",
                    "source_archive": path.name,
                    "source_file": info["source_file"],
                    "status": info["status"],
                    "notes": info["notes"],
                }
            )
    return rows


def write_csv(path: Path, rows: list[dict[str, str]], fields: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fields})


def make_progression_events(
    worlds_by_id: dict[int, dict[str, str]], stage_points: list[dict[str, str]]
) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    moon_rock_points = {
        point["stage"]: point
        for point in stage_points
        if point["identifier"] == "MoonRock" and point["unit_config_name"] == "PlayerStartObjNoLink"
    }
    for world_id in sorted(worlds_by_id):
        world = worlds_by_id[world_id]
        develop = world["develop"]
        main_stage = world["main_stage"]
        for event_type, token, display, notes in [
            (
                "world_peace",
                f"WorldPeace_{develop}",
                f'{world["display"]} World Peace',
                "Fill the story moons/bosses that must be completed for world peace in this kingdom.",
            ),
            (
                "moon_rock_active",
                f"MoonRockActive_{develop}",
                f'{world["display"]} Moon Rock Active',
                "Fill the route to the Moon Rock and any prerequisite story/global state.",
            ),
        ]:
            moon_rock_point = moon_rock_points.get(main_stage) if event_type == "moon_rock_active" else None
            event_node_id = node_id(main_stage, "event", token)
            rows.append(
                {
                    "event_token": token,
                    "event_type": event_type,
                    "kingdom": world["display"],
                    "world_id": str(world_id),
                    "develop_name": develop,
                    "main_stage": main_stage,
                    "event_node_id": event_node_id,
                    "event_location_node_id": event_node_id if moon_rock_point else "",
                    "display_name": "Moon Rock" if moon_rock_point else display,
                    "obj_id": moon_rock_point.get("obj_id", "") if moon_rock_point else "",
                    "x": moon_rock_point.get("x", "") if moon_rock_point else "",
                    "y": moon_rock_point.get("y", "") if moon_rock_point else "",
                    "z": moon_rock_point.get("z", "") if moon_rock_point else "",
                    "scenario_indices": moon_rock_point.get("scenario_indices", "") if moon_rock_point else "",
                    "requirement_expr": "",
                    "status": "todo",
                    "notes": (
                        f'{notes} Location imported from StageData obj {moon_rock_point["obj_id"]}.'
                        if moon_rock_point
                        else notes
                    ),
                }
            )
    return rows


def build_stage_world_map(
    worlds_by_id: dict[int, dict[str, str]],
    worlds_by_stage: dict[str, dict[str, str]],
    stage_entries: list[dict[str, str]],
    shines: list[dict[str, str]],
    entrances: list[dict[str, str]],
) -> dict[str, dict[str, str]]:
    stage_world: dict[str, dict[str, str]] = dict(worlds_by_stage)
    display_to_world = {world["display"]: world for world in worlds_by_id.values()}
    current_world: dict[str, str] | None = None
    for entry in stage_entries:
        stage = entry["stage"]
        display = entry["stage_display_name"]
        if stage in worlds_by_stage:
            current_world = worlds_by_stage[stage]
        elif display in display_to_world:
            current_world = display_to_world[display]
        if current_world:
            stage_world.setdefault(stage, current_world)
    for shine in shines:
        world = worlds_by_id.get(shine["world_id"])
        if world:
            stage_world.setdefault(shine["stage"], world)
    for link in entrances:
        world = stage_world.get(link["home_stage"])
        if world:
            stage_world.setdefault(link["sub_stage"], world)
    return stage_world


def make_logic_nodes(
    stage_world: dict[str, dict[str, str]],
    stage_names: dict[str, str],
    stage_entries: list[dict[str, str]],
    shines: list[dict[str, str]],
    entrances: list[dict[str, str]],
    stage_points: list[dict[str, str]],
    progression_events: list[dict[str, str]],
) -> list[dict[str, str]]:
    rows: dict[str, dict[str, str]] = {}
    stage_change_points: dict[tuple[str, str], list[dict[str, str]]] = defaultdict(list)
    for point in stage_points:
        if point["point_type"] == "stage_change":
            stage_change_points[(point["stage"], point["identifier"])].append(point)

    def add(row: dict[str, str]) -> None:
        rows.setdefault(row["node_id"], row)

    def stage_change_anchor(stage: str, ident: str) -> dict[str, str]:
        points = stage_change_points.get((stage, ident), [])
        if not points:
            return {"obj_id": "", "x": "", "y": "", "z": "", "scenario_indices": "", "notes": ""}

        scenario_indices: set[str] = set()
        obj_ids: set[str] = set()
        unit_configs: set[str] = set()
        for point in points:
            scenario_indices.update(filter(None, point["scenario_indices"].split(";")))
            if point["obj_id"]:
                obj_ids.add(point["obj_id"])
            if point["unit_config_name"]:
                unit_configs.add(point["unit_config_name"])

        coordinates = points[0] if len(points) == 1 else {"x": "", "y": "", "z": ""}
        return {
            "obj_id": ";".join(sorted(obj_ids)),
            "x": coordinates["x"],
            "y": coordinates["y"],
            "z": coordinates["z"],
            "scenario_indices": ";".join(sorted(scenario_indices, key=int)) if scenario_indices else "",
            "notes": f' StageData anchor UnitConfigName={";".join(sorted(unit_configs))}.' if unit_configs else "",
        }

    stages = set(stage_world)
    stages.update(entry["stage"] for entry in stage_entries)
    stages.update(shine["stage"] for shine in shines)
    for link in entrances:
        stages.add(link["home_stage"])
        stages.add(link["sub_stage"])
    for point in stage_points:
        stages.add(point["stage"])

    for stage in sorted(stages):
        world = stage_world.get(stage, {})
        add(
            {
                "node_id": node_id(stage, "start"),
                "kingdom": world.get("display", ""),
                "stage": stage,
                "stage_display_name": stage_names.get(stage, stage),
                "area_scope": area_scope(stage, stage_world),
                "node_type": "stage_start",
                "source": "generated",
                "entrance_id": "",
                "side": "",
                "obj_id": "",
                "display_name": "Default start",
                "x": "",
                "y": "",
                "z": "",
                "scenario_indices": "",
                "status": "seed",
                "notes": "Rename or split if this stage has multiple meaningful starts.",
            }
        )

    for event in progression_events:
        world = stage_world.get(event["main_stage"], {})
        add(
            {
                "node_id": event["event_node_id"],
                "kingdom": event["kingdom"],
                "stage": event["main_stage"],
                "stage_display_name": stage_names.get(event["main_stage"], event["main_stage"]),
                "area_scope": "overworld",
                "node_type": "state_event",
                "source": "StageData:progression_events" if event.get("obj_id") else "progression_events",
                "entrance_id": "",
                "side": "",
                "obj_id": event.get("obj_id", ""),
                "display_name": event.get("display_name", event["event_token"]),
                "x": event.get("x", ""),
                "y": event.get("y", ""),
                "z": event.get("z", ""),
                "scenario_indices": event.get("scenario_indices", ""),
                "status": event["status"],
                "notes": event["notes"] or world.get("display", ""),
            }
        )

    for link in entrances:
        world = stage_world.get(link["home_stage"], {})
        stage_display = stage_names.get(link["home_stage"], link["home_stage"])
        for kind, ident, side, display in [
            ("entrance", link["home_entrance_in"], "home_in", "Home entrance"),
            ("entrance", link["home_entrance_out"], "home_out", "Home return"),
        ]:
            source = "EntranceDB" if link.get("rando_status") == "active" else "EntranceDB:excluded"
            anchor = stage_change_anchor(link["home_stage"], ident)
            add(
                {
                    "node_id": node_id(link["home_stage"], kind, ident, side),
                    "kingdom": world.get("display", ""),
                    "stage": link["home_stage"],
                    "stage_display_name": stage_display,
                    "area_scope": area_scope(link["home_stage"], stage_world),
                    "node_type": "home_entrance",
                    "source": source,
                    "entrance_id": ident,
                    "side": side,
                    "obj_id": anchor["obj_id"],
                    "display_name": f"{display}: {ident}",
                    "x": anchor["x"],
                    "y": anchor["y"],
                    "z": anchor["z"],
                    "scenario_indices": anchor["scenario_indices"],
                    "status": "generated" if link.get("rando_status") == "active" else "excluded",
                    "notes": f'Links to {link["sub_stage"]}.{anchor["notes"]}',
                }
            )

        sub_world = stage_world.get(link["sub_stage"], world)
        sub_display = stage_names.get(link["sub_stage"], link["sub_stage"])
        for kind, ident, side, display in [
            ("entrance", link["sub_entrance_in"], "sub_near", "Substage near side"),
            ("entrance", link["sub_entrance_out"], "sub_far", "Substage far side"),
        ]:
            source = "EntranceDB" if link.get("rando_status") == "active" else "EntranceDB:excluded"
            anchor = stage_change_anchor(link["sub_stage"], ident)
            add(
                {
                    "node_id": node_id(link["sub_stage"], kind, ident, side),
                    "kingdom": sub_world.get("display", ""),
                    "stage": link["sub_stage"],
                    "stage_display_name": sub_display,
                    "area_scope": area_scope(link["sub_stage"], stage_world),
                    "node_type": "substage_entrance",
                    "source": source,
                    "entrance_id": ident,
                    "side": side,
                    "obj_id": anchor["obj_id"],
                    "display_name": f"{display}: {ident}",
                    "x": anchor["x"],
                    "y": anchor["y"],
                    "z": anchor["z"],
                    "scenario_indices": anchor["scenario_indices"],
                    "status": "generated" if link.get("rando_status") == "active" else "excluded",
                    "notes": f'Connected from {link["home_stage"]}.{anchor["notes"]}',
                }
            )

    for point in stage_points:
        if point["identifier"] == "MoonRock" and point["unit_config_name"] == "PlayerStartObjNoLink":
            continue
        world = stage_world.get(point["stage"], {})
        ident = point["identifier"]
        kind = "checkpoint" if point["point_type"] == "checkpoint" else "stage_change"
        side = "" if kind == "checkpoint" else "raw"
        add(
            {
                "node_id": node_id(point["stage"], kind, ident, side or None),
                "kingdom": world.get("display", ""),
                "stage": point["stage"],
                "stage_display_name": stage_names.get(point["stage"], point["stage"]),
                "area_scope": area_scope(point["stage"], stage_world),
                "node_type": kind,
                "source": "StageData",
                "entrance_id": ident if kind == "stage_change" else "",
                "side": side,
                "obj_id": point["obj_id"],
                "display_name": f'{kind.replace("_", " ").title()} {ident}',
                "x": point["x"],
                "y": point["y"],
                "z": point["z"],
                "scenario_indices": point["scenario_indices"],
                "status": "needs_name",
                "notes": f'Raw UnitConfigName={point["unit_config_name"]}.'
                + (f' Target={point["target_stage"]}.' if point.get("target_stage") else ""),
            }
        )

    result = list(rows.values())
    semanticize_fixed_transition_nodes(result)
    return sorted(result, key=lambda row: (row["kingdom"], row["stage"], row["node_type"], row["node_id"]))


def make_entrance_links(entrances: list[dict[str, str]], stage_world: dict[str, dict[str, str]]) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for link in entrances:
        world = stage_world.get(link["home_stage"], {})
        rows.append(
            {
                **{key: str(value) for key, value in link.items()},
                "kingdom": world.get("display", ""),
                "area_scope": area_scope(link["home_stage"], stage_world),
                "home_in_node_id": node_id(link["home_stage"], "entrance", link["home_entrance_in"], "home_in"),
                "home_out_node_id": node_id(link["home_stage"], "entrance", link["home_entrance_out"], "home_out"),
                "sub_near_node_id": node_id(link["sub_stage"], "entrance", link["sub_entrance_in"], "sub_near"),
                "sub_far_node_id": node_id(link["sub_stage"], "entrance", link["sub_entrance_out"], "sub_far"),
                "status": link.get("rando_status", "active"),
                "notes": link.get("notes", ""),
            }
        )
    return rows


def make_rooms(
    stage_world: dict[str, dict[str, str]],
    stage_names: dict[str, str],
    stage_entries: list[dict[str, str]],
    shines: list[dict[str, str]],
    entrances: list[dict[str, str]],
    stage_points: list[dict[str, str]],
) -> list[dict[str, str]]:
    listed_stages = {entry["stage"] for entry in stage_entries}
    shine_stages = {shine["stage"] for shine in shines}
    stage_data_stages = {point["stage"] for point in stage_points}
    active_counts: dict[str, int] = defaultdict(int)
    excluded_counts: dict[str, int] = defaultdict(int)
    for link in entrances:
        counts = active_counts if link.get("rando_status") == "active" else excluded_counts
        counts[link["home_stage"]] += 1
        counts[link["sub_stage"]] += 1

    stages = set(listed_stages) | set(stage_world) | shine_stages | stage_data_stages | set(active_counts) | set(excluded_counts)
    rows: list[dict[str, str]] = []
    for stage in sorted(stages, key=lambda item: (stage_world.get(item, {}).get("display", ""), item)):
        world = stage_world.get(stage, {})
        rows.append(
            {
                "stage": stage,
                "kingdom": world.get("display", ""),
                "stage_display_name": stage_names.get(stage, stage),
                "stage_start_node_id": node_id(stage, "start"),
                "area_scope": area_scope(stage, stage_world),
                "listed_in_stage_warp": str(stage in listed_stages).lower(),
                "has_shine_db_moons": str(stage in shine_stages).lower(),
                "active_entrance_link_count": str(active_counts.get(stage, 0)),
                "excluded_entrance_link_count": str(excluded_counts.get(stage, 0)),
                "has_raw_stage_data_points": str(stage in stage_data_stages).lower(),
                "status": "listed" if stage in listed_stages else "discovered",
                "notes": "Every listed room gets a stage_start node even when EntranceDB has no active link.",
            }
        )
    return rows


def make_runtime_anchors(nodes: list[dict[str, str]]) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for node in nodes:
        anchor_kind = ""
        if node["node_type"] in {"home_entrance", "substage_entrance"}:
            anchor_kind = "entrance_side"
        elif node["node_type"] == "stage_change":
            anchor_kind = "change_stage_id"
        elif node["node_type"] == "checkpoint":
            anchor_kind = "checkpoint_obj"
        elif node["node_type"] == "stage_start":
            anchor_kind = "stage_start"
        elif node["node_type"] == "state_event":
            anchor_kind = "state_event"
        else:
            continue

        rows.append(
            {
                "anchor_id": node["node_id"],
                "node_id": node["node_id"],
                "anchor_kind": anchor_kind,
                "kingdom": node["kingdom"],
                "stage": node["stage"],
                "area_scope": node["area_scope"],
                "change_stage_id": node["entrance_id"],
                "entrance_side": node["side"],
                "obj_id": node["obj_id"],
                "x": node["x"],
                "y": node["y"],
                "z": node["z"],
                "source": node["source"],
                "status": node.get("status", ""),
                "notes": "Runtime/stable anchor for graph initialization or event checks.",
            }
        )
    return rows


def write_kingdom_splits(
    output_root: Path,
    fields_by_file: dict[str, list[str]],
    data_by_file: dict[str, list[dict[str, str]]],
) -> None:
    split_root = output_root / "by_kingdom"
    kingdoms = sorted(
        {
            row.get("kingdom", "")
            for rows in data_by_file.values()
            for row in rows
            if row.get("kingdom")
        }
    )
    for kingdom in kingdoms:
        kingdom_dir = split_root / slugify(kingdom)
        for filename, rows in data_by_file.items():
            filtered = [row for row in rows if row.get("kingdom") == kingdom]
            if not filtered and filename != "route_edges.csv":
                continue
            write_csv(kingdom_dir / filename, filtered, fields_by_file[filename])


def write_area_graphs(
    output_root: Path,
    rooms: list[dict[str, str]],
    nodes: list[dict[str, str]],
    edges: list[dict[str, str]],
    moons: list[dict[str, str]],
    anchors: list[dict[str, str]],
    progression_events: list[dict[str, str]],
) -> None:
    graph_root = output_root / "area_graphs"
    expected_targets: set[Path] = set()
    nodes_by_stage: dict[str, list[dict[str, str]]] = defaultdict(list)
    moons_by_stage: dict[str, list[dict[str, str]]] = defaultdict(list)
    anchors_by_stage: dict[str, list[dict[str, str]]] = defaultdict(list)
    events_by_stage: dict[str, list[dict[str, str]]] = defaultdict(list)
    scenarios_by_stage: dict[str, list[dict[str, str]]] = defaultdict(list)
    for node in nodes:
        nodes_by_stage[node["stage"]].append(node)
    for moon in moons:
        moons_by_stage[moon["stage"]].append(moon)
    for anchor in anchors:
        anchors_by_stage[anchor["stage"]].append(anchor)
    for event in progression_events:
        events_by_stage[event["main_stage"]].append(event)
    scenario_path = output_root / "scenario_metadata.csv"
    if scenario_path.exists():
        with scenario_path.open(newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                scenarios_by_stage[row["stage"]].append(row)

    for room in rooms:
        stage = room["stage"]
        kingdom_slug = slugify(room["kingdom"])
        stage_slug = slugify(stage)
        room_node_ids = {node["node_id"] for node in nodes_by_stage.get(stage, [])}
        room_edges = [
            edge
            for edge in edges
            if edge["from_node_id"] in room_node_ids or edge["to_node_id"] in room_node_ids
        ]
        graph = {
            "room": room,
            "nodes": nodes_by_stage.get(stage, []),
            "edges": room_edges,
            "moons": moons_by_stage.get(stage, []),
            "anchors": anchors_by_stage.get(stage, []),
            "progression_events": events_by_stage.get(stage, []),
            "scenario_metadata": scenarios_by_stage.get(stage, []),
        }
        target = graph_root / kingdom_slug / f"{stage_slug}.json"
        expected_targets.add(target.resolve())
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(json.dumps(graph, indent=2), encoding="utf-8")

    if graph_root.exists():
        for existing in graph_root.rglob("*.json"):
            if existing.resolve() not in expected_targets:
                existing.unlink()


def make_route_edges(
    stage_world: dict[str, dict[str, str]], nodes: list[dict[str, str]], entrances: list[dict[str, str]]
) -> list[dict[str, str]]:
    # Connections are intentionally human-authored. Entrance metadata remains in
    # entrance_links.csv and logic_nodes.csv. Fixed vanilla transitions are the
    # exception: they are surfaced automatically and cannot be randomized.
    return fixed_transition_edges()


def make_moon_access(
    shines: list[dict[str, str]],
    worlds: dict[int, dict[str, str]],
    stage_names: dict[str, str],
    legacy: dict[str, str],
    moon_placements: dict[str, dict[str, str]],
) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for shine in shines:
        world = worlds.get(shine["world_id"], {})
        ident = moon_id(shine)
        placement = moon_placements.get(ident, {})
        moon_rock_token = f'MoonRockActive_{world.get("develop", "")}' if world else ""
        world_peace_token = f'WorldPeace_{world.get("develop", "")}' if world else ""
        rows.append(
            {
                "moon_id": ident,
                "kingdom": world.get("display", ""),
                "stage": shine["stage"],
                "stage_display_name": stage_names.get(shine["stage"], shine["stage"]),
                "area_scope": "overworld" if is_overworld_stage(shine["stage"], world) else "subarea",
                "moon_name": shine["name"],
                "runtime_shine_key": shine.get("runtime_shine_key", ident),
                "obj_id": str(shine["obj_id"]),
                "uid": str(shine["uid"]),
                "flags": shine["flags"],
                "from_node_id": "",
                "state_requirement_expr": "",
                "requirement_expr": "",
                "world_peace_token": world_peace_token,
                "moon_rock_token": moon_rock_token,
                "access_type": "primary",
                "effort_level": "unrated",
                "effort_notes": "",
                "status": "todo",
                "legacy_requirement_notes": legacy.get(ident, ""),
                "placement_found": placement.get("placement_found", "false"),
                "placement_unit_config_names": placement.get("placement_unit_config_names", ""),
                "placement_layer_config_names": placement.get("placement_layer_config_names", ""),
                "placement_scenario_no_values": placement.get("placement_scenario_no_values", ""),
                "scenario_move_to": "",
                "placement_stage_map_indices": placement.get("placement_stage_map_indices", ""),
                "placement_quest_no_values": placement.get("placement_quest_no_values", ""),
                "placement_shine_optional_ids": placement.get("placement_shine_optional_ids", ""),
                "placement_change_stage_ids": placement.get("placement_change_stage_ids", ""),
                "placement_x": placement.get("placement_x", ""),
                "placement_y": placement.get("placement_y", ""),
                "placement_z": placement.get("placement_z", ""),
                "placement_source_archives": placement.get("placement_source_archives", ""),
                "placement_source_files": placement.get("placement_source_files", ""),
                "placement_count": placement.get("placement_count", "0"),
                "shine_info_hint_idx": placement.get("shine_info_hint_idx", ""),
                "shine_info_main_scenario_no": placement.get("shine_info_main_scenario_no", ""),
                "shine_info_progress_bit_flag": placement.get("shine_info_progress_bit_flag", ""),
                "shine_info_is_grand": placement.get("shine_info_is_grand", ""),
                "shine_info_is_moon_rock": placement.get("shine_info_is_moon_rock", ""),
                "shine_info_available_scenarios": placement.get("shine_info_available_scenarios", ""),
                "shine_info_object_name": placement.get("shine_info_object_name", ""),
                "shine_info_scenario_name": placement.get("shine_info_scenario_name", ""),
                "notes": "ShineInfo ProgressBitFlag is authoritative moon scenario availability. StageMap indices are zero-based placement evidence; ShinePosList keys are positional only. Use requirement_expr for local ability/capture needs.",
            }
        )
    return rows


def make_tokens(
    abilities: list[dict[str, str]],
    captures: list[dict[str, str]],
    progression_events: list[dict[str, str]],
    worlds_by_id: dict[int, dict[str, str]],
) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for row in abilities + captures:
        rows.append(
            {
                "token": row["token"],
                "token_type": row["token_type"],
                "display_name": row["display"],
                "category": row["category"],
                "prerequisite": row["prereq"],
                "always_unlocked": row["always"],
                "requires_any_cap_throw": row["requires_throw"],
                "can_start_with_seed": row["can_start"],
                "aliases": row["aliases"],
                "notes": "",
            }
        )
    for event in progression_events:
        rows.append(
            {
                "token": event["event_token"],
                "token_type": "event",
                "display_name": event["event_token"],
                "category": event["event_type"],
                "prerequisite": "",
                "always_unlocked": "false",
                "requires_any_cap_throw": "false",
                "can_start_with_seed": "false",
                "aliases": event["notes"],
                "notes": f'See progression_events.csv for {event["kingdom"]}.',
            }
        )
    rows.extend(
        [
            {
                "token": "free",
                "token_type": "constant",
                "display_name": "Free",
                "category": "Logic",
                "prerequisite": "",
                "always_unlocked": "true",
                "requires_any_cap_throw": "false",
                "can_start_with_seed": "false",
                "aliases": "none; no requirement",
                "notes": "Use when no ability, capture, event, or item is required.",
            },
            {
                "token": "impossible",
                "token_type": "constant",
                "display_name": "Impossible",
                "category": "Logic",
                "prerequisite": "",
                "always_unlocked": "false",
                "requires_any_cap_throw": "false",
                "can_start_with_seed": "false",
                "aliases": "unreachable",
                "notes": "Use for intentionally blocked paths.",
            },
        ]
    )
    for world_id in sorted(worlds_by_id):
        world = worlds_by_id[world_id]
        develop = world["develop"]
        display = world["display"]
        rows.append(
            {
                "token": f"KingdomAccess_{develop}",
                "token_type": "kingdom",
                "display_name": f"{display} Access",
                "category": "Kingdom Access",
                "prerequisite": "",
                "always_unlocked": "false",
                "requires_any_cap_throw": "false",
                "can_start_with_seed": "false",
                "aliases": f"{display}",
                "notes": f"Requires being able to reach the {display}." + (" (e.g. a hint-art moon that depends on another kingdom)." if develop == "Cap" else ""),
            }
        )
    return rows


def main() -> None:
    worlds_by_id, worlds_by_stage = parse_worlds()
    stage_names, stage_entries = parse_stage_names()
    shines = parse_shines()
    entrances = parse_entrances()
    loading_stages = (
        set(worlds_by_stage)
        | {entry["stage"] for entry in stage_entries}
        | {
            stage
            for link in entrances
            for stage in (link["home_stage"], link["sub_stage"])
            if stage
        }
    )
    embedded_zones = discover_embedded_zones(STAGE_DATA_DIRS, loading_stages)
    abilities = parse_abilities()
    captures = parse_captures()
    legacy = parse_legacy_sheet(shines, worlds_by_id)
    stage_points = parse_stage_points()
    progression_events = make_progression_events(worlds_by_id, stage_points)
    stage_world = build_stage_world_map(worlds_by_id, worlds_by_stage, stage_entries, shines, entrances)
    moon_placements = parse_moon_placements(shines)
    scenario_metadata = parse_scenario_metadata(stage_world, stage_names)
    nodes = make_logic_nodes(stage_world, stage_names, stage_entries, shines, entrances, stage_points, progression_events)
    tokens = make_tokens(abilities, captures, progression_events, worlds_by_id)
    rooms = make_rooms(stage_world, stage_names, stage_entries, shines, entrances, stage_points)
    entrance_links = make_entrance_links(entrances, stage_world)
    route_edges = make_route_edges(stage_world, nodes, entrances)
    moon_access = make_moon_access(shines, worlds_by_id, stage_names, legacy, moon_placements)

    meaningful_embedded_stages = {
        node["stage"]
        for node in nodes
        if node["source"] not in {"generated", "StageData"}
        or node["node_type"] == "custom_place"
    }
    meaningful_embedded_stages.update(
        (moon.get("runtime_shine_key") or moon["moon_id"]).split(":", 1)[0]
        for moon in moon_access
    )
    node_stage = {node["node_id"]: node["stage"] for node in nodes}
    meaningful_embedded_stages.update(
        node_stage[node_id]
        for edge in route_edges
        for node_id in (edge["from_node_id"], edge["to_node_id"])
        if node_id in node_stage
    )
    validate_ambiguous_zones(embedded_zones, meaningful_embedded_stages)

    flattened = flatten_embedded_zones(
        nodes,
        route_edges,
        moon_access,
        rooms,
        scenario_metadata,
        embedded_zones,
    )
    nodes = flattened["nodes"]
    route_edges = flattened["routes"]
    moon_access = flattened["moons"]
    rooms = flattened["rooms"]
    scenario_metadata = flattened["scenario_metadata"]

    runtime_anchors = make_runtime_anchors(nodes)

    fields_by_file = {
        "logic_tokens.csv": [
            "token",
            "token_type",
            "display_name",
            "category",
            "prerequisite",
            "always_unlocked",
            "requires_any_cap_throw",
            "can_start_with_seed",
            "aliases",
            "notes",
        ],
        "rooms.csv": [
            "stage",
            "kingdom",
            "stage_display_name",
            "stage_start_node_id",
            "area_scope",
            "listed_in_stage_warp",
            "has_shine_db_moons",
            "active_entrance_link_count",
            "excluded_entrance_link_count",
            "has_raw_stage_data_points",
            "status",
            "notes",
        ],
        "embedded_zones.csv": EMBEDDED_ZONE_FIELDS,
        "progression_events.csv": [
            "event_token",
            "event_type",
            "kingdom",
            "world_id",
            "develop_name",
            "main_stage",
            "event_node_id",
            "event_location_node_id",
            "requirement_expr",
            "status",
            "notes",
        ],
        "scenario_metadata.csv": [
            "stage",
            "kingdom",
            "stage_display_name",
            "area_scope",
            "scenario_no",
            "active_layers",
            "stage_map_indices",
            "source_archive",
            "source_file",
            "status",
            "notes",
        ],
        "runtime_anchors.csv": [
            "anchor_id",
            "node_id",
            "anchor_kind",
            "kingdom",
            "stage",
            "area_scope",
            "change_stage_id",
            "entrance_side",
            "obj_id",
            "x",
            "y",
            "z",
            "source",
            "status",
            "notes",
        ],
        "entrance_links.csv": [
            "link_index",
            "kingdom",
            "area_scope",
            "home_stage",
            "home_entrance_in",
            "home_entrance_out",
            "sub_stage",
            "sub_entrance_in",
            "sub_entrance_out",
            "home_in_node_id",
            "home_out_node_id",
            "sub_near_node_id",
            "sub_far_node_id",
            "status",
            "notes",
        ],
        "logic_nodes.csv": [
            "node_id",
            "kingdom",
            "stage",
            "stage_display_name",
            "area_scope",
            "node_type",
            "source",
            "entrance_id",
            "side",
            "obj_id",
            "display_name",
            "x",
            "y",
            "z",
            "scenario_indices",
            "status",
            "notes",
        ],
        "route_edges.csv": [
            "edge_id",
            "kingdom",
            "area_scope",
            "from_node_id",
            "to_node_id",
            "requirement_expr",
            "requirement_notes",
            "edge_type",
            "difficulty",
            "option_scenarios",
            "bidirectional",
            "status",
            "source",
            "notes",
        ],
        "moon_access.csv": [
            "moon_id",
            "kingdom",
            "stage",
            "stage_display_name",
            "area_scope",
            "moon_name",
            "runtime_shine_key",
            "obj_id",
            "uid",
            "flags",
            "from_node_id",
            "state_requirement_expr",
            "requirement_expr",
            "world_peace_token",
            "moon_rock_token",
            "access_type",
            "effort_level",
            "effort_notes",
            "status",
            "legacy_requirement_notes",
            "placement_found",
            "placement_unit_config_names",
            "placement_layer_config_names",
            "placement_scenario_no_values",
            "scenario_move_to",
            "placement_stage_map_indices",
            "placement_quest_no_values",
            "placement_shine_optional_ids",
            "placement_change_stage_ids",
            "placement_x",
            "placement_y",
            "placement_z",
            "placement_source_archives",
            "placement_source_files",
            "placement_count",
            "shine_info_hint_idx",
            "shine_info_main_scenario_no",
            "shine_info_progress_bit_flag",
            "shine_info_is_grand",
            "shine_info_is_moon_rock",
            "shine_info_available_scenarios",
            "shine_info_object_name",
            "shine_info_scenario_name",
            "notes",
        ],
        "raw_stage_points.csv": [
            "stage",
            "point_type",
            "identifier",
            "target_stage",
            "obj_id",
            "unit_config_name",
            "x",
            "y",
            "z",
            "scenario_indices",
            "source_archive",
            "source_file",
        ],
    }

    write_csv(
        OUT / "logic_tokens.csv",
        tokens,
        fields_by_file["logic_tokens.csv"],
    )
    write_csv(
        OUT / "rooms.csv",
        rooms,
        fields_by_file["rooms.csv"],
    )
    write_csv(
        OUT / "embedded_zones.csv",
        embedded_zones,
        fields_by_file["embedded_zones.csv"],
    )
    write_csv(
        OUT / "progression_events.csv",
        progression_events,
        fields_by_file["progression_events.csv"],
    )
    write_csv(
        OUT / "scenario_metadata.csv",
        scenario_metadata,
        fields_by_file["scenario_metadata.csv"],
    )
    write_csv(
        OUT / "runtime_anchors.csv",
        runtime_anchors,
        fields_by_file["runtime_anchors.csv"],
    )
    write_csv(
        OUT / "entrance_links.csv",
        entrance_links,
        fields_by_file["entrance_links.csv"],
    )
    write_csv(
        OUT / "logic_nodes.csv",
        nodes,
        fields_by_file["logic_nodes.csv"],
    )
    write_csv(
        OUT / "route_edges.csv",
        route_edges,
        fields_by_file["route_edges.csv"],
    )
    write_csv(
        OUT / "moon_access.csv",
        moon_access,
        fields_by_file["moon_access.csv"],
    )
    write_csv(
        OUT / "raw_stage_points.csv",
        stage_points,
        fields_by_file["raw_stage_points.csv"],
    )

    write_kingdom_splits(
        OUT,
        fields_by_file,
        {
            "rooms.csv": rooms,
            "embedded_zones.csv": embedded_zones,
            "progression_events.csv": progression_events,
            "scenario_metadata.csv": scenario_metadata,
            "entrance_links.csv": entrance_links,
            "logic_nodes.csv": nodes,
            "route_edges.csv": route_edges,
            "moon_access.csv": moon_access,
            "runtime_anchors.csv": runtime_anchors,
        },
    )
    write_area_graphs(
        OUT,
        rooms,
        nodes,
        route_edges,
        moon_access,
        runtime_anchors,
        progression_events,
    )

    print(f"Wrote logic templates to {OUT.relative_to(ROOT)}")
    print(f"  moons: {len(shines)}")
    print(f"  entrance links: {len(entrances)}")
    print(f"  logic nodes: {len(nodes)}")
    print(f"  rooms: {len(rooms)}")
    print(
        "  embedded zones: "
        f"{sum(row['status'] == 'active' for row in embedded_zones)} active, "
        f"{sum(row['status'] != 'active' for row in embedded_zones)} unresolved"
    )
    print(f"  progression events: {len(progression_events)}")
    print(f"  scenario metadata rows: {len(scenario_metadata)}")
    print(f"  moon placement matches: {len(moon_placements)}")
    print(f"  runtime anchors: {len(runtime_anchors)}")
    print(f"  raw stage points: {len(stage_points)}")


if __name__ == "__main__":
    main()
