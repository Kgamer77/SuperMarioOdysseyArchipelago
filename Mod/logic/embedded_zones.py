"""Discover and flatten map zones that are loaded inside another stage.

Super Mario Odyssey stores large maps as a loading-stage archive plus one or
more embedded zone archives.  The zone archive names look like stages, but the
game loads their geometry in place without crossing a loading boundary.  Logic
therefore belongs to the nearest real loading stage, while physical moon keys
and stable node ids must continue to use their original zone names.

This module is shared by template generation and the saved-project migration.
"""

from __future__ import annotations

import math
from collections import defaultdict
from pathlib import Path
from typing import Any, Iterable


Matrix34 = tuple[
    float, float, float, float,
    float, float, float, float,
    float, float, float, float,
]

IDENTITY_MATRIX: Matrix34 = (
    1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
)

EMBEDDED_ZONE_FIELDS = [
    "embedded_stage",
    "parent_stage",
    "source_parent_stage",
    "translation_x",
    "translation_y",
    "translation_z",
    "rotation_x",
    "rotation_y",
    "rotation_z",
    "scale_x",
    "scale_y",
    "scale_z",
    "matrix_00",
    "matrix_01",
    "matrix_02",
    "matrix_03",
    "matrix_10",
    "matrix_11",
    "matrix_12",
    "matrix_13",
    "matrix_20",
    "matrix_21",
    "matrix_22",
    "matrix_23",
    "scenario_indices",
    "status",
    "notes",
]


def _maybe_import_oead():
    try:
        import oead  # type: ignore

        return oead
    except Exception:
        return None


def _hget(value, key: str, default=None):
    return value[key] if hasattr(value, "__contains__") and key in value else default


def _float(value: Any, default: float) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def _vector(value, defaults: tuple[float, float, float]) -> tuple[float, float, float]:
    value = value or {}
    return (
        _float(_hget(value, "X"), defaults[0]),
        _float(_hget(value, "Y"), defaults[1]),
        _float(_hget(value, "Z"), defaults[2]),
    )


def _srt_matrix(
    translation: tuple[float, float, float],
    rotation_degrees: tuple[float, float, float],
    scale: tuple[float, float, float],
) -> Matrix34:
    """Build an affine matrix using scale, then X/Y/Z Euler rotation."""
    rx, ry, rz = (math.radians(value) for value in rotation_degrees)
    sx, sy, sz = scale
    cx, sin_x = math.cos(rx), math.sin(rx)
    cy, sin_y = math.cos(ry), math.sin(ry)
    cz, sin_z = math.cos(rz), math.sin(rz)

    # Rz * Ry * Rx, with each scale applied to the corresponding input axis.
    r00 = cz * cy
    r01 = cz * sin_y * sin_x - sin_z * cx
    r02 = cz * sin_y * cx + sin_z * sin_x
    r10 = sin_z * cy
    r11 = sin_z * sin_y * sin_x + cz * cx
    r12 = sin_z * sin_y * cx - cz * sin_x
    r20 = -sin_y
    r21 = cy * sin_x
    r22 = cy * cx
    tx, ty, tz = translation
    return (
        r00 * sx, r01 * sy, r02 * sz, tx,
        r10 * sx, r11 * sy, r12 * sz, ty,
        r20 * sx, r21 * sy, r22 * sz, tz,
    )


def compose_matrices(parent: Matrix34, child: Matrix34) -> Matrix34:
    """Return parent * child for two affine 3x4 matrices."""
    result: list[float] = []
    for row in range(3):
        for column in range(3):
            result.append(sum(parent[row * 4 + k] * child[k * 4 + column] for k in range(3)))
        result.append(
            parent[row * 4 + 3]
            + sum(parent[row * 4 + k] * child[k * 4 + 3] for k in range(3))
        )
    return tuple(result)  # type: ignore[return-value]


def transform_point(matrix: Matrix34, x: float, y: float, z: float) -> tuple[float, float, float]:
    return (
        matrix[0] * x + matrix[1] * y + matrix[2] * z + matrix[3],
        matrix[4] * x + matrix[5] * y + matrix[6] * z + matrix[7],
        matrix[8] * x + matrix[9] * y + matrix[10] * z + matrix[11],
    )


def _decompose_matrix(
    matrix: Matrix34,
) -> tuple[
    tuple[float, float, float],
    tuple[float, float, float],
    tuple[float, float, float],
]:
    translation = (matrix[3], matrix[7], matrix[11])
    scale = (
        math.sqrt(matrix[0] ** 2 + matrix[4] ** 2 + matrix[8] ** 2),
        math.sqrt(matrix[1] ** 2 + matrix[5] ** 2 + matrix[9] ** 2),
        math.sqrt(matrix[2] ** 2 + matrix[6] ** 2 + matrix[10] ** 2),
    )
    sx, sy, sz = (value if value > 1e-9 else 1.0 for value in scale)
    r00, r10, r20 = matrix[0] / sx, matrix[4] / sx, matrix[8] / sx
    r21, r22 = matrix[9] / sy, matrix[10] / sz
    r01, r11 = matrix[1] / sy, matrix[5] / sy
    ry = math.asin(max(-1.0, min(1.0, -r20)))
    if abs(math.cos(ry)) > 1e-7:
        rx = math.atan2(r21, r22)
        rz = math.atan2(r10, r00)
    else:
        rx = 0.0
        rz = math.atan2(-r01, r11)
    rotation = tuple(math.degrees(value) for value in (rx, ry, rz))
    return translation, rotation, scale


def _format_float(value: float) -> str:
    if abs(value) < 0.0000005:
        value = 0.0
    return f"{value:.6f}"


def matrix_from_row(row: dict[str, Any]) -> Matrix34:
    matrix_values = []
    for matrix_row in range(3):
        for column in range(4):
            key = f"matrix_{matrix_row}{column}"
            matrix_values.append(_float(row.get(key), IDENTITY_MATRIX[matrix_row * 4 + column]))
    return tuple(matrix_values)  # type: ignore[return-value]


def _matrix_key(matrix: Matrix34) -> tuple[float, ...]:
    return tuple(round(value, 6) for value in matrix)


def _iter_stage_objects(value, scenario_index: int):
    oead = _maybe_import_oead()
    if not oead:
        return
    if isinstance(value, oead.byml.Hash):
        if "UnitConfigName" in value:
            yield value, scenario_index
        for key, child in value.items():
            if str(key) in {"Links", "UnitConfig"}:
                continue
            yield from _iter_stage_objects(child, scenario_index)
    elif isinstance(value, oead.byml.Array):
        for child in value:
            yield from _iter_stage_objects(child, scenario_index)


def discover_embedded_zones(
    stage_data_dirs: Iterable[Path],
    loading_stages: set[str],
) -> list[dict[str, str]]:
    """Return active and unresolved embedded-zone mappings.

    ``loading_stages`` are hard boundaries: WorldMeta main stages, stage-warp
    stages, and every EntranceDB home/sub stage.  Any other referenced map
    archive is geometry embedded in the current boundary.
    """
    oead = _maybe_import_oead()
    if not oead:
        return []

    archive_by_stage: dict[str, Path] = {}
    for stage_dir in stage_data_dirs:
        if not stage_dir.exists():
            continue
        for path in stage_dir.rglob("*Map.szs"):
            archive_by_stage.setdefault(path.name[:-7], path)

    root_cache: dict[str, Any] = {}

    def load_root(stage: str):
        if stage in root_cache:
            return root_cache[stage]
        path = archive_by_stage.get(stage)
        if not path:
            root_cache[stage] = None
            return None
        try:
            data = path.read_bytes()
            if data.startswith(b"Yaz0"):
                data = oead.yaz0.decompress(data)
            archive = oead.Sarc(data)
            root_cache[stage] = next(
                (
                    oead.byml.from_binary(file.data)
                    for file in archive.get_files()
                    if file.name.endswith("Map.byml")
                ),
                None,
            )
        except Exception:
            root_cache[stage] = None
        return root_cache[stage]

    placement_cache: dict[str, list[dict[str, Any]]] = {}

    def child_placements(parent_stage: str) -> list[dict[str, Any]]:
        if parent_stage in placement_cache:
            return placement_cache[parent_stage]
        root = load_root(parent_stage)
        if root is None:
            placement_cache[parent_stage] = []
            return []
        scenarios = list(root) if isinstance(root, oead.byml.Array) else [root]
        grouped: dict[tuple[str, tuple[float, ...]], dict[str, Any]] = {}
        for scenario_index, scenario in enumerate(scenarios):
            for obj, _ in _iter_stage_objects(scenario, scenario_index):
                child = str(_hget(obj, "UnitConfigName", "") or "")
                if (
                    not child
                    or child == parent_stage
                    or child in loading_stages
                    or child not in archive_by_stage
                ):
                    continue
                translation = _vector(_hget(obj, "Translate"), (0.0, 0.0, 0.0))
                rotation = _vector(_hget(obj, "Rotate"), (0.0, 0.0, 0.0))
                scale = _vector(_hget(obj, "Scale"), (1.0, 1.0, 1.0))
                matrix = _srt_matrix(translation, rotation, scale)
                key = (child, _matrix_key(matrix))
                row = grouped.setdefault(
                    key,
                    {
                        "child": child,
                        "source_parent": parent_stage,
                        "matrix": matrix,
                        "scenarios": set(),
                    },
                )
                row["scenarios"].add(scenario_index)
        placement_cache[parent_stage] = list(grouped.values())
        return placement_cache[parent_stage]

    resolved: dict[str, list[dict[str, Any]]] = defaultdict(list)

    def visit(
        boundary: str,
        current_stage: str,
        current_matrix: Matrix34,
        chain: tuple[str, ...],
    ) -> None:
        for placement in child_placements(current_stage):
            child = placement["child"]
            if child in chain:
                continue
            matrix = compose_matrices(current_matrix, placement["matrix"])
            resolved[child].append(
                {
                    "parent": boundary,
                    "source_parent": current_stage,
                    "matrix": matrix,
                    "scenarios": set(placement["scenarios"]),
                    "chain": (*chain, child),
                }
            )
            visit(boundary, child, matrix, (*chain, child))

    for boundary in sorted(loading_stages):
        if boundary in archive_by_stage:
            visit(boundary, boundary, IDENTITY_MATRIX, (boundary,))

    rows: list[dict[str, str]] = []
    for child, candidates in sorted(resolved.items()):
        unique: dict[tuple[str, tuple[float, ...]], dict[str, Any]] = {}
        for candidate in candidates:
            key = (candidate["parent"], _matrix_key(candidate["matrix"]))
            existing = unique.get(key)
            if existing:
                existing["scenarios"].update(candidate["scenarios"])
                continue
            unique[key] = candidate

        status = "active" if len(unique) == 1 else "ambiguous_unused"
        for candidate in unique.values():
            matrix = candidate["matrix"]
            translation, rotation, scale = _decompose_matrix(matrix)
            row = {
                "embedded_stage": child,
                "parent_stage": candidate["parent"],
                "source_parent_stage": candidate["source_parent"],
                "translation_x": _format_float(translation[0]),
                "translation_y": _format_float(translation[1]),
                "translation_z": _format_float(translation[2]),
                "rotation_x": _format_float(rotation[0]),
                "rotation_y": _format_float(rotation[1]),
                "rotation_z": _format_float(rotation[2]),
                "scale_x": _format_float(scale[0]),
                "scale_y": _format_float(scale[1]),
                "scale_z": _format_float(scale[2]),
                "scenario_indices": ";".join(str(value) for value in sorted(candidate["scenarios"])),
                "status": status,
                "notes": (
                    f"Loaded through {' -> '.join(candidate['chain'])}."
                    if status == "active"
                    else "Reused by multiple loading stages; ignored because it has no authored logic data."
                ),
            }
            for matrix_row in range(3):
                for column in range(4):
                    row[f"matrix_{matrix_row}{column}"] = _format_float(
                        matrix[matrix_row * 4 + column]
                    )
            rows.append(row)
    return rows


def active_mapping(rows: Iterable[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    return {
        row["embedded_stage"]: row
        for row in rows
        if row.get("status") == "active" and row.get("embedded_stage") and row.get("parent_stage")
    }


def validate_ambiguous_zones(
    rows: Iterable[dict[str, Any]],
    meaningful_stages: set[str],
) -> None:
    ambiguous = {
        row["embedded_stage"]
        for row in rows
        if row.get("status") == "ambiguous_unused"
        and row.get("embedded_stage") in meaningful_stages
    }
    if ambiguous:
        names = ", ".join(sorted(ambiguous))
        raise ValueError(
            "Embedded zones with authored logic are loaded by multiple parents. "
            f"Add an explicit parent override before generating: {names}"
        )


def _is_true(value: Any) -> bool:
    return str(value).strip().lower() == "true"


def _physical_stage(moon: dict[str, Any]) -> str:
    runtime_key = str(moon.get("runtime_shine_key") or moon.get("moon_id") or "")
    return runtime_key.split(":", 1)[0]


def _format_coordinate(value: float) -> str:
    return f"{value:.3f}"


def _transform_row_coordinates(
    row: dict[str, Any],
    matrix: Matrix34,
    fields: tuple[str, str, str],
) -> None:
    x_key, y_key, z_key = fields
    values = [str(row.get(key, "")).strip() for key in fields]
    if not all(values):
        return
    try:
        x, y, z = (float(value) for value in values)
    except ValueError:
        return
    transformed = transform_point(matrix, x, y, z)
    row[x_key], row[y_key], row[z_key] = (
        _format_coordinate(value) for value in transformed
    )


def _bool_text(value: bool) -> str:
    return "true" if value else "false"


def _parent_metadata(
    parent: str,
    room_by_stage: dict[str, dict[str, Any]],
) -> tuple[str, str, str]:
    room = room_by_stage.get(parent, {})
    return (
        str(room.get("kingdom", "")),
        str(room.get("stage_display_name", parent) or parent),
        str(room.get("area_scope", "subarea") or "subarea"),
    )


def flatten_embedded_zones(
    nodes: list[dict],
    routes: list[dict],
    moons: list[dict],
    rooms: list[dict],
    scenario_metadata: list[dict] | None,
    mapping_rows: Iterable[dict[str, Any]],
    node_positions: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Flatten embedded-zone graph rows into their real loading-stage parent.

    The operation is idempotent.  Stable ids remain zone-qualified; only the
    graph ``stage`` changes.  A zone's generated stage start becomes an internal
    custom-place landmark, so it is never independently seeded as reachable.
    """
    mappings = active_mapping(mapping_rows)
    if not mappings:
        return {
            "nodes": nodes,
            "routes": routes,
            "moons": moons,
            "rooms": rooms,
            "scenario_metadata": scenario_metadata,
            "merged": {},
        }

    room_by_stage = {room["stage"]: room for room in rooms if room.get("stage")}
    affected_parents: set[str] = set()
    changed_children: set[str] = set()
    content_stages: set[str] = set()

    for node in nodes:
        source_stage = str(node.get("stage", ""))
        id_stage = str(node.get("node_id", "")).split("::", 1)[0]
        if source_stage in mappings or id_stage in mappings:
            content_stages.add(source_stage if source_stage in mappings else id_stage)
    for moon in moons:
        source_stage = str(moon.get("stage", ""))
        physical_stage = _physical_stage(moon)
        if source_stage in mappings or physical_stage in mappings:
            content_stages.add(source_stage if source_stage in mappings else physical_stage)
    if scenario_metadata is not None:
        for scenario in scenario_metadata:
            if scenario.get("stage") in mappings:
                content_stages.add(scenario["stage"])
    for room in rooms:
        if room.get("stage") in mappings:
            content_stages.add(room["stage"])

    # Recreate starts removed by the earlier moon-pocket migration.
    node_ids = {node.get("node_id", "") for node in nodes}
    for child in sorted(content_stages):
        mapping = mappings[child]
        parent = str(mapping["parent_stage"])
        landmark_id = f"{child}::start"
        if landmark_id in node_ids:
            continue
        kingdom, parent_display, parent_scope = _parent_metadata(parent, room_by_stage)
        matrix = matrix_from_row(mapping)
        nodes.append(
            {
                "node_id": landmark_id,
                "kingdom": kingdom,
                "stage": parent,
                "stage_display_name": parent_display,
                "area_scope": parent_scope,
                "node_type": "custom_place",
                "source": "embedded_zone",
                "entrance_id": "",
                "side": "",
                "obj_id": "",
                "display_name": child,
                "x": _format_coordinate(matrix[3]),
                "y": _format_coordinate(matrix[7]),
                "z": _format_coordinate(matrix[11]),
                "scenario_indices": str(mapping.get("scenario_indices", "")),
                "status": "generated",
                "notes": f"Embedded map landmark from {child}.",
            }
        )
        node_ids.add(landmark_id)
        affected_parents.add(parent)
        changed_children.add(child)

    for node in nodes:
        current_stage = str(node.get("stage", ""))
        id_stage = str(node.get("node_id", "")).split("::", 1)[0]
        child = current_stage if current_stage in mappings else id_stage if id_stage in mappings else ""
        if not child:
            continue
        mapping = mappings[child]
        parent = str(mapping["parent_stage"])
        matrix = matrix_from_row(mapping)
        needs_transform = current_stage == child
        node_changed = current_stage != parent
        if needs_transform:
            _transform_row_coordinates(node, matrix, ("x", "y", "z"))
        kingdom, parent_display, parent_scope = _parent_metadata(parent, room_by_stage)
        node["kingdom"] = kingdom or node.get("kingdom", "")
        node["stage"] = parent
        node["stage_display_name"] = parent_display
        node["area_scope"] = parent_scope
        if (
            node.get("node_id") == f"{child}::start"
            and node.get("node_type") == "stage_start"
        ):
            node_changed = True
            node["node_type"] = "custom_place"
            node["source"] = "embedded_zone"
            node["display_name"] = (
                node.get("display_name")
                if node.get("display_name") not in {"", "Default start"}
                else child
            )
            if not all(str(node.get(key, "")).strip() for key in ("x", "y", "z")):
                node["x"] = _format_coordinate(matrix[3])
                node["y"] = _format_coordinate(matrix[7])
                node["z"] = _format_coordinate(matrix[11])
            node["notes"] = f"Embedded map landmark from {child}."
        if node_changed:
            affected_parents.add(parent)
            changed_children.add(child)

    for moon in moons:
        current_stage = str(moon.get("stage", ""))
        physical_stage = _physical_stage(moon)
        child = (
            current_stage
            if current_stage in mappings
            else physical_stage
            if physical_stage in mappings
            else ""
        )
        if not child:
            continue
        mapping = mappings[child]
        parent = str(mapping["parent_stage"])
        matrix = matrix_from_row(mapping)
        from_node = str(moon.get("from_node_id", ""))
        already_flattened = current_stage == parent and (
            from_node.startswith(f"{child}::") or from_node == f"{child}::start"
        )
        if not already_flattened:
            _transform_row_coordinates(
                moon,
                matrix,
                ("placement_x", "placement_y", "placement_z"),
            )
        if not from_node or (
            current_stage != child and not from_node.startswith(f"{child}::")
        ):
            moon["from_node_id"] = f"{child}::start"
        moon_changed = current_stage != parent or moon.get("from_node_id") != from_node
        kingdom, parent_display, parent_scope = _parent_metadata(parent, room_by_stage)
        moon["kingdom"] = kingdom or moon.get("kingdom", "")
        moon["stage"] = parent
        moon["stage_display_name"] = parent_display
        moon["area_scope"] = parent_scope
        if moon_changed:
            affected_parents.add(parent)
            changed_children.add(child)

    if scenario_metadata is not None:
        for scenario in scenario_metadata:
            child = str(scenario.get("stage", ""))
            if child not in mappings:
                continue
            parent = str(mappings[child]["parent_stage"])
            kingdom, parent_display, parent_scope = _parent_metadata(parent, room_by_stage)
            scenario["kingdom"] = kingdom or scenario.get("kingdom", "")
            scenario["stage"] = parent
            scenario["stage_display_name"] = parent_display
            scenario["area_scope"] = parent_scope
            affected_parents.add(parent)
            changed_children.add(child)

    removed_rooms: dict[str, dict] = {}
    for child, mapping in mappings.items():
        child_room = room_by_stage.get(child)
        if not child_room:
            continue
        parent = str(mapping["parent_stage"])
        parent_room = room_by_stage.get(parent)
        if not parent_room:
            raise ValueError(f"Embedded-zone parent room is missing: {child} -> {parent}")
        removed_rooms[child] = child_room
        for field in ("has_shine_db_moons", "has_raw_stage_data_points"):
            parent_room[field] = _bool_text(
                _is_true(parent_room.get(field)) or _is_true(child_room.get(field))
            )
        for field in ("active_entrance_link_count", "excluded_entrance_link_count"):
            parent_room[field] = str(
                int(parent_room.get(field) or 0) + int(child_room.get(field) or 0)
            )
        note = f"Includes embedded map {child}."
        if note not in str(parent_room.get("notes", "")):
            parent_room["notes"] = (
                f"{parent_room.get('notes', '').strip()} {note}".strip()
            )
        affected_parents.add(parent)
        changed_children.add(child)
    filtered_rooms = [room for room in rooms if room.get("stage") not in removed_rooms]

    node_by_id = {node.get("node_id", ""): node for node in nodes}
    filtered_room_by_stage = {
        room["stage"]: room for room in filtered_rooms if room.get("stage")
    }
    for route in routes:
        endpoint = node_by_id.get(route.get("from_node_id", "")) or node_by_id.get(
            route.get("to_node_id", "")
        )
        if not endpoint:
            continue
        endpoint_room = filtered_room_by_stage.get(endpoint.get("stage", ""), {})
        route["kingdom"] = endpoint.get("kingdom", route.get("kingdom", ""))
        route["area_scope"] = endpoint_room.get(
            "area_scope", endpoint.get("area_scope", route.get("area_scope", ""))
        )

    if node_positions is not None and affected_parents:
        clear_ids = {
            node.get("node_id", "")
            for node in nodes
            if node.get("stage") in affected_parents
        }
        clear_ids.update(
            f"moon:{moon.get('__editor_id') or moon.get('moon_id')}"
            for moon in moons
            if moon.get("stage") in affected_parents
        )
        for node_id in clear_ids:
            node_positions.pop(node_id, None)

    return {
        "nodes": nodes,
        "routes": routes,
        "moons": moons,
        "rooms": filtered_rooms,
        "scenario_metadata": scenario_metadata,
        "merged": {
            child: str(mapping["parent_stage"])
            for child, mapping in mappings.items()
            if child in changed_children
        },
    }
