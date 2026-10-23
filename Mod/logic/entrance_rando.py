#!/usr/bin/env python3
"""
Bijective entrance randomization algorithm.

This module implements a random bijective entrance mapping that ensures:
1. All entrances are assigned (no undefined entrances)
2. Each entrance maps to a unique destination (a permutation, not a cycle-free
   graph -- A->B->A loops and same-kingdom mappings are both allowed)
3. Cross-kingdom connections are allowed
4. Truly random distribution

The UnionFind helper below is unused by build_cycle_free_map; entrance-rando
does not currently reject cycles.
"""

from typing import List, Dict, Tuple
import random


class UnionFind:
    """Union-Find data structure for cycle detection."""

    def __init__(self, size: int):
        self.parent = list(range(size))
        self.rank = [0] * size

    def find(self, x: int) -> int:
        """Find the root of the set containing x with path compression."""
        if self.parent[x] != x:
            self.parent[x] = self.find(self.parent[x])
        return self.parent[x]

    def union(self, x: int, y: int) -> bool:
        """
        Union the sets containing x and y.
        Returns False if x and y are already in the same set (would create cycle).
        """
        px = self.find(x)
        py = self.find(y)

        if px == py:
            return False  # Already in same set, would create cycle

        # Union by rank
        if self.rank[px] < self.rank[py]:
            self.parent[px] = py
        elif self.rank[px] > self.rank[py]:
            self.parent[py] = px
        else:
            self.parent[py] = px
            self.rank[px] += 1

        return True


def build_cycle_free_map(
    randomizable_indices: List[int],
    rng: random.Random
) -> Dict[int, int]:
    """
    Build a bijective entrance mapping (permutation).

    This creates a random permutation where each entrance maps to a unique destination.
    The mapping is truly random and ensures all entrances are assigned.

    Args:
        randomizable_indices: List of entrance indices that can be randomized
        rng: Random number generator for shuffling

    Returns:
        Dictionary mapping source entrance index to destination entrance index
    """
    if not randomizable_indices:
        return {}

    print(f"Building bijective entrance map for {len(randomizable_indices)} entrances...")

    # Create a shuffled list of destinations
    destinations = randomizable_indices.copy()
    rng.shuffle(destinations)

    # Create the mapping
    mapping = {}
    for i, source in enumerate(randomizable_indices):
        mapping[source] = destinations[i]

    print(f"Successfully built bijective entrance map with {len(mapping)} assignments")
    return mapping


def verify_bijection(mapping: Dict[int, int], expected_size: int) -> bool:
    """
    Verify that the mapping is a proper bijection.

    Args:
        mapping: Dictionary mapping source to destination
        expected_size: Expected number of mappings

    Returns:
        True if mapping is a valid bijection, False otherwise
    """
    # Check size
    if len(mapping) != expected_size:
        print(f"ERROR: Expected {expected_size} mappings, got {len(mapping)}")
        return False

    # Check that all destinations are unique
    destinations = list(mapping.values())
    if len(destinations) != len(set(destinations)):
        print("ERROR: Duplicate destinations found")
        return False

    # Check that all sources are unique
    sources = list(mapping.keys())
    if len(sources) != len(set(sources)):
        print("ERROR: Duplicate sources found")
        return False

    return True


def test_cycle_free_algorithm():
    """Test the bijective entrance randomization algorithm."""

    print("=" * 60)
    print("Testing Bijective Entrance Randomization")
    print("=" * 60)

    # Test 1: Small set
    print("\nTest 1: Small set (10 entrances)")
    indices = list(range(10))
    rng = random.Random(42)
    mapping = build_cycle_free_map(indices, rng)

    if mapping:
        print(f"Mapping: {mapping}")
        is_valid = verify_bijection(mapping, 10)
        print(f"Valid bijection: {is_valid}")
        assert is_valid, "Test 1 failed: invalid bijection!"
        print("[PASS] Test 1 passed")

    # Test 2: Medium set
    print("\nTest 2: Medium set (50 entrances)")
    indices = list(range(50))
    rng = random.Random(123)
    mapping = build_cycle_free_map(indices, rng)

    if mapping:
        is_valid = verify_bijection(mapping, 50)
        print(f"Valid bijection: {is_valid}")
        assert is_valid, "Test 2 failed: invalid bijection!"
        print("[PASS] Test 2 passed")

    # Test 3: Large set
    print("\nTest 3: Large set (115 entrances - real-world size)")
    indices = list(range(115))
    rng = random.Random(0)
    mapping = build_cycle_free_map(indices, rng)

    if mapping:
        is_valid = verify_bijection(mapping, 115)
        print(f"Valid bijection: {is_valid}")
        assert is_valid, "Test 3 failed: invalid bijection!"
        print("[PASS] Test 3 passed")

    # Test 4: Multiple seeds
    print("\nTest 4: Multiple seeds (verify randomness)")
    for seed in [0, 1, 42, 123, 999]:
        rng = random.Random(seed)
        mapping = build_cycle_free_map(list(range(115)), rng)
        is_valid = verify_bijection(mapping, 115)
        assert is_valid, f"Test 4 failed for seed {seed}: invalid bijection!"
    print("[PASS] Test 4 passed")

    # Test 5: Verify different seeds produce different mappings
    print("\nTest 5: Verify different seeds produce different mappings")
    mappings = []
    for seed in [0, 1, 42]:
        rng = random.Random(seed)
        mapping = build_cycle_free_map(list(range(20)), rng)
        mappings.append(mapping)

    # Check that at least some mappings are different
    all_same = all(m == mappings[0] for m in mappings)
    assert not all_same, "Test 5 failed: all seeds produced the same mapping!"
    print("[PASS] Test 5 passed")

    print("\n" + "=" * 60)
    print("All tests passed!")
    print("=" * 60)


if __name__ == "__main__":
    test_cycle_free_algorithm()
