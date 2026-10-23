#!/usr/bin/env python3
"""Grant-everything reachability audit.

Unlike the seed sweep (verify_beatability.py), this grants ALL abilities and
captures up front, opens every kingdom, and then asks the pure topology
question: across every valid scenario, can we

  1. physically reach every loading zone (entrance link), and
  2. collect every moon?

Scenario matters: a moon or loading zone can be available only in a window of
scenarios (temporary moons, the rollingstart moon-pipe that only exists at
Cap scenario 4, etc.). So we sweep each kingdom's scenario across its full
valid range (holding the other kingdoms at their max) and take the UNION of
what is ever reachable. Anything never reachable at any scenario is a genuine
logic/topology gap.

Run from repo root:  python logic/scratch/audit_full_reachability.py
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import randomizer_logic as R


def kingdom_scenario_range(kingdom: str) -> range:
    start = R.SCENARIO_START_BY_KINGDOM.get(kingdom, 1)
    start = R.LOGIC_SCENARIO_START_OVERRIDE.get(kingdom, start)
    top = R.SCENARIO_MAX_LOGIC_BY_KINGDOM.get(kingdom)
    if top is None or top < start:
        return range(start, start + 1)
    return range(start, top + 1)


def main() -> int:
    world = R.load_world(ROOT / "templates", None)
    # Vanilla topology so every loading zone sits in its real place; ER off.
    settings = R.LogicSettings(seed=1, entrance_rando=False)
    rnd = R.LogicRandomizer(world, settings)

    # Identity entrance mapping (each door -> its own vanilla destination).
    entrance_lookup = {
        link.link_index: R.EntrancePlacement(link.link_index, link.link_index)
        for link in world.entrances
        if rnd.is_entrance_randomizable(link)
    }

    all_moon_ids = set(world.moons.keys())
    reachable_moons: set[str] = set()
    # A loading zone counts as reached if we can physically stand on any of its
    # sides (home or sub) at some scenario.
    reachable_zones: set[int] = set()

    kingdoms = list(R.WORLD_IDS)

    def run_state(overrides: dict[str, int]) -> None:
        state = rnd.entrance_proof_state()
        # Grant full story progression too: this audit asks "can every moon EVER
        # be collected with everything unlocked", so story_order / Peach gates
        # should not stand in for physical reachability.
        for k in kingdoms:
            state.story_counts[k] = 999
        state.tokens.add(R.MUSHROOM_REACHED_TOKEN)
        for k, s in overrides.items():
            state.kingdom_scenarios[k] = s
        # Drive reachability to a fixed point (mirrors reachable_locations).
        while True:
            before = set(state.accessible_stages)
            rnd.expand_stage_access(state, entrance_lookup, rnd.reachable_nodes(state))
            reachable_nodes = rnd.expand_cross_stage_route_access(state)
            event_unlocks = state.refresh_reachable_events(world, reachable_nodes)
            if before == state.accessible_stages and not event_unlocks:
                break

        for mid in all_moon_ids - reachable_moons:
            if rnd.is_location_reachable(world.moons[mid], state, reachable_nodes):
                reachable_moons.add(mid)

        for link in world.entrances:
            if link.link_index in reachable_zones:
                continue
            if link.status == "excluded":
                continue
            sides = (
                link.home_in_node_id, link.home_out_node_id,
                link.sub_near_node_id, link.sub_far_node_id,
            )
            if any(n and n in reachable_nodes for n in sides):
                reachable_zones.add(link.link_index)

    # Primary pass: every kingdom at max scenario.
    run_state({})
    # Per-kingdom sweep across the full scenario range (others stay max).
    for k in kingdoms:
        for s in kingdom_scenario_range(k):
            run_state({k: s})

    # ---- Report --------------------------------------------------------
    considered_zones = [l for l in world.entrances if l.status != "excluded"]
    missing_zones = [l for l in considered_zones if l.link_index not in reachable_zones]
    missing_moons = sorted(
        (world.moons[m] for m in all_moon_ids - reachable_moons),
        key=lambda mo: (mo.kingdom, mo.stage, mo.moon_name),
    )

    print(f"Loading zones reachable: {len(reachable_zones)}/{len(considered_zones)}")
    print(f"Moons reachable:         {len(reachable_moons)}/{len(all_moon_ids)}")
    print()

    if missing_zones:
        print(f"UNREACHABLE loading zones ({len(missing_zones)}):")
        for l in sorted(missing_zones, key=lambda x: (x.kingdom, x.home_stage, x.sub_stage)):
            print(f"  [{l.link_index}] {l.kingdom}: {l.home_stage} ({l.home_entrance_in}) "
                  f"-> {l.sub_stage} status={l.status}")
        print()
    else:
        print("All loading zones reachable.")
        print()

    if missing_moons:
        print(f"UNREACHABLE moons ({len(missing_moons)}):")
        cur_k = None
        for mo in missing_moons:
            if mo.kingdom != cur_k:
                cur_k = mo.kingdom
                print(f"  -- {cur_k} --")
            print(f"     \"{mo.moon_name}\"  [{mo.stage}]  node={mo.from_node_id or '(none)'}")
    else:
        print("All moons reachable.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
