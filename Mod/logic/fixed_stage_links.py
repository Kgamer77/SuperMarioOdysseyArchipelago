"""Fixed, non-randomized cross-stage links used by the logic graph."""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class FixedStageLink:
    edge_id: str
    source_stage: str
    change_id: str
    destination_stage: str
    destination_name: str

    @property
    def source_node_id(self) -> str:
        return f"{self.source_stage}::stage_change::{self.change_id}::raw"

    @property
    def destination_node_id(self) -> str:
        return f"{self.destination_stage}::start"


FIXED_STAGE_LINKS = (
    FixedStageLink(
        "fixed_mushroom_boss_forest",
        "PeachWorldPictureBossForestStage",
        "PictureBossForest",
        "RevengeForestBossStage",
        "Torkdrift Boss Re-fight",
    ),
    FixedStageLink(
        "fixed_mushroom_boss_knuckle",
        "PeachWorldPictureBossKnuckleStage",
        "PictureBossKnuckle",
        "RevengeBossKnuckleStage",
        "Knucklotec Boss Re-fight",
    ),
    FixedStageLink(
        "fixed_mushroom_boss_magma",
        "PeachWorldPictureBossMagmaStage",
        "PictureBossMagma",
        "RevengeBossMagmaStage",
        "Cookatiel Boss Re-fight",
    ),
    FixedStageLink(
        "fixed_mushroom_boss_raid",
        "PeachWorldPictureBossRaidStage",
        "PictureBossRaid",
        "RevengeBossRaidStage",
        "Lord of Lightning Boss Re-fight",
    ),
    FixedStageLink(
        "fixed_mushroom_giant_wander_boss",
        "PeachWorldPictureGiantWanderBossStage",
        "PictureGiantWanderBoss",
        "RevengeGiantWanderBossStage",
        "Mollusque-Lanceur Boss Re-fight",
    ),
    FixedStageLink(
        "fixed_mushroom_mofumofu",
        "PeachWorldPictureMofumofuStage",
        "PictureMofumofu",
        "RevengeMofumofuStage",
        "Mechawiggler Boss Re-fight",
    ),
)


def semanticize_fixed_transition_nodes(rows: list[dict]) -> None:
    links_by_node = {link.source_node_id: link for link in FIXED_STAGE_LINKS}
    for row in rows:
        link = links_by_node.get(row.get("node_id", ""))
        if not link:
            continue
        row.update(
            {
                "node_type": "fixed_transition",
                "source": "fixed_transition",
                "side": "",
                "display_name": f"Painting to {link.destination_name}",
                "status": "fixed",
                "notes": (
                    f"Fixed vanilla painting transition to {link.destination_stage}; "
                    "not part of entrance or painting randomization."
                ),
            }
        )


def fixed_transition_edges() -> list[dict[str, str]]:
    return [
        {
            "edge_id": link.edge_id,
            "kingdom": "Mushroom Kingdom",
            "area_scope": "subarea",
            "from_node_id": link.source_node_id,
            "to_node_id": link.destination_node_id,
            "requirement_expr": "free",
            "requirement_notes": "",
            "edge_type": "fixed_stage_link",
            "difficulty": "casual",
            "option_scenarios": "",
            "bidirectional": "true",
            "status": "fixed",
            "source": "fixed_transition",
            "notes": (
                f"Fixed vanilla painting transition from {link.source_stage} "
                f"to {link.destination_stage}."
            ),
        }
        for link in FIXED_STAGE_LINKS
    ]


def canonicalize_fixed_room_routes(nodes: list[dict], edges: list[dict]) -> list[dict]:
    """Replace duplicate raw pipe anchors with their surfaced EntranceDB nodes."""
    fixed_stages = {link.source_stage for link in FIXED_STAGE_LINKS}
    canonical_by_raw_id: dict[str, str] = {}

    for raw in nodes:
        if (
            raw.get("stage") not in fixed_stages
            or raw.get("node_type") != "stage_change"
            or raw.get("source") != "StageData"
            or raw.get("side") != "raw"
            or not raw.get("entrance_id")
            or raw.get("entrance_id", "").startswith("Picture")
        ):
            continue
        for candidate in nodes:
            if (
                candidate.get("stage") == raw.get("stage")
                and candidate.get("node_type") == "substage_entrance"
                and candidate.get("entrance_id") == raw.get("entrance_id")
                and raw.get("obj_id")
                and candidate.get("obj_id") == raw.get("obj_id")
            ):
                canonical_by_raw_id[raw["node_id"]] = candidate["node_id"]
                break

    normalized: list[dict] = []
    seen: set[tuple[str, str, str]] = set()
    for edge in edges:
        row = dict(edge)
        row["from_node_id"] = canonical_by_raw_id.get(
            row.get("from_node_id", ""), row.get("from_node_id", "")
        )
        row["to_node_id"] = canonical_by_raw_id.get(
            row.get("to_node_id", ""), row.get("to_node_id", "")
        )
        if row["from_node_id"] == row["to_node_id"]:
            continue
        key = (
            row["from_node_id"],
            row["to_node_id"],
            row.get("requirement_expr", ""),
        )
        if key in seen:
            continue
        seen.add(key)
        normalized.append(row)
    return normalized
