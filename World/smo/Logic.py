from BaseClasses import CollectionState
from.Items import outfits, shop_items

def count_moons(self, state: CollectionState, kingdom : str, player: int) -> int:
    amt = 0
    player_prog_items = state.prog_items[player]
    for item_name in self.multiworld.worlds[player].item_name_groups[kingdom]:
        if state.has(item_name, player):
            amt += player_prog_items[item_name] if "Multi-Moon" not in item_name else 3
    return amt


def total_moons(self, state: CollectionState, player: int) -> int:
    """Returns the cumulative count of items from an item group present in state."""
    amt = 0
    player_prog_items = state.prog_items[player]
    for item_name in self.multiworld.worlds[player].item_names:
        if item_name not in outfits and item_name not in shop_items:
            amt += player_prog_items[item_name] if "Multi-Moon" not in item_name else 3
    return amt


