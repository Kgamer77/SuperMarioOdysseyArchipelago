from dataclasses import dataclass

from Options import Toggle, Choice, FreeText, PerGameCommonOptions

class Goal(Choice):
    """Sets the completion goal. This is the kingdom you must get the last story multi moon in to win the game.
    Valid options: Metro (A Traditional Festival), Luncheon (Cookatiel Showdown), Moon (Beat the game), Dark (Arrival at Rabbit Ridge), Darker (A Long Journey's End)"""
    display_name = "Goal"
    option_sand = 4
    option_lake = 5
    option_metro = 9
    option_luncheon = 12
    option_moon = 14
    option_dark = 16
    option_darker = 17
    default = 14  # default to moon

class StorySanity(Choice):
    """Adds story progression moons to the pool."""
    display_name = "Randomize Story Moons"
    option_single_moons = 1
    option_multi_moons = 2
    option_all = 3
    option_off = 0
    default = 0 # default to off

class ShopSanity(Choice):
    """Adds various shop items to the pool.
    shuffle: shuffles outfits amongst themselves keeping them in your game."""
    display_name = "Randomize Shops"
    option_shuffle = 1
    option_outfits  = 2
    option_non_outfits = 3
    option_all = 4
    option_off = 0
    default = 0  # default to off

class RandomizeMoonColors(Toggle):
    """Randomizes each kingdom's moon color."""
    display_name = "Randomize Moon Colors"
    #visibility = 0b1101

class RandomizeMoonCount(Choice):
    """Randomizes each kingdom's moon count.
    same total: Moon counts still add up to 124 like in the base game.
    lock ruined: Moon counts still add up to 124, but ruined kingdom is always a 3 moon requirement.
    moderate: Up to +25% and down to -20% of normal per kingdom counts.
    extreme: Up to 200% of normal count.
    """
    display_name = "Randomize Moon Requirements"
    #visibility = 0b1101
    option_same_total = 1
    option_same_total_lock_ruined = 2
    option_moderate = 3
    option_extreme = 4
    option_off = 0
    default = 0

@dataclass
class SMOOptions(PerGameCommonOptions):
    goal: Goal
    story : StorySanity
    shop_sanity : ShopSanity
    # replace: ReplaceUnneededMoons
    colors : RandomizeMoonColors
    counts : RandomizeMoonCount


