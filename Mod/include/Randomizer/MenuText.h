#pragma once

#include <basis/seadTypes.h>

#include "Randomizer/FeatureFlags.h"

namespace MenuText {

    // ==========================================================================
    // Main Settings
    // ==========================================================================

    inline constexpr const char16_t* PAGE_MAIN = u"Seed and Presets";

    inline constexpr const char16_t* OPT_CURRENT_SEED_LABEL = u"Current Seed: 0";
    inline constexpr const char* DESC_CURRENT_SEED = "The seed that determines this run's randomization.";
    inline constexpr const char16_t* OPT_RANDOM_SEED_LABEL = u"Random Seed";
    inline constexpr const char* DESC_RANDOM_SEED = "Generate a fresh, truly random seed.";
    inline constexpr const char16_t* OPT_SET_SEED_LABEL = u"Set Seed";
    inline constexpr const char* DESC_SET_SEED = "Enter a seed number manually.";
    inline constexpr const char16_t* OPT_PRESET_LABEL = u"Default Preset:";
    inline constexpr const char16_t* OPT_USER_PRESET_LABEL = u"User Preset:";
    inline constexpr const char* DESC_USER_PRESET_EMPTY = "No saved presets yet. Configure settings and use \"Save as Preset\" on the Summary page.";
    inline constexpr const char16_t* OPT_RUN_MODE_LABEL = u"Run Mode:";
    inline constexpr const char* DESC_RUN_MODE_TOADETTE = "Collect enough Toadette Medals hidden in the game and pay your respects to Toadette to win.";
    inline constexpr const char* DESC_RUN_MODE_BINGO = "Complete any row, column, or diagonal on a 5x5 objective board.";
    inline constexpr const char* DESC_RUN_MODE_ASCEND = "Reveal a path upward and find and complete the top-row winning square.";
    inline constexpr const char* DESC_RUN_MODE_NUZLOCKE = "A Mario death deletes this save slot and returns to the title screen.";
    inline constexpr const char* DESC_RUN_MODE_SOFT_NUZLOCKE = "A Mario death removes a share of your collected moon pickups, reshuffles the seed, and restarts world progress.";
    inline constexpr const char* DESC_RUN_MODE_TIMER = "Race a countdown clock. Every moon buys more time; running out deletes this save slot.";
    inline constexpr const char* DESC_RUN_MODE_STANDARD = "Open the chapel door and defeat Bowser.";

    inline constexpr const char16_t* RUN_MODE_STANDARD = u"Standard";
#if SMOR_FEATURE_TOADETTE_MEDALS
    inline constexpr const char16_t* RUN_MODE_TOADETTE_MEDALS = u"Toadette Medal Hunt";
#endif
#if SMOR_FEATURE_BINGO_ASCEND
    inline constexpr const char16_t* RUN_MODE_BINGO = u"Bingo";
    inline constexpr const char16_t* RUN_MODE_ASCEND = u"Ascend";
#endif
#if SMOR_FEATURE_NUZLOCKE_MODES
    inline constexpr const char16_t* RUN_MODE_NUZLOCKE = u"Nuzlocke";
#endif
#if SMOR_FEATURE_TIMER_MODE
    inline constexpr const char16_t* RUN_MODE_TIMER = u"Timer";
#endif
    inline constexpr const char16_t* ROLL_RUN_MODE[] = {
        RUN_MODE_STANDARD,
#if SMOR_FEATURE_TOADETTE_MEDALS
        RUN_MODE_TOADETTE_MEDALS,
#endif
#if SMOR_FEATURE_BINGO_ASCEND
        RUN_MODE_BINGO,
        RUN_MODE_ASCEND,
#endif
#if SMOR_FEATURE_NUZLOCKE_MODES
        RUN_MODE_NUZLOCKE,
#endif
#if SMOR_FEATURE_TIMER_MODE
        RUN_MODE_TIMER,
#endif
    };
    inline constexpr s32 ROLL_RUN_MODE_COUNT = static_cast<s32>(sizeof(ROLL_RUN_MODE) / sizeof(ROLL_RUN_MODE[0]));

    // ==========================================================================
    // Game Mode Settings
    // ==========================================================================

    inline constexpr const char16_t* PAGE_GAME_MODE = u"Game Mode Settings";

    inline constexpr const char16_t* OPT_BOARD_SCALE_LABEL = u"Board Size:";
    inline constexpr const char* DESC_BOARD_SCALE = "Scale the Bingo and Ascend board HUD. Medium preserves the current layout; Small is 80% and Large is 120%.";
    inline constexpr const char16_t* OPT_TOADETTE_MEDAL_RATIO_LABEL = u"Medal Ratio:";
    inline constexpr const char* DESC_TOADETTE_MEDAL_RATIO = "How many Toadette Medals are required, and how many exist in the pool.";
    inline constexpr const char16_t* OPT_TOADETTE_MEDAL_CUSTOM_LABEL = u"Custom Toadette Medal Counts";
    inline constexpr const char* DESC_TOADETTE_MEDAL_CUSTOM = "Replace the preset ratios with your own available and required medal counts.";
    inline constexpr const char16_t* OPT_TOADETTE_MEDAL_AVAILABLE_LABEL = u"Medals Available: 12";
    inline constexpr const char* DESC_TOADETTE_MEDAL_AVAILABLE = "How many Toadette Medals exist in the pool. Never lower than the required count.";
    inline constexpr const char16_t* OPT_TOADETTE_MEDAL_REQUIRED_LABEL = u"Medals Required: 8";
    inline constexpr const char* DESC_TOADETTE_MEDAL_REQUIRED = "How many Toadette Medals must be collected to win. Never higher than the pool.";
    inline constexpr const char16_t* OPT_SOFT_NUZLOCKE_LABEL = u"Soft Nuzlocke";
    inline constexpr const char* DESC_SOFT_NUZLOCKE = "A death costs a share of your moons and reshuffles the seed instead of deleting the save.";
    inline constexpr const char16_t* OPT_EXTREME_NUZLOCKE_LABEL = u"Extreme Mode";
    inline constexpr const char* DESC_EXTREME_NUZLOCKE = "Mario has one effective HP. One hit and game over. Might make a seed impossible if it requires damage.";
    inline constexpr const char16_t* OPT_SOFT_NUZLOCKE_LOSS_LABEL = u"Moons Lost on Death:";
    inline constexpr const char* DESC_SOFT_NUZLOCKE_LOSS = "Share of your collected moons a Soft Nuzlocke death surrenders.";
    inline constexpr const char16_t* OPT_SOFT_NUZLOCKE_ABILITY_LOSS_LABEL = u"Abilities Lost on Death:";
    inline constexpr const char* DESC_SOFT_NUZLOCKE_ABILITY_LOSS = "Share of your abilities a Soft Nuzlocke death surrenders. Progressive moves are lost from the tip of their chain down.";
    inline constexpr const char16_t* OPT_SOFT_NUZLOCKE_CAPTURE_LOSS_LABEL = u"Captures Lost on Death:";
    inline constexpr const char* DESC_SOFT_NUZLOCKE_CAPTURE_LOSS = "Share of your captures a Soft Nuzlocke death surrenders.";
    inline constexpr const char16_t* OPT_TIMER_START_LABEL = u"Starting Time:";
    inline constexpr const char* DESC_TIMER_START = "How much time the run starts with. The clock is frozen during cutscenes and while paused.";
    inline constexpr const char16_t* OPT_TIMER_MOON_BONUS_LABEL = u"Time per Moon:";
    inline constexpr const char* DESC_TIMER_MOON_BONUS = "Time added by each new moon collected.";
    inline constexpr const char16_t* OPT_TIMER_TRIPLE_BONUS_LABEL = u"Time per Triple Moon:";
    inline constexpr const char* DESC_TIMER_TRIPLE_BONUS = "Time added by each new triple moon. Replaces the per-moon bonus, it is not added on top.";
    inline constexpr const char16_t* OPT_TIMER_DAMAGE_PENALTY_LABEL = u"Damage Penalty:";
    inline constexpr const char* DESC_TIMER_DAMAGE_PENALTY = "Time removed by each hit of damage Mario takes. Falls into the void and poison kill outright, so they cost the death penalty instead.";
    inline constexpr const char16_t* OPT_TIMER_DEATH_PENALTY_LABEL = u"Death Penalty:";
    inline constexpr const char* DESC_TIMER_DEATH_PENALTY = "Time removed by each death. The clock never drops below zero, so a death cannot take more than is left.";
    inline constexpr const char16_t* OPT_TIMER_HALF_LIFE_LABEL = u"Half Life Point:";
    inline constexpr const char* DESC_TIMER_HALF_LIFE = "Time awarded decreases over the game. This is how long until the time awarded is halved. It halves again at twice that, and never reaches zero.";

    inline constexpr const char16_t* ROLL_BOARD_SCALE[] = { u"Small", u"Medium", u"Large" };
    inline constexpr s32 ROLL_BOARD_SCALE_COUNT = 3;
    inline constexpr const char16_t* ROLL_TOADETTE_MEDAL_RATIO[] = {
        u"4 / 12", u"8 / 12", u"12 / 12", u"8 / 64", u"16 / 64", u"24 / 64",
        u"32 / 64", u"40 / 64", u"48 / 64", u"56 / 64", u"64 / 64"
    };
    inline constexpr s32 ROLL_TOADETTE_MEDAL_RATIO_COUNT = 11;
    inline constexpr const char16_t* ROLL_SOFT_NUZLOCKE_LOSS[] = {
        u"25%", u"50%", u"75%"
    };
    inline constexpr s32 ROLL_SOFT_NUZLOCKE_LOSS_COUNT = 3;
    inline constexpr const char16_t* ROLL_SOFT_NUZLOCKE_LOSS_CUSTOM[] = {
        u"25%", u"50%", u"75%", u"Custom"
    };
    inline constexpr s32 ROLL_SOFT_NUZLOCKE_LOSS_CUSTOM_COUNT = 4;
    inline constexpr const char16_t* ROLL_SOFT_NUZLOCKE_EQUIPMENT_LOSS[] = {
        u"0%", u"25%", u"50%", u"75%", u"100%"
    };
    inline constexpr s32 ROLL_SOFT_NUZLOCKE_EQUIPMENT_LOSS_COUNT = 5;
    inline constexpr const char16_t* ROLL_SOFT_NUZLOCKE_EQUIPMENT_LOSS_CUSTOM[] = {
        u"0%", u"25%", u"50%", u"75%", u"100%", u"Custom"
    };
    inline constexpr s32 ROLL_SOFT_NUZLOCKE_EQUIPMENT_LOSS_CUSTOM_COUNT = 6;
    inline constexpr const char16_t* ROLL_TIMER_START[] = {
        u"1:00", u"2:00", u"3:00", u"5:00"
    };
    inline constexpr s32 ROLL_TIMER_START_COUNT = 4;
    inline constexpr const char16_t* ROLL_TIMER_START_CUSTOM[] = {
        u"1:00", u"2:00", u"3:00", u"5:00", u"Custom"
    };
    inline constexpr s32 ROLL_TIMER_START_CUSTOM_COUNT = 5;
    inline constexpr const char16_t* ROLL_TIMER_MOON_BONUS[] = {
        u"20s", u"30s", u"45s", u"60s"
    };
    inline constexpr s32 ROLL_TIMER_MOON_BONUS_COUNT = 4;
    inline constexpr const char16_t* ROLL_TIMER_MOON_BONUS_CUSTOM[] = {
        u"20s", u"30s", u"45s", u"60s", u"Custom"
    };
    inline constexpr s32 ROLL_TIMER_MOON_BONUS_CUSTOM_COUNT = 5;
    inline constexpr const char16_t* ROLL_TIMER_TRIPLE_BONUS[] = {
        u"30s", u"45s", u"60s", u"90s", u"120s"
    };
    inline constexpr s32 ROLL_TIMER_TRIPLE_BONUS_COUNT = 5;
    inline constexpr const char16_t* ROLL_TIMER_TRIPLE_BONUS_CUSTOM[] = {
        u"30s", u"45s", u"60s", u"90s", u"120s", u"Custom"
    };
    inline constexpr s32 ROLL_TIMER_TRIPLE_BONUS_CUSTOM_COUNT = 6;
    inline constexpr const char16_t* ROLL_TIMER_DAMAGE_PENALTY[] = {
        u"Off", u"-1s", u"-5s", u"-15s"
    };
    inline constexpr s32 ROLL_TIMER_DAMAGE_PENALTY_COUNT = 4;
    inline constexpr const char16_t* ROLL_TIMER_DAMAGE_PENALTY_CUSTOM[] = {
        u"Off", u"-1s", u"-5s", u"-15s", u"Custom"
    };
    inline constexpr s32 ROLL_TIMER_DAMAGE_PENALTY_CUSTOM_COUNT = 5;
    inline constexpr const char16_t* ROLL_TIMER_DEATH_PENALTY[] = {
        u"Off", u"-15s", u"-30s", u"-45s"
    };
    inline constexpr s32 ROLL_TIMER_DEATH_PENALTY_COUNT = 4;
    inline constexpr const char16_t* ROLL_TIMER_DEATH_PENALTY_CUSTOM[] = {
        u"Off", u"-15s", u"-30s", u"-45s", u"Custom"
    };
    inline constexpr s32 ROLL_TIMER_DEATH_PENALTY_CUSTOM_COUNT = 5;
    inline constexpr const char16_t* ROLL_TIMER_HALF_LIFE[] = {
        u"Disabled", u"1 Hour", u"1.5 Hours", u"2 Hours"
    };
    inline constexpr s32 ROLL_TIMER_HALF_LIFE_COUNT = 4;
    inline constexpr const char16_t* ROLL_TIMER_HALF_LIFE_CUSTOM[] = {
        u"Disabled", u"1 Hour", u"1.5 Hours", u"2 Hours", u"Custom"
    };
    inline constexpr s32 ROLL_TIMER_HALF_LIFE_CUSTOM_COUNT = 5;

    // ==========================================================================
    // Randomization
    // ==========================================================================

    inline constexpr const char16_t* PAGE_RANDOMIZATION = u"Randomization";

    inline constexpr const char16_t* OPT_MOONS_LABEL = u"Randomize Moons";
    inline constexpr const char* DESC_MOONS = "Randomize Power Moon placement in the game.";
    inline constexpr const char16_t* OPT_PAINTINGS_LABEL = u"Randomize Paintings";
    inline constexpr const char* DESC_PAINTINGS = "Randomize painting destinations pairs.";
    inline constexpr const char16_t* OPT_KINGDOM_ORDER_LABEL = u"Randomize Kingdom Order";
    inline constexpr const char* DESC_KINGDOM_ORDER = "Randomize the starting kingdom and kingdom progress order.";
    inline constexpr const char16_t* OPT_START_KINGDOM_STATE_LABEL = u"Randomize Start Kingdom State";
    inline constexpr const char* DESC_START_KINGDOM_STATE = "Randomize each kingdom's initial state between its pre-peace, post-peace, and Moon Cube scenarios for eligible kingdoms.";
    inline constexpr const char16_t* OPT_CHEST_ORDER_LABEL = u"Randomize Chest Order";
    inline constexpr const char* DESC_CHEST_ORDER = "Shuffle the order the chests must be opened in inside the Wooded, Mushroom, and Seaside sequential chest rooms. The moon comes from whichever chest is last.";
    inline constexpr const char16_t* OPT_COIN_RANDO_LABEL = u"Randomize Coins";
    inline constexpr const char* DESC_COIN_RANDO = "Randomize standalone regular coins and purple coins among locations in the same map. Hidden Coin Chameleon locations participate; purple-coin collection follows the shuffled coin and the result follows the seed.";
    inline constexpr const char16_t* OPT_SHOP_PRICE_RANDO_LABEL = u"Randomize Shop Prices";
    inline constexpr const char* DESC_SHOP_PRICE_RANDO = "Give every gold coin item in Crazy Cap a seeded price from 1 to 1000. Outfits and caps cost the same in every kingdom; the Life-Up Heart and the shop's Power Moon are re-rolled per kingdom. Regional coin souvenirs keep their prices.";

    // ==========================================================================
    // Randomization II
    // ==========================================================================

    inline constexpr const char16_t* PAGE_RANDOMIZATION_II = u"Randomization II";

    inline constexpr const char16_t* OPT_RANDOMIZED_START_POSITION_LABEL = u"Starting Position:";
    inline constexpr const char* DESC_RANDOMIZED_START_POSITION_OVERWORLD = "A new file starts at a random sub-area exit in the first kingdom instead of at the Odyssey.";
    inline constexpr const char* DESC_RANDOMIZED_START_POSITION_MAX = "A new file starts at a random sub-area exit in the first kingdom, or inside a sub-area you can still walk back out of.";
    inline constexpr const char* DESC_RANDOMIZED_START_POSITION_ODYSSEY = "A new file starts inside the Odyssey, so you do not see which kingdom you are in until you step outside.";
    inline constexpr const char* DESC_RANDOMIZED_START_POSITION_VANILLA = "A new file starts where the Odyssey drops Mario off, as normal.";
    inline constexpr const char16_t* OPT_LOADING_ZONES_LABEL = u"Loading Zones:";
    inline constexpr const char* DESC_LOADING_ZONES_OFF = "Off: loading zones keep their original destinations.";
    inline constexpr const char* DESC_LOADING_ZONES_LIMITED = "Limited: loading zones swap sub-areas within a kingdom and never chain.";
    inline constexpr const char* DESC_LOADING_ZONES_DECOUPLED = "Decoupled: entrances and exits are shuffled separately, so leaving may take you somewhere new.";
    inline constexpr const char* DESC_LOADING_ZONES_COUPLED = "Coupled: loading zones are paired, so entering and backing out return you to the same place. Seed Openness decides how long the chains get.";
    inline constexpr const char16_t* OPT_SHOP_LOCATIONS_LABEL = u"Shop Locations:";
    inline constexpr const char16_t* OPT_REQUIREMENT_KINGDOMS_LABEL = u"Kingdoms with Requirements:";
    // "All" is only offered on a randomized route - see optionVisible().
    inline constexpr const char16_t* ROLL_REQUIREMENT_KINGDOMS[] = {
        u"Vanilla", u"Vanilla + Cap", u"Vanilla + Cloud", u"Vanilla + Moon",
        u"All", u"Custom"
    };
    inline constexpr s32 ROLL_REQUIREMENT_KINGDOMS_COUNT = 6;
    inline constexpr const char* DESC_REQUIREMENT_KINGDOMS_VANILLA = "Vanilla: only the kingdoms that ask for moons in the base game gate progress.";
    inline constexpr const char* DESC_REQUIREMENT_KINGDOMS_CAP = "Cap Kingdom gates progress too, on top of the base game's requirement kingdoms.";
    inline constexpr const char* DESC_REQUIREMENT_KINGDOMS_CLOUD = "Cloud Kingdom gates progress too, on top of the base game's requirement kingdoms.";
    inline constexpr const char* DESC_REQUIREMENT_KINGDOMS_MOON = "Moon Kingdom gates progress too, on top of the base game's requirement kingdoms.";
    inline constexpr const char* DESC_REQUIREMENT_KINGDOMS_ALL = "All: every kingdom on the main route gates progress, Cap, Cloud and Moon included.";
    inline constexpr const char* DESC_REQUIREMENT_KINGDOMS_CUSTOM = "Custom: the requirement kingdoms were chosen by hand. Press A to edit them.";
    inline constexpr const char* KEYBOARD_REQ_TOTAL_HEADER = "Moon Requirement Total";
    inline constexpr const char* KEYBOARD_REQ_TOTAL_SUBTEXT = "Moons the requirement kingdoms ask for between them. Spread over them in the vanilla proportions.";
    inline constexpr const char* KEYBOARD_REQ_BASE_HEADER = "Kingdom Moon Requirement";
    inline constexpr const char* KEYBOARD_REQ_BASE_SUBTEXT = "Moons this kingdom asks for. Req Difficulty still moves it a little each seed. 0 stops it gating.";
    inline constexpr const char16_t* PAGE_ADVANCED_MOON_REQUIREMENT = u"Advanced: Moon Requirements";
    inline constexpr const char16_t* ADVANCED_REQ_TOTAL_FORMAT = u"Total: %d moons";
    inline constexpr const char* DESC_ADVANCED_REQ_TOTAL = "How many moons the requirement kingdoms ask for between them. Typing a number here spreads it over them in the vanilla proportions.";
    inline constexpr const char* DESC_ADVANCED_REQ_BASE = "This kingdom's share of the total. Req Difficulty still moves it a little each seed. 0 means it does not gate; type a number to put it back.";
    inline constexpr const char16_t* PAGE_ADVANCED_REQUIREMENT_KINGDOMS = u"Advanced: Kingdoms with Requirements";
    inline constexpr const char* DESC_ADVANCED_REQUIREMENT_KINGDOM = "Gate progress behind a moon requirement in this kingdom. Its size follows the share of its own moons that the other requirement kingdoms ask for.";
    inline constexpr const char* DESC_SHOP_LOCATIONS_DISABLED = "Where the Crazy Cap doors lead. Needs Loading Zones on: with it Off nothing is shuffled, so the shops stay put either way.";
    inline constexpr const char* DESC_SHOP_LOCATIONS_SWAP = "Swap: shop doors shuffle only with each other, so a shop door always leads to some shop. Metro's two street doors count as one.";
    inline constexpr const char* DESC_SHOP_LOCATIONS_VANILLA = "Vanilla: shop doors keep their original destinations. Sand's back way into its shop is not a shop door and still shuffles.";
    inline constexpr const char* DESC_SHOP_LOCATIONS_RANDOMIZED = "Randomized: shop doors are ordinary loading zones and shuffle with everything else.";

    inline constexpr const char16_t* ROLL_START_POSITION[] = {
        u"Vanilla", u"Overworld", u"Max", u"Odyssey"
    };
    inline constexpr s32 ROLL_START_POSITION_COUNT = 4;
    inline constexpr const char16_t* ROLL_ENTRANCE[] = {
        u"Off", u"Coupled", u"Limited", u"Decoupled"
    };
    inline constexpr s32 ROLL_ENTRANCE_COUNT = 4;
    inline constexpr const char16_t* ROLL_SHOP_LOCATION[] = {
        u"Randomized", u"Swap", u"Vanilla"
    };
    inline constexpr s32 ROLL_SHOP_LOCATION_COUNT = 3;

    // ==========================================================================
    // Moon Allocation
    // ==========================================================================

    // The moon-allocation rows share a page with the moon-requirement rows now.
    inline constexpr const char16_t* PAGE_RANDOMIZATION_III = u"Randomization III";

    inline constexpr const char16_t* OPT_MOON_ALLOCATION_MODE_LABEL = u"Moon Allocation:";
    inline constexpr const char* DESC_MOON_ALLOCATION_MODE = "Vanilla keeps moon kingdoms unchanged. Custom enables the difficulty and per-kingdom allocation controls.";
    inline constexpr const char16_t* OPT_ENABLE_MOON_REALLOCATION_LABEL = u"Enable Moon Re-allocation";
    inline constexpr const char* DESC_ENABLE_MOON_REALLOCATION = "Expose controls for re-allocating eligible moons by kingdom. Off keeps the child settings without changing allocation.";
    inline constexpr const char16_t* OPT_RANDOMIZE_MOON_ALLOCATION_LABEL = u"Randomize Moon Allocation";
    inline constexpr const char* DESC_RANDOMIZE_MOON_ALLOCATION = "Allocate the eligible global Moon pool across kingdoms. Needs Moon Randomization and Moon Re-allocation.";
    inline constexpr const char16_t* OPT_MOON_ALLOCATION_DIFFICULTY_LABEL = u"Moon Allocation Difficulty:";
    inline constexpr const char* DESC_MOON_ALLOCATION_DIFFICULTY_EASY = "Easy: spread eligible moons toward an even kingdom balance.";
    inline constexpr const char* DESC_MOON_ALLOCATION_DIFFICULTY_HARD = "Hard: give one or two additional kingdoms only 0-20 moons above their base requirement.";
    inline constexpr const char* DESC_MOON_ALLOCATION_DIFFICULTY_EXPERT = "Expert: tighten another one or two kingdoms beyond Hard and concentrate the displaced moons.";
    inline constexpr const char* DESC_MOON_ALLOCATION_DIFFICULTY_MAX = "Max: tighten one to seven additional kingdoms to 0-20 excess moons.";
    inline constexpr const char* DESC_MOON_ALLOCATION_DIFFICULTY_VANILLA = "Vanilla: shuffle the exact vanilla excess totals above each kingdom's base requirement.";
    inline constexpr const char16_t* OPT_MOON_COIN_POLICY_LABEL = u"Exclude Moon Coins:";
    inline constexpr const char* DESC_MOON_COIN_POLICY_STANDARD = "Standard: move moons that award coins, but never to Mushroom, Dark, or Darker.";
    inline constexpr const char* DESC_MOON_COIN_POLICY_ABILITY_RUSH = "Ability Rush: coin Moons can go anywhere, including Mushroom, Dark, and Darker.";
    inline constexpr const char* DESC_MOON_COIN_POLICY_VANILLA = "Vanilla: keep moons that award coins intact.";
    inline constexpr const char16_t* OPT_TRIPLE_MOONS_LABEL = u"Triple Moons:";
    inline constexpr const char* DESC_TRIPLE_MOONS_ANYWHERE = "Anywhere: triple-moon rewards can appear at any moon location. Story progression still follows the physical location.";
    inline constexpr const char* DESC_TRIPLE_MOONS_RANDOMIZED = "Randomized: triple moons can be from any kingdom. The same total is kept and the triple spots stay put; only the pool changes. Needs Randomize Moons on.";
    inline constexpr const char* DESC_TRIPLE_MOONS_ANYWHERE_RANDOMIZED = "Anywhere + Randomized: triple moons can be from any kingdom and can appear at any location. Needs Randomize Moons on.";
    inline constexpr const char* DESC_TRIPLE_MOONS_VANILLA = "Vanilla: triple moons are swapped between one another, staying in their existing triple spots.";
    inline constexpr const char* DESC_TRIPLE_MOONS_CUSTOM = "Custom: choose how many triple moons each kingdom receives. The total number of triple moons stays unchanged.";
    inline constexpr const char16_t* OPT_TRIPLE_MOONS_ANYWHERE_LABEL = u"Triple Moons Anywhere";
    inline constexpr const char* DESC_TRIPLE_MOONS_ANYWHERE_CHECK = "Allow triple-moon rewards at any moon location. Story progression still follows the physical location.";

    inline constexpr const char16_t* ROLL_TRIPLE_MOONS[] = {
        u"Vanilla", u"Randomized", u"Custom"
    };
    inline constexpr s32 ROLL_TRIPLE_MOONS_COUNT = 3;
    inline constexpr const char16_t* ROLL_MOON_ALLOCATION_MODE[] = {
        u"Vanilla", u"Custom"
    };
    inline constexpr s32 ROLL_MOON_ALLOCATION_MODE_COUNT = 2;
    inline constexpr const char16_t* ROLL_MOON_ALLOCATION_DIFFICULTY[] = {
        u"Easy", u"Vanilla", u"Hard", u"Expert", u"Max"
    };
    inline constexpr s32 ROLL_MOON_ALLOCATION_DIFFICULTY_COUNT = 5;
    inline constexpr const char16_t* ROLL_MOON_COIN_POLICY[] = {
        u"Vanilla", u"Standard", u"Ability Rush"
    };
    inline constexpr s32 ROLL_MOON_COIN_POLICY_COUNT = 3;

    // ==========================================================================
    // Logic Settings
    // ==========================================================================

    inline constexpr const char16_t* PAGE_LOGIC_SETTINGS = u"Logic Settings";

    inline constexpr const char16_t* OPT_PLACEMENT_LOGIC_LABEL = u"Logic Level:";
    inline constexpr const char* DESC_PLACEMENT_LOGIC_NO_LOGIC = "No Logic: fully random. The seed might not be beatable.";
    inline constexpr const char* DESC_PLACEMENT_LOGIC_CASUAL = "Casual: the seed is beatable with casual movement.";
    inline constexpr const char* DESC_PLACEMENT_LOGIC_ADVANCED = "Advanced: the seed might require advanced movement to beat.";
    inline constexpr const char* DESC_PLACEMENT_LOGIC_EXPERT = "Expert: the seed might require expert movement to beat.";
    inline constexpr const char* DESC_PLACEMENT_LOGIC_GLITCHED = "Glitched: the seed might require glitches to beat.";
    inline constexpr const char16_t* OPT_UNLOCK_PACING_LABEL = u"Abilities & Captures:";
    inline constexpr const char* DESC_UNLOCK_PACING_FRONT_LOADED = "Front-Loaded: most of your moveset lands in the first few spheres, so the run opens into free movement fast.";
    inline constexpr const char* DESC_UNLOCK_PACING_EARLY = "Early: captures and abilities show up sooner than usual, giving you more movement options early on.";
    inline constexpr const char* DESC_UNLOCK_PACING_LATE = "Late: long stretches run on base Mario; at most one capture or ability per sphere.";
    inline constexpr const char* DESC_UNLOCK_PACING_BACK_LOADED = "Back-Loaded: most of your moveset arrives in the last third of the run.";
    inline constexpr const char* DESC_UNLOCK_PACING_RANDOM = "Random: the pacing is chosen from the seed, so you find out by playing.";
    inline constexpr const char* DESC_UNLOCK_PACING_STANDARD = "Standard: captures and abilities are spread evenly across the run.";
    inline constexpr const char16_t* OPT_SEED_OPENNESS_LABEL = u"Seed Openness:";
    inline constexpr const char* DESC_SEED_OPENNESS_CLAUSTROPHOBIC = "Claustrophobic: long single-file door chains, lots of dead ends, and few places open at once.";
    inline constexpr const char* DESC_SEED_OPENNESS_COMPACT = "Compact: fewer things open at a time and doors tend to keep chaining onward.";
    inline constexpr const char* DESC_SEED_OPENNESS_OPEN = "Open: shorter chains, more ways into each kingdom, and more open at once.";
    inline constexpr const char* DESC_SEED_OPENNESS_WIDE_OPEN = "Wide Open: short chains and lots of direct kingdom-to-kingdom doors; plenty always open to explore.";
    inline constexpr const char* DESC_SEED_OPENNESS_RANDOM = "Random: how open the seed is is chosen from the seed itself.";
    inline constexpr const char* DESC_SEED_OPENNESS_STANDARD = "Standard: a balanced mix of dead ends, chains and cross-kingdom doors.";
    inline constexpr const char16_t* OPT_SEED_DIFFICULTY_LABEL = u"Seed Difficulty:";
    inline constexpr const char* DESC_SEED_DIFFICULTY_RELAXED = "Relaxed: a short path, and what you need is found close to where you need it. Prefers casual movement.";
    inline constexpr const char* DESC_SEED_DIFFICULTY_EASY = "Easy: a slightly shorter path with less back-and-forth than usual.";
    inline constexpr const char* DESC_SEED_DIFFICULTY_HARD = "Hard: a longer path, unlocks placed well away from the gates they open, and harder movement is required where the Logic Level allows it.";
    inline constexpr const char* DESC_SEED_DIFFICULTY_BRUTAL = "Brutal: the longest path, maximum back-and-forth, and the hardest movement the Logic Level allows.";
    inline constexpr const char* DESC_SEED_DIFFICULTY_RANDOM = "Random: the difficulty is chosen from the seed.";
    inline constexpr const char* DESC_SEED_DIFFICULTY_STANDARD = "Standard: the usual path length, spread and movement demands.";

    inline constexpr const char16_t* ROLL_LOGIC[] = {
        u"No Logic", u"Casual", u"Advanced", u"Expert", u"Glitched"
    };
    inline constexpr s32 ROLL_LOGIC_COUNT = 5;
    inline constexpr const char16_t* ROLL_UNLOCK_PACING[] = {
        u"Front-Loaded", u"Early", u"Standard", u"Late", u"Back-Loaded", u"Random"
    };
    inline constexpr const char16_t* ROLL_SEED_OPENNESS[] = {
        u"Claustrophobic", u"Compact", u"Standard", u"Open", u"Wide Open", u"Random"
    };
    inline constexpr const char16_t* ROLL_SEED_DIFFICULTY[] = {
        u"Relaxed", u"Easy", u"Standard", u"Hard", u"Brutal", u"Random"
    };
    inline constexpr s32 ROLL_SEED_AXIS_COUNT = 6;

    // ==========================================================================
    // Chaotic Randomization
    // ==========================================================================

    inline constexpr const char16_t* PAGE_CHAOTIC_RANDOMIZATION = u"Enemy Randomizer";

#if SMOR_FEATURE_ENEMY_RANDOMIZATION
    inline constexpr const char16_t* OPT_ENEMY_SWAP_LABEL = u"Randomize Enemies/Captures";
    inline constexpr const char* DESC_ENEMY_SWAP = "Randomize eligible enemies and captures with other enemies and captures. Seeds should remain beatable.";
    inline constexpr const char16_t* OPT_NPC_SWAP_LABEL = u"Randomize NPCs:";
    inline constexpr const char* DESC_NPC_SWAP = "Randomize eligible NPCs with other NPCs or NPCs and captures. Seeds should remain beatable.";
    inline constexpr const char16_t* OPT_TWO_D_SWAP_LABEL = u"Randomize 2D Enemies";
    inline constexpr const char* DESC_TWO_D_SWAP = "Shuffle the enemies in flat 2D sections among themselves.";
    inline constexpr const char16_t* ROLL_NPC_SWAP_MODE[] = {
        u"None", u"NPCs", u"NPCs or Captures"
    };
    inline constexpr s32 ROLL_NPC_SWAP_MODE_COUNT = 3;
#endif
#if SMOR_FEATURE_PROP_RANDOMIZATION
    inline constexpr const char16_t* OPT_PROP_SWAP_LABEL = u"Randomize Props:";
    inline constexpr const char* DESC_PROP_SWAP = "Randomize some props into other props, captures, or just delete them. Seeds may not be beatable.";
    inline constexpr const char16_t* OPT_PROP_SWAP_RATE_LABEL = u"Prop Swap Rate:";
    inline constexpr const char* DESC_PROP_SWAP_RATE = "Choose the percentage of eligible props to be randomized. Higher has more chaos but more risk of making the seed unbeatable.";
    inline constexpr const char16_t* ROLL_PROP_SWAP_RATE[] = {
        u"25%", u"50%", u"75%", u"100%"
    };
    inline constexpr s32 ROLL_PROP_SWAP_RATE_COUNT = 4;
    inline constexpr const char16_t* ROLL_PROP_SWAP_MODE[] = {
        u"None", u"Props", u"Props or Captures", u"Props/Captures/Delete"
    };
    inline constexpr s32 ROLL_PROP_SWAP_MODE_COUNT = 4;
#endif

    // ==========================================================================
    // Ability & Capture Lock
    // ==========================================================================

    inline constexpr const char16_t* PAGE_ABILITY_CAPTURE_LOCK = u"Ability & Capture Lock";

    inline constexpr const char16_t* OPT_LOCKED_CAPTURES_LABEL = u"Locked Captures:";
    inline constexpr const char16_t* ROLL_LOCKED_CAPTURES[] = {
        u"Unlocked", u"All", u"Critical", u"Custom"
    };
    inline constexpr s32 ROLL_LOCKED_CAPTURES_COUNT = 4;
    inline constexpr const char* DESC_LOCKED_CAPTURES_VANILLA = "Vanilla: every capture is available from the start.";
    inline constexpr const char* DESC_LOCKED_CAPTURES_ALL = "All: every capture starts locked and is unlocked by collecting moons.";
    inline constexpr const char* DESC_LOCKED_CAPTURES_CRITICAL = "Critical: only the captures a run cannot be finished without start locked. The rest are available from the start.";
    inline constexpr const char* DESC_LOCKED_CAPTURES_CUSTOM = "Custom: the locked set was chosen by hand. Press A to edit it.";
    inline constexpr const char16_t* OPT_LOCKED_ABILITIES_LABEL = u"Locked Abilities:";
    inline constexpr const char16_t* ROLL_LOCKED_ABILITIES[] = {
        u"Unlocked", u"All", u"Original", u"Non-Progressive", u"Custom"
    };
    inline constexpr s32 ROLL_LOCKED_ABILITIES_COUNT = 5;
    inline constexpr const char* DESC_LOCKED_ABILITIES_VANILLA = "Vanilla: every ability is available from the start.";
    inline constexpr const char* DESC_LOCKED_ABILITIES_ALL = "All: every ability starts locked and is unlocked by collecting moons.";
    inline constexpr const char* DESC_LOCKED_ABILITIES_ORIGINAL = "Original: Mario's moves start locked, but the traversal abilities (swimming, grabbing, Jaxi, scooter, and so on) stay available.";
    inline constexpr const char* DESC_LOCKED_ABILITIES_NON_PROGRESSIVE = "Non-Progressive: every ability starts locked and each one is granted in its final form - the triple jump arrives whole rather than one stage at a time.";
    inline constexpr const char* DESC_LOCKED_ABILITIES_CUSTOM = "Custom: the locked set was chosen by hand. Press A to edit it.";
    inline constexpr const char16_t* OPT_ADVANCED_PROGRESSIVE_ABILITIES_LABEL = u"Progressive Abilities";
    inline constexpr const char* DESC_ADVANCED_PROGRESSIVE_ABILITIES = "Unlock multi-stage moves one stage at a time. Off grants each move in its final form the first time it is unlocked.";
    inline constexpr const char16_t* OPT_CAPTURE_GRANT_KINGDOMS_LABEL = u"Moons Granting Captures:";
    inline constexpr const char16_t* ROLL_CAPTURE_GRANT_KINGDOMS[] = {
        u"Mushroom", u"Custom"
    };
    inline constexpr s32 ROLL_CAPTURE_GRANT_KINGDOMS_COUNT = 2;
    inline constexpr const char* DESC_CAPTURE_GRANT_KINGDOMS_DEFAULT = "Mushroom Kingdom moons hand out the locked captures. Press A to choose a different set of kingdoms.";
    inline constexpr const char* DESC_CAPTURE_GRANT_KINGDOMS_CUSTOM = "Custom: the kingdoms handing out captures were chosen by hand. Press A to edit them.";
    inline constexpr const char16_t* OPT_ABILITY_GRANT_KINGDOMS_LABEL = u"Moons Granting Abilities:";
    inline constexpr const char16_t* ROLL_ABILITY_GRANT_KINGDOMS[] = {
        u"Dark + Darker", u"Custom"
    };
    inline constexpr s32 ROLL_ABILITY_GRANT_KINGDOMS_COUNT = 2;
    inline constexpr const char* DESC_ABILITY_GRANT_KINGDOMS_DEFAULT = "Dark Side and Darker Side moons hand out the locked abilities. Press A to choose a different set of kingdoms.";
    inline constexpr const char* DESC_ABILITY_GRANT_KINGDOMS_CUSTOM = "Custom: the kingdoms handing out abilities were chosen by hand. Press A to edit them.";
    inline constexpr const char16_t* PAGE_ADVANCED_CAPTURE_GRANT_KINGDOMS = u"Advanced: Moons Granting Captures";
    inline constexpr const char16_t* PAGE_ADVANCED_ABILITY_GRANT_KINGDOMS = u"Advanced: Moons Granting Abilities";
    inline constexpr const char* DESC_ADVANCED_CAPTURE_GRANT_KINGDOM = "Hand out locked captures on this kingdom's moons. A kingdom left with nothing to give falls back to coins.";
    inline constexpr const char* DESC_ADVANCED_ABILITY_GRANT_KINGDOM = "Hand out locked abilities on this kingdom's moons. A kingdom left with nothing to give falls back to coins.";
    inline constexpr const char16_t* OPT_STARTING_EQUIPMENT_LABEL = u"Starting Equipment:";
    inline constexpr const char* DESC_STARTING_EQUIPMENT_SCARCE = "Scarce: start with just a single jump and a cap throw.";
    inline constexpr const char* DESC_STARTING_EQUIPMENT_NORMAL = "Normal: also start with 2 captures and an extra ability.";
    inline constexpr const char* DESC_STARTING_EQUIPMENT_PLENTIFUL = "Plentiful: also start with 4 captures and 2 extra abilities.";
    inline constexpr const char* DESC_STARTING_EQUIPMENT_CUSTOM = "Custom: use the starting captures, cap throws, and abilities chosen in Advanced settings.";
    inline constexpr const char* DESC_ADVANCED_CAPTURE_POOL = "Include this capture in the locked capture reward pool.";
    inline constexpr const char* DESC_ADVANCED_ABILITY_POOL = "Include this ability in the locked ability reward pool.";
    inline constexpr const char* DESC_ADVANCED_STARTING_CAPTURES = "How many seeded captures are granted at the start.";
    inline constexpr const char* DESC_ADVANCED_STARTING_THROWS = "How many cap throws are granted at the start.";
    inline constexpr const char* DESC_ADVANCED_STARTING_ABILITIES = "How many extra abilities are granted at the start.";
    inline constexpr const char16_t* OPT_CAPTURE_DIFFICULTY_LABEL = u"Item Pool:";
    inline constexpr const char* DESC_CAPTURE_DIFFICULTY_SCARCE = "Scarce: one copy of each capture and ability on Mushroom and Dark/Darker Side moons.";
    inline constexpr const char* DESC_CAPTURE_DIFFICULTY_NORMAL = "Normal: duplicated captures and abilities can appear on Mushroom and Dark/Darker Side moons.";

    inline constexpr const char16_t* ROLL_STARTING_EQUIPMENT[] = {
        u"Scarce", u"Normal", u"Plentiful", u"Custom"
    };
    inline constexpr s32 ROLL_STARTING_EQUIPMENT_COUNT = 4;
    inline constexpr const char16_t* ROLL_CAPTURE_DIFFICULTY[] = {
        u"Scarce", u"Normal"
    };
    inline constexpr s32 ROLL_CAPTURE_DIFFICULTY_COUNT = 2;

    // ==========================================================================
    // Movement Abilities
    // ==========================================================================

    inline constexpr const char16_t* PAGE_MOVEMENT_ABILITIES = u"Movement Abilities";

    inline constexpr const char16_t* OPT_MOON_GRAVITY_LABEL = u"Moon Low Gravity:";
    inline constexpr const char* DESC_MOON_GRAVITY_ABILITY_LOCK = "Ability Lock: Moon Kingdom is normal gravity until a Dark Side moon grants low gravity.";
    inline constexpr const char* DESC_MOON_GRAVITY_REQUIREMENTS = "Moon Requirements: low gravity arrives once you hold Moon Kingdom's own moon requirement.";
    inline constexpr const char* DESC_MOON_GRAVITY_EXTRA_REQUIREMENTS = "Extra Moon Reqs: as Moon Requirements, plus 5-10 more moons chosen by the seed.";
    inline constexpr const char* DESC_MOON_GRAVITY_UNLOCKED = "Unlocked: Moon Kingdom is low gravity from the start, as in the base game.";
    inline constexpr const char16_t* OPT_SWIM_LOCK_LABEL = u"Locked Swim";
    inline constexpr const char* DESC_SWIM_LOCK = "Mario cannot swim until unlocked. Entering water bubbles him back to safe ground.";
    inline constexpr const char16_t* OPT_GRAB_LOCK_LABEL = u"Locked Grab";
    inline constexpr const char* DESC_GRAB_LOCK = "Mario cannot pick up or carry objects until unlocked.";
    inline constexpr const char16_t* OPT_JAXI_LOCK_LABEL = u"Locked Jaxi Riding";
    inline constexpr const char* DESC_JAXI_LOCK = "Mario can mount Jaxi but it will not run until unlocked.";
    inline constexpr const char16_t* OPT_SCOOTER_LOCK_LABEL = u"Locked Scooter Riding";
    inline constexpr const char* DESC_SCOOTER_LOCK = "Mario can stand on the Motor Scooter but cannot drive it until unlocked.";
    inline constexpr const char16_t* OPT_ROCKET_FLOWER_LOCK_LABEL = u"Locked Rocket Flower";
    inline constexpr const char* DESC_ROCKET_FLOWER_LOCK = "Cappy cannot pick up Rocket Flowers until unlocked.";
    inline constexpr const char16_t* OPT_NPC_TALK_LOCK_LABEL = u"Locked NPC Talking";
    inline constexpr const char* DESC_NPC_TALK_LOCK = "NPCs cannot be talked to until unlocked. The prompt still shows, but Cappy answers instead.";
    inline constexpr const char16_t* OPT_PIPE_2D_LOCK_LABEL = u"Locked 2D Pipes";
    inline constexpr const char* DESC_PIPE_2D_LOCK = "Pipes into 2D sections refuse Mario until unlocked. Pipes back out of 2D always work.";
    inline constexpr const char16_t* OPT_TIMER_CHALLENGE_LOCK_LABEL = u"Locked Timer Challenges";
    inline constexpr const char* DESC_TIMER_CHALLENGE_LOCK = "P-Switch and Scarecrow timers run out the moment they start until unlocked.";

    // No "Unlocked" entry: that state is the ability's own row in the Locked
    // Abilities advanced page being unchecked, which hides this row entirely.
    inline constexpr const char16_t* ROLL_MOON_GRAVITY[] = {
        u"Ability Lock", u"Moon Requirements", u"Extra Moon Reqs"
    };
    inline constexpr s32 ROLL_MOON_GRAVITY_COUNT = 3;

    // ==========================================================================
    // Purple Coin Shop
    // ==========================================================================

    inline constexpr const char16_t* PAGE_PURPLE_COIN_SHOP = u"Purple Coin Shop";

    inline constexpr const char16_t* OPT_PURPLE_SHOP_ABILITIES_LABEL = u"Abilities For Sale:";
    inline constexpr const char* DESC_PURPLE_SHOP_ABILITIES = "Each Purple Shop sells this many seeded progressive ability offers. Shops can repeat offers, but an individual shop cannot.";
    inline constexpr const char16_t* OPT_PURPLE_SHOP_CAPTURES_LABEL = u"Captures For Sale:";
    inline constexpr const char* DESC_PURPLE_SHOP_CAPTURES = "Each Purple Shop sells this many seeded capture offers. Shops can repeat offers, but an individual shop cannot.";
    inline constexpr const char16_t* OPT_PURPLE_SHOP_MOONS_LABEL = u"Moons For Sale";
    inline constexpr const char* DESC_PURPLE_SHOP_MOONS = "Every Purple Shop sells one Power Moon for every eligible kingdom.";
    inline constexpr const char16_t* OPT_PURPLE_SHOP_PRICE_LABEL = u"Purple Shop Price:";
    inline constexpr const char* DESC_PURPLE_SHOP_PRICE_RANDOM = "Every Purple Shop offer gets its own seeded price, between 1 and that kingdom's own purple coin total, so two things on the same shelf rarely cost the same.";
    inline constexpr const char* DESC_PURPLE_SHOP_PRICE_PROPORTIONAL = "The share of the kingdom's own purple coin total that every Purple Shop offer costs there.";
    inline constexpr const char* DESC_PURPLE_SHOP_PRICE_FLAT = "The regional-purple-coin price of every Purple Shop offer.";
    inline constexpr const char16_t* OPT_PROPORTIONAL_KINGDOM_PRICING_LABEL = u"Proportional Kingdom Pricing";
    inline constexpr const char* DESC_PROPORTIONAL_KINGDOM_PRICING = "Read the price above as a percentage of the kingdom's own purple coin total instead of a flat amount, so a kingdom with fewer coins charges less. Kingdoms with no purple coins keep the flat price.";

    inline constexpr const char16_t* ROLL_PURPLE_SHOP_OFFERS[] = {
        u"0", u"1", u"2", u"3"
    };
    inline constexpr s32 ROLL_PURPLE_SHOP_OFFERS_COUNT = 4;
    inline constexpr const char16_t* ROLL_PURPLE_SHOP_PRICE[] = {
        u"10", u"20", u"25", u"50", u"100", u"Random", u"Custom"
    };
    inline constexpr s32 ROLL_PURPLE_SHOP_PRICE_COUNT = 7;
    inline constexpr const char16_t* ROLL_PURPLE_SHOP_PRICE_PERCENT[] = {
        u"10%", u"20%", u"25%", u"50%", u"100%", u"Random", u"Custom"
    };
    inline constexpr s32 ROLL_PURPLE_SHOP_PRICE_PERCENT_COUNT = 7;

    // ==========================================================================
    // Additional Options
    // ==========================================================================

    inline constexpr const char16_t* OPT_MOON_REQUIREMENT_LABEL = u"Moon Requirement: 124";
    inline constexpr const char* DESC_MOON_REQUIREMENT = "Press A to set the total sum of kingdom moon requirements. Harder through Max can leave kingdoms at 0; other modes keep at least one per required kingdom.";
    inline constexpr const char16_t* OPT_MOON_REQ_DIFFICULTY_LABEL = u"Req Difficulty:";
    inline constexpr const char* ROW_MOON_REQ_DEVIATION_FORMAT = "Req Deviation: %d";
    inline constexpr const char* DESC_MOON_REQ_DEVIATION = "How far each kingdom requirement may sit from its base value. Press A to type a number; 5 is the spread the named difficulties describe.";
    inline constexpr const char* DESC_MOON_REQ_DIFFICULTY_EASY = "Easy: kingdoms with fewer moons and earlier kingdoms need fewer.";
    inline constexpr const char* DESC_MOON_REQ_DIFFICULTY_NORMAL = "Normal: vanilla requirement distribution.";
    inline constexpr const char* DESC_MOON_REQ_DIFFICULTY_HARD = "Hard: kingdoms with fewer moons and earlier kingdoms need more, capped at 30% of each kingdom's moons.";
    inline constexpr const char* DESC_MOON_REQ_DIFFICULTY_HARDER = "Harder: randomly assigns each eligible kingdom 0 to 40% of its moons, preserving the total.";
    inline constexpr const char* DESC_MOON_REQ_DIFFICULTY_HARDEST = "Hardest: randomly assigns each eligible kingdom 0 to 60% of its moons, preserving the total.";
    inline constexpr const char* DESC_MOON_REQ_DIFFICULTY_INSANE = "Insane: randomly assigns each eligible kingdom 0 to 80% of its moons, preserving the total.";
    inline constexpr const char* DESC_MOON_REQ_DIFFICULTY_MAX = "Max: randomly assigns each eligible kingdom 0 to its max moons, preserving the total.";
    inline constexpr const char16_t* OPT_WORLD_PEACE_REQ_LABEL = u"World Peace:";
    inline constexpr const char* DESC_WORLD_PEACE_REQ_ON_KINGDOM_COMPLETE = "The chapel door stays shut until you leave every kingdom, but leaving a kingdom for another one automatically brings it to peace.";
    inline constexpr const char* DESC_WORLD_PEACE_REQ_REQUIRED = "The chapel door stays shut until world peace is achieved in every kingdom.";
    inline constexpr const char* DESC_WORLD_PEACE_REQ_STORY = "The chapel door only needs the normal moon requirement.";

    // The eighth value is derived: it appears only while the per-kingdom deviation
    // differs from its default, and rolling off it restores that default.
    inline constexpr const char16_t* ROLL_MOON_REQ_DIFFICULTY[] = {
        u"Easy", u"Normal", u"Hard", u"Harder", u"Hardest", u"Insane", u"Max",
        u"Custom"
    };
    inline constexpr s32 ROLL_MOON_REQ_DIFFICULTY_COUNT = 7;
    inline constexpr s32 ROLL_MOON_REQ_DIFFICULTY_CUSTOM_COUNT = 8;
    inline constexpr const char* DESC_MOON_REQ_DIFFICULTY_CUSTOM = "Custom: kingdom requirements are shifted by the deviation you entered. Press A to change it.";
    inline constexpr const char16_t* ROLL_WORLD_PEACE_REQ[] = {
        u"Story Progression", u"On Kingdom Complete", u"Required"
    };
    inline constexpr s32 ROLL_WORLD_PEACE_REQ_COUNT = 3;

    // ==========================================================================
    // Additional Options II
    // ==========================================================================

    inline constexpr const char16_t* OPT_MOON_ROCK_KEY_LABEL = u"Moon Rock Key";
    inline constexpr const char* DESC_MOON_ROCK_KEY = "Each Moon Rock kingdom has a randomized kingdom moon that must be found before Mario can open that kingdom's Moon Rock.";
    inline constexpr const char16_t* OPT_SUBAREA_UNLOCKS_LABEL = u"Subarea Unlocks:";
    inline constexpr const char* DESC_SUBAREA_UNLOCKS_PER_TYPE = "Optional sub-areas are locked by the kind of door they use. One key per kind - Warp Pipes, Moon Pipes, Open Passages, Doorways, Scarecrow Doors, Beanstalks - unlocks that kind everywhere.";
    inline constexpr const char* DESC_SUBAREA_UNLOCKS_PER_KINGDOM = "Optional sub-areas are locked per kingdom. One key per kingdom unlocks every kind of door in it.";
    inline constexpr const char* DESC_SUBAREA_UNLOCKS_PER_KINGDOM_AND_TYPE = "Optional sub-areas are locked per kingdom and door kind, e.g. Sand Kingdom's Warp Pipes. The finest setting, and the only one that needs both Cap and Moon moons.";
    inline constexpr const char* DESC_SUBAREA_UNLOCKS_UNLOCKED = "Optional sub-areas are open from the start. Story routes are never locked either way.";
    inline constexpr const char16_t* OPT_SUBAREA_UNLOCK_HOST_LABEL = u"Unlock Moons:";
    inline constexpr const char* DESC_SUBAREA_UNLOCK_HOST_CAP = "Cap Kingdom moons carry the sub-area keys. Its collection list becomes the key list and its leftover moons move to the Moon Kingdom page.";
    inline constexpr const char* DESC_SUBAREA_UNLOCK_HOST_MOON = "Moon Kingdom moons carry the sub-area keys. Its collection list becomes the key list and its leftover moons move to the Cap Kingdom page.";
    inline constexpr const char* DESC_SUBAREA_UNLOCK_HOST_CAP_AND_MOON = "Cap and Moon Kingdom moons both carry the sub-area keys. The Moon Kingdom list becomes the key list and every leftover moon moves to the Cap Kingdom page.";
    inline constexpr const char* DESC_SUBAREA_UNLOCK_HOST_ALL = "Any kingdom can carry sub-area keys. The generator chooses key locations from the full moon pool.";
    inline constexpr const char16_t* OPT_VISITED_KINGDOMS_MAP_LABEL = u"Kingdoms on Map:";
    inline constexpr const char* DESC_VISITED_KINGDOMS_MAP_VISITED = "Kingdoms visited but not unlocked appear in the World Map and are accessible from the Odyssey.";
    inline constexpr const char* DESC_VISITED_KINGDOMS_MAP_UNLOCKED = "Every kingdom is on the World Map from the start, in vanilla order, and accessible from the Odyssey. Progression still unlocks them one by one as normal.";
    inline constexpr const char* DESC_VISITED_KINGDOMS_MAP_VANILLA = "Only the kingdoms progression has unlocked appear in the World Map.";
    inline constexpr const char* DESC_VISITED_KINGDOMS_MAP_CUSTOM = "Choose which kingdoms are visible and reachable from the Odyssey at the start of the run.";

    // Display order only. SubareaUnlockMode's values are persisted directly and
    // are NOT in this order - subareaUnlockRollToMode() maps between them.
    inline constexpr const char16_t* ROLL_SUBAREA_UNLOCKS[] = {
        u"Disabled", u"Per Kingdom", u"Per Type", u"Per Kingdom + Type"
    };
    inline constexpr s32 ROLL_SUBAREA_UNLOCKS_COUNT = 4;
    inline constexpr const char16_t* ROLL_SUBAREA_UNLOCK_HOST[] = {
        u"Cap", u"Moon", u"Cap + Moon", u"All Kingdoms"
    };
    inline constexpr s32 ROLL_SUBAREA_UNLOCK_HOST_COUNT = 4;
    inline constexpr const char16_t* ROLL_KEY_SCOPE[] = {
        u"Any Moon", u"Within Kingdom"
    };
    inline constexpr s32 ROLL_KEY_SCOPE_COUNT = 2;
    // Custom is a leftover value that only a per-kingdom editor could set, and
    // there is no such editor on this row - it is shown only when a loaded save or
    // preset already carries it, so an imported setting is not misreported.
    inline constexpr const char16_t* ROLL_VISITED_KINGDOMS_MAP[] = {
        u"Vanilla", u"Visited", u"Unlocked", u"Custom"
    };
    inline constexpr s32 ROLL_VISITED_KINGDOMS_MAP_COUNT = 3;
    inline constexpr s32 ROLL_VISITED_KINGDOMS_MAP_CUSTOM_COUNT = 4;

    // ==========================================================================
    // Hint Configuration
    // ==========================================================================

    inline constexpr const char16_t* PAGE_HINT_CONFIGURATION = u"Hint Settings";

    // The left column of the Hint Settings grid: what each category is bought with.
    inline constexpr const char16_t* ROLL_HINT_CURRENCY[] = { u"Coins", u"Hint Tokens" };
    inline constexpr s32 ROLL_HINT_CURRENCY_COUNT = 2;
    inline constexpr const char16_t* OPT_HINT_CURRENCY_MOONS_LABEL = u"Moons:";
    inline constexpr const char16_t* OPT_HINT_CURRENCY_CAPTURES_LABEL = u"Captures:";
    inline constexpr const char16_t* OPT_HINT_CURRENCY_ABILITIES_LABEL = u"Abilities:";
    inline constexpr const char16_t* OPT_HINT_CURRENCY_PATH_LABEL = u"Path:";
    inline constexpr const char16_t* OPT_HINT_CURRENCY_PURCHASES_LABEL = u"Purchases:";
    inline constexpr const char* DESC_HINT_CURRENCY = "Whether this kind of hint is bought with gold coins or with Hint Tokens.";
    inline constexpr const char* DESC_HINT_CURRENCY_NO_TOKENS = "Bought with gold coins. No kingdom hands out Hint Tokens, so there is no other currency to pick - set a kingdom to Hint Tokens on the Kingdom Moons page first.";

    inline constexpr const char16_t* OPT_HINT_COIN_PROGRESSIVE_LABEL = u"Progressive (Coins):";
    inline constexpr const char16_t* OPT_HINT_TOKEN_PROGRESSIVE_LABEL = u"Progressive (Tokens):";
    inline constexpr const char* DESC_HINT_COIN_PROGRESSIVE = "How much more each coin-priced hint costs after a successful purchase.";
    inline constexpr const char* DESC_HINT_TOKEN_PROGRESSIVE = "How much more each token-priced hint costs after a successful purchase.";
    inline constexpr const char16_t* OPT_HINT_COIN_PROGRESSIVE_GLOBAL_LABEL = u"Global Progression (Coins)";
    inline constexpr const char16_t* OPT_HINT_TOKEN_PROGRESSIVE_GLOBAL_LABEL = u"Global Progression (Tokens)";

    inline constexpr const char16_t* OPT_HINT_PRICE_MOONS_LABEL = u"Moons:";
    inline constexpr const char16_t* OPT_HINT_PRICE_CAPTURES_LABEL = u"Captures:";
    inline constexpr const char16_t* OPT_HINT_PRICE_ABILITIES_LABEL = u"Abilities:";
    inline constexpr const char16_t* OPT_HINT_PRICE_PATH_LABEL = u"Path:";
    inline constexpr const char16_t* OPT_HINT_PRICE_PURCHASES_LABEL = u"Purchases:";
    inline constexpr const char* DESC_HINT_PRICE = "Set this category's fixed cost or disable it.";
    inline constexpr const char16_t* OPT_HINT_PROGRESSIVE_PRICE_LABEL = u"Progressive Price:";
    inline constexpr const char* DESC_HINT_PROGRESSIVE_PRICE = "Price increase upon each individual purchase (tracked separately for moons, captures, abilities, paths and purchases, unless Use Global Progressive Cost is on)";
    inline constexpr const char16_t* OPT_HINT_PROGRESSIVE_GLOBAL_LABEL = u"Use Global Progressive Cost";
    inline constexpr const char* DESC_HINT_PROGRESSIVE_GLOBAL = "Charge the price increase off one shared count: every hint purchase, of any type, makes every later hint more expensive.";

    inline constexpr const char16_t* ROLL_HINT_COIN_PRICE[] = {
        u"Disabled", u"100 coins", u"200 coins", u"250 coins", u"500 coins",
        u"1000 coins", u"2000 coins", u"2500 coins"
    };
    inline constexpr s32 ROLL_HINT_COIN_PRICE_COUNT = 8;
    inline constexpr const char16_t* ROLL_HINT_COIN_PRICE_CUSTOM[] = {
        u"Disabled", u"100 Coins", u"200 Coins", u"250 Coins", u"500 Coins",
        u"1000 Coins", u"2000 Coins", u"2500 Coins", u"Custom"
    };
    inline constexpr s32 ROLL_HINT_COIN_PRICE_CUSTOM_COUNT = 9;
    inline constexpr const char16_t* ROLL_HINT_TOKEN_PRICE[] = {
        u"Disabled", u"1 Hint Token", u"2 Hint Tokens", u"3 Hint Tokens",
        u"5 Hint Tokens"
    };
    inline constexpr s32 ROLL_HINT_TOKEN_PRICE_COUNT = 5;
    inline constexpr const char16_t* ROLL_HINT_TOKEN_PRICE_CUSTOM[] = {
        u"Disabled", u"1 Hint Token", u"2 Hint Tokens", u"3 Hint Tokens",
        u"5 Hint Tokens", u"Custom"
    };
    inline constexpr s32 ROLL_HINT_TOKEN_PRICE_CUSTOM_COUNT = 6;
    inline constexpr const char16_t* ROLL_HINT_COIN_PROGRESSIVE_PRICE[] = {
        u"Disabled", u"+100 coins", u"+200 coins", u"+250 coins", u"+500 coins",
        u"+1000 coins", u"+2000 coins", u"+2500 coins"
    };
    inline constexpr s32 ROLL_HINT_COIN_PROGRESSIVE_PRICE_COUNT = 8;
    inline constexpr const char16_t* ROLL_HINT_TOKEN_PROGRESSIVE_PRICE[] = {
        u"Disabled", u"+1 Hint Token", u"+2 Hint Tokens", u"+3 Hint Tokens",
        u"+5 Hint Tokens"
    };
    inline constexpr s32 ROLL_HINT_TOKEN_PROGRESSIVE_PRICE_COUNT = 5;

    // ==========================================================================
    // Misc Randomization
    // ==========================================================================

    // Skyboxes, shader stabilization and every music row, on one page.
    inline constexpr const char16_t* PAGE_VISUAL_AUDIO = u"Visual and Audio";

    inline constexpr const char16_t* OPT_SKYBOXES_LABEL = u"Skyboxes:";
    inline constexpr const char* DESC_SKYBOXES = "Skybox randomization: None, Kingdoms, Subareas, Own Areas (same-type only), or Max (anything goes).";
    inline constexpr const char16_t* OPT_STABILIZE_SHADERS_LABEL = u"Stabilize Shaders";
    inline constexpr const char* DESC_STABILIZE_SHADERS = "Use original kingdom lighting and shaders when swapping skyboxes. Avoids brightness issues, removes some mood from scene.";
    inline constexpr const char16_t* OPT_SPHINX_LABEL = u"Randomize Sphinx Questions";
    inline constexpr const char* DESC_SPHINX = "Randomize Sphinx quiz questions.";

    inline constexpr const char16_t* ROLL_SKYBOXES[] = {
        u"None", u"Kingdoms", u"Subareas", u"Own Areas", u"Max"
    };
    inline constexpr s32 ROLL_SKYBOXES_COUNT = 5;

    // ==========================================================================
    // Music Randomization
    // ==========================================================================

    inline constexpr const char16_t* OPT_MUSIC_LABEL = u"Music Rando:";
    inline constexpr const char16_t* OPT_MUSIC_SOURCE_LABEL = u"Music Source:";
    inline constexpr const char* DESC_MUSIC_MAX = "Each newly requested source theme picks a fresh random track.";
    inline constexpr const char* DESC_MUSIC_EXTENDED = "Each physical kingdom or subarea gets a stable randomized track for each vanilla music cue.";
    inline constexpr const char* DESC_MUSIC_STANDARD = "Randomizes most BGM with one fixed shuffle per seed. Does not affect some event-specific music or cutscenes.";
    inline constexpr const char* DESC_MUSIC_OFF = "Use the game's original background music.";
    inline constexpr const char16_t* OPT_MUSIC_OVERLAY_LABEL = u"Music Overlay";
    inline constexpr const char* DESC_MUSIC_OVERLAY = "Overlay randomized music name.";
    inline constexpr const char16_t* OPT_STREAMER_MODE_LABEL = u"Streamer Mode";
    inline constexpr const char* DESC_STREAMER_MODE = "Exclude Japanese-language theme variants from the music randomizer's track pool.";
    inline constexpr const char16_t* OPT_MUSIC_DISABLE_2D_LABEL = u"Disable 2D Themes";
    inline constexpr const char* DESC_MUSIC_DISABLE_2D = "Keep the 8-bit arrangements out of the track pool, so no area is randomized into its 2D theme.";
    inline constexpr const char16_t* OPT_MUSIC_DISABLE_EXTRA_LABEL = u"Disable Extra Themes";
    inline constexpr const char* DESC_MUSIC_DISABLE_EXTRA = "Keep the tracks that are not kingdom or sub-area themes out of the pool.";
    inline constexpr const char16_t* OPT_MUSIC_DISABLE_BOSS_LABEL = u"Disable Boss Themes";
    inline constexpr const char* DESC_MUSIC_DISABLE_BOSS = "Keep the boss battle themes out of the pool, so every fight keeps its own music and no ordinary area is randomized into a boss theme.";

    inline constexpr const char16_t* ROLL_MUSIC[] = {
        u"Off", u"Standard", u"Extended", u"Max"
    };
    inline constexpr s32 ROLL_MUSIC_COUNT = 4;

    // ==========================================================================
    // Quality of Life
    // ==========================================================================

    inline constexpr const char16_t* PAGE_QUALITY_OF_LIFE = u"Quality of Life";

    inline constexpr const char16_t* OPT_FAST_MOON_DEMOS_LABEL = u"Fast Moon Demos";
    inline constexpr const char* DESC_FAST_MOON_DEMOS = "Moons are collected almost instantly.";
    inline constexpr const char16_t* OPT_SKIP_CUTSCENE_CAMERAS_LABEL = u"Skip Cutscene Cameras";
    inline constexpr const char* DESC_SKIP_CUTSCENE_CAMERAS = "Skip the camera pan that plays when a switch reveals something (faster, less interruption).";
    inline constexpr const char16_t* OPT_MOTION_FREE_LABEL = u"Motion-Free Controls";
    inline constexpr const char* DESC_MOTION_FREE = "No motion needed for cap throws/captures/rolling - each role (up-throw, down-throw/jump, spin/shake) uses a button instead. Remap which button from Pause -> Options -> Controls, in-game.";
    inline constexpr const char16_t* OPT_EASY_MINIGAMES_LABEL = u"Easy Minigames";
    inline constexpr const char* DESC_EASY_MINIGAMES = "Jump-rope and volleyball award their two moons after 10 and 25 successful hits.";
    inline constexpr const char16_t* OPT_ONE_SHEEP_HERDING_LABEL = u"Sheep Herding:";
    inline constexpr const char* DESC_ONE_SHEEP_HERDING = "Sheep-herding moons spawn after the selected number of sheep reaches the pen.";
    inline constexpr const char16_t* ROLL_SHEEP_HERDING[] = {
        u"Off", u"1 Sheep", u"2 Sheep", u"3 Sheep", u"4 Sheep"
    };
    inline constexpr s32 ROLL_SHEEP_HERDING_COUNT = 5;
    inline constexpr const char16_t* ROLL_SHEEP_COUNT[] = {
        u"1 Sheep", u"2 Sheep", u"3 Sheep", u"4 Sheep"
    };
    inline constexpr s32 ROLL_SHEEP_COUNT_COUNT = 4;
    inline constexpr const char16_t* OPT_FREE_COSTUME_MOON_LABEL = u"Free Costume Moon";
    inline constexpr const char* DESC_FREE_COSTUME_MOON = "Costume-check NPCs give their moon without requiring the matching cap and outfit.";
    inline constexpr const char16_t* OPT_UNLOCK_HINT_ART_LABEL = u"Unlocked Hint Art";
    inline constexpr const char* DESC_UNLOCK_HINT_ART = "Treat every Hint Art photo as viewed so dig-point moons can be collected immediately.";

    // ==========================================================================
    // Quality of Life II
    // ==========================================================================

    inline constexpr const char16_t* OPT_COMPLETED_KINGDOM_MOON_COLLECTED_LABEL = u"Treat completed kingdom moons as collected";
    inline constexpr const char* DESC_COMPLETED_KINGDOM_MOON_COLLECTED = "When a visited kingdom reaches its known moon requirement by collecting or depositing moons, treat its remaining ordinary physical pickups as collected for world gameplay without marking them collected in the moon list. Physical NPC/event-awarded locations, story/multi locations, triple rewards, Toadette Medals, captures, abilities, Moon Rock Keys, paintings and coin rewards remain normal.";
    inline constexpr const char16_t* OPT_AREA_NAME_OVERLAY_LABEL = u"Area Name Overlay";
    inline constexpr const char* DESC_AREA_NAME_OVERLAY = "Show a brief banner with the current area's name when entering an overworld kingdom or a subarea.";
    inline constexpr const char16_t* OPT_RANDOMIZE_OUTFITS_LABEL = u"Randomize Outfits";
    inline constexpr const char* DESC_RANDOMIZE_OUTFITS = "Change to a random outfit every time you die.";
    inline constexpr const char16_t* OPT_ALWAYS_NEUTRAL_CAP_THROW_LABEL = u"Always start with Neutral Cap Throw";
    inline constexpr const char* DESC_ALWAYS_NEUTRAL_CAP_THROW = "Always hand out Neutral Throw as the free starting cap throw instead of rolling one of the four. Logic solves the seed around it.";
    inline constexpr const char16_t* OPT_NO_CLIP_LABEL = u"Enable No Clip";
    inline constexpr const char* DESC_NO_CLIP = "Allow ZL + ZR + D-Pad Up to toggle No Clip. Disabled by default.";

    // ==========================================================================
    // Tracker
    // ==========================================================================

    inline constexpr const char16_t* PAGE_KINGDOM_MOONS = u"Kingdom Moon Rewards";
    // "<Kingdom> Moon Rewards" - the sub-page edits one kingdom, so its title has
    // to say which.
    inline constexpr const char16_t* PAGE_KINGDOM_MOON_REWARDS_FORMAT = u"%s Moon Rewards";
    inline constexpr const char16_t* PAGE_ADVANCED_KINGDOM_MOON_USES = u"Kingdom Moon Rewards";

    // One display order, and every one of the seventeen kingdoms offers all of it.
    // Custom is the derived value that shows while a kingdom has more than one
    // reward selected.
    // What a kingdom's moons hand out. Whether it also gates progress is the
    // Kingdoms with Requirements setting's question, not this one, so Progress is
    // not among the values and most kingdoms read None.
    //
    // Two derived values sit past the named ones. They are mutually exclusive
    // states of the reward mask - empty, or more than one bit - so a row shows at
    // most one of them, and neither can be rolled onto: both are set on the
    // advanced page.
    inline constexpr const char16_t* ROLL_KINGDOM_MOON_USE[] = {
        u"Coins", u"Captures", u"Abilities",
        u"Captures or Abilities", u"Hint Tokens", u"Painting Unlock",
        u"None", u"Custom"
    };
    inline constexpr s32 ROLL_KINGDOM_MOON_USE_COUNT = 6;

    // One question with five answers, not a set of independent rewards. The two
    // that name both differ in what a single moon carries: "or" is one bit meaning
    // each moon grants one or the other, "and" is the capture and ability bits held
    // together, so the kingdom hosts both pools and a moon can carry one of each.
    inline constexpr const char16_t* ROLL_KINGDOM_UNLOCK[] = {
        u"None", u"Captures", u"Abilities",
        u"Captures or Abilities", u"Captures and Abilities"
    };
    inline constexpr s32 ROLL_KINGDOM_UNLOCK_COUNT = 5;
    inline constexpr const char16_t* ADVANCED_KINGDOM_UNLOCK_LABEL = u"Unlocks:";
    inline constexpr const char* DESC_ADVANCED_KINGDOM_UNLOCK = "Which unlock this kingdom's moons carry. Or: each moon grants one of the two. And: the kingdom hosts both, so a moon can carry one of each.";

    // Dark and Darker Side share one row: they are the same kind of end-game
    // kingdom and the generator treats a split between them as a trap.
    inline constexpr const char16_t* KINGDOM_MOONS_DARK_SIDES_LABEL = u"Dark / Darker Side";
    inline constexpr const char* DESC_KINGDOM_MOON_USE = "What this kingdom's moons hand out on top of counting toward its requirement. Press A for more than one.";
    inline constexpr const char* DESC_KINGDOM_MOON_USE_CUSTOM = "This kingdom hands out several things at once. Press A to change which.";
    inline constexpr const char* DESC_ADVANCED_KINGDOM_MOON_USE = "Pick every reward this kingdom's moons can hand out. With none of them on, its moons are plain moons - which is the usual answer.";

    inline constexpr const char16_t* PAGE_TRACKER = u"Tracker";

    inline constexpr const char16_t* OPT_TRACKER_OVERLAY_LABEL = u"Tracker Overlay:";
    inline constexpr const char* DESC_TRACKER_OVERLAY_BASIC = "Basic: Only kingdoms with requirements are displayed on screen";
    inline constexpr const char* DESC_TRACKER_OVERLAY_EXTENDED = "Extended: All kingdoms are displayed on screen";
    inline constexpr const char* DESC_TRACKER_OVERLAY_HINTS = "Hints: Show available checks from that kingdom next to each kingdom";
    inline constexpr const char* DESC_TRACKER_OVERLAY_GUIDED = "Guided: Show progress related checks from that kingdom next to each kingdom";
    inline constexpr const char* DESC_TRACKER_OVERLAY_NONE = "None: No tracker is displayed on screen";
    inline constexpr const char16_t* OPT_TRACKER_RIGHT_ALIGN_LABEL = u"Tracker on Right";
    inline constexpr const char* DESC_TRACKER_RIGHT_ALIGN = "Anchor the progress tracker to the right side of the screen instead of the left.";
    inline constexpr const char16_t* OPT_TRACKER_CAVE_SKIP_LABEL = u"Cave Skip:";
    inline constexpr const char* DESC_TRACKER_CAVE_SKIP_EASY = "Easy: show Ground Pound Jump-or-Backflip, Wall Jump, and Dive beside Cave Requirements.";
    inline constexpr const char* DESC_TRACKER_CAVE_SKIP_HARD = "Hard: show only Wall Jump beside Cave Requirements.";
    inline constexpr const char* DESC_TRACKER_CAVE_SKIP_NONE = "None: do not show the Cave Skip checklist on the tracker.";
    inline constexpr const char16_t* OPT_TRACKER_MOON_LIMITS_LABEL = u"Moon Limits:";
    inline constexpr const char* DESC_TRACKER_MOON_LIMITS_UPPER = "Upper: a kingdom's moon count turns green once it reaches the highest requirement that kingdom could have been given.";
    inline constexpr const char* DESC_TRACKER_MOON_LIMITS_FULL = "Full: red below the lowest requirement that kingdom could have been given, yellow inside the range, green at or past the highest.";
    inline constexpr const char* DESC_TRACKER_MOON_LIMITS_NONE = "None: kingdom moon counts are never color coded on the tracker.";
    inline constexpr const char16_t* OPT_TRACKER_TIMER_LABEL = u"Tracker Timer";
    inline constexpr const char* DESC_TRACKER_TIMER = "Show the elapsed playtime clock at the top of the progress tracker.";
    inline constexpr const char16_t* OPT_TRACKER_WEB_HEADER_LABEL = u"Web Tracker";
    inline constexpr const char* DESC_TRACKER_WEB_HEADER = "Live auto-tracking for the companion web tracker (moon counts, captures, abilities, kingdom status).";
    inline constexpr const char16_t* OPT_TRACKER_ROOM_CODE_LABEL = u"Room Code: (not set)";
    inline constexpr const char* DESC_TRACKER_ROOM_CODE = "Set the same room code you'll enter on the web tracker page. Sticks with this save file - check the Randomizer Overview page in the pause menu if you forget it.";

    inline constexpr const char16_t* ROLL_TRACKER_MODE[] = {
        u"None", u"Basic", u"Extended", u"Hints", u"Guided"
    };
    inline constexpr s32 ROLL_TRACKER_MODE_COUNT = 5;
    inline constexpr const char16_t* ROLL_TRACKER_CAVE_SKIP[] = {
        u"None", u"Easy", u"Hard"
    };
    inline constexpr s32 ROLL_TRACKER_CAVE_SKIP_COUNT = 3;
    inline constexpr const char16_t* ROLL_TRACKER_MOON_LIMITS[] = {
        u"None", u"Upper", u"Full"
    };
    inline constexpr s32 ROLL_TRACKER_MOON_LIMITS_COUNT = 3;

    // ==========================================================================
    // Multiplayer
    // ==========================================================================

    inline constexpr const char16_t* PAGE_MULTIPLAYER = u"Multiplayer";

    inline constexpr const char16_t* OPT_TEAM_LABEL = u"Your Team:";
    inline constexpr const char* DESC_TEAM = "Which team you join with. Moons, coins, checkpoints and kingdom progress are shared within a team only. To change teams later, return to the title and join again - you'll get a fresh save built from that team's progress.";
    inline constexpr const char16_t* OPT_DEATH_LINK_LABEL = u"Death Link";
    inline constexpr const char* DESC_DEATH_LINK = "When any player on your team dies, the whole team dies. Set by the room's host - a guest's own setting is ignored.";
    inline constexpr const char16_t* OPT_SYNC_OTHER_TEAMS_LABEL = u"Sync Other Teams";
    inline constexpr const char* DESC_SYNC_OTHER_TEAMS = "Let opposing teams see each other. Off means rival players are invisible - the server never sends you their position. Set by the room's host.";
    inline constexpr const char16_t* ROLL_TEAM[] = {
        u"Team 1", u"Team 2", u"Team 3", u"Team 4"
    };
    inline constexpr s32 ROLL_TEAM_COUNT = 4;

    // ==========================================================================
    // Summary
    // ==========================================================================

    inline constexpr const char16_t* PAGE_SUMMARY = u"Summary";
    inline constexpr const char16_t* PAGE_JOIN_ROOM = u"Join Room";

    inline constexpr const char16_t* OPT_SAVE_AS_PRESET_LABEL = u"Save as Preset";
    inline constexpr const char* DESC_SAVE_AS_PRESET = "Save your current settings as a named preset you can apply again later.";
    inline constexpr const char16_t* OPT_GENERATE_SEED_LABEL = u"Generate Seed";
    inline constexpr const char16_t* OPT_JOIN_GAME_LABEL = u"Join Game";

    // ==========================================================================
    // Customize Tabs
    // ==========================================================================

    inline constexpr const char16_t* OPT_CUSTOM_TAB_PAGE_MAIN_LABEL = u"Main Settings:";
    inline constexpr const char16_t* OPT_CUSTOM_TAB_PAGE_GAME_MODE_LABEL = u"Game Mode Settings:";
    inline constexpr const char16_t* OPT_CUSTOM_TAB_PAGE_RANDOMIZATION_LABEL = u"Randomization:";
    inline constexpr const char16_t* OPT_CUSTOM_TAB_PAGE_RANDOMIZATION_II_LABEL = u"Randomization II:";
    inline constexpr const char16_t* OPT_CUSTOM_TAB_PAGE_MOON_ALLOCATION_LABEL = u"Moon Allocation:";
    inline constexpr const char16_t* OPT_CUSTOM_TAB_PAGE_LOGIC_LABEL = u"Logic Settings:";
    inline constexpr const char16_t* OPT_CUSTOM_TAB_PAGE_CHAOS_LABEL = u"Chaotic Randomization:";
    inline constexpr const char16_t* OPT_CUSTOM_TAB_PAGE_LOCK_LABEL = u"Ability & Capture Lock:";
    inline constexpr const char16_t* OPT_CUSTOM_TAB_PAGE_MOVEMENT_LABEL = u"Movement Abilities:";
    inline constexpr const char16_t* OPT_CUSTOM_TAB_PAGE_PURPLE_SHOP_LABEL = u"Purple Coin Shop:";
    inline constexpr const char16_t* OPT_CUSTOM_TAB_PAGE_ADDITIONAL_LABEL = u"Additional Options:";
    inline constexpr const char16_t* OPT_CUSTOM_TAB_PAGE_ADDITIONAL_II_LABEL = u"Additional Options II:";
    inline constexpr const char16_t* OPT_CUSTOM_TAB_PAGE_ADDITIONAL_III_LABEL = u"Additional Options III:";
    inline constexpr const char16_t* OPT_CUSTOM_TAB_PAGE_HINT_LABEL = u"Hint Configuration:";
    inline constexpr const char16_t* OPT_CUSTOM_TAB_PAGE_MISC_LABEL = u"Misc Randomization:";
    inline constexpr const char16_t* OPT_CUSTOM_TAB_PAGE_QOL_LABEL = u"Quality of Life:";
    inline constexpr const char16_t* OPT_CUSTOM_TAB_PAGE_QOL_II_LABEL = u"Quality of Life II:";
    inline constexpr const char16_t* OPT_CUSTOM_TAB_PAGE_TRACKER_LABEL = u"Tracker:";
    inline constexpr const char16_t* OPT_CUSTOM_TAB_PAGE_MULTIPLAYER_LABEL = u"Multiplayer:";
    inline constexpr const char16_t* OPT_CUSTOM_TAB_PAGE_SUMMARY_LABEL = u"Summary:";

    // ========================================================================
    // Advanced Settings
    // ========================================================================

    // These pages are built on demand by the setup menu. Keeping their titles and
    // the shared reset/help text here preserves the same player-facing string
    // ownership as the ordinary settings pages.
    inline constexpr const char16_t* PAGE_ADVANCED_MOON_ALLOCATION_KINGDOMS = u"Moon Allocation";
    inline constexpr const char16_t* PAGE_ADVANCED_TRIPLE_MOON_KINGDOMS = u"Triple Moons";
    inline constexpr const char16_t* PAGE_ADVANCED_LOCKED_CAPTURE_POOL = u"Locked Captures";
    inline constexpr const char16_t* PAGE_ADVANCED_LOCKED_ABILITY_POOL = u"Locked Abilities";
    inline constexpr const char16_t* PAGE_ADVANCED_STARTING_EQUIPMENT = u"Starting Equipment";
    inline constexpr const char16_t* PAGE_ADVANCED_PURPLE_SHOP_PRICE = u"Purple Shop Price";
    inline constexpr const char16_t* PAGE_ADVANCED_REQ_DEVIATION = u"Moon Requirement";
    inline constexpr const char16_t* PAGE_ADVANCED_MOON_ROCK_KEY_SCOPE = u"Moon Rock Key Scope";
    inline constexpr const char16_t* PAGE_ADVANCED_SUBAREA_SCOPE = u"Subarea Scope";
    inline constexpr const char16_t* PAGE_ADVANCED_START_KINGDOMS = u"Starting Kingdoms";
    inline constexpr const char16_t* PAGE_ADVANCED_HINT_ENTRY = u"Hint Entry";
    inline constexpr const char16_t* PAGE_ADVANCED_TIMER_VALUES = u"Timer Values";
    inline constexpr const char16_t* PAGE_ADVANCED_NUZLOCKE_VALUES = u"Nuzlocke Values";
    inline constexpr const char16_t* PAGE_ADVANCED_MINIGAME_RANGE = u"Minigame Range";
    inline constexpr const char16_t* PAGE_ADVANCED_SHEEP_COUNT = u"Sheep Count";

    inline constexpr const char16_t* OPT_ADVANCED_RESET_LABEL = u"Reset Defaults";
    inline constexpr const char* DESC_ADVANCED_RESET = "Restore this advanced settings page to its defaults.";
    inline constexpr const char* DESC_ADVANCED_RESET_NOTICE = "Advanced settings reset to their defaults.";
    inline constexpr const char* DESC_SUFFIX_ADVANCED = "  Press Y for advanced settings.";
    inline constexpr const char16_t* ADVANCED_TIMER_START_FORMAT = u"Starting Time: %d seconds";
    inline constexpr const char16_t* ADVANCED_TIMER_MOON_BONUS_FORMAT = u"Time per Moon: %d seconds";
    inline constexpr const char16_t* ADVANCED_TIMER_TRIPLE_BONUS_FORMAT = u"Time per Triple Moon: %d seconds";
    inline constexpr const char16_t* ADVANCED_TIMER_DAMAGE_PENALTY_FORMAT = u"Damage Penalty: %d seconds";
    inline constexpr const char16_t* ADVANCED_TIMER_DEATH_PENALTY_FORMAT = u"Death Penalty: %d seconds";
    inline constexpr const char16_t* ADVANCED_TIMER_HALF_LIFE_FORMAT = u"Half Life Point: %d seconds";
    inline constexpr const char16_t* ADVANCED_NUZLOCKE_MOONS_FORMAT = u"Moons Lost: %d%%";
    inline constexpr const char16_t* ADVANCED_NUZLOCKE_ABILITY_FORMAT = u"Abilities Lost: %d%%";
    inline constexpr const char16_t* ADVANCED_NUZLOCKE_CAPTURE_FORMAT = u"Captures Lost: %d%%";
    inline constexpr const char* DESC_ADVANCED_HINT_COIN_OVERRIDE = "Use regional coins for this hint entry instead of Hint Tokens.";
    inline constexpr const char16_t* ADVANCED_HINT_PRICE_FORMAT = u"Price: %d";
    inline constexpr const char16_t* ADVANCED_MINIGAME_LOW_FORMAT = u"Low Threshold: %d hits";
    inline constexpr const char16_t* ADVANCED_MINIGAME_HIGH_FORMAT = u"High Threshold: %d hits";
    inline constexpr const char16_t* ADVANCED_SHEEP_COUNT_LABEL = u"Sheep Count:";
    inline constexpr const char* DESC_ADVANCED_MOON_ALLOCATION_KINGDOM = "Set this kingdom's moon target. Auto uses the generated distribution.";
    inline constexpr const char* DESC_ADVANCED_TRIPLE_MOON_KINGDOM = "Set this kingdom's triple-moon target. Auto uses the vanilla distribution.";

    // ==========================================================================
    // Shared values and dynamic menu text
    // ==========================================================================

    inline constexpr const char16_t* EMPTY = u"";
    inline constexpr const char* EMPTY_ASCII = "";
    inline constexpr const char* VALUE_OFF = "Off";
    inline constexpr const char* VALUE_ON = "On";
    inline constexpr const char* VALUE_NONE = "None";
    inline constexpr const char16_t* VALUE_AUTO = u"Auto";
    inline constexpr const char16_t* ROLL_EMPTY[] = { EMPTY };
    inline constexpr s32 ROLL_EMPTY_COUNT = 1;

    inline constexpr const char16_t* INTEGER_FORMAT = u"%d";
    inline constexpr const char16_t* PAGE_COUNTER_FORMAT = u" (%d/%d)";
    inline constexpr const char16_t* ADVANCED_BALANCE_FORMAT = u"  %d / %d (%d)";

    inline constexpr const char16_t* SLOT_LIST_TITLE = u"Select File";
    inline constexpr const char16_t* SLOT_LIST_TITLE_MULTIPLAYER = u"Select File: Multiplayer";
    inline constexpr const char16_t* SLOT_LIST_TITLE_SINGLEPLAYER = u"Select File: Singleplayer";
    inline constexpr const char16_t* SLOT_ACTION_TITLE = u"File Options";
    inline constexpr const char16_t* SLOT_DELETE_TITLE = u"Delete File";
    inline constexpr const char16_t* SLOT_NEW_FILE_TITLE = u"New File";
    inline constexpr const char16_t* SLOT_NUMBER_FORMAT = u"File %d";
    inline constexpr const char16_t* SLOT_DATE_FORMAT = u"%02d/%02d/%04d %02d:%02d";
    inline constexpr const char* SLOT_MODE_FORMAT = "File %d - %s";
    inline constexpr const char16_t* SLOT_MODE_FORMAT_WIDE = u"File %d - %s";
    inline constexpr const char* SLOT_TOTAL_MOONS_FORMAT = "Total Power Moons: %d";
    inline constexpr const char* SLOT_SEED_FORMAT = "Seed %u";
    inline constexpr const char* ROW_CURRENT_SEED_FORMAT = "Current Seed: %u";
    inline constexpr const char* ROW_MOON_REQUIREMENT_FORMAT = "Moon Requirement: %u";
    inline constexpr const char* ROW_MEDALS_AVAILABLE_FORMAT = "Medals Available: %d";
    inline constexpr const char* ROW_MEDALS_REQUIRED_FORMAT = "Medals Required: %d";
    inline constexpr const char* ROW_ROOM_CODE_FORMAT = "Room Code: %s";
    inline constexpr const char16_t* ROW_STARTING_CAPTURES_FORMAT = u"Starting Captures: %d";
    inline constexpr const char16_t* ROW_STARTING_THROWS_FORMAT = u"Starting Throws: %d";
    inline constexpr const char16_t* ROW_STARTING_ABILITIES_FORMAT = u"Starting Abilities: %d";
    inline constexpr const char16_t* SLOT_INCOMPATIBLE = u"Incompatible";
    inline constexpr const char16_t* SLOT_NEW_GAME = u"New Game";
    inline constexpr const char16_t* SLOT_LOAD = u"Load";
    inline constexpr const char16_t* SLOT_DELETE = u"Delete";
    inline constexpr const char16_t* SLOT_BACK = u"Back";
    inline constexpr const char16_t* SLOT_CANCEL = u"Cancel";
    inline constexpr const char16_t* ROLL_NO_USER_PRESET[] = { u"None" };
    inline constexpr s32 ROLL_NO_USER_PRESET_COUNT = 1;

    inline constexpr const char16_t* DLG_DELETE_SLOT_FORMAT = u"Delete Slot %d";
    inline constexpr const char16_t* DLG_APPLY_PRESET_TITLE = u"Apply Preset";
    inline constexpr const char16_t* DLG_APPLY_PRESET_FORMAT = u"Apply \"%s\"";
    inline constexpr const char16_t* DLG_DELETE_PRESET_TITLE = u"Delete Preset";
    inline constexpr const char16_t* DLG_DELETE_PRESET_FORMAT = u"Delete \"%s\"";
    inline constexpr const char16_t* DLG_OVERWRITE_PRESET_TITLE = u"Overwrite Preset";
    inline constexpr const char16_t* DLG_OVERWRITE_PRESET_FORMAT = u"Overwrite \"%s\"";
    inline constexpr const char16_t* DLG_REDISTRIBUTE_TITLE = u"Over the Limit";
    inline constexpr const char16_t* DLG_REDISTRIBUTE_MOONS_FORMAT = u"You have assigned %d of %d moons. %d will be redistributed automatically.";
    inline constexpr const char16_t* DLG_REDISTRIBUTE_TRIPLE_MOONS_FORMAT = u"You have assigned %d of %d triple moons. %d will be redistributed automatically.";

    inline constexpr const char16_t* GENERATE_TITLE = u"Generating";
    inline constexpr const char16_t* GENERATE_FAILED_TITLE = u"Generation Failed";
    inline constexpr const char16_t* GENERATE_PROGRESS_FORMAT = u"Generating... %d%%";
    inline constexpr const char16_t* GENERATE_PROGRESS_ZERO = u"Generating... 0%";
    inline constexpr const char16_t* GENERATE_PROGRESS_FOUR = u"Generating... 4%";
    inline constexpr const char16_t* GENERATE_PROGRESS_FIVE = u"Generating... 5%";
    inline constexpr const char16_t* GENERATE_PROGRESS_COMPLETE = u"Generating... 100%";
    inline constexpr const char16_t* GENERATE_PREPARING = u"Preparing generator";
    inline constexpr const char16_t* GENERATE_READY = u"Generator ready";
    inline constexpr const char16_t* GENERATE_LAUNCHING = u"Launching generator";
    inline constexpr const char16_t* GENERATE_FAILED = u"Generation Failed";
    inline constexpr const char16_t* GENERATE_START_FAILED = u"Could not start generator";

    inline constexpr const char16_t* JOIN_TITLE = u"Joining Room";
    inline constexpr const char16_t* JOIN_FAILED_TITLE = u"Could Not Join";
    inline constexpr const char16_t* JOIN_UPLOAD_FAILED_TITLE = u"Could Not Start Room";
    inline constexpr const char16_t* JOIN_CONTACTING_SERVER = u"Contacting the server...";
    inline constexpr const char16_t* JOIN_CANCEL_HINT = u"B: Cancel";
    inline constexpr const char16_t* JOIN_FAILED_UPLOAD = u"The seed was not saved on the server.";
    inline constexpr const char16_t* JOIN_FAILED = u"Could not join the room.";
    inline constexpr const char16_t* JOIN_WAITING = u"Waiting for the host...";
    inline constexpr const char16_t* JOIN_DOWNLOADING_FORMAT = u"Downloading the seed... %d%%";
    inline constexpr const char16_t* JOIN_RECEIVED = u"Seed received.";
    inline constexpr const char16_t* JOIN_HOST_SEED_READ_FAILED = u"The host's seed could not be read.";
    inline constexpr const char16_t* JOIN_ROOM_PROGRESS_MISSING = u"The room save has no progression record.";
    inline constexpr const char16_t* JOIN_REPLAY_REQUEST_FAILED = u"Could not request the room's saved progress.";
    inline constexpr const char16_t* JOIN_REPLAY_FAILED = u"The server could not apply the room's saved progress.";
    inline constexpr const char16_t* JOIN_UPLOAD_RETRY = u"Reconnect or fix the server's room storage, then generate again.";

    inline constexpr const char* KEYBOARD_SEED_HEADER = "Randomizer Seed";
    inline constexpr const char* KEYBOARD_SEED_SUBTEXT = "Enter a seed number (up to 8 digits)";
    inline constexpr const char* KEYBOARD_MOON_REQUIREMENT_HEADER = "Total Moon Requirement";
    inline constexpr const char* KEYBOARD_MOON_REQUIREMENT_SUBTEXT = "Enter a total from 0 to 657";
    inline constexpr const char* KEYBOARD_MOON_ALLOCATION_HEADER = "Kingdom Moon Target";
    inline constexpr const char* KEYBOARD_MOON_ALLOCATION_SUBTEXT = "Enter a target from 0 to 657";
    inline constexpr const char* KEYBOARD_TRIPLE_MOON_HEADER = "Kingdom Triple-Moon Target";
    inline constexpr const char* KEYBOARD_TRIPLE_MOON_SUBTEXT = "Enter a target from 0 to 8";
    inline constexpr const char* KEYBOARD_MEDALS_AVAILABLE_HEADER = "Toadette Medals Available";
    inline constexpr const char* KEYBOARD_MEDALS_AVAILABLE_SUBTEXT = "How many medals exist in the pool";
    inline constexpr const char* KEYBOARD_MEDALS_REQUIRED_HEADER = "Toadette Medals Required";
    inline constexpr const char* KEYBOARD_MEDALS_REQUIRED_SUBTEXT = "How many medals the run needs to win";
    inline constexpr const char* KEYBOARD_PRESET_NAME_HEADER = "Preset Name";
    inline constexpr const char* KEYBOARD_PRESET_NAME_SUBTEXT = "Enter a name for this preset";
    inline constexpr const char* KEYBOARD_TRACKER_ROOM_CODE_HEADER = "Tracker Room Code";
    inline constexpr const char* KEYBOARD_TRACKER_ROOM_CODE_SUBTEXT = "Enter the same room code you'll type into the tracker page";
    inline constexpr const char* KEYBOARD_TRACKER_ROOM_CODE_STREAMER_SUBTEXT = "Enter the same room code as the tracker page. Streamer Mode: "
                                                                               "keys stay visible on-screen.";
    inline constexpr const char* KEYBOARD_PURPLE_SHOP_PRICE_HEADER = "Purple Shop Price";
    inline constexpr const char* KEYBOARD_PURPLE_SHOP_PRICE_SUBTEXT = "Enter a price from 1 to 999 coins";
    inline constexpr const char* KEYBOARD_STARTING_CAPTURES_HEADER = "Starting Captures";
    inline constexpr const char* KEYBOARD_STARTING_CAPTURES_SUBTEXT = "Enter a number from 0 to 4";
    inline constexpr const char* KEYBOARD_STARTING_THROWS_HEADER = "Starting Cap Throws";
    inline constexpr const char* KEYBOARD_STARTING_THROWS_SUBTEXT = "Enter a number from 1 to 4";
    inline constexpr const char* KEYBOARD_STARTING_ABILITIES_HEADER = "Starting Abilities";
    inline constexpr const char* KEYBOARD_STARTING_ABILITIES_SUBTEXT = "Enter a number from 0 to 2";
    inline constexpr const char* KEYBOARD_MOON_REQUIREMENT_DEVIATION_HEADER = "Kingdom Moon Requirement";
    inline constexpr const char* KEYBOARD_MOON_REQUIREMENT_DEVIATION_SUBTEXT = "Enter an offset from -20 to +20 moons";
    inline constexpr const char* KEYBOARD_TIMER_VALUE_HEADER = "Timer Value";
    inline constexpr const char* KEYBOARD_TIMER_VALUE_SUBTEXT = "Enter a value in seconds";
    inline constexpr const char* KEYBOARD_NUZLOCKE_VALUE_HEADER = "Nuzlocke Loss";
    inline constexpr const char* KEYBOARD_NUZLOCKE_VALUE_SUBTEXT = "Enter a percentage from 0 to 100";
    inline constexpr const char* KEYBOARD_HINT_PRICE_HEADER = "Hint Price";
    inline constexpr const char* KEYBOARD_HINT_PRICE_SUBTEXT = "Enter a price from 1 to 9999";
    inline constexpr const char* KEYBOARD_MINIGAME_VALUE_HEADER = "Minigame Threshold";
    inline constexpr const char* KEYBOARD_MINIGAME_VALUE_SUBTEXT = "Enter a number from 1 to 9999";
    inline constexpr const char* PRESET_LIMIT_REACHED = "Preset limit reached (20). Delete a preset first.";

    inline constexpr const char* SUMMARY_DESCRIPTION_FORMAT = "Mode: %s | Logic: %s | Pacing: %s | Openness: %s | Difficulty: %s | Moons: %s | Allocation: %s | Loading Zones: %s | Paintings: %s | Kingdom Order: %s | Start State: %s | Start Pos: %s | Moon Req: %s";
    inline constexpr const char* GENERATE_DESCRIPTION_FORMAT = "Seed %u | Mode: %s | Logic: %s | Pacing: %s | Openness: %s | Difficulty: %s | Moons: %s | Allocation: %s | Loading Zones: %s | Paintings: %s | Kingdom Order: %s | Start State: %s | Start Pos: %s | Moon Req: %s";
    inline constexpr const char* JOIN_DESCRIPTION_FORMAT = "Download this room's seed from the server, with everything your team has already done on it. %s%s";
    inline constexpr const char* JOIN_DESCRIPTION_PENDING_SUFFIX = " Its seed is still being set up.";

} // namespace MenuText
