"""Randomized Starting Position: which logic node a brand new save opens at.

Mirror of ``src/Randomizer/StartPositionRando.cpp`` (and the ``sNodeAnchors``
table ``logic/generate_runtime_logic.py`` emits from the same CSV), kept
structurally identical so the Python runner and the Switch runtime pick the same
node for a given seed. Same relationship ``logic/enemy_swap.py`` has to
``EnemySwapDB.h``.

Only candidate *enumeration* and the seeded *order* live here. Whether a
candidate is actually usable - can the player walk back out to the starting
kingdom's overworld with nothing but his starting equipment - is proved by
``LogicRandomizer.can_start_at_node``, which runs the ordinary reachability
fixed point and therefore honors the entrance shuffle, one-way pockets,
dead-end sub-areas and scenario gates without re-deriving any of it.
"""

from __future__ import annotations

import csv
from dataclasses import dataclass
from pathlib import Path

START_POSITION_MODES = ("vanilla", "overworld", "max", "odyssey")

# Modes that never pick a logic node. "odyssey" starts the file inside the ship
# (HomeShipInsideStage), whose only exit is the unshuffled CurrentWorldHome door
# back to the starting kingdom's overworld - so it lands the player exactly where
# a vanilla file starts and leaves the reachable set alone. It exists to hide the
# starting kingdom from the player, not to move him.
NODELESS_START_POSITION_MODES = frozenset({"vanilla", "odyssey"})

# Anchor kinds whose recorded coordinates are a loading-zone trigger volume
# rather than a place to stand. Spawning on those re-fires the transition, so the
# runtime places the player through the zone's authored PlayerStart instead.
LOADING_ZONE_ANCHOR_KINDS = frozenset({"entrance_side", "change_stage_id"})

DEFAULT_ANCHORS_PATH = Path(__file__).resolve().parent / "templates" / "runtime_anchors.csv"


@dataclass(frozen=True)
class NodeAnchor:
    node_id: str
    kind: str
    change_stage_id: str
    x: float
    y: float
    z: float

    @property
    def is_loading_zone(self) -> bool:
        return self.kind in LOADING_ZONE_ANCHOR_KINDS


def normalize_start_position_mode(value: str | None) -> str:
    mode = (value or "vanilla").strip().lower()
    return mode if mode in START_POSITION_MODES else "vanilla"


def load_node_anchors(path: Path | str | None = None) -> dict[str, NodeAnchor]:
    """Coordinate-bearing anchors by node id.

    Rows without a full coordinate triple are dropped: they cannot place the
    player anywhere, which is exactly the ``hasCoordinates`` filter the
    generated C++ table applies.
    """
    anchors_path = Path(path) if path is not None else DEFAULT_ANCHORS_PATH
    anchors: dict[str, NodeAnchor] = {}
    with anchors_path.open(newline="", encoding="utf-8-sig") as handle:
        for row in csv.DictReader(handle):
            node_id = (row.get("node_id") or "").strip()
            x = (row.get("x") or "").strip()
            y = (row.get("y") or "").strip()
            z = (row.get("z") or "").strip()
            if not node_id or not x or not y or not z:
                continue
            anchors[node_id] = NodeAnchor(
                node_id=node_id,
                kind=(row.get("anchor_kind") or "").strip(),
                change_stage_id=(row.get("change_stage_id") or "").strip(),
                x=float(x),
                y=float(y),
                z=float(z),
            )
    return anchors


def home_side_nodes(world) -> set[str]:
    """Home-out endpoints of every runtime-usable sub-area door.

    These are the places a player surfaces when he leaves a sub-area. The
    home-in endpoint is the entrance trigger on the way into the sub-area and
    must not be used as an opening spawn. Vanilla-status links count too: they
    are still real doors, just not in the shuffle pool (matching the runtime's
    runtime-usable-link filter and requiring the authored return id).
    """
    nodes: set[str] = set()
    for link in world.entrances:
        if link.status not in ("active", "vanilla"):
            continue
        if link.home_out_node_id and link.home_entrance_out:
            nodes.add(link.home_out_node_id)
    return nodes


def is_overworld_candidate(node, node_id: str, home_sides: set[str], main_stage: str) -> bool:
    return node.stage == main_stage and node_id in home_sides


def is_sub_area_candidate(node, main_stages: set[str], linked_stages: set[str]) -> bool:
    """A node inside a sub-area chain LINKED FROM the starting kingdom's overworld.

    Not "any sub-area anywhere": with entrances shuffled, a sub-area sitting
    physically inside the starting kingdom can belong to a chain rooted in a
    different one, and starting there strands the player in kingdoms his route
    has not reached yet. Whether he can also get back out is proven per candidate
    by LogicRandomizer.can_start_at_node.
    """
    return bool(node.stage) and node.stage not in main_stages and node.stage in linked_stages


def collect_candidates(
    world,
    anchors: dict[str, NodeAnchor],
    mode: str,
    main_stage: str,
    main_stages: set[str],
    linked_stages: set[str] | None = None,
) -> list[str]:
    """Ordered candidate list. ``None`` is the sentinel for the Odyssey drop-off,
    which stays one of the possible outcomes in both non-vanilla modes."""
    mode = normalize_start_position_mode(mode)
    if mode in NODELESS_START_POSITION_MODES:
        return []
    linked = linked_stages if linked_stages is not None else set()

    # Walk world.nodes rather than collecting into a set: that preserves
    # logic_nodes.csv row order, which IS the order of the generated sNodes table
    # the runtime shuffles. Same pool in the same order means both runtimes land
    # on the same node for a seed.
    home_sides = home_side_nodes(world)
    candidates: list[str | None] = [None]
    for node_id, node in world.nodes.items():
        if node_id not in anchors or node.status == "excluded":
            continue
        anchor = anchors.get(node_id)
        is_overworld = is_overworld_candidate(node, node_id, home_sides, main_stage)
        if is_overworld:
            if not anchor or anchor.kind != "entrance_side" or not anchor.change_stage_id:
                continue
            candidates.append(node_id)
        elif mode == "max" and is_sub_area_candidate(node, main_stages, linked):
            candidates.append(node_id)
    return candidates


def shuffled_candidates(candidates: list[str], seed: int, attempt: int, rng_factory) -> list[str]:
    """Fisher-Yates in the same direction and stream as StartPositionRando."""
    ordered = list(candidates)
    rng = rng_factory((seed ^ (attempt * 0x9E3779B9)) & 0xFFFFFFFF, "StartPosition")
    rng.shuffle(ordered)
    return ordered
