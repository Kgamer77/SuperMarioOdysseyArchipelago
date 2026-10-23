#!/usr/bin/env python3
"""Sweep many seeds to verify beatability and surface reachability gaps.

Loads the logic world once, then runs the randomizer across a range of seeds,
recording whether the final objective was proven reachable and collecting
warnings (which is where unreachable-area / gap notes surface).
"""
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import randomizer_logic as R


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 50
    world = R.load_world(Path(__file__).resolve().parent.parent / "templates", None)

    beatable = 0
    not_beatable = []
    warn_counter = Counter()
    path_lengths = []

    for seed in range(1, n + 1):
        settings = R.LogicSettings(seed=seed)
        result = R.LogicRandomizer(world, settings).randomize()
        if result.final_objective_reached:
            beatable += 1
            path_lengths.append(len(result.path))
        else:
            not_beatable.append((seed, result.final_objective_note))
        for w in result.warnings:
            # normalize numbers out so similar warnings group
            key = "".join(c if not c.isdigit() else "#" for c in w)
            warn_counter[key] += 1

    print(f"Seeds tested: {n}")
    print(f"Beatable (final objective proven): {beatable}/{n}")
    if path_lengths:
        print(f"Path steps: min={min(path_lengths)} max={max(path_lengths)} "
              f"avg={sum(path_lengths)/len(path_lengths):.1f}")
    print()
    if not_beatable:
        print("NOT BEATABLE seeds:")
        for seed, note in not_beatable:
            print(f"  seed {seed}: {note}")
    else:
        print("All tested seeds proven beatable.")
    print()
    print("Warning categories (count across all seeds):")
    for key, count in warn_counter.most_common():
        print(f"  [{count:>4}x] {key}")


if __name__ == "__main__":
    main()
