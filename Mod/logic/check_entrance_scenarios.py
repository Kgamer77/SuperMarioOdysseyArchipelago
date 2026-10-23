#!/usr/bin/env python3
from randomizer_logic import *

# Load the world
world = load_world(Path("templates"))

# Check scenario requirements for some entrances
entrances_to_check = [
    "rollingstart",
    "FrogSearchExStageEnt",
    "PushBlockExStageEnt",
    "byoubu",
    "dot00",
]

for entrance_name in entrances_to_check:
    # Find the entrance
    entrance = None
    for e in world.entrances:
        if e.home_entrance_in == entrance_name:
            entrance = e
            break

    if entrance:
        # Get the entrance node
        node = world.nodes.get(entrance.home_in_node_id)
        if node:
            scenarios = scenario_mask(node.scenario_indices)
            print(f"{entrance_name}: scenario_indices='{node.scenario_indices}' -> mask={scenarios} (binary: {bin(scenarios)})")

            # Show which scenarios are valid
            valid_scenarios = []
            for s in range(32):
                if scenario_mask_contains(scenarios, s):
                    valid_scenarios.append(s)
            print(f"  Valid scenarios: {valid_scenarios}")
        else:
            print(f"{entrance_name}: No node found for {entrance.home_in_node_id}")
    else:
        print(f"{entrance_name}: Entrance not found")
    print()
