from dataclasses import dataclass

from Options import Toggle, Choice, PerGameCommonOptions

class Goal(Choice):
    """Sets the completion goal. This is the kingdom you must get the last story multi moon in to win the game.
    Valid options: Metro (A Traditional Festival), Luncheon (Cookatiel Showdown), Moon (Beat the game), Dark (Arrival at Rabbit Ridge), Darker (A Long Journey's End)"""
    display_name = "Goal"
    option_sand = 4
    option_lake = 5
    option_metro = 9
    option_luncheon = 12
    option_moon = 15
    option_dark = 17
    option_darker = 18
    default = 15  # default to moon

class StorySanity(Choice):
    """Adds story progression moons to the pool."""
    display_name = "Randomize Story Moons"
    option_single_moons = 1
    option_multi_moons = 2
    option_all = 3
    option_off = 0
    default = 0 # default to off

class ShopSanity(Choice):
    """Adds various shop items to the pool."""
    display_name = "Randomize Shops"
    option_shuffle = 1
    option_outfits  = 2
    option_non_outfits = 3
    option_all = 4
    option_off = 0
    default = 0  # default to off

class ReplaceUnneededMoons(Toggle):
    """Replaces moons from kingdoms not required to reach the win condition with filler items (Coins)."""
    display_name = "Replace Unnecessary Moons"



@dataclass
class SMOOptions(PerGameCommonOptions):
    goal: Goal
    story : StorySanity
    shops: ShopSanity
    replace: ReplaceUnneededMoons
