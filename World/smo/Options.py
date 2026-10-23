from dataclasses import dataclass

from Options import Toggle, DefaultOnToggle, Choice, FreeText, Range, PerGameCommonOptions, DeathLink, OptionSet, OptionGroup, Visibility

#Moon Randomization
class Goal(Choice):
    """Sets the completion goal. This is the kingdom you must get the last story multi moon in to win the game.
    Valid options: Metro (A Traditional Festival), Luncheon (Cookatiel Showdown), Moon (Beat the game), Dark (Arrival at Rabbit Ridge), Darker (A Long Journey's End)"""
    display_name = "Goal"

    option_sand = 2
    option_lake = 4
    option_cloud = 5
    option_metro = 7
    option_luncheon = 10
    option_moon = 13
    option_dark = 15
    option_darker = 16

    default = 13  # default to moon

class RandomizeMoons(DefaultOnToggle):
    """
    Randomizes the placement of moons in the game.
    """
    display_name = "Randomize Moons"

class KingdomMoonCounts(Choice):
    """
    Vanilla: every kingdom keeps the number of moons it has in the vanilla game.
    Enabled: the total number of moons is shared out across the kingdoms again. Moon Allocation Difficulty says how.
    """
    display_name = "Moon Reallocation"

    option_vanilla = 0
    option_enabled = 1

class TripleMoons(Choice):
    """
    Vanilla: triple moons are swapped between one another, staying in their existing triple spots.
    Anywhere: triple-moon rewards can appear at any moon location. Story Progression still follows physical location.
    Randomized: triple moons can be from any kingdom. The same total is kept and the triple spots stay put; only the pool changes. Needs Randomized Moons on.
    Randomized + Anywhere: triple moons can be from any kingdom and can appear at any location. Needs Randomized Moons on.
    One Check Triple: the triple moon no longer counts as three checks, but as a single check. That also means you get triple moons as items recieved.
    """
    
    display_name = "Triple Moons"

    option_vanilla = 0
    option_anywhere = 1
    option_randomized = 2
    option_randomized_anywhere = 3
    option_one_check_triple = 4

    default = 0  # default to vanilla

class WithRequirements(OptionSet):
    """
    Determines whether certain options require other options to be enabled.
    """

    display_name = "Kingdoms with Requirements"
    
    valid_keys = frozenset({
        "Vanilla Kingdoms", "Cap Kingdom", "Cloud Kingdom", "Moon Kingdom",
    })
    default = frozenset({"Moon Kingdom"})

class MoonRequirement(Range):
    """
    Determines the minimum number of moons required to progress in the game.
    """
    display_name = "Moon Requirement"
    default = 124
    low = 0
    high = 657

class RequirementDifficulty(Choice):
    """
    Determines the difficulty of meeting the moon requirement.
    """
    display_name = "Requirement Difficulty"

    option_easy = 0
    option_normal = 1
    option_hard = 2
    option_harder = 3
    option_hardest = 4
    option_insane = 5
    option_max = 6

    default = 1  # default to normal

class RequirementDeviation(Range):
    """
    How far each kingdom requirement may sit either side of its base value. A kingdom is rolled somewhere in base-deviation to base+deviation; the total stays what you set. Press A to type a number; 5 is the vanilla setting
    """
    display_name = "Requirement Deviation"
    default = 5
    low = 0
    high = 20

#Progression and Logic

class LogicLevel(Choice):
    """
    Sets the difficulty of the logic used to determine what is considered in logic.
    No Logic: fully random. The seed is most likely not possible. PLEASE DO NOT DO THIS IN A SYNC NOT MEANT FOR THIS
    Casual: the seed is beatable with casual movement.
    Advanced: the seed might require advanced movement to beat.
    Expert: the seed might require expert movement to beat.
    Glitched: the seed might require glitches to beat.
    """

    display_name = "Logic Level"

    option_no_logic = 0
    option_casual = 1
    option_advanced = 2
    option_expert = 3
    option_glitched = 4

    default = 1  # default to Casual

class SeedOpenness(Choice):
    """
    Claustrophbic: few places open at once.
    Compact: fewer things open at a time compared to standard.
    Standard: a balanced mix of dead ends, chains and cross-kingdom doors.
    Open: more options open at any time.
    Wide Open: plenty of options to explore.
    """
    
    display_name = "Seed Openness"

    option_claustrophobic = 0
    option_compact = 1
    option_standard = 2
    option_open = 3
    option_wide_open = 4

    default = 2  # default to standard

class SeedDifficulty(Choice):
    """
    Relaxed: a short path, and what you need is found close to where you need it. Prefers casual movement.
    Easy: a slightly shorter path with less back-and-forth than usual.
    Standard: the usual path length, spread and movement demands.
    Hard: a longer path, and harder movement is required where the Logic Level allows it.
    Brutal: the longest path, maximum back-and-forth, and the hardest movement the Logic Level allows.
    """
    
    display_name = "Seed Difficulty"

    option_relaxed = 0
    option_easy = 1
    option_standard = 2
    option_hard = 3
    option_brutal = 4

    default = 2  # default to standard

class UnlockPacing(Choice):
    """
    Front-Loaded: get abilities and captures early, so you can use them for movement throughout the run. 
    Early: captures and abilities show up sooner than usual, giving you more movement options early on.
    Standard: captures and abilities are spread evenly across the run.
    Late: captures and abilities show up later than usual.
    Back-Loaded: most of your moveset arrives in the last third of the run.
    """

    display_name = "Unlock Pacing"

    option_front_loaded = 0
    option_early = 1
    option_standard = 2
    option_late = 3
    option_back_loaded = 4

    default = 2  # default to standard

class RandomizeKingdomOrder(DefaultOnToggle):
    """
    Randomize the starting kingdom and kingdom progress order.
    """

    display_name = "Randomize Kingdom Order"

class RandomizeStartKingdomState(Toggle):
    """
    Randomized each kingdom’s initial state between its pre-peace, post-peace, and Moon Cube scenarios for eligible kingdoms.
    """

    display_name = "Randomize Start Kingdom State"

class WorldPeace(Choice):
    """
    Story Progression: The chapel door only needs the normal moon requirement.
    On Kingdom Complete: The chapel door stays shut until you leave for every kingdom, but leaving a kingdom for another one automatically brings it to peace
    Required: The chapel door stays shut until world peace is achieved in every kingdom.
    """

    display_name = "World Peace"

    option_story_progression = 0
    option_on_kingdom_complete = 1
    option_required = 2

    default = 0  # default to story progression

class StartingPosition(Choice):
    """
    Vanilla: A new file starts where the Odyssey drops Mario off, as normal.
    Overworld: A new file starts at a random sub-area exit in the first kingdom instead of at the Odyssey.
    Max: A new file starts at a random sub-area exit in the first kingdom, or inside a sub-area you can still walk back out of.
    Odyssey: A new save file starts inside the Odyssey, so you do not see what kingdom you are in until you step outside.
    """
    display_name = "Starting Position"
    option_vanilla = 0
    option_overworld = 1
    option_max = 2
    option_odyssey = 3

    default = 0  # default to vanilla

class RandomizePaintings(DefaultOnToggle):
    """
    Randomizes the paintings in the game.
    """
    display_name = "Randomize Paintings"

class PaintingsPool(OptionSet):
    """
    Vanilla: only the ten kingdoms that ship a painting are eligible. Checkpoints next to inaccessible painting moons are unlocked on kingdom world peace.
    Extended: the vanilla ten plus Cap, Lost and Moon are eligible - thirteen kingdoms. Checkpoints next to inaccessible painting moons are unlocked on kingdom world peace.
    All: Extended plus Cloud, Ruined and Dark Side are eligible - sixteen kingdoms. Checkpoints next to inaccessible painting moons are unlocked on kingdom world peace.
    Custom: When Custom is selected only the kingdoms you select are eligible for having painting.
    """

    valid_keys = frozenset({
        "Vanilla", "Extended", "All", "Custom", "Cap", "Cascade", "Sand", "Lake", "Wooded", "Cloud", "Lost", "Metro", "Seaside", "Luncheon", "Ruined", "Moon", "Dark Side"
    })
    default = frozenset({"Vanilla"})

class PaintingPairs(Range):
    """
    A pair count you set yourself, from none at all up to the whole pool. Checkpoints next to inaccessible painting moons are unlocked on kingdom world peace.
    NOTE: This option can only go up to the max amount of paintings in the pool, which is determined by the Painting Pool option. If you set this higher than the max, it will be capped to the max.
    """

    display_name = "Painting Pairs"

    range_start = 0
    range_end = 16

    # Range options must define an explicit default value.
    default = 10

class PaintingUnlocks(DefaultOnToggle):
    """
    Paintings are earned from moons in the kingdoms whose Kingdom Moon Rewards include Painting Unlocks (Cloud Kingdom by default). Off, every painting in the seed is open from the start.
    You will only start with one painting unlocked.
    """

    display_name = "Painting Unlocks"

#Random Options
class LoadingZones(Choice):
    """
    Randomizes the loading zones in the game.

    Off: loading zones keep their original destinations
    Coupled: loading zones are paired, so entering and backing out returns you to the same place. How often a chain keeps going is random.
    Limited: loading zones swap sub-areas but never chain.
    Decoupled: entrances and exits are shuffled separately, 
    """

    display_name = "Randomize Loading Zones"

    option_off = 0
    option_coupled = 1
    option_limited = 2
    option_decoupled = 3

    default = 1  # default to Coupled

class ShuffleStoryLoadingZones(Toggle):
    """
    Also shuffle the loading zones the story uses. The chapel door on the Moon is never shuffled. A shuffled story room still shows its OWN kingdom's progress, so finding the Metro sewer in Lost Kingdom still shows you Metro's scenario of it. (BETA ONLY: may break story progression.)
    """
    
    display_name = "Shuffle Story Loading Zones"

class ShopLocations(Choice):
    """
    Randomizes the locations of shops in the game.

    Randomized: shop doors are ordinary loading zones and shuffle with everything else.
    Swap: shop doors shuffle only with each other, so a shop door always leads to some shop. Metro's two street doors count as one.
    Vanilla: shop doors keep their original destinations. Sand's back way into its shop is not a shop door and still shuffles.
    """

    display_name = "Randomize Shop Locations"

    option_off = 0
    option_randomized = 1
    option_swap = 2
    option_vanilla = 3

    default = 1  # default to randomized

class MoonRockKey(DefaultOnToggle):
    """
    Moon rocks are locked behind a check.
    """

    display_name = "Moon Rock Key"

class SubareaUnlocks(Choice):
    """
    Disabled: Optional sub-areas are open from the start. Story routes are never locked either way.
    Per Kingdom: Optional sub-areas are locked per kingdom. One key per kingdom unlocks every kind of door in it.
    Per Type: Optional sub-areas are locked by the kind of door they use. One key per kind - Warp Pipes, Moon Pipes, Open Passages, Doorways, Scarecrow Doors, Beanstalks
    Per Kingdom + Type: Optional sub-areas are locked per kingdom and door kind, e.g. Sand Kingdom's Warp Pipes.
    """

    display_name = "Subarea Unlocks"

    option_disabled = 0
    option_per_kingdom = 1
    option_per_type = 2
    option_per_kingdom_and_type = 3

    default = 1  # default to Per Kingdom

#Sanities
class CaptureSanity(DefaultOnToggle):
    """
    Every Capture starts locked and is unlocked via checks.
    """

    display_name = "Capture Sanity"

class AbilitySanity(Choice):
    """
    Vanilla: every ability is available from the start.
    All: every ability starts locked and is unlocked by collecting moons.
    Original: Mario's moves start locked, but the traversal abilities (swimming, grabbing, Jaxi, scooter, and so on) stay available.
    Non-Progressive: every ability starts locked but you get the final form of each move immediately.
    """

    display_name = "Ability Sanity"

    option_vanilla = 0
    option_all = 1
    option_original = 2
    option_non_progressive = 3

    default = 2  # default to original

class HealthUpgradeSanity(Toggle):
    """
    Health upgrades are locked and unlocked by other people sending them to you. You start at 1 HP.
    """

    display_name = "Health Upgrade Sanity"

class StartingEquipment(Choice):
    """
    Scarce: Start with just a single jump and a cap throw.
    Normal: also start with 2 captures and an extra ability
    Plentiful: also start with 4 captures and 2 extra abilities
    """

    display_name = "Starting Equipment"

    option_scarce = 0
    option_normal = 1
    option_plentiful = 2

    default = 1 # default to normal

class BonusAbilitiesSanity(OptionSet):
    """
    Jump: a jump right after catching Cappy always works. 
    Warp: no checkpoint warps; the Odyssey and Return to Start stay open. 
    Wallet: yellow coins are not kept, so no gold-coin shopping. 
    Purple Wallet: purple coins are not kept (out of logic). 
    Paintings: paintings cannot be entered. 
    Odyssey Travel: no moon deposits or flights; use Return to Start.
    """

    display_name = "Bonus Ability Sanity"
    
    valid_keys = frozenset({
        "Jump", "Warp To Checkpoints", "Wallet", "Purple Wallet", "Paintings", "Odyssey Travel"
    })
    default = frozenset({})

class RegionalCoinSanity(Choice):
    """
    Add Regional Coins to the pool. 
    groups: Each group of regional coins is a check. 
    individual: Each regional coin is a check.
    """

    display_name = "Regional Coin Sanity"

    option_off = 0
    option_groups = 1
    option_individual = 2

    default = 0  # default to off

class CheckpointSanity(Toggle):
    """
    Randomizes the checkpoints in the game. 
    """

    display_name = "Checkpoint Sanity"

    option_off = 0
    option_groups = 1
    option_individual = 2

    default = 0  # default to off
    visibility = Visibility.none


#Enemy Randomizer
# class RandomizeEnemiesAndCaptures(Toggle):
#     """
#     Randomizes the enemies and captures with other enemies and captures.
#     """

#     display_name = "Randomize Enemies and Captures"
#     visibility = Visibility.none

# class RandomizeNPCS(Choice):
#     """
#     Randomizes the NPCs with one another.
#     NPCS or Captures: Randomizes NPCs with other NPCS and captures.
#     """

#     display_name = "Randomize NPCs"

#     option_off = 0
#     option_npcs = 1
#     option_npcs_or_captures = 2

#     default = 0  # default to off
#     visibility = Visibility.none

#Shop Stuff
class YellowCoinShopSlots(Range):
    """
    Yellow Coin Shops sells this many items.
    """

    display_name = "Yellow Coin Shop Slots"

    range_start = 0
    range_end = 60

    # Range options must define an explicit default value.
    default = 1

class PurpleCoinShopSlots(Range):
    """
    Purple Coin Shops sells this many items.
    """

    display_name = "Purple Coin Shop Slots"

    range_start = 0
    range_end = 20

    # Range options must define an explicit default value.
    default = 1

class MinimumYellowCoinShopPrice(Range):
    """
    The minimum yellow coin price of every Yellow Coin Shop offer.
    """

    display_name = "Minimum Yellow Coin Shop Price"

    range_start = 0
    range_end = 9999

    # Range options must define an explicit default value.
    default = 100

class MaximumYellowCoinShopPrice(Range):
    """
    The maximum yellow coin price of every Yellow Coin Shop offer.
    """

    display_name = "Maximum Yellow Coin Shop Price"

    range_start = 0
    range_end = 9999

    # Range options must define an explicit default value.
    default = 2000

class MinimumPurpleCoinShopPrice(Range):
    """
    The minimum purple coin price of every Purple Coin Shop offer.
    """

    display_name = "Minimum Purple Coin Shop Price"

    range_start = 0
    range_end = 100

    # Range options must define an explicit default value.
    default = 10

class MaximumPurpleCoinShopPrice(Range):
    """
    The maximum purple coin price of every Purple Coin Shop offer.
    """

    display_name = "Maximum Purple Coin Shop Price"

    range_start = 0
    range_end = 100

    # Range options must define an explicit default value.
    default = 100

#DeathLink
class SMODeathLink(DeathLink):
   __doc__ = DeathLink.__doc__ + "\n    In Super Mario Odyssey, Mario dying in any way sends a death and receiving a death causes Mario to die where he stands."

@dataclass
class SMOOptions(PerGameCommonOptions):
    goal: Goal
    randomize_moons: RandomizeMoons
    kingdom_moon_counts: KingdomMoonCounts
    triple_moons: TripleMoons
    with_requirements: WithRequirements
    moon_requirement: MoonRequirement
    requirement_difficulty: RequirementDifficulty
    requirement_deviation: RequirementDeviation
    logic_level: LogicLevel
    seed_openness: SeedOpenness
    seed_difficulty: SeedDifficulty
    unlock_pacing: UnlockPacing
    randomize_kingdom_order: RandomizeKingdomOrder
    randomize_start_kingdom_state: RandomizeStartKingdomState
    world_peace : WorldPeace
    starting_position : StartingPosition
    randomize_paintings : RandomizePaintings
    paintings_pool : PaintingsPool
    painting_pairs : PaintingPairs
    painting_unlocks : PaintingUnlocks
    loading_zones : LoadingZones
    shuffle_story_loading_zones : ShuffleStoryLoadingZones
    shop_locations : ShopLocations
    moon_rock_key : MoonRockKey
    subarea_unlocks : SubareaUnlocks
    capture_sanity : CaptureSanity
    ability_sanity : AbilitySanity
    health_upgrade_sanity : HealthUpgradeSanity
    starting_equipment : StartingEquipment
    bonus_abilities_sanity : BonusAbilitiesSanity
    regional_coins : RegionalCoinSanity
    checkpoint_sanity : CheckpointSanity
    yellow_coin_shop_slots : YellowCoinShopSlots
    purple_coin_shop_slots : PurpleCoinShopSlots
    minimum_yellow_coin_shop_price : MinimumYellowCoinShopPrice
    maximum_yellow_coin_shop_price : MaximumYellowCoinShopPrice
    minimum_purple_coin_shop_price : MinimumPurpleCoinShopPrice
    maximum_purple_coin_shop_price : MaximumPurpleCoinShopPrice
    death_link : SMODeathLink
    # replace: ReplaceUnneededMoons


option_groups = [
    OptionGroup(
        "Main Randomization",
        [Goal, RandomizeMoons, KingdomMoonCounts, TripleMoons, WithRequirements, MoonRequirement, RequirementDifficulty, RequirementDeviation],
    ),
    OptionGroup(
        "Progression and Logic",
        [LogicLevel, SeedOpenness, SeedDifficulty, UnlockPacing, RandomizeKingdomOrder, RandomizeStartKingdomState, WorldPeace, StartingPosition, RandomizePaintings, PaintingsPool, PaintingPairs, PaintingUnlocks],
    ),
    OptionGroup(
        "Random Options",
        [LoadingZones, ShuffleStoryLoadingZones, ShopLocations, MoonRockKey, SubareaUnlocks],
    ),
    OptionGroup(
        "Sanities",
        [CaptureSanity, AbilitySanity, HealthUpgradeSanity, StartingEquipment, BonusAbilitiesSanity, RegionalCoinSanity, CheckpointSanity],
    ),
    OptionGroup(
        "Shop Stuff",
        [YellowCoinShopSlots, PurpleCoinShopSlots, MinimumYellowCoinShopPrice, MaximumYellowCoinShopPrice, MinimumPurpleCoinShopPrice, MaximumPurpleCoinShopPrice],
    ),
    OptionGroup(
        "DeathLink",
        [SMODeathLink],
    ),
]