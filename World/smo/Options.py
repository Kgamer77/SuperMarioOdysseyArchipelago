from dataclasses import dataclass

from Options import Toggle, DefaultOnToggle, Choice, FreeText, Range, PerGameCommonOptions, DeathLink, NamedRange, OptionGroup, Visibility

#Main Randomization

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


class RandomizePaintings(DefaultOnToggle):
    """
    Randomizes the paintings in the game.
    """
    display_name = "Randomize Paintings"

class RandomizeKingdomOrder(Toggle):
    """
    Randomize the starting kingdom and kingdom progress order.
    """

    display_name = "Randomize Kingdom Order"

class RandomizeStartKingdomState(Toggle):
    """
    Randomized each kingdom’s initial state between its pre-peace, post-peace, and Moon Cube scenarios for eligible kingdoms.
    """

    display_name = "Randomize Start Kingdom State"

class TripleMoons(Choice):
    """
    Standard: triple moons are swapped between one another, staying in their existing triple spots.
    Anywhere: triple-moon rewards can appear at any moon location. Story Progression still follows physical location.
    Randomized: triple moons can be from any kingdom. The same total is kept and the triple spots stay put; only the pool changes. Needs Randomized Moons on.
    Randomized + Anywhere: triple moons can be from any kingdom and can appear at any location. Needs Randomized Moons on.
    One Check Triple: the triple moon no longer counts as three checks, but as a single check. That also means you get triple moons as items recieved.
    """
    
    display_name = "Triple Moons"

    option_standard = 0
    option_anywhere = 1
    option_randomized = 2
    option_randomized_anywhere = 3
    option_one_check_triple = 4

    default = 0  # default to standard

class LoadingZones(Choice):
    """
    Randomizes the loading zones in the game.

    Off: loading zones keep their original destinations
    Small Chains: subareas are connecting in shorter sequences
    Standard: loading zones are paired, so entering and backing out returns you to the same place. How often a chain keeps going is random.
    Long Chains: subareas are connected in longer sequences.
    Limited: loading zones swap sub-areas but never chain.
    Decoupled: entrances and exits are shuffled separately, so leaving may take you somewhere new.
    """

    display_name = "Randomize Loading Zones"

    option_off = 0
    option_small_chain = 1
    option_standard = 2
    option_long_chain = 3
    option_limited = 4
    option_decoupled = 5

    default = 2  # default to Standard

#Logic Settings

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

class PrioritizeDifficultMovement(Toggle): #This is a stupid setting that probably needs to be removed.
    """
    Prioritize progress through harder movement than casual instead of being left to chance.

    """

    display_name = "Prioritize Difficult Movement"
    visibility = Visibility.none

class SeedStyle(Choice):
    """
    Sets the style of the seed.
    Standard: The standard progression, with a mix of captures abilities and path options randomly placed throughout the run.
    Ability Rush: Captures and abilities appear earlier in the run, giving you more movement options.
    Focused Route: Less overall exploration options for a more focused experience.
    Explorer: More paths will open earlier, giving you more exploration options.
    Mystery: The seed style is randomly chosen, for a more random experience. (dont really need this)
    """

    display_name = "Seed Style"

    option_standard = 0
    option_ability_rush = 1
    option_focused_route = 2
    option_explorer = 3
    option_mystery = 4

    default = 0  # default to Standard
    visibility = Visibility.none

#Ability and Capture Lock

class LockedCaptures(DefaultOnToggle):
    """
    Captures are unlocked by collecting Mushroom Kingdom moons. Or receiving the item from another game.
    """

    display_name = "Locked Captures"

class LockedAbilites(DefaultOnToggle):
    """
    Abilities are unlocked by collecting Dark Side moons. Or receiving the item from another game.
    """

    display_name = "Locked Abilities"

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

class ItemPool(Choice):
    """
    Scarce: one copy of each capture and ability.
    Normal: duplicated captures CAN appear.
    """

    display_name = "Item Pool"

    option_scarce = 0
    option_normal = 1

    default = 1 # default to normal

class AbilitiesForSale(Range):
    """
    Each Purple Shop sells this many seeded progressive ability offers. Shops can repeat offers, but an individual shop cannot.
    """

    display_name = "Abilities For Sale"

    range_start = 0
    range_end = 3

    # Range options must define an explicit default value.
    default = 1


class CapturesForSale(Range):
    """
    Each Purple Shop sells this many seeded capture offers. Shops can repeat offers, but an individual shop cannot.
    """

    display_name = "Captures For Sale"

    range_start = 0
    range_end = 3

    # Range options must define an explicit default value.
    default = 1

class MoonsForSale(DefaultOnToggle):
    """
    Each Purple Shop sells one Power Moon for every eligible kingdom.
    """

    display_name = "Moons For Sale"


class PurpleShopPrice(Choice):
    """
    The regional-purple-coin price of every Purple Coin Shop offer.

    """

    display_name = "Purple Shop Price"

    option_ten = 1
    option_twenty = 20
    option_twenty_five = 25
    option_fifty = 50
    option_onehundred = 100

    default = 50 # default to fifty

#Additional Options

class MoonRequirements(Range):
    """
    The total number of moons required to unlock each kingdom.
    """

    display_name = "Moon Requirements"

    range_start = 1
    range_end = 657

    # Range options must define an explicit default value.
    default = 124


class RequiredDifficulty(Choice):
    """
    Easy: kingdoms with fewer moons and earlier kingdoms need fewer.
    Normal: vanilla requirement distribution.
    Hard: kingdoms with fewer moons and earlier kingdoms need more, capped at 30% of each kingdom’s moons.
    Harder: randomly assigns each eligible kingdom 0 to 40% of its moons, preserving the total.
    Hardest: randomly assigns each eligible kingdom 0 to 60% of its moons, preserving the total.
    Insane: randomly assigns each eligible kingdom 0 to 80% of its moons, preserving the total.
    Max: randomly assigns each eligible kingdom 0 to its max moons, preserving the total.
    """

    display_name = "Required Difficulty"

    option_easy = 0
    option_normal = 1
    option_hard = 2
    option_harder = 3
    option_hardest = 4
    option_insane = 5
    option_max = 6

    default = 1 # default to normal

class CapMoons(Choice):
    """
    Choose whether Cap moons award coins, abilities, captures, one of those two, or unlocks the next kingdom.
    """

    display_name = "Cap Moons"

    option_coins = 0
    option_abilities = 1
    option_captures = 2
    option_next_kingdom = 3

    default = 0  # default to coins

class CloudMoons(Choice):
    """
    Choose whether Cloud moons award coins, abilities, captures, one of those two, unlocks the next kingdom or unlocks paintings.

    """

    display_name = "Cloud Moons"

    option_coins = 0
    option_abilities = 1
    option_captures = 2
    option_next_kingdom = 3
    option_paintings = 4

    default = 0  # default to coins

class MoonMoons(Choice):
    """
    Choose whether Moon Kingdom moons award coins, abilities, captures, one of those two, or unlocks the kingdom exit and chapel door.

    """

    display_name = "Moon Moons"

    option_coins = 0
    option_abilities = 1
    option_captures = 2
    option_exit_and_chapel = 3

    default = 0  # default to coins

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

class MoonRockKey(Toggle):
    """
    Each Moon Rock kingdom has a randomized kingdom moon that must be found before Mario can open that kingdom’s Moon Rock.

    """

    display_name = "Moon Rock Key"

class KingdomsOnMap(Choice):
    """
    Vanilla: #Only the kingdoms progression has unlocked appear in the World Map.
    Visited: Kingdoms visited but not unlocked appear in the World Map and are accessible from the Odyssey
    Unlocked: Every kingdom is on the World Map from the start, in vanilla order, and accessible from the Odyssey. Progression still unlocks them one by one as normal.
    """

    display_name = "Kingdoms On Map"

    option_vanilla = 0
    option_visited = 1
    option_unlocked = 2

    default = 1  # default to visited

class SMODeathLink(DeathLink):
   __doc__ = DeathLink.__doc__ + "\n    In Super Mario Odyssey, Mario dying in any way sends a death and receiving a death causes Mario to die where he stands."

#Misc Randomization

class RandomizeChestOrder(Toggle):
    """
    Shuffle the order the chests must be opened in inside the Wooded, Mushroom, and Seaside sequential chest rooms. The moon comes from whichever chest is last.
    """

    display_name = "Randomize Chest Order"

class RandomizeSkyboxes(Choice):
    """
    Skybox Randomization: None, Kingdoms, Subareas, Own Areas (same-type only), or Max (anything goes) (Can also be changed in game)
    """

    display_name = "Randomize Skyboxes"

    option_none = 0
    option_kingdoms = 1
    option_subareas = 2
    option_own_areas = 3
    option_max = 4

    default = 1  # default to kingdoms

class StabilizeShaders(DefaultOnToggle):
    """
    Use original kingdom lighting and shaders when swapping skyboxes. Avoids brightness issues, removes some mood from scene. (Can also be changed in game)
    """

    display_name = "Stabilize Shaders"

class RandomizeSphinxQuestions(DefaultOnToggle):
    """
    Randomizes Sphinx quiz questions. (Can also be changed in game)
    """

    display_name = "Randomize Sphinx Questions"

class HintPrices(Choice):
    """
    How much hinting a location would cost. Cheap - 500 coins  Normal - 750 coins  Expensive - 1000 coins (Can also be changed in game)
    """

    display_name = "Hint Prices"

    option_cheap = 0
    option_normal = 1
    option_expensive = 2

    default = 1  # default to normal

#Sanities

class ShopSanity(Choice):
    """
    Adds various shop items to the pool. 
    shuffle: shuffles outfits amongst themselves keeping them in your game 
    Non-Outfits: the last things in purple coin shops
    """

    display_name = "Shop Sanity"

    option_off = 0
    option_outfits = 1
    option_non_outfits = 2
    option_all = 3
    option_shuffle = 4

    default = 0  # default to off

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

#Music Randomization

class MusicRandomization(Choice):
    """
    Off: Uses the game’s original background music.
    Standard: Randomized most BGM with one fixed shuffle per seed. Does not affect some event-specific music or cutscenes.
    Extended: Each physical kingdom or subarea gets a stable randomized track for each vanilla music cue
    Max: Each newly requested source theme picks a fresh random track (Can also be changed in game)
    """

    display_name = "Music Randomization"

    option_off = 0
    option_standard = 1
    option_extended = 2
    option_max = 3

    default = 1  # default to standard

class MusicOverlay(DefaultOnToggle):
    """
    Overlay randomized music name (Can also be changed in game)
    """

    display_name = "Music Overlay"

class StreamerMode(Toggle):
    """
    Exclude Japanese-language theme variants from the music randomizer’s track pool.(Can also be changed in game)
    """

    display_name = "Streamer Mode"

class Disable2DThemes(Toggle):
    """
    Keep the 8-bit arrangements out of the track pool, so no area is randomized into its 2D theme.
    """

    display_name = "Disable 2D Themes"

class DisableExtraThemes(Toggle):
    """
    Keep the tracks that are not kingdom or sub-are themes out of the pool.
    """

    display_name = "Disable Extra Themes"

class DisableBossThemes(Toggle):
    """
    Keeps the boss battle themes out of the pool, so every fight keeps its own music and no ordinary area is randomized into a boss theme
    """

    display_name = "Disable Boss Themes"

#Quality of Life

class FastMoonDemos(Toggle):
    """
    Moons are collected almost instantly. (Can also be changed in game)
    """

    display_name = "Fast Moon Demos"

class SkipCutsceneCameras(Toggle):
    """
    Skip the camera pan that plays when a switch reveals something (faster, less interruption). (Can also be changed in game)
    """

    display_name = "Skip Cutscene Cameras"

class MotionFreeControls(Toggle):
    """
    No motion needed for cap throws/captures/rolling - each role uses a button instead. Remap which button on the Controls page (L/R to page there). (Can also be changed in game)
    """

    display_name = "Motion-Free Controls"

class EasyMinigames(Toggle):
    """
    Jump-rope and volleyball award their two moons after 10 and 25 successful hits.
    """

    display_name = "Easy Minigames"

class OneSheepHerding(Toggle):
    """
    Sheep-herding moons spawn after any one sheep reaches the pen.
    """

    display_name = "One Sheep Herding"

class FreeCostumeMoons(Toggle):
    """
    Costume-check NPC’s give their moon without requiring the matching cap and outfit. 
    """

    display_name = "Free Costume Moons"

class AreaNameOverlay(Toggle):
    """
    Show a brief banner with the current area’s name when entering an overworld kingdom or a subarea. 
    """

    display_name = "Area Name Overlay"

class RandomizeOutfits(Toggle):
    """
    Change to a random outfit everytime you die.
    """

    display_name = "Randomize Outfits"

class StartWithNeutralCapThrow(Toggle):
    """
    Always hand out Neutral Throw as the free starting cap throws instead of rolling one of the four. Logic solves the seed around it
    """

    display_name = "Start With Neutral Cap Throw"

class EnableNoClip(Toggle):
    """
    Allow ZL + ZR + D-Pad Up to toggle No Clip. Disabled by default.
    """

    display_name = "Enable No-Clip"

#Tracker

class TrackerOverlay(Choice):
    """
    None: No tracker is displayed on screen
    Basic: Only kingdoms with requirements are displayed on screen
    Extended: All kingdoms are displayed on screen
    Hints: Show available checks from that kingdom next to each kingdom
    Guided: Show progress related checks from that kingdom next to each kingdom
    (Can also be changed in game)
    """

    display_name = "Tracker Overlay"

    option_none = 0
    option_basic = 1
    option_extended = 2
    option_hints = 3
    option_guided = 4

    default = 0  # default to off

class TrackerOnRight(Toggle):
    """
    Show the tracker on the right side of the screen instead of the left. (Can also be changed in game)
    """

    display_name = "Tracker On Right"

class CaveSkip(Choice):
    """
    None: do not show the Cave Skip checklist on the tracker.
    Easy: show Ground Pound Jump-or-Backflip, Wall Jump, and Dive beside Cave Requirements.
    Hard: show only Wall Jump beside Cave Requirements. (Can also be changed in game)

    """

    display_name = "Cave Skip"

    option_none = 0
    option_easy = 1
    option_hard = 2

    default = 0  # default to none

class MoonLimits(Choice):
    """
    None: kingdom counts are never color coded on the tracker.
    Upper: a kingdom’s moon count turns green once it reaches the highest requirement that a kingdom could have been given.
    Full: red below the lowest requirement that kingdom could have been given, yellow inside the range, green at or past the highest. (Can also be changed in game)

    """

    display_name = "Moon Limits"

    option_none = 0
    option_upper = 1
    option_full = 2

    default = 0  # default to none

class TrackerTimer(DefaultOnToggle):
    """
    Shows the elapsed playtime clock at the top of the progress tracker. (Can also be changed in game)
    """

    display_name = "Tracker Timer"




@dataclass
class SMOOptions(PerGameCommonOptions):
    starting_position: StartingPosition
    goal: Goal
    seed_style : SeedStyle
    locked_captures : LockedCaptures
    locked_abilities : LockedAbilites
    starting_equipment : StartingEquipment
    item_pool : ItemPool
    abilities_for_sale : AbilitiesForSale
    captures_for_sale : CapturesForSale
    moons_for_sale : MoonsForSale
    purple_shop_price : PurpleShopPrice
    moon_requirements : MoonRequirements
    required_difficulty : RequiredDifficulty
    cap_moons : CapMoons
    cloud_moons : CloudMoons
    moon_moons : MoonMoons
    world_peace : WorldPeace
    moon_rock_key : MoonRockKey
    kingdoms_on_map : KingdomsOnMap
    death_link : SMODeathLink
    randomize_chest_order : RandomizeChestOrder
    randomize_skyboxes : RandomizeSkyboxes
    stabilize_shaders : StabilizeShaders
    randomize_sphinx_questions : RandomizeSphinxQuestions
    hint_prices : HintPrices
    shop_sanity : ShopSanity
    regional_coins : RegionalCoinSanity
    checkpoint_sanity : CheckpointSanity
    music_randomization : MusicRandomization
    music_overlay : MusicOverlay
    streamer_mode : StreamerMode
    disable_2d_themes : Disable2DThemes
    disable_extra_themes : DisableExtraThemes
    disable_boss_themes : DisableBossThemes
    fast_moon_demos : FastMoonDemos
    skip_cutscene_cameras : SkipCutsceneCameras
    motion_free_controls : MotionFreeControls
    easy_minigames : EasyMinigames
    one_sheep_herding : OneSheepHerding
    free_costume_moons : FreeCostumeMoons
    area_name_overlay : AreaNameOverlay
    randomize_outfits : RandomizeOutfits
    start_with_neutral_cap_throw : StartWithNeutralCapThrow
    enable_no_clip : EnableNoClip
    tracker_overlay : TrackerOverlay
    tracker_on_right : TrackerOnRight
    cave_skip : CaveSkip
    moon_limits : MoonLimits
    tracker_timer : TrackerTimer
    # replace: ReplaceUnneededMoons


option_groups = [
    OptionGroup(
        "Main Randomization",
        [StartingPosition, Goal, RandomizePaintings, RandomizeKingdomOrder, RandomizeStartKingdomState, TripleMoons, LoadingZones],
    ),
    OptionGroup(
        "Logic Settings",
        [LogicLevel, PrioritizeDifficultMovement, SeedStyle],
    ),
    OptionGroup(
        "Ability and Capture Lock",
        [LockedCaptures, LockedAbilites, StartingEquipment, ItemPool, AbilitiesForSale, CapturesForSale, MoonsForSale, PurpleShopPrice],
    ),
    OptionGroup(
        "Additional Options",
        [MoonRequirements, RequiredDifficulty, CapMoons, CloudMoons, MoonMoons, WorldPeace, MoonRockKey, KingdomsOnMap, DeathLink],
    ),
    OptionGroup(
        "Misc. Randomization",
        [RandomizeChestOrder, RandomizeSkyboxes, StabilizeShaders, RandomizeSphinxQuestions, HintPrices],
    ),
    OptionGroup(
        "Sanities",
        [ShopSanity, RegionalCoinSanity, CheckpointSanity],
    ),
    OptionGroup(
        "Music Randomization",
        [MusicRandomization, MusicOverlay, StreamerMode, Disable2DThemes, DisableExtraThemes, DisableBossThemes],
    ),
    OptionGroup(
        "Quality of Life",
        [FastMoonDemos, SkipCutsceneCameras, MotionFreeControls, EasyMinigames, OneSheepHerding, FreeCostumeMoons, AreaNameOverlay, RandomizeOutfits, StartWithNeutralCapThrow, EnableNoClip],
    ),
    OptionGroup(
        "Tracker",
        [TrackerOverlay, TrackerOnRight, CaveSkip, MoonLimits, TrackerTimer],
    ),
]