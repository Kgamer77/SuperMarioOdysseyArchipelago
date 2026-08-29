from BaseClasses import Location
from .Data.LocationData import SMOLocationData
from .Data.RuleData import SMOKingdoms
from .Data.RegionData import SMORegion
from .Options import Goal
from enum import StrEnum


class SMOLocation(Location):
    game: str = "Super Mario Odyssey"

# Cap
loc_Cap = {
        SMOLocationData.frog_jumping_above_the_fog: 1019,
        SMOLocationData.frog_jumping_from_the_top_deck: 815,
        SMOLocationData.cap_kingdom_timer_challenge_1: 861,
        SMOLocationData.good_evening_captain_toad: 227,
        SMOLocationData.shopping_in_bonneton: 230,
}


loc_Cascade = {
        SMOLocationData.our_first_power_moon: 205,
        SMOLocationData.multi_moon_atop_the_falls: 218,
        SMOLocationData.chomp_through_the_rocks: 206,
        SMOLocationData.behind_the_waterfall: 212,
}


loc_Cascade_Peace = {
        SMOLocationData.on_top_of_the_rubble: 1145,
        SMOLocationData.treasure_of_the_waterfall_basin: 216,
        SMOLocationData.above_a_high_cliff: 210,
        SMOLocationData.across_the_floating_isles: 208,
        SMOLocationData.cascade_kingdom_timer_challenge_1: 669,
        SMOLocationData.cascade_kingdom_timer_challenge_2: 670,
        SMOLocationData.good_morning_captain_toad: 204,
}


loc_Cascade_Revisit = {
        SMOLocationData.rolling_rock_by_the_falls: 1004,
        SMOLocationData.shopping_in_fossil_falls: 211,
}


loc_Cascade_Post_Metro = {
        SMOLocationData.a_tourist_in_the_cascade_kingdom: 906,
}


loc_Cascade_Post_Snow = {
        SMOLocationData.secret_path_to_fossil_falls: 207,
}

loc_Sand = {
        SMOLocationData.atop_the_highest_tower: 497,
        SMOLocationData.moon_shards_in_the_sand: 496,
        SMOLocationData.overlooking_the_desert_town: 517,
        SMOLocationData.alcove_in_the_ruins: 518,
        SMOLocationData.on_the_leaning_pillar: 516,
        SMOLocationData.hidden_room_in_the_flowing_sands: 523,
        SMOLocationData.secret_of_the_mural: 505,
        SMOLocationData.on_top_of_stone_archway: 520,
        SMOLocationData.from_a_crate_in_the_ruins: 507,
        SMOLocationData.on_the_lone_pillar: 522,
        SMOLocationData.where_the_birds_gather: 511,
        SMOLocationData.top_of_a_dune: 506,
        SMOLocationData.lost_in_the_luggage: 510,
        SMOLocationData.inside_a_block_is_a_hard_place: 508,
        SMOLocationData.bird_traveling_the_desert: 526,
        SMOLocationData.the_treasure_of_jaxi_ruins: 530,
        SMOLocationData.desert_gardening_plaza_seed: 1112,
        SMOLocationData.desert_gardening_ruins_seed: 1113,
        SMOLocationData.desert_gardening_seed_on_the_cliff: 1114,
        SMOLocationData.taking_notes_jump_on_the_palm: 525,
        SMOLocationData.among_the_five_cactuses: 893,
        SMOLocationData.wandering_cactus: 509,
        SMOLocationData.sand_quiz_wonderful: 498,
        SMOLocationData.shopping_in_tostarena: 565,
        SMOLocationData.secret_of_the_inverted_mural: 504,
}

loc_Sand_Pyramid = {
        SMOLocationData.showdown_on_the_inverted_pyramid: 495,
        SMOLocationData.on_the_statues_tail: 513,
}


loc_Night_Sand = {
        SMOLocationData.bullet_bill_breakthrough: 519,
}


loc_Sand_Underground = {
        SMOLocationData.the_hole_in_the_desert: 560,
        SMOLocationData.underground_treasure_chest: 953,
        SMOLocationData.goomba_tower_assembly: 558,
}


loc_Sand_Peace = {
        SMOLocationData.herding_sheep_in_the_dunes: 532,
        SMOLocationData.walking_the_desert: 533,
        SMOLocationData.hang_your_hat_on_the_fountain: 534,
        SMOLocationData.found_in_the_sand_good_dog: 512,
        SMOLocationData.sand_kingdom_timer_challenge_1: 531,
        SMOLocationData.sand_kingdom_timer_challenge_2: 536,
        SMOLocationData.sand_kingdom_timer_challenge_3: 539,
        SMOLocationData.fishing_in_the_oasis: 502,
        SMOLocationData.love_in_the_heart_of_the_desert: 493,
        SMOLocationData.youre_quite_a_catch_captain_toad: 692,
        SMOLocationData.jaxi_reunion: 1059,
        SMOLocationData.bird_traveling_wastes: 552,
}


loc_Sand_Pyramid_Peace = {
        SMOLocationData.welcome_back_jaxi: 501,
        SMOLocationData.the_lurker_under_the_stone: 538,
}


loc_Sand_Revisit = {
        SMOLocationData.secret_path_to_tostarena: 524,
}


loc_Lake = {
        SMOLocationData.broodals_over_the_lake: 424,
        SMOLocationData.dorrie_back_rider: 411,
        SMOLocationData.cheep_cheep_crossing: 412,
        SMOLocationData.end_of_the_hidden_passage: 410,
        SMOLocationData.whats_in_the_box: 404,
        SMOLocationData.on_the_lakeshore: 401,
        SMOLocationData.from_the_broken_pillar: 405,
        SMOLocationData.treasure_in_the_spiky_waterway: 415,
        SMOLocationData.lake_gardening_spiky_passage_seed: 1166,
        SMOLocationData.lake_kingdom_timer_challenge_1: 715,
        SMOLocationData.lake_kingdom_timer_challenge_2: 420,
        SMOLocationData.moon_shards_in_the_lake: 413,
        SMOLocationData.taking_notes_dive_and_swim: 402,
        SMOLocationData.taking_notes_in_the_cliffside: 407,
        SMOLocationData.lake_fishing: 416,
        SMOLocationData.i_met_a_lake_cheep_cheep: 409,
        SMOLocationData.our_secret_little_room: 414,
        SMOLocationData.lets_go_swimming_captain_toad: 403,
        SMOLocationData.shopping_in_lake_lamode: 430,
        SMOLocationData.i_feel_underdressed: 406,
        SMOLocationData.found_with_lake_kingdom_art: 1094,
}


loc_Lake_Post_Seaside = {
        SMOLocationData.secret_path_to_lake_lamode: 417,
}


loc_Wooded = {
        SMOLocationData.road_to_sky_garden: 129,
        SMOLocationData.rolling_rock_in_the_woods: 149,
        SMOLocationData.caught_hopping_in_the_forest: 148,
        SMOLocationData.atop_a_tall_tree: 139,
        SMOLocationData.tucked_away_inside_a_tunnel: 143,
        SMOLocationData.the_nut_round_the_corner: 145,
        SMOLocationData.climb_the_cliff_to_get_the_nut: 140,
        SMOLocationData.the_nut_in_the_red_maze: 141,
        SMOLocationData.the_nut_at_the_dead_end: 142,
        SMOLocationData.fire_in_the_cave: 136,
        SMOLocationData.shopping_in_steam_gardens: 138,
        SMOLocationData.nut_planted_in_the_tower: 180,
        SMOLocationData.stretching_your_legs: 179,
}


loc_Wooded_Post_Metro = {
        SMOLocationData.secret_path_to_the_steam_gardens: 137,
}


loc_Wooded_Post_Story1 = {
        SMOLocationData.flower_thieves_of_sky_garden: 130,
        SMOLocationData.path_to_the_secret_flower_field: 159,
        SMOLocationData.cracked_nut_on_a_crumbling_tower: 144,
        SMOLocationData.the_nut_that_grew_on_the_tall_fence: 147,
        SMOLocationData.love_in_the_forest_ruins: 131,
        SMOLocationData.thanks_for_the_charge: 135,
        SMOLocationData.over_the_cliffs_edge: 146,
        SMOLocationData.behind_the_rock_wall: 134,
        SMOLocationData.back_way_up_the_mountain: 150,
}


loc_Wooded_Peace = {
        SMOLocationData.inside_the_rock_in_the_forest: 1026,
        SMOLocationData.hey_out_there_captain_toad: 133,
        SMOLocationData.wooded_kingdom_timer_challenge_1: 157,
        SMOLocationData.wooded_kingdom_timer_challenge_2: 676,
        SMOLocationData.found_with_wooded_kingdom_art: 1089,
}


loc_Cloud = {
}


loc_Lost = {
        SMOLocationData.atop_a_propeller_pillar: 376,
        SMOLocationData.below_the_cliffs_edge: 380,
        SMOLocationData.inside_the_stone_cage: 374,
        SMOLocationData.on_a_tree_in_the_swamp: 377,
        SMOLocationData.over_the_fuzzies_above_the_swamp: 384,
        SMOLocationData.avoiding_fuzzies_inside_the_wall: 387,
        SMOLocationData.inside_the_rising_stone_pillar: 375,
        SMOLocationData.enjoying_the_view_of_forgotten_isle: 385,
        SMOLocationData.on_the_mountain_road: 379,
        SMOLocationData.a_propeller_pillars_secret: 373,
        SMOLocationData.wrecked_rock_block: 942,
        SMOLocationData.a_butterflys_treasure: 388,
        SMOLocationData.cave_gardening: 378,
        SMOLocationData.moon_shards_in_the_jungle: 386,
        SMOLocationData.peeking_out_from_under_the_bridge: 382,
        SMOLocationData.twist_n_turn_up_treasure: 1075,
        SMOLocationData.soaring_over_the_forgotten_isle: 381,
        SMOLocationData.the_caged_gold: 383,
        SMOLocationData.get_some_rest_captain_toad: 372,
        SMOLocationData.shopping_on_forgotten_isle: 398,
}


loc_Lost_Revisit = {
        SMOLocationData.caught_hopping_in_the_jungle: 390,
}


loc_Night_Metro = {
        SMOLocationData.new_donk_citys_pest_problem: 37,
        SMOLocationData.swaying_in_the_breeze: 39,
        SMOLocationData.girder_sandwich: 54,
}


loc_Metro = {
        SMOLocationData.drummer_on_board: 42,
        SMOLocationData.guitarist_on_board: 41,
        SMOLocationData.bassist_on_board: 44,
        SMOLocationData.trumpeter_on_board: 43,
        SMOLocationData.inside_an_iron_girder: 38,
        SMOLocationData.glittering_above_the_pool: 65,
        SMOLocationData.dizzying_heights: 61,
        SMOLocationData.secret_girder_tunnel: 59,
        SMOLocationData.who_piled_garbage_on_this: 49,
        SMOLocationData.hidden_in_the_scrap: 62,
        SMOLocationData.left_at_the_cafe: 77,
        SMOLocationData.how_do_they_take_out_the_trash: 68,
        SMOLocationData.city_gardening_building_planter: 70,
        SMOLocationData.city_gardening_plaza_planter: 71,
        SMOLocationData.city_gardening_rooftop_planter: 69,
        SMOLocationData.how_you_doin_captain_toad: 46,
        SMOLocationData.free_parking_rooftop_hop: 809,
        SMOLocationData.bench_friends: 50,
        SMOLocationData.shopping_in_new_donk_city: 101,
        SMOLocationData.jump_rope_hero: 60,
        SMOLocationData.jump_rope_genius: 51,
        SMOLocationData.remotely_captured_car: 53,
        SMOLocationData.found_with_metro_kingdom_art: 1088,
}


loc_Metro_Post_Sand = {
        SMOLocationData.secret_path_to_new_donk_city: 40,
}


loc_Metro_Sewer_Access = {
}


loc_Metro_Peace = {
        SMOLocationData.a_traditional_festival: 95,
        SMOLocationData.celebrating_in_the_streets: 875,
        SMOLocationData.caught_hopping_on_a_building: 99,
        SMOLocationData.metro_kingdom_timer_challenge_1: 1054,
        SMOLocationData.metro_kingdom_timer_challenge_2: 839,
        SMOLocationData.a_tourist_in_the_metro_kingdom: 881,
}


loc_Snow = {
        SMOLocationData.captain_toad_is_chilly: 694,
        SMOLocationData.shopping_in_shiveria: 868,
}


loc_Snow_Peace = {
        SMOLocationData.caught_hopping_in_the_snow: 1002,
        SMOLocationData.snow_kingdom_timer_challenge_1: 678,
        SMOLocationData.snow_kingdom_timer_challenge_2: 686,
        SMOLocationData.moon_shards_in_the_snow: 2,
        SMOLocationData.taking_notes_snow_path_dash: 3,
        SMOLocationData.fishing_in_the_glacier: 4,
}


loc_Seaside = {
        SMOLocationData.the_stone_pillar_seal: 438,
        SMOLocationData.the_lighthouse_seal: 441,
        SMOLocationData.the_hot_spring_seal: 440,
        SMOLocationData.the_seal_above_the_canyon: 439,
        SMOLocationData.on_the_cliff_overlooking_the_beach: 449,
        SMOLocationData.ride_the_jetstream: 455,
        SMOLocationData.ocean_bottom_maze_treasure: 466,
        SMOLocationData.ocean_bottom_maze_hidden_room: 467,
        SMOLocationData.underwater_highway_tunnel: 450,
        SMOLocationData.shh_its_a_shortcut: 451,
        SMOLocationData.gap_in_the_ocean_trench: 458,
        SMOLocationData.slip_through_the_nesting_spot: 459,
        SMOLocationData.merci_dorrie: 453,
        SMOLocationData.under_a_dangerous_ceiling: 445,
        SMOLocationData.what_the_waves_left_behind: 447,
        SMOLocationData.the_back_canyon_excavate: 444,
        SMOLocationData.bubblaine_northern_reaches: 1151,
        SMOLocationData.glass_palace_treasure_chest: 1057,
        SMOLocationData.treasure_trap_hidden_in_the_inlet: 448,
        SMOLocationData.sea_gardening_inlet_seed: 1103,
        SMOLocationData.sea_gardening_canyon_seed: 1104,
        SMOLocationData.sea_gardening_hot_spring_seed: 1106,
        SMOLocationData.sea_gardening_ocean_trench_seed: 1105,
        SMOLocationData.seaside_kingdom_timer_challenge_1: 468,
        SMOLocationData.moon_shards_in_the_sea: 454,
        SMOLocationData.taking_notes_ocean_surface_dash: 442,
        SMOLocationData.love_by_the_seaside: 446,
        SMOLocationData.good_job_captain_toad: 443,
        SMOLocationData.ocean_quiz_good: 457,
        SMOLocationData.shopping_in_bubblaine: 460,
        SMOLocationData.found_with_seaside_kingdom_art: 1095,
}


loc_Seaside_Peace = {
        SMOLocationData.the_glass_is_half_full: 437,
        SMOLocationData.bonjour_dorrie: 452,
        SMOLocationData.seaside_kingdom_timer_challenge_2: 863,
        SMOLocationData.found_on_the_beach_good_dog: 471,
        SMOLocationData.lighthouse_leaper: 474,
        SMOLocationData.beach_volleyball_champ: 472,
        SMOLocationData.beach_volleyball_hero_of_the_beach: 473,
}


loc_Luncheon = {
        SMOLocationData.the_broodals_are_after_some_cookin: 291,
        SMOLocationData.piled_on_the_salt: 256,
        SMOLocationData.lurking_in_the_pillars_shadow: 250,
        SMOLocationData.overlooking_a_bunch_of_ingredients: 246,
        SMOLocationData.luncheon_kingdom_timer_challenge_1: 264,
        SMOLocationData.love_above_the_lava: 803,
}


loc_Luncheon_Post_Wooded = {
        SMOLocationData.secret_path_to_mount_volbono: 260,
}


loc_Luncheon_Post_Spewart = {
        SMOLocationData.under_the_cheese_rocks: 251,
        SMOLocationData.atop_the_jutting_crag: 253,
        SMOLocationData.is_this_an_ingredient_too: 240,
        SMOLocationData.atop_a_column_in_a_row: 247,
        SMOLocationData.island_of_salt_floating_in_the_lava: 245,
        SMOLocationData.golden_turnip_recipe_1: 242,
        SMOLocationData.golden_turnip_recipe_2: 241,
        SMOLocationData.shopping_in_mount_volbono: 294,
}


loc_Luncheon_Post_Cheese_Rocks = {
        SMOLocationData.big_pot_on_the_volcano_dive_in: 292,
        SMOLocationData.surrounded_by_tall_mountains: 248,
        SMOLocationData.light_the_lantern_on_the_small_island: 254,
        SMOLocationData.golden_turnip_recipe_3: 243,
        SMOLocationData.beneath_the_rolling_vegetables: 263,
        SMOLocationData.all_the_cracks_are_fixed: 970,
        SMOLocationData.taking_notes_swimming_in_magma: 244,
        SMOLocationData.luncheon_kingdom_timer_challenge_2: 249,
        SMOLocationData.treasure_beneath_the_cheese_rocks: 952,
        SMOLocationData.light_the_two_flames: 255,
        SMOLocationData.found_with_luncheon_kingdom_art: 1090,
}


loc_Luncheon_Peace = {
        SMOLocationData.cookatiel_showdown: 290,
        SMOLocationData.luncheon_kingdom_timer_challenge_3: 711,
        SMOLocationData.caught_hopping_at_the_volcano: 289,
        SMOLocationData.light_the_far_off_lanterns: 261,
        SMOLocationData.bon_appetit_captain_toad: 265,
        SMOLocationData.taking_notes_big_pot_swim: 833,
        SMOLocationData.a_tourist_in_the_luncheon_kingdom: 908,
}


loc_Ruined = {
        SMOLocationData.battle_with_the_lord_of_lightning: 795,
        SMOLocationData.in_the_ancient_treasure_chest: 793,
}


loc_Bowser = {
        SMOLocationData.caught_on_the_iron_fence: 345,
        SMOLocationData.stack_up_above_the_wall: 324,
}


loc_Bowser_Infiltrate = {
        SMOLocationData.behind_the_big_wall: 321,
        SMOLocationData.taking_notes_between_spinies: 315,
        SMOLocationData.infiltrate_bowsers_castle: 325,
        SMOLocationData.smart_bombing: 334,
        SMOLocationData.treasure_inside_the_turret: 333,
        SMOLocationData.poking_your_nose_in_the_plaster_wall: 323,
        SMOLocationData.poking_the_turret_wall: 337,
}


loc_Bowser_Post_Bombing = {
        SMOLocationData.big_broodal_battle: 314,
        SMOLocationData.from_the_side_above_the_castle_gate: 335,
        SMOLocationData.exterminate_the_ogres: 320,
        SMOLocationData.jizo_all_in_a_row: 316,
        SMOLocationData.underground_jizo: 317,
        SMOLocationData.shopping_at_bowsers_castle: 360,
}


loc_Bowser_Mecha_Broodal = {
        SMOLocationData.showdown_at_bowsers_castle: 332,
}


loc_Bowser_Peace = {
        SMOLocationData.sunken_treasure_in_the_moat: 327,
        SMOLocationData.on_the_giant_bowser_statues_nose: 339,
        SMOLocationData.inside_a_block_in_the_castle: 319,
        SMOLocationData.hidden_corridor_under_the_floor: 1092,
        SMOLocationData.poking_your_nose_by_the_great_gate: 328,
        SMOLocationData.found_behind_bars: 331,
        SMOLocationData.good_to_see_you_captain_toad: 318,
        SMOLocationData.past_the_moving_wall: 340,
        SMOLocationData.above_the_poison_swamp: 326,
        SMOLocationData.knocking_down_the_nice_frame: 1133,
        SMOLocationData.caught_hopping_at_bowsers_castle: 343,
        SMOLocationData.bowsers_kingdom_timer_challenge_1: 691,
        SMOLocationData.fishing_in_bowsers_castle: 690,
}


loc_Moon = {
        SMOLocationData.shining_above_the_moon: 579,
        SMOLocationData.along_the_cliff_face: 586,
        SMOLocationData.the_tip_of_a_white_spire: 577,
        SMOLocationData.rolling_rock_on_the_moon: 583,
        SMOLocationData.caught_hopping_on_the_moon: 585,
        SMOLocationData.cliffside_treasure_chest: 582,
        SMOLocationData.moon_kingdom_timer_challenge_1: 584,
        SMOLocationData.taking_notes_on_the_moons_surface: 576,
}


loc_Mushroom_Post_Luncheon = {
        SMOLocationData.secret_path_to_peachs_castle: 608,
}


loc_Cap_Postgame = {
        SMOLocationData.the_forgotten_treasure: 228,
        SMOLocationData.taxi_flying_through_bonneton: 1073,
        SMOLocationData.bonnetter_blockade: 1038,
        SMOLocationData.cap_kingdom_regular_cup: 1033,
        SMOLocationData.peach_in_the_cap_kingdom: 229,
        SMOLocationData.found_with_cap_kingdom_art: 1086,
        SMOLocationData.next_to_glasses_bridge: 233,
        SMOLocationData.danger_sign: 816,
        SMOLocationData.under_the_big_ones_brim: 817,
        SMOLocationData.fly_to_the_edge_of_the_fog: 1158,
        SMOLocationData.spin_the_hat_get_a_prize: 1025,
        SMOLocationData.hidden_in_a_sunken_hat: 231,
        SMOLocationData.fog_shrouded_platform: 813,
        SMOLocationData.bird_traveling_in_the_fog: 1156,
        SMOLocationData.caught_hopping_near_the_ship: 232,
        SMOLocationData.taking_notes_in_the_fog: 1018,
        SMOLocationData.cap_kingdom_timer_challenge_2: 981,
        SMOLocationData.cap_kingdom_master_cup: 1034,
}


loc_Cascade_Postgame = {
        SMOLocationData.peach_in_the_cascade_kingdom: 209,
        SMOLocationData.cascade_kingdom_regular_cup: 708,
        SMOLocationData.cascade_kingdom_master_cup: 894,
        SMOLocationData.caveman_cave_fan: 866,
        SMOLocationData.sphynx_traveling_to_the_waterfall: 1041,
        SMOLocationData.bottom_of_the_waterfall_basin: 213,
        SMOLocationData.just_a_hat_skip_and_a_jump: 826,
        SMOLocationData.treasure_under_the_cliff: 214,
        SMOLocationData.next_to_the_stone_arch: 215,
        SMOLocationData.guarded_by_a_colossal_fossil: 825,
        SMOLocationData.under_the_old_electrical_pole: 823,
        SMOLocationData.under_the_ground: 822,
        SMOLocationData.inside_the_busted_fossil: 1167,
        SMOLocationData.caught_hopping_at_the_waterfall: 217,
        SMOLocationData.taking_notes_hurry_upward: 770,
}


loc_Sand_Postgame = {
        SMOLocationData.found_with_sand_kingdom_art: 1096,
        SMOLocationData.jammin_in_the_sand_kingdom: 910,
        SMOLocationData.hat_and_seek_in_the_sand: 924,
        SMOLocationData.sand_kingdom_regular_cup: 1045,
        SMOLocationData.round_the_world_tourist: 535,
        SMOLocationData.peach_in_the_sand_kingdom: 544,
        SMOLocationData.mighty_leap_from_the_palm_tree: 547,
        SMOLocationData.on_the_north_pillar: 546,
        SMOLocationData.into_the_flowing_sands: 799,
        SMOLocationData.in_the_skies_above_the_canyon: 550,
        SMOLocationData.island_in_the_poison_swamp: 521,
        SMOLocationData.an_invisible_gleam: 542,
        SMOLocationData.on_the_eastern_pillar: 540,
        SMOLocationData.caught_hopping_in_the_desert: 553,
        SMOLocationData.poster_cleanup: 545,
        SMOLocationData.taking_notes_running_down: 515,
        SMOLocationData.taking_notes_in_the_wall_painting: 783,
        SMOLocationData.love_at_the_edge_of_the_desert: 965,
        SMOLocationData.more_walking_in_the_desert: 712,
        SMOLocationData.sand_kingdom_master_cup: 1046,
}


loc_Lake_Postgame = {
        SMOLocationData.taxi_flying_through_lake_lamode: 1078,
        SMOLocationData.that_trendy_pirate_look: 870,
        SMOLocationData.space_is_in_right_now: 869,
        SMOLocationData.that_old_west_style: 871,
        SMOLocationData.lake_kingdom_regular_cup: 709,
        SMOLocationData.peach_in_the_lake_kingdom: 408,
        SMOLocationData.behind_the_floodgate: 422,
        SMOLocationData.high_flying_leap: 771,
        SMOLocationData.deep_deep_down: 719,
        SMOLocationData.rooftop_of_the_water_plaza: 1015,
        SMOLocationData.bird_traveling_over_the_lake: 423,
        SMOLocationData.love_by_the_lake: 1017,
        SMOLocationData.lake_kingdom_master_cup: 720,
}


loc_Wooded_Postgame = {
        SMOLocationData.swing_around_secret_flower_field: 1011,
        SMOLocationData.jammin_in_the_wooded_kingdom: 905,
        SMOLocationData.wooded_kingdom_regular_cup: 153,
        SMOLocationData.peach_in_the_wooded_kingdom: 155,
        SMOLocationData.high_up_in_the_cave: 168,
        SMOLocationData.lost_in_the_tall_trees: 892,
        SMOLocationData.looking_down_on_the_goombas: 160,
        SMOLocationData.high_up_on_a_rock_wall: 177,
        SMOLocationData.the_nut_in_the_robot_storeroom: 169,
        SMOLocationData.above_the_iron_mountain_path: 161,
        SMOLocationData.the_nut_under_the_observation_deck: 174,
        SMOLocationData.bird_traveling_the_forest: 166,
        SMOLocationData.invader_in_the_sky_garden: 156,
        SMOLocationData.hot_hot_hot_from_the_campfire: 163,
        SMOLocationData.wooded_kingdom_timer_challenge_3: 1010,
        SMOLocationData.moon_shards_in_the_forest: 152,
        SMOLocationData.taking_notes_on_top_of_the_wall: 170,
        SMOLocationData.taking_notes_stretching: 151,
        SMOLocationData.wooded_kingdom_master_cup: 901,
        SMOLocationData.i_met_an_uproot: 1001,
}


loc_Cloud_Postgame = {
        SMOLocationData.peach_in_the_cloud_kingdom: 1118,
        SMOLocationData.digging_in_the_cloud: 851,
        SMOLocationData.high_high_above_the_clouds: 852,
        SMOLocationData.crossing_the_cloud_sea: 853,
        SMOLocationData.taking_notes_up_and_down: 1160,
}


loc_Lost_Postgame = {
        SMOLocationData.taxi_flying_through_forgotten_isle: 1077,
        SMOLocationData.i_met_a_tropical_wiggler: 943,
        SMOLocationData.lost_kingdom_regular_cup: 832,
        SMOLocationData.peach_in_the_lost_kingdom: 394,
        SMOLocationData.the_shining_fruit: 395,
        SMOLocationData.jump_down_to_the_top_of_a_tree: 396,
        SMOLocationData.line_it_up_blow_it_up: 392,
        SMOLocationData.taking_notes_stretch_and_shrink: 393,
        SMOLocationData.lost_kingdom_master_cup: 897,
        SMOLocationData.lost_kingdom_timer_challenge: 855,
}


loc_Metro_Postgame = {
        SMOLocationData.bird_traveling_in_the_city: 1150,
        SMOLocationData.mario_signs_his_name: 97,
        SMOLocationData.surprise_clown: 874,
        SMOLocationData.a_request_from_the_mayor: 96,
        SMOLocationData.jammin_in_the_metro_kingdom: 904,
        SMOLocationData.sphynx_in_the_city: 1037,
        SMOLocationData.free_parking_leap_of_faith: 979,
        SMOLocationData.metro_kingdom_regular_cup: 939,
        SMOLocationData.hat_and_seek_in_the_city: 81,
        SMOLocationData.peach_in_the_metro_kingdom: 75,
        SMOLocationData.hanging_between_buildings: 84,
        SMOLocationData.crossing_lines: 78,
        SMOLocationData.out_of_a_crate_in_the_city: 74,
        SMOLocationData.bird_traveling_in_the_park: 1155,
        SMOLocationData.metro_kingdom_timer_challenge_3: 938,
        SMOLocationData.found_in_the_park_good_dog: 82,
        SMOLocationData.metro_kingdom_master_cup: 940,
}


loc_Snow_Postgame = {
        SMOLocationData.secret_path_to_shiveria: 937,
        SMOLocationData.snow_kingdom_regular_cup: 695,
        SMOLocationData.hat_and_seek_in_the_snow: 903,
        SMOLocationData.peach_in_the_snow_kingdom: 19,
        SMOLocationData.shining_on_high: 8,
        SMOLocationData.above_the_freezing_fish_pond: 6,
        SMOLocationData.ice_floe_swimming: 7,
        SMOLocationData.forgotten_in_the_holding_room: 843,
        SMOLocationData.it_popped_out_of_the_ice: 10,
        SMOLocationData.deep_in_the_cold_cold_water: 9,
        SMOLocationData.snow_kingdom_timer_challenge_3: 1003,
        SMOLocationData.i_met_a_snow_cheep_cheep: 973,
        SMOLocationData.snow_kingdom_master_cup: 900,
}


loc_Seaside_Postgame = {
        SMOLocationData.secret_path_to_bubblaine: 464,
        SMOLocationData.seaside_kingdom_regular_cup: 1028,
        SMOLocationData.peach_in_the_seaside_kingdom: 477,
        SMOLocationData.above_the_parasol_catch: 780,
        SMOLocationData.what_shines_inside_the_glass: 779,
        SMOLocationData.a_fine_detail_on_the_glass: 920,
        SMOLocationData.underwater_highway_west_explore: 476,
        SMOLocationData.underwater_highway_east_explore: 773,
        SMOLocationData.rapid_ascent_on_hot_spring_island: 1043,
        SMOLocationData.a_light_next_to_the_lighthouse: 776,
        SMOLocationData.the_tall_rock_shell_in_the_deep_ocean: 778,
        SMOLocationData.at_the_base_of_the_lighthouse: 775,
        SMOLocationData.bird_traveling_over_the_ocean: 1120,
        SMOLocationData.caught_hopping_at_glass_palace: 475,
        SMOLocationData.seaside_kingdom_timer_challenge_3: 864,
        SMOLocationData.taking_notes_ocean_bottom_maze: 790,
        SMOLocationData.taking_notes_in_the_sea: 1163,
        SMOLocationData.seaside_kingdom_master_cup: 1029,
}


loc_Luncheon_Postgame = {
        SMOLocationData.the_rooftop_lantern: 269,
        SMOLocationData.jammin_in_the_luncheon_kingdom: 907,
        SMOLocationData.mechanic_repairs_complete: 886,
        SMOLocationData.diving_from_the_big_pot: 1005,
        SMOLocationData.hat_and_seek_among_the_food: 280,
        SMOLocationData.luncheon_kingdom_regular_cup: 266,
        SMOLocationData.peach_in_the_luncheon_kingdom: 268,
        SMOLocationData.from_inside_a_bright_stone: 271,
        SMOLocationData.under_the_meat_plateau: 829,
        SMOLocationData.on_top_of_a_tall_tall_roof: 287,
        SMOLocationData.from_a_crack_in_the_hard_ground: 276,
        SMOLocationData.by_the_cannon_pointed_at_the_big_pot: 277,
        SMOLocationData.luncheon_kingdom_master_cup: 895,
}


loc_Ruined_Postgame = {
        SMOLocationData.peach_in_the_ruined_kingdom: 1117,
        SMOLocationData.caught_in_a_big_horn: 834,
        SMOLocationData.upon_the_broken_arch: 835,
        SMOLocationData.rolling_rock_on_the_battlefield: 909,
}


loc_Bowser_Postgame = {
        SMOLocationData.sphynx_over_bowsers_castle: 1039,
        SMOLocationData.i_met_a_pokio: 941,
        SMOLocationData.bowsers_kingdom_regular_cup: 859,
        SMOLocationData.a_rumble_under_the_arena_floor: 1161,
        SMOLocationData.secret_path_to_bowsers_castle: 322,
        SMOLocationData.peach_in_bowsers_kingdom: 338,
        SMOLocationData.found_with_bowsers_kingdom_art: 1091,
        SMOLocationData.behind_the_tall_wall_poke_poke: 350,
        SMOLocationData.from_crates_in_the_moat: 1032,
        SMOLocationData.caught_on_the_giant_horn: 1016,
        SMOLocationData.inside_a_block_at_the_gate: 356,
        SMOLocationData.small_bird_in_bowsers_castle: 1014,
        SMOLocationData.invader_in_bowsers_castle: 349,
        SMOLocationData.jumping_from_flag_to_flag: 355,
        SMOLocationData.bowsers_kingdom_timer_challenge_2: 963,
        SMOLocationData.taking_notes_on_the_wall: 351,
        SMOLocationData.taking_notes_with_a_spinning_throw: 1102,
        SMOLocationData.third_courtyard_outskirts: 348,
        SMOLocationData.stone_wall_circuit: 1143,
        SMOLocationData.bowsers_kingdom_master_cup: 896,
}


loc_Moon_Postgame = {
        SMOLocationData.sneaking_around_in_the_crater: 693,
        SMOLocationData.found_on_the_moon_good_dog: 672,
        SMOLocationData.moon_shards_on_the_moon: 671,
        SMOLocationData.moon_quiz_amazing: 580,
        SMOLocationData.thanks_captain_toad: 1000,
        SMOLocationData.shopping_in_honeylune_ridge: 1157,
        SMOLocationData.walking_on_the_moon: 578,
        SMOLocationData.moon_kingdom_regular_cup: 707,
        SMOLocationData.doctor_in_the_house: 1164,
        SMOLocationData.a_tourist_in_the_moon_kingdom: 911,
        SMOLocationData.peach_in_the_moon_kingdom: 581,
        SMOLocationData.found_with_moon_kindgom_art: 1165,
        SMOLocationData.mysterious_flying_object: 1049,
        SMOLocationData.hidden_on_the_side_of_the_cliff: 810,
        SMOLocationData.jumping_high_as_a_frog: 811,
        SMOLocationData.moon_kingdom_timer_challenge_2: 798,
        SMOLocationData.walking_on_the_moon_again: 714,
        SMOLocationData.moon_kingdom_master_cup: 899,
        SMOLocationData.taking_notes_in_low_gravity: 828,
}


loc_Mushroom = {
        SMOLocationData.a_tourist_in_the_mushroom_kingdom: 912,
        SMOLocationData.perched_on_the_castle_roof: 1134,
        SMOLocationData.pops_out_of_the_tail: 1027,
        SMOLocationData.caught_hopping_at_peachs_castle: 613,
        SMOLocationData.gardening_for_toad_garden_seed: 1108,
        SMOLocationData.gardening_for_toad_field_seed: 1109,
        SMOLocationData.gardening_for_toad_pasture_seed: 1111,
        SMOLocationData.gardening_for_toad_lake_seed: 1110,
        SMOLocationData.grow_a_flower_garden: 959,
        SMOLocationData.mushroom_kingdom_timer_challenge: 977,
        SMOLocationData.found_at_peachs_castle_good_dog: 840,
        SMOLocationData.taking_notes_around_the_well: 1121,
        SMOLocationData.herding_sheep_at_peachs_castle: 607,
        SMOLocationData.gobbling_fruit_with_yoshi: 856,
        SMOLocationData.yoshis_second_helping: 857,
        SMOLocationData.yoshis_all_filled_up: 858,
        SMOLocationData.love_at_peachs_castle: 606,
        SMOLocationData.toad_defender: 984,
        SMOLocationData.forever_onward_captain_toad: 605,
        SMOLocationData.jammin_in_the_mushroom_kingdom: 913,
        SMOLocationData.shopping_near_peachs_castle: 933,
        SMOLocationData.mushroom_kingdom_regular_cup: 967,
        SMOLocationData.mushroom_kingdom_master_cup: 968,
        SMOLocationData.found_with_mushroom_kingdom_art: 1152,
        SMOLocationData.hat_and_seek_mushroom_kingdom: 978,
        SMOLocationData.princess_peach_home_again: 1119,
}


loc_Dark = {
        SMOLocationData.arrival_at_rabbit_ridge: 1055,
        SMOLocationData.captain_toad_on_the_dark_side: 1122,
        SMOLocationData.found_with_dark_side_art_1: 1132,
        SMOLocationData.found_with_dark_side_art_2: 1130,
        SMOLocationData.found_with_dark_side_art_3: 1131,
        SMOLocationData.found_with_dark_side_art_4: 1124,
        SMOLocationData.found_with_dark_side_art_5: 1129,
        SMOLocationData.found_with_dark_side_art_6: 1127,
        SMOLocationData.found_with_dark_side_art_7: 1126,
        SMOLocationData.found_with_dark_side_art_8: 1123,
        SMOLocationData.found_with_dark_side_art_9: 1128,
        SMOLocationData.found_with_dark_side_art_10: 1125,
}


loc_Darker = {
        SMOLocationData.a_long_journeys_end: 1061,
}


loc_Cap_Shop = {
        SMOLocationData.cap_kingdom_sticker: 2582,
        SMOLocationData.plush_frog: 2599,
        SMOLocationData.bonneton_tower_model: 2600,
        SMOLocationData.black_top_hat: 2501,
        SMOLocationData.black_tuxedo: 2539,
}


loc_Cascade_Shop = {
        SMOLocationData.caveman_headwear: 2502,
        SMOLocationData.caveman_outfit: 2540,
        SMOLocationData.cascade_kingdom_sticker: 2583,
        SMOLocationData.t_rex_model: 2601,
        SMOLocationData.triceratops_trophy: 2602,
}


loc_Sand_Shop = {
        SMOLocationData.sombrero: 2503,
        SMOLocationData.poncho: 2541,
        SMOLocationData.cowboy_hat: 2504,
        SMOLocationData.cowboy_outfit: 2542,
        SMOLocationData.sand_kingdom_sticker: 2584,
        SMOLocationData.jaxi_statue: 2604,
        SMOLocationData.inverted_pyramid_model: 2603,
}


loc_Wooded_Shop = {
        SMOLocationData.explorer_hat: 2506,
        SMOLocationData.explorer_outfit: 2544,
        SMOLocationData.scientist_visor: 2507,
        SMOLocationData.scientist_outfit: 2545,
        SMOLocationData.wooded_kingdom_sticker: 2586,
        SMOLocationData.flowers_from_steam_gardens: 2607,
        SMOLocationData.steam_gardener_watering_can: 2608,
}


loc_Lake_Shop = {
        SMOLocationData.swim_goggles: 2505,
        SMOLocationData.swimwear: 2543,
        SMOLocationData.lake_kingdom_sticker: 2585,
        SMOLocationData.rubber_dorrie: 2606,
        SMOLocationData.underwater_dome: 2605,
}


loc_Lost_Shop = {
        SMOLocationData.aviator_cap: 2508,
        SMOLocationData.aviator_outfit: 2546,
        SMOLocationData.lost_kingdom_sticker: 2587,
        SMOLocationData.potted_palm_tree: 2609,
        SMOLocationData.butterfly_mobile: 2610,
}


loc_Metro_Shop = {
        SMOLocationData.builder_helmet: 2509,
        SMOLocationData.builder_outfit: 2547,
        SMOLocationData.golf_cap: 2510,
        SMOLocationData.golf_outfit: 2548,
        SMOLocationData.metro_kingdom_sticker: 2588,
        SMOLocationData.new_donk_city_hall_model: 2612,
        SMOLocationData.pauline_statue: 2611,
}


loc_Seaside_Shop = {
        SMOLocationData.resort_hat: 2512,
        SMOLocationData.resort_outfit: 2550,
        SMOLocationData.sailor_hat: 2513,
        SMOLocationData.sailor_suit: 2551,
        SMOLocationData.seaside_kingdom_sticker: 2590,
        SMOLocationData.glass_tower_model: 2616,
        SMOLocationData.sand_jar: 2615,
}


loc_Snow_Shop = {
        SMOLocationData.snow_hood: 2511,
        SMOLocationData.snow_suit: 2549,
        SMOLocationData.snow_kingdom_sticker: 2589,
        SMOLocationData.shiverian_rug: 2613,
        SMOLocationData.shiverian_nesting_dolls: 2614,
}


loc_Luncheon_Shop = {
        SMOLocationData.chef_hat: 2514,
        SMOLocationData.chef_suit: 2552,
        SMOLocationData.painters_cap: 2515,
        SMOLocationData.painter_outfit: 2553,
        SMOLocationData.luncheon_kingdom_sticker: 2591,
        SMOLocationData.souvenir_forks: 2617,
        SMOLocationData.vegetable_plate: 2618,
}


loc_Bowser_Shop = {
        SMOLocationData.samurai_helmet: 2516,
        SMOLocationData.samurai_armor: 2554,
        SMOLocationData.happi_headband: 2517,
        SMOLocationData.happi_outfit: 2555,
        SMOLocationData.bowsers_kingdom_sticker: 2592,
        SMOLocationData.paper_lantern: 2619,
        SMOLocationData.jizo_statue: 2620,
}


loc_Moon_Outfit = {
        SMOLocationData.marios_top_hat: 2538,
        SMOLocationData.marios_tuxedo: 2576,
}


loc_Moon_Shop = {
        SMOLocationData.space_helmet: 2518,
        SMOLocationData.space_suit: 2556,
        SMOLocationData.moon_kingdom_sticker: 2593,
        SMOLocationData.moon_rock_fragment: 2621,
        SMOLocationData.moon_lamp: 2622,
}


loc_Mushroom_Shop = {
        SMOLocationData.mario_64_cap: 2519,
        SMOLocationData.mario_64_suit: 2557,
        SMOLocationData.mushroom_cushion_set: 2623,
        SMOLocationData.peachs_castle_model: 2624,
        SMOLocationData.pipe_sticker: 2594,
        SMOLocationData.coin_sticker: 2595,
        SMOLocationData.block_sticker: 2596,
        SMOLocationData.question_block_sticker: 2597,
        SMOLocationData.mushroom_kingdom_sticker: 2598,
}


loc_Postgame_Shop = {
        SMOLocationData.luigi_cap: 2528,
        SMOLocationData.luigi_suit: 2566,
        SMOLocationData.doctor_headwear: 2532,
        SMOLocationData.doctor_outfit: 2570,
        SMOLocationData.waluigi_cap: 2530,
        SMOLocationData.waluigi_suit: 2568,
        SMOLocationData.diddy_kong_hat: 2533,
        SMOLocationData.diddy_kong_suit: 2571,
        SMOLocationData.wario_cap: 2529,
        SMOLocationData.wario_suit: 2567,
        SMOLocationData.hakama: 2579,
        SMOLocationData.bowsers_top_hat: 2534,
        SMOLocationData.bowsers_tuxedo: 2572,
        SMOLocationData.bridal_veil: 2535,
        SMOLocationData.bridal_gown: 2573,
        SMOLocationData.gold_mario_cap: 2531,
        SMOLocationData.gold_mario_suit: 2569,
        SMOLocationData.metal_mario_cap: 2536,
        SMOLocationData.metal_mario_suit: 2574,
}


loc_Dark_Outfit = {
        SMOLocationData.kings_crown: 2537,
        SMOLocationData.kings_outfit: 2575,
}


loc_Darker_Outfit = {
        SMOLocationData.invisibility_hat: 2581,
}


loc_odyssey_outfit = {
        SMOLocationData.captains_hat: 2577,
}


shop_sand_coin = {
        SMOLocationData.employee_cap: 2520,
        SMOLocationData.employee_uniform: 2558,
        SMOLocationData.boxer_shorts: 2578,
}


shop_lake_coin = {
        SMOLocationData.fashionable_cap: 2521,
        SMOLocationData.fashionable_outfit: 2559,
}


shop_wooded_coin = {
        SMOLocationData.mechanic_cap: 2522,
        SMOLocationData.mechanic_outfit: 2560,
}


shop_metro_coin = {
        SMOLocationData.black_fedora: 2523,
        SMOLocationData.black_suit: 2561,
}


shop_seaside_coin = {
        SMOLocationData.pirate_hat: 2524,
        SMOLocationData.pirate_outfit: 2562,
}


shop_luncheon_coin = {
        SMOLocationData.clown_hat: 2525,
        SMOLocationData.clown_suit: 2563,
}


shop_moon_coin = {
        SMOLocationData.football_helmet: 2526,
        SMOLocationData.football_uniform: 2564,
}


shop_post_game_coin = {
        SMOLocationData.classic_cap: 2527,
        SMOLocationData.classic_suit: 2565,
        SMOLocationData.skeleton_suit: 2580,
}


loc_Post_Cloud = {
        SMOLocationData.beat_bowser_in_cloud: 2500,
}

loc_Moon_Post_Moon = {
        SMOLocationData.beat_the_game: 2499,
}

loc_Captures = {
        SMOLocationData.frog: 4025,
        SMOLocationData.spark_pylon: 4026,
        SMOLocationData.paragoomba: 4027,
        SMOLocationData.chain_chomp: 4028,
        SMOLocationData.big_chain_chomp: 4029,
        SMOLocationData.broodes_chain_chomp: 4030,
        SMOLocationData.t_rex: 4031,
        SMOLocationData.binoculars: 4032,
        SMOLocationData.bullet_bill: 4033,
        SMOLocationData.moe_eye: 4034,
        SMOLocationData.cactus: 4035,
        SMOLocationData.goomba: 4036,
        SMOLocationData.knucklotecs_fist: 4037,
        SMOLocationData.mini_rocket: 4038,
        SMOLocationData.glydon: 4039,
        SMOLocationData.lakitu: 4040,
        SMOLocationData.zipper: 4041,
        SMOLocationData.cheep_cheep: 4042,
        SMOLocationData.puzzle_part_lake_kingdom: 4043,
        SMOLocationData.poison_piranha_plant: 4044,
        SMOLocationData.uproot: 4045,
        SMOLocationData.fire_bro: 4046,
        SMOLocationData.sherm: 4047,
        SMOLocationData.coin_coffer: 4048,
        SMOLocationData.tree: 4049,
        SMOLocationData.boulder: 4050,
        SMOLocationData.picture_match_part_goomba: 4051,
        SMOLocationData.tropical_wiggler: 4052,
        SMOLocationData.pole: 4053,
        SMOLocationData.manhole: 4054,
        SMOLocationData.taxi: 4055,
        SMOLocationData.rc_car: 4056,
        SMOLocationData.ty_foo: 4057,
        SMOLocationData.shiverian_racer: 4058,
        SMOLocationData.cheep_cheep_snow_kingdom: 4059,
        SMOLocationData.gushen: 4060,
        SMOLocationData.lava_bubble: 4061,
        SMOLocationData.volbonan: 4062,
        SMOLocationData.hammer_bro: 4063,
        SMOLocationData.meat: 4064,
        SMOLocationData.fire_piranha_plant: 4065,
        SMOLocationData.pokio: 4066,
        SMOLocationData.jizo: 4067,
        SMOLocationData.bowser_statue: 4068,
        SMOLocationData.parabones: 4069,
        SMOLocationData.banzai_bill: 4070,
        SMOLocationData.chargin_chuck: 4071,
        SMOLocationData.bowser: 4072,
        SMOLocationData.letter: 4073,
        SMOLocationData.puzzle_part_metro_kingdom: 4074,
        SMOLocationData.picture_match_part_mario: 4075,
        SMOLocationData.yoshi: 4076,
}

sub_area_frog = {
        SMOLocationData.searching_the_frog_pond: 238,
        SMOLocationData.secrets_of_the_frog_pond: 239,
}

sub_area_poison_tide = {
        SMOLocationData.skimming_the_poison_tide: 236,
        SMOLocationData.slipping_through_the_poison_tide: 237,
}

sub_area_push_block = {
        SMOLocationData.push_block_peril: 234,
        SMOLocationData.hidden_among_the_push_blocks: 235,
}

sub_area_rolling = {
        SMOLocationData.roll_on_and_on: 950,
        SMOLocationData.precision_rolling: 951,
}

sub_area_chain_chomp = {
        SMOLocationData.nice_shot_with_the_chain_chomp: 225,
        SMOLocationData.very_nice_shot_with_the_chain_chomp: 226,
}

sub_area_trex_nest = {
        SMOLocationData.dinosaur_nest_big_cleanup: 1116,
        SMOLocationData.dinosaur_nest_running_wild: 1115,
}

sub_area_cascade_2d = {
        SMOLocationData.past_the_chasm_lifts: 221,
        SMOLocationData.hidden_chasm_passage: 222,
}

sub_area_gusty_bridges = {
        SMOLocationData.across_the_gusty_bridges: 673,
        SMOLocationData.flying_far_away_from_gusty_bridges: 674,
}

sub_area_invisible_maze = {
        SMOLocationData.the_invisible_maze: 562,
        SMOLocationData.skull_sign_in_the_transparent_maze: 561,
}

sub_area_bullet_bill_maze = {
        SMOLocationData.the_bullet_bill_maze_break_through: 555,
        SMOLocationData.the_bullet_bill_maze_side_path: 554,
}

sub_area_jaxi = {
        SMOLocationData.jaxi_driver: 557,
        SMOLocationData.jaxi_stunt_driving: 556,
}

sub_area_strange_neighborhood = {
        SMOLocationData.strange_neighborhood: 570,
        SMOLocationData.above_a_strange_neighborhood: 571,
}

sub_area_sand_outfit = {
        SMOLocationData.dancing_with_new_friends: 569,
}

sub_area_sand_rumbling_floor = {
        SMOLocationData.a_rumble_from_the_sandy_floor: 1136,
}

sub_area_sand_employee = {
        SMOLocationData.employees_only: 564,
}

sub_area_jaxi_ruins = {
        SMOLocationData.ice_cave_treasure: 567,
}

sub_area_sand_sphinx = {
        SMOLocationData.sphynxs_treasure_vault: 566,
}

sub_area_sand_slots = {
        SMOLocationData.sand_kingdom_slots: 1047,
}

sub_area_sand_underground = {
        SMOLocationData.underground_treasure_chest: 953,
        SMOLocationData.goomba_tower_assembly: 558,
}

sub_area_sand_arena = {
        SMOLocationData.the_hole_in_the_desert: 560,
}

sub_area_sand_arena_peace = {
        SMOLocationData.under_the_mummys_curse: 995,
}

sub_area_sand_arena_post = {
        SMOLocationData.binding_band_returned: 887,
}

sub_area_transparent_platform = {
        SMOLocationData.where_the_transparent_platforms_end: 572,
        SMOLocationData.jump_onto_the_transparent_lift: 573,
}

sub_area_colossal_ruins = {
        SMOLocationData.colossal_ruins_dash_jump: 575,
        SMOLocationData.sinking_colossal_ruins_hurry: 574,
}

sub_area_freezing_waterway = {
        SMOLocationData.through_the_freezing_waterway: 35,
        SMOLocationData.freezing_waterway_hidden_room: 36,
}

sub_area_repair = {
        SMOLocationData.a_successful_repair_job: 971,
}

sub_area_zipper = {
        SMOLocationData.unzip_the_chasm: 1008,
        SMOLocationData.super_secret_zipper: 1009,
}

sub_area_jump_grab_climb = {
        SMOLocationData.jump_grab_cling_and_climb: 425,
        SMOLocationData.jump_grab_and_climb_some_more: 426,
}

sub_area_waves_poison = {
        SMOLocationData.waves_of_poison_hoppin_over: 434,
        SMOLocationData.waves_of_poison_hop_to_it: 431,
}

sub_area_deep_woods = {
        SMOLocationData.rolling_rock_in_the_deep_woods: 183,
        SMOLocationData.glowing_in_the_deep_woods: 1137,
        SMOLocationData.past_the_peculiar_pipes: 1159,
        SMOLocationData.by_the_babbling_brook_in_the_deep_woods: 185,
        SMOLocationData.the_hard_rock_in_deep_woods: 186,
        SMOLocationData.a_treasure_made_of_coins: 1153,
        SMOLocationData.beneath_the_roots_of_a_moving_tree: 184,
}

sub_area_woods_treasure_trap = {
        SMOLocationData.deep_woods_treasure_trap: 188,
}

sub_area_explorer = {
        SMOLocationData.exploring_for_treasure: 187,
}

sub_area_flooding_pipe = {
        SMOLocationData.flooding_pipeway: 196,
        SMOLocationData.flooding_pipeway_ceiling_secret: 197,
}

sub_area_flower_road = {
        SMOLocationData.flower_road_run: 191,
        SMOLocationData.flower_road_reach: 192,
}

sub_area_elevator_escalation = {
        SMOLocationData.elevator_escalation: 190,
        SMOLocationData.elevator_blind_spot: 189,
}

sub_area_wooded_fog = {
        SMOLocationData.wandering_in_the_fog: 193,
        SMOLocationData.nut_hidden_in_the_fog: 194,
}

sub_area_wooded_clouds = {
        SMOLocationData.walking_on_clouds: 198,
        SMOLocationData.above_the_clouds: 199,
}

sub_area_flower_field = {
        SMOLocationData.defend_the_secret_flower_field: 181,
}

sub_area_flower_field_peace = {
        SMOLocationData.make_the_secret_flower_field_bloom: 182,
}

sub_area_nut_room = {
        SMOLocationData.spinning_platforms_treasure: 195,
}

sub_area_wooded_invisible_road = {
        SMOLocationData.invisible_road_danger: 202,
        SMOLocationData.invisible_road_hidden_room: 203,
}

sub_area_sheep = {
        SMOLocationData.herding_sheep_above_the_forest_fog: 200,
        SMOLocationData.herding_sheep_on_the_iron_bridge: 201,
}

sub_area_wooded_breakdown_road = {
        SMOLocationData.down_and_back_breakdown_road: 882,
        SMOLocationData.below_breakdown_road: 883,
}

sub_area_cloud_picture = {
        SMOLocationData.picture_match_basically_a_goomba: 982,
}

sub_area_cloud_picture_post = {
        SMOLocationData.picture_match_a_stellar_goomba: 983,
}

sub_area_cube = {
        SMOLocationData.king_of_the_cube: 914,
        SMOLocationData.the_sixth_face: 915,
}

sub_area_jungle = {
        SMOLocationData.stretch_and_traverse_the_jungle: 399,
        SMOLocationData.aglow_in_the_jungle: 400,
}

sub_area_klepto = {
        SMOLocationData.chasing_klepto: 491,
        SMOLocationData.extremely_hot_bath: 492,
}

sub_area_metro_slots = {
        SMOLocationData.metro_kingdom_slots: 1040,
}

sub_area_rc = {
        SMOLocationData.rc_car_pro: 105,
}

sub_area_rc_post = {
        SMOLocationData.rc_car_champ: 106,
}

sub_area_private_room = {
        SMOLocationData.taking_notes_in_the_private_room: 102,
}

sub_area_city_hall = {
        SMOLocationData.city_hall_lost_and_found: 100,
}

sub_area_crowd = {
        SMOLocationData.pushing_through_the_crowd: 1022,
        SMOLocationData.high_over_the_crowd: 1023,
}

sub_area_rewiring = {
        SMOLocationData.rewiring_the_neighborhood: 122,
        SMOLocationData.off_the_beaten_wire: 121,
}

sub_area_siege = {
        SMOLocationData.moon_shards_under_siege: 114,
        SMOLocationData.sharpshooting_under_siege: 113,
}

sub_area_rotating_maze = {
        SMOLocationData.inside_the_rotating_maze: 118,
        SMOLocationData.outside_the_rotating_maze: 119,
}

sub_area_high_rise = {
        SMOLocationData.hanging_from_a_high_rise: 103,
        SMOLocationData.vaulting_up_a_high_rise: 104,
}

sub_area_bullet_billding = {
        SMOLocationData.bullet_billding: 115,
        SMOLocationData.one_mans_trash: 116,
}

sub_area_motor_scooter = {
        SMOLocationData.motor_scooter_escape: 1139,
        SMOLocationData.big_jump_escape: 1140,
}

sub_area_big_screen = {
        SMOLocationData.up_on_the_big_screen: 928,
        SMOLocationData.down_inside_the_big_screen: 929,
}

sub_area_pitch_black = {
        SMOLocationData.scaling_pitchblack_mountain: 949,
        SMOLocationData.reaching_pitchblack_island: 948,
}

sub_area_swinging_scaffolding = {
        SMOLocationData.swinging_scaffolding_jump: 125,
        SMOLocationData.swinging_scaffolding_break: 126,
}

sub_area_motor_daredevil = {
        SMOLocationData.moto_scooter_daredevil: 123,
        SMOLocationData.full_throttle_scooting: 124,
}

sub_area_crowd_post_game = {
        SMOLocationData.hat_and_seek_in_the_crowd: 1021,
}

sub_area_sewer = {
        SMOLocationData.powering_up_the_station: 107,
        SMOLocationData.sewer_treasure: 1100,
}

sub_area_sewer_post_game = {
        SMOLocationData.powering_up_the_power_plant: 848,
}

sub_area_sandy_bottom = {
        SMOLocationData.wriggling_on_the_sandy_bottom: 1024,
}

sub_area_seaside_waterway = {
        SMOLocationData.looking_back_in_the_dark_waterway: 1107,
}

sub_area_seaside_sphynx = {
        SMOLocationData.the_sphynxs_underwater_vault: 483,
}

sub_area_seaside_rumble = {
        SMOLocationData.a_rumble_on_the_seaside_floor: 1135,
}

sub_area_resort = {
        SMOLocationData.a_relaxing_dance: 484,
}

sub_area_cloud_sea = {
        SMOLocationData.wading_in_the_cloud_sea: 485,
        SMOLocationData.sunken_treasure_in_the_cloud_sea: 486,
}

sub_area_valley = {
        SMOLocationData.fly_through_the_narrow_valley: 478,
        SMOLocationData.treasure_chest_in_the_narrow_valley: 479,
}

sub_area_seaside_stretch = {
        SMOLocationData.hurry_and_stretch: 482,
        SMOLocationData.stretch_on_the_side_path: 481,
}

sub_area_seaside_pokio = {
        SMOLocationData.aim_poke: 488,
        SMOLocationData.poke_roll: 487,
}

sub_area_seaside_maze = {
        SMOLocationData.the_spinning_maze_search: 489,
        SMOLocationData.the_spinning_maze_open: 490,
}

sub_area_icicle_post = {
        SMOLocationData.stacked_up_ice_climb: 699,
}

sub_area_ice_wall_post = {
        SMOLocationData.water_pooling_in_the_crevasse: 701,
}

sub_area_gusty_barrier_post = {
        SMOLocationData.icy_jump_challenge: 703,
}

sub_area_snowy_mountain_post = {
        SMOLocationData.squirming_under_the_ice: 704,
}

sub_area_magma_swamp = {
        SMOLocationData.magma_swamp_floating_and_sinking: 302,
        SMOLocationData.corner_of_the_magma_swamp: 303,
}

sub_area_veggies = {
        SMOLocationData.the_treasure_chest_in_the_veggies: 992,
}

sub_area_cook = {
        SMOLocationData.a_strong_simmer: 991,
        SMOLocationData.an_extreme_simmer: 990,
}

sub_area_forks = {
        SMOLocationData.fork_flickin_to_the_summit: 296,
        SMOLocationData.fork_flickin_detour: 297,
}

sub_area_cheese = {
        SMOLocationData.excavate_n_search_the_cheese_rocks: 300,
        SMOLocationData.climb_the_cheese_rocks: 301,
}

sub_area_lava_bubble = {
        SMOLocationData.magma_narrow_path: 299,
        SMOLocationData.crossing_to_the_magma: 298,
}

sub_area_spinning_athletics = {
        SMOLocationData.spinning_athletics_end_goal: 307,
        SMOLocationData.taking_notes_spinning_athletics: 306,
}

sub_area_luncheon_story = {
        SMOLocationData.climb_up_the_cascading_magma: 257,
        SMOLocationData.alcove_behind_the_pillars_of_magma: 259,
}

sub_area_luncheon_slots = {
        SMOLocationData.luncheon_kingdom_slots: 1042,
}

sub_area_gear_steps = {
        SMOLocationData.stepping_over_the_gears_and_lanterns_on_the_gear_steps: 313,
        SMOLocationData.lanterns_on_the_gear_steps: 312,
}

sub_area_volcano_cave = {
        SMOLocationData.volcano_cave_cruisin: 310,
        SMOLocationData.volcano_cave_and_mysterious_clouds: 311,
}

sub_area_lava_islands = {
        SMOLocationData.treasure_of_the_lava_islands: 1101,
        SMOLocationData.flying_over_the_lava_islands: 308,
}

sub_area_roulette_tower = {
        SMOLocationData.roulette_tower_climbed: 888,
        SMOLocationData.roulette_tower_stopped: 889,
}

sub_area_ruined_charging = {
        SMOLocationData.charging_through_an_army: 891,
        SMOLocationData.the_mummys_curse: 890,
}

sub_area_samurai = {
        SMOLocationData.scene_of_crossing_the_poison_swamp: 987,
        SMOLocationData.taking_notes_in_the_folding_screen: 988,
}

sub_area_bowser_vault = {
        SMOLocationData.bowsers_castle_treasure_vault: 999,
}

sub_area_jizo_adventure = {
        SMOLocationData.jizos_big_adventure: 363,
        SMOLocationData.jizo_and_the_hidden_room: 364,
}

sub_area_spinning_tower = {
        SMOLocationData.on_top_of_the_spinning_tower: 368,
        SMOLocationData.down_and_up_the_spinning_tower: 367,
}

sub_area_hexagon_tower = {
        SMOLocationData.searching_hexagon_tower: 801,
        SMOLocationData.center_of_hexagon_tower: 802,
}

sub_area_wooden_tower = {
        SMOLocationData.climb_the_wooden_tower: 370,
        SMOLocationData.poke_the_wooden_tower: 369,
}

sub_area_galaxy = {
        SMOLocationData.edge_of_the_galaxy: 604,
        SMOLocationData.center_of_the_galaxy: 603,
}

sub_area_swings = {
        SMOLocationData.navigating_giant_swings: 601,
        SMOLocationData.a_swing_on_top_of_a_swing: 602,
}

sub_area_sphynx_moon = {
        SMOLocationData.sphynxs_hidden_vault: 599,
}

sub_area_mushroom_picture = {
        SMOLocationData.picture_match_basically_mario: 985,
        SMOLocationData.picture_match_a_stellar_mario: 986,
}

sub_area_64 = {
        SMOLocationData.totally_classic: 934,
        SMOLocationData.courtyard_chest_trap: 1144,
}

sub_area_castle = {
        SMOLocationData.light_from_the_ceiling: 807,
        SMOLocationData.loose_tile_trackdown: 956,
}

sub_area_mushroom_well = {
        SMOLocationData.secret_2d_treasure: 1050,
        SMOLocationData.dot_boost_from_bullet_bill: 1051,
}

sub_area_yoshi_clouds = {
        SMOLocationData.yoshis_feast_in_the_sea_of_clouds: 974,
        SMOLocationData.sunken_star_in_the_sea_of_clouds: 975,
}

sub_area_rematch_tostarena = {
        SMOLocationData.tussle_in_tostarena_rematch: 1148,
}

sub_area_rematch_steam_gardens = {
        SMOLocationData.struggle_in_steam_gardens_rematch: 1149,
}

sub_area_rematch_bubblaine = {
        SMOLocationData.battle_in_bubblaine_rematch: 1147,
}

sub_area_rematch_metro = {
        SMOLocationData.dust_up_in_new_donk_city_rematch: 1141,
}

sub_area_rematch_volbono = {
        SMOLocationData.blowup_at_mount_volbono_rematch: 1142,
}

sub_area_rematch_crumbleden = {
        SMOLocationData.rumble_in_crumbleden_rematch: 1146,
}

sub_area_darker_invisible = {
        SMOLocationData.invisible_road_rush: 1069,
        SMOLocationData.invisible_road_secret: 1068,
}

sub_area_darker_breakdown = {
        SMOLocationData.breakdown_road_hurry: 1062,
        SMOLocationData.breakdown_road_final_challenge: 1063,
}

sub_area_darker_vanishing = {
        SMOLocationData.vanishing_road_rush: 1065,
        SMOLocationData.vanishing_road_challenge: 1064,
}

sub_area_darker_yoshi_siege = {
        SMOLocationData.yoshi_under_siege: 1066,
        SMOLocationData.fruit_feast_under_siege: 1067,
}

sub_area_darker_yoshi_sinking = {
        SMOLocationData.yoshi_on_the_sinking_island: 1070,
        SMOLocationData.fruit_feast_on_the_sinking_island: 1071,
}

sub_area_darker_yoshi_magma = {
        SMOLocationData.yoshis_magma_swamp: 1082,
        SMOLocationData.fruit_feast_in_the_magma_swamp: 1083,
}

sub_area_inverted_pyramid = {
        SMOLocationData.hidden_room_in_the_inverted_pyramid: 563,
}

loc_Sand_Pyramid_Mural = {
        SMOLocationData.secret_of_the_inverted_mural: 504,
}

sub_area_mysterious_clouds = {
        SMOLocationData.across_the_mysterious_clouds: 224,
        SMOLocationData.atop_a_wall_among_the_clouds: 223,
}

sub_area_moon_cave = {
        SMOLocationData.under_the_bowser_statue: 595,
        SMOLocationData.in_a_hole_in_the_magma: 596,
        SMOLocationData.around_the_barrier_wall: 597,
        SMOLocationData.on_top_of_the_cannon: 594,
        SMOLocationData.fly_to_the_treasure_chest_and_back: 598,
}

sub_area_snow_koopa = {
        SMOLocationData.walking_on_ice: 877,
}

sub_area_snow_koopa_post = {
        SMOLocationData.even_more_walking_on_ice: 878,
}

sub_area_snow_outfit = {
        SMOLocationData.moon_shards_in_the_cold_room: 1030,
        SMOLocationData.slip_behind_the_ice: 1031,
}

sub_area_snow_dashing = {
        SMOLocationData.dashing_over_cold_water: 12,
        SMOLocationData.dashing_above_and_beyond: 13,
}

sub_area_snow_freezing_water = {
        SMOLocationData.jump_n_swim_in_the_freezing_water: 14,
        SMOLocationData.freezing_water_near_the_ceiling: 15,
}

sub_area_blowing = {
        SMOLocationData.blowing_and_sliding: 33,
}

sub_area_snow_spinning = {
        SMOLocationData.spinning_above_the_clouds: 830,
        SMOLocationData.high_altitude_spinning: 831,
}

sub_area_snow_flower_road = {
        SMOLocationData.running_the_flower_road: 31,
        SMOLocationData.looking_back_on_the_flower_road: 32,
}

sub_area_iceburn = {
        SMOLocationData.iceburn_circuit_class_a: 998,
        SMOLocationData.iceburn_circuit_class_s: 997,
}

sub_area_bowser_clouds = {
        SMOLocationData.dashing_above_the_clouds: 365,
        SMOLocationData.dashing_through_the_clouds: 366,
}

sub_area_church = {
        SMOLocationData.up_in_the_rafters: 593,
}

sub_area_shiveria = {
        SMOLocationData.entrance_to_shiveria: 1081,
        SMOLocationData.shining_in_the_snow_in_town: 1080,
        SMOLocationData.the_shiverian_treasure_chest: 23,
        SMOLocationData.found_with_snow_kingdom_art: 1087,
        SMOLocationData.the_icicle_barrier: 17,
        SMOLocationData.ice_dodging_goomba_stack: 20,
        SMOLocationData.the_ice_wall_barrier: 22,
        SMOLocationData.treasure_in_the_ice_wall: 24,
        SMOLocationData.the_gusty_barrier: 18,
        SMOLocationData.atop_a_blustery_arch: 16,
        SMOLocationData.the_snowy_mountain_barrier: 25,
        SMOLocationData.behind_the_snowy_mountain: 21,
}

sub_area_shiveria_peace = {
        SMOLocationData.im_not_cold: 873,
}

sub_area_snowline = {
        SMOLocationData.the_bound_bowl_grand_prix: 1020,
        SMOLocationData.snowline_circuit_class_s: 879,
}



base_locations_table = {
        **loc_Cap,
        **loc_Cascade,
        **loc_Cascade_Peace,
        **loc_Cascade_Revisit,
        **loc_Cascade_Post_Metro,
        **loc_Cascade_Post_Snow,
        **loc_Sand,
        **loc_Sand_Pyramid,
        **loc_Night_Sand,
        **loc_Sand_Underground,
        **loc_Sand_Peace,
        **loc_Sand_Pyramid_Peace,
        **loc_Sand_Revisit,
        **loc_Lake,
        **loc_Lake_Post_Seaside,
        **loc_Wooded,
        **loc_Wooded_Post_Metro,
        **loc_Wooded_Post_Story1,
        **loc_Wooded_Peace,
        **loc_Cloud,
        **loc_Lost,
        **loc_Lost_Revisit,
        **loc_Night_Metro,
        **loc_Metro,
        **loc_Metro_Post_Sand,
        **loc_Metro_Sewer_Access,
        **loc_Metro_Peace,
        **loc_Snow,
        **loc_Snow_Peace,
        **loc_Seaside,
        **loc_Seaside_Peace,
        **loc_Luncheon,
        **loc_Luncheon_Post_Wooded,
        **loc_Luncheon_Post_Spewart,
        **loc_Luncheon_Post_Cheese_Rocks,
        **loc_Luncheon_Peace,
        **loc_Ruined,
        **loc_Bowser,
        **loc_Bowser_Infiltrate,
        **loc_Bowser_Post_Bombing,
        **loc_Bowser_Mecha_Broodal,
        **loc_Bowser_Peace,
        **loc_Moon,
        **loc_Mushroom_Post_Luncheon
}

shop_locations_table = {
        **loc_Cap_Shop,
        **loc_Cascade_Shop,
        **loc_Sand_Shop,
        **loc_Wooded_Shop,
        **loc_Lake_Shop,
        **loc_Lost_Shop,
        **loc_Metro_Shop,
        **loc_Seaside_Shop,
        **loc_Snow_Shop,
        **loc_Luncheon_Shop,
        **loc_Bowser_Shop,
        **loc_Moon_Outfit,
        **loc_Moon_Shop,
        **loc_Mushroom_Shop,
        **loc_Postgame_Shop,
        **loc_Dark_Outfit,
        **loc_Darker_Outfit,
        **loc_odyssey_outfit,
        **shop_sand_coin,
        **shop_lake_coin,
        **shop_wooded_coin,
        **shop_metro_coin,
        **shop_seaside_coin,
        **shop_luncheon_coin,
        **shop_moon_coin,
        **shop_post_game_coin
}

post_game_locations_table = {
        **loc_Cap_Postgame,
        **loc_Cascade_Postgame,
        **loc_Sand_Postgame,
        **loc_Lake_Postgame,
        **loc_Wooded_Postgame,
        **loc_Cloud_Postgame,
        **loc_Lost_Postgame,
        **loc_Metro_Postgame,
        **loc_Snow_Postgame,
        **loc_Seaside_Postgame,
        **loc_Luncheon_Postgame,
        **loc_Ruined_Postgame,
        **loc_Bowser_Postgame,
        **loc_Moon_Postgame,
        **loc_Mushroom
}

special_locations_table = {
        **loc_Dark,
        **loc_Darker
}

sub_area_table = {
        **sub_area_frog,
        **sub_area_poison_tide,
        **sub_area_push_block,
        **sub_area_rolling,
        **sub_area_chain_chomp,
        **sub_area_trex_nest,
        **sub_area_cascade_2d,
        **sub_area_gusty_bridges,
        **sub_area_invisible_maze,
        **sub_area_bullet_bill_maze,
        **sub_area_jaxi,
        **sub_area_strange_neighborhood,
        **sub_area_sand_outfit,
        **sub_area_sand_rumbling_floor,
        **sub_area_sand_employee,
        **sub_area_jaxi_ruins,
        **sub_area_sand_sphinx,
        **sub_area_sand_slots,
        **sub_area_sand_underground,
        **sub_area_sand_arena,
        **sub_area_sand_arena_peace,
        **sub_area_sand_arena_post,
        **sub_area_transparent_platform,
        **sub_area_colossal_ruins,
        **sub_area_freezing_waterway,
        **sub_area_repair,
        **sub_area_zipper,
        **sub_area_jump_grab_climb,
        **sub_area_waves_poison,
        **sub_area_deep_woods,
        **sub_area_woods_treasure_trap,
        **sub_area_explorer,
        **sub_area_flooding_pipe,
        **sub_area_flower_road,
        **sub_area_elevator_escalation,
        **sub_area_wooded_fog,
        **sub_area_wooded_clouds,
        **sub_area_flower_field,
        **sub_area_flower_field_peace,
        **sub_area_nut_room,
        **sub_area_wooded_invisible_road,
        **sub_area_sheep,
        **sub_area_wooded_breakdown_road,
        **sub_area_cloud_picture,
        **sub_area_cloud_picture_post,
        **sub_area_cube,
        **sub_area_jungle,
        **sub_area_klepto,
        **sub_area_metro_slots,
        **sub_area_rc,
        **sub_area_rc_post,
        **sub_area_private_room,
        **sub_area_city_hall,
        **sub_area_crowd,
        **sub_area_rewiring,
        **sub_area_siege,
        **sub_area_rotating_maze,
        **sub_area_high_rise,
        **sub_area_bullet_billding,
        **sub_area_motor_scooter,
        **sub_area_big_screen,
        **sub_area_pitch_black,
        **sub_area_swinging_scaffolding,
        **sub_area_motor_daredevil,
        **sub_area_crowd_post_game,
        **sub_area_sewer,
        **sub_area_sewer_post_game,
        **sub_area_sandy_bottom,
        **sub_area_seaside_waterway,
        **sub_area_seaside_sphynx,
        **sub_area_seaside_rumble,
        **sub_area_resort,
        **sub_area_cloud_sea,
        **sub_area_valley,
        **sub_area_seaside_stretch,
        **sub_area_seaside_pokio,
        **sub_area_seaside_maze,
        **sub_area_icicle_post,
        **sub_area_ice_wall_post,
        **sub_area_gusty_barrier_post,
        **sub_area_snowy_mountain_post,
        **sub_area_magma_swamp,
        **sub_area_veggies,
        **sub_area_cook,
        **sub_area_forks,
        **sub_area_cheese,
        **sub_area_lava_bubble,
        **sub_area_spinning_athletics,
        **sub_area_luncheon_story,
        **sub_area_luncheon_slots,
        **sub_area_gear_steps,
        **sub_area_volcano_cave,
        **sub_area_lava_islands,
        **sub_area_roulette_tower,
        **sub_area_ruined_charging,
        **sub_area_samurai,
        **sub_area_bowser_vault,
        **sub_area_jizo_adventure,
        **sub_area_spinning_tower,
        **sub_area_hexagon_tower,
        **sub_area_wooden_tower,
        **sub_area_galaxy,
        **sub_area_swings,
        **sub_area_sphynx_moon,
        **sub_area_mushroom_picture,
        **sub_area_64,
        **sub_area_castle,
        **sub_area_mushroom_well,
        **sub_area_yoshi_clouds,
        **sub_area_rematch_tostarena,
        **sub_area_rematch_steam_gardens,
        **sub_area_rematch_bubblaine,
        **sub_area_rematch_metro,
        **sub_area_rematch_volbono,
        **sub_area_rematch_crumbleden,
        **sub_area_darker_invisible,
        **sub_area_darker_breakdown,
        **sub_area_darker_vanishing,
        **sub_area_darker_yoshi_siege,
        **sub_area_darker_yoshi_sinking,
        **sub_area_darker_yoshi_magma,
        **sub_area_inverted_pyramid,
        **loc_Sand_Pyramid_Mural,
        **sub_area_mysterious_clouds,
        **sub_area_moon_cave,
        **sub_area_snow_koopa,
        **sub_area_snow_koopa_post,
        **sub_area_snow_outfit,
        **sub_area_snow_dashing,
        **sub_area_snow_freezing_water,
        **sub_area_blowing,
        **sub_area_snow_spinning,
        **sub_area_snow_flower_road,
        **sub_area_iceburn,
        **sub_area_bowser_clouds,
        **sub_area_church,
        **sub_area_shiveria,
        **sub_area_shiveria_peace,
        **sub_area_snowline,
}

#region Regional Coins

cap_kingdom_regional_groups = {
        SMOLocationData.cap_kingdom_regional_coin_group_1: 2700,
        SMOLocationData.cap_kingdom_regional_coin_group_2: 2705,
        SMOLocationData.cap_kingdom_regional_coin_group_3: 2710,
        SMOLocationData.cap_kingdom_regional_coin_group_4: 2715,
        SMOLocationData.cap_kingdom_regional_coin_group_5: 2720,
        SMOLocationData.cap_kingdom_regional_coin_group_6: 2724,
        SMOLocationData.cap_kingdom_regional_coin_group_7: 2728,
        SMOLocationData.cap_kingdom_regional_coin_group_8: 2732,
        SMOLocationData.cap_kingdom_regional_coin_group_9: 2736,
}

top_hat_tower_regional_groups = {
        SMOLocationData.top_hat_tower_regional_coin_group_1: 2740,
        SMOLocationData.top_hat_tower_regional_coin_group_2: 2745,
}

frog_pond_regional_groups = {
        SMOLocationData.frog_pond_regional_coin_group_1: 2751,
}

pushblocks_regional_groups = {
        SMOLocationData.pushblocks_regional_coin_group_1: 2756,
}

poison_tides_regional_groups = {
        SMOLocationData.poison_tides_regional_coin_group_1: 2760,
}

cap_kingdom_regional_coins = {
        SMOLocationData.cap_kingdom_regional_coin_1: 2701,
        SMOLocationData.cap_kingdom_regional_coin_2: 2702,
        SMOLocationData.cap_kingdom_regional_coin_3: 2703,
        SMOLocationData.cap_kingdom_regional_coin_4: 2704,
        SMOLocationData.cap_kingdom_regional_coin_5: 2706,
        SMOLocationData.cap_kingdom_regional_coin_6: 2707,
        SMOLocationData.cap_kingdom_regional_coin_7: 2708,
        SMOLocationData.cap_kingdom_regional_coin_8: 2709,
        SMOLocationData.cap_kingdom_regional_coin_9: 2711,
        SMOLocationData.cap_kingdom_regional_coin_10: 2712,
        SMOLocationData.cap_kingdom_regional_coin_11: 2713,
        SMOLocationData.cap_kingdom_regional_coin_12: 2714,
        SMOLocationData.cap_kingdom_regional_coin_13: 2716,
        SMOLocationData.cap_kingdom_regional_coin_14: 2717,
        SMOLocationData.cap_kingdom_regional_coin_15: 2718,
        SMOLocationData.cap_kingdom_regional_coin_16: 2719,
        SMOLocationData.cap_kingdom_regional_coin_17: 2721,
        SMOLocationData.cap_kingdom_regional_coin_18: 2722,
        SMOLocationData.cap_kingdom_regional_coin_19: 2723,
        SMOLocationData.cap_kingdom_regional_coin_20: 2725,
        SMOLocationData.cap_kingdom_regional_coin_21: 2726,
        SMOLocationData.cap_kingdom_regional_coin_22: 2727,
        SMOLocationData.cap_kingdom_regional_coin_23: 2729,
        SMOLocationData.cap_kingdom_regional_coin_24: 2730,
        SMOLocationData.cap_kingdom_regional_coin_25: 2731,
        SMOLocationData.cap_kingdom_regional_coin_26: 2733,
        SMOLocationData.cap_kingdom_regional_coin_27: 2734,
        SMOLocationData.cap_kingdom_regional_coin_28: 2735,
        SMOLocationData.cap_kingdom_regional_coin_29: 2737,
        SMOLocationData.cap_kingdom_regional_coin_30: 2738,
        SMOLocationData.cap_kingdom_regional_coin_31: 2739,
}

top_hat_tower_regional_coins = {
        SMOLocationData.top_hat_tower_regional_coin_1: 2741,
        SMOLocationData.top_hat_tower_regional_coin_2: 2742,
        SMOLocationData.top_hat_tower_regional_coin_3: 2743,
        SMOLocationData.top_hat_tower_regional_coin_4: 2744,
        SMOLocationData.top_hat_tower_regional_coin_5: 2746,
        SMOLocationData.top_hat_tower_regional_coin_6: 2747,
        SMOLocationData.top_hat_tower_regional_coin_7: 2748,
        SMOLocationData.top_hat_tower_regional_coin_8: 2749,
        SMOLocationData.top_hat_tower_regional_coin_9: 2750,
}

frog_pond_regional_coins = {
        SMOLocationData.frog_pond_regional_coin_1: 2752,
        SMOLocationData.frog_pond_regional_coin_2: 2753,
        SMOLocationData.frog_pond_regional_coin_3: 2754,
        SMOLocationData.frog_pond_regional_coin_4: 2755,
}

pushblocks_regional_coins = {
        SMOLocationData.pushblocks_regional_coin_1: 2757,
        SMOLocationData.pushblocks_regional_coin_2: 2758,
        SMOLocationData.pushblocks_regional_coin_3: 2759,
}

poison_tides_regional_coins = {
        SMOLocationData.poison_tides_regional_coin_1: 2761,
        SMOLocationData.poison_tides_regional_coin_2: 2762,
        SMOLocationData.poison_tides_regional_coin_3: 2763,
}

cascade_kingdom_regional_groups = {
        SMOLocationData.cascade_kingdom_regional_coin_group_1: 2764,
        SMOLocationData.cascade_kingdom_regional_coin_group_2: 2768,
        SMOLocationData.cascade_kingdom_regional_coin_group_3: 2772,
        SMOLocationData.cascade_kingdom_regional_coin_group_4: 2776,
        SMOLocationData.cascade_kingdom_regional_coin_group_5: 2780,
        SMOLocationData.cascade_kingdom_regional_coin_group_6: 2784,
        SMOLocationData.cascade_kingdom_regional_coin_group_7: 2788,
        SMOLocationData.cascade_kingdom_regional_coin_group_8: 2792,
        SMOLocationData.cascade_kingdom_regional_coin_group_9: 2796,
        SMOLocationData.cascade_kingdom_regional_coin_group_10: 2800,
        SMOLocationData.cascade_kingdom_regional_coin_group_11: 2805,
        SMOLocationData.cascade_kingdom_regional_coin_group_12: 2809,
        SMOLocationData.cascade_kingdom_regional_coin_group_13: 2813,
}

cascade_kingdom_peace_regional_groups = {
        SMOLocationData.cascade_kingdom_regional_coin_group_14: 2817,
        SMOLocationData.cascade_kingdom_regional_coin_group_15: 2821,
}

chasm_lifts_regional_groups = {
        SMOLocationData.chasm_lifts_regional_coin_group_1: 2825,
}

cascade_kingdom_regional_coins = {
        SMOLocationData.cascade_kingdom_regional_coin_1: 2765,
        SMOLocationData.cascade_kingdom_regional_coin_2: 2766,
        SMOLocationData.cascade_kingdom_regional_coin_3: 2767,
        SMOLocationData.cascade_kingdom_regional_coin_4: 2769,
        SMOLocationData.cascade_kingdom_regional_coin_5: 2770,
        SMOLocationData.cascade_kingdom_regional_coin_6: 2771,
        SMOLocationData.cascade_kingdom_regional_coin_7: 2773,
        SMOLocationData.cascade_kingdom_regional_coin_8: 2774,
        SMOLocationData.cascade_kingdom_regional_coin_9: 2775,
        SMOLocationData.cascade_kingdom_regional_coin_10: 2777,
        SMOLocationData.cascade_kingdom_regional_coin_11: 2778,
        SMOLocationData.cascade_kingdom_regional_coin_12: 2779,
        SMOLocationData.cascade_kingdom_regional_coin_13: 2781,
        SMOLocationData.cascade_kingdom_regional_coin_14: 2782,
        SMOLocationData.cascade_kingdom_regional_coin_15: 2783,
        SMOLocationData.cascade_kingdom_regional_coin_16: 2785,
        SMOLocationData.cascade_kingdom_regional_coin_17: 2786,
        SMOLocationData.cascade_kingdom_regional_coin_18: 2787,
        SMOLocationData.cascade_kingdom_regional_coin_19: 2789,
        SMOLocationData.cascade_kingdom_regional_coin_20: 2790,
        SMOLocationData.cascade_kingdom_regional_coin_21: 2791,
        SMOLocationData.cascade_kingdom_regional_coin_22: 2793,
        SMOLocationData.cascade_kingdom_regional_coin_23: 2794,
        SMOLocationData.cascade_kingdom_regional_coin_24: 2795,
        SMOLocationData.cascade_kingdom_regional_coin_25: 2797,
        SMOLocationData.cascade_kingdom_regional_coin_26: 2798,
        SMOLocationData.cascade_kingdom_regional_coin_27: 2799,
        SMOLocationData.cascade_kingdom_regional_coin_28: 2801,
        SMOLocationData.cascade_kingdom_regional_coin_29: 2802,
        SMOLocationData.cascade_kingdom_regional_coin_30: 2803,
        SMOLocationData.cascade_kingdom_regional_coin_31: 2804,
        SMOLocationData.cascade_kingdom_regional_coin_32: 2806,
        SMOLocationData.cascade_kingdom_regional_coin_33: 2807,
        SMOLocationData.cascade_kingdom_regional_coin_34: 2808,
        SMOLocationData.cascade_kingdom_regional_coin_35: 2810,
        SMOLocationData.cascade_kingdom_regional_coin_36: 2811,
        SMOLocationData.cascade_kingdom_regional_coin_37: 2812,
        SMOLocationData.cascade_kingdom_regional_coin_38: 2814,
        SMOLocationData.cascade_kingdom_regional_coin_39: 2815,
        SMOLocationData.cascade_kingdom_regional_coin_40: 2816,
}

cascade_kingdom_peace_regional_coins = {
        SMOLocationData.cascade_kingdom_regional_coin_41: 2818,
        SMOLocationData.cascade_kingdom_regional_coin_42: 2819,
        SMOLocationData.cascade_kingdom_regional_coin_43: 2820,
        SMOLocationData.cascade_kingdom_regional_coin_44: 2822,
        SMOLocationData.cascade_kingdom_regional_coin_45: 2823,
        SMOLocationData.cascade_kingdom_regional_coin_46: 2824,
}

chasm_lifts_regional_coins = {
        SMOLocationData.chasm_lifts_regional_coin_1: 2826,
        SMOLocationData.chasm_lifts_regional_coin_2: 2827,
        SMOLocationData.chasm_lifts_regional_coin_3: 2828,
        SMOLocationData.chasm_lifts_regional_coin_4: 2829,
}

sand_kingdom_regional_groups = {
        SMOLocationData.sand_kingdom_regional_coin_group_1: 2830,
        SMOLocationData.sand_kingdom_regional_coin_group_2: 2834,
        SMOLocationData.sand_kingdom_regional_coin_group_3: 2838,
        SMOLocationData.sand_kingdom_regional_coin_group_4: 2842,
        SMOLocationData.sand_kingdom_regional_coin_group_5: 2846,
        SMOLocationData.sand_kingdom_regional_coin_group_6: 2850,
        SMOLocationData.sand_kingdom_regional_coin_group_7: 2853,
        SMOLocationData.sand_kingdom_regional_coin_group_8: 2856,
        SMOLocationData.sand_kingdom_regional_coin_group_9: 2860,
        SMOLocationData.sand_kingdom_regional_coin_group_11: 2868,
        SMOLocationData.sand_kingdom_regional_coin_group_12: 2872,
        SMOLocationData.sand_kingdom_regional_coin_group_13: 2876,
        SMOLocationData.sand_kingdom_regional_coin_group_14: 2880,
        SMOLocationData.sand_kingdom_regional_coin_group_15: 2884,
        SMOLocationData.sand_kingdom_regional_coin_group_16: 2887,
        SMOLocationData.sand_kingdom_regional_coin_group_17: 2894,
        SMOLocationData.sand_kingdom_regional_coin_group_20: 2908,
}

sand_kingdom_pyramid_over_world_regional_groups = {
        SMOLocationData.sand_kingdom_regional_coin_group_10: 2864,
}

sand_kingdom_peace_regional_groups = {
        SMOLocationData.sand_kingdom_regional_coin_group_18: 2898,
        SMOLocationData.sand_kingdom_regional_coin_group_19: 2903,
}

bullet_bill_maze_regional_groups = {
        SMOLocationData.bullet_bill_maze_regional_coin_group_1: 2914,
}

moeeye_invisible_maze_regional_groups = {
        SMOLocationData.moeeye_invisible_maze_regional_coin_group_1: 2921,
}

ice_cave_regional_groups = {
        SMOLocationData.ice_cave_regional_coin_group_1: 2926,
        SMOLocationData.ice_cave_regional_coin_group_2: 2929,
}

pyramid_upper_interior_regional_groups = {
        SMOLocationData.pyramid_upper_interior_regional_coin_group_1: 2932,
}

strange_neighborhood_regional_groups = {
        SMOLocationData.strange_neighborhood_regional_coin_group_1: 2936,
        SMOLocationData.strange_neighborhood_regional_coin_group_2: 2939,
}

underground_ruins_regional_groups = {
        SMOLocationData.underground_ruins_regional_coin_group_1: 2943,
        SMOLocationData.underground_ruins_regional_coin_group_2: 2947,
}

jaxi_ruins_regional_groups = {
        SMOLocationData.jaxi_ruins_regional_coin_group_1: 2952,
        SMOLocationData.jaxi_ruins_regional_coin_group_2: 2955,
        SMOLocationData.jaxi_ruins_regional_coin_group_3: 2959,
}

sand_kingdom_regional_coins = {
        SMOLocationData.sand_kingdom_regional_coin_1: 2831,
        SMOLocationData.sand_kingdom_regional_coin_2: 2832,
        SMOLocationData.sand_kingdom_regional_coin_3: 2833,
        SMOLocationData.sand_kingdom_regional_coin_4: 2835,
        SMOLocationData.sand_kingdom_regional_coin_5: 2836,
        SMOLocationData.sand_kingdom_regional_coin_6: 2837,
        SMOLocationData.sand_kingdom_regional_coin_7: 2839,
        SMOLocationData.sand_kingdom_regional_coin_8: 2840,
        SMOLocationData.sand_kingdom_regional_coin_9: 2841,
        SMOLocationData.sand_kingdom_regional_coin_10: 2843,
        SMOLocationData.sand_kingdom_regional_coin_11: 2844,
        SMOLocationData.sand_kingdom_regional_coin_12: 2845,
        SMOLocationData.sand_kingdom_regional_coin_13: 2847,
        SMOLocationData.sand_kingdom_regional_coin_14: 2848,
        SMOLocationData.sand_kingdom_regional_coin_15: 2849,
        SMOLocationData.sand_kingdom_regional_coin_16: 2851,
        SMOLocationData.sand_kingdom_regional_coin_17: 2852,
        SMOLocationData.sand_kingdom_regional_coin_18: 2854,
        SMOLocationData.sand_kingdom_regional_coin_19: 2855,
        SMOLocationData.sand_kingdom_regional_coin_20: 2857,
        SMOLocationData.sand_kingdom_regional_coin_21: 2858,
        SMOLocationData.sand_kingdom_regional_coin_22: 2859,
        SMOLocationData.sand_kingdom_regional_coin_23: 2861,
        SMOLocationData.sand_kingdom_regional_coin_24: 2862,
        SMOLocationData.sand_kingdom_regional_coin_25: 2863,
        SMOLocationData.sand_kingdom_regional_coin_29: 2869,
        SMOLocationData.sand_kingdom_regional_coin_30: 2870,
        SMOLocationData.sand_kingdom_regional_coin_31: 2871,
        SMOLocationData.sand_kingdom_regional_coin_32: 2873,
        SMOLocationData.sand_kingdom_regional_coin_33: 2874,
        SMOLocationData.sand_kingdom_regional_coin_34: 2875,
        SMOLocationData.sand_kingdom_regional_coin_35: 2877,
        SMOLocationData.sand_kingdom_regional_coin_36: 2878,
        SMOLocationData.sand_kingdom_regional_coin_37: 2879,
        SMOLocationData.sand_kingdom_regional_coin_38: 2881,
        SMOLocationData.sand_kingdom_regional_coin_39: 2882,
        SMOLocationData.sand_kingdom_regional_coin_40: 2883,
        SMOLocationData.sand_kingdom_regional_coin_41: 2885,
        SMOLocationData.sand_kingdom_regional_coin_42: 2886,
        SMOLocationData.sand_kingdom_regional_coin_43: 2888,
        SMOLocationData.sand_kingdom_regional_coin_44: 2889,
        SMOLocationData.sand_kingdom_regional_coin_45: 2890,
        SMOLocationData.sand_kingdom_regional_coin_46: 2891,
        SMOLocationData.sand_kingdom_regional_coin_47: 2892,
        SMOLocationData.sand_kingdom_regional_coin_48: 2893,
        SMOLocationData.sand_kingdom_regional_coin_49: 2895,
        SMOLocationData.sand_kingdom_regional_coin_50: 2896,
        SMOLocationData.sand_kingdom_regional_coin_51: 2897,
        SMOLocationData.sand_kingdom_regional_coin_60: 2909,
        SMOLocationData.sand_kingdom_regional_coin_61: 2910,
        SMOLocationData.sand_kingdom_regional_coin_62: 2912,
        SMOLocationData.sand_kingdom_regional_coin_63: 2913,
}

sand_kingdom_peace_regional_coins = {
        SMOLocationData.sand_kingdom_regional_coin_52: 2899,
        SMOLocationData.sand_kingdom_regional_coin_53: 2900,
        SMOLocationData.sand_kingdom_regional_coin_54: 2901,
        SMOLocationData.sand_kingdom_regional_coin_55: 2902,
        SMOLocationData.sand_kingdom_regional_coin_56: 2904,
        SMOLocationData.sand_kingdom_regional_coin_57: 2905,
        SMOLocationData.sand_kingdom_regional_coin_58: 2906,
        SMOLocationData.sand_kingdom_regional_coin_59: 2907,
}

sand_kingdom_pyramid_over_world_regional_coins = {
        SMOLocationData.sand_kingdom_regional_coin_26: 2865,
        SMOLocationData.sand_kingdom_regional_coin_27: 2866,
        SMOLocationData.sand_kingdom_regional_coin_28: 2867,
}

bullet_bill_maze_regional_coins = {
        SMOLocationData.bullet_bill_maze_regional_coin_1: 2915,
        SMOLocationData.bullet_bill_maze_regional_coin_2: 2916,
        SMOLocationData.bullet_bill_maze_regional_coin_3: 2917,
        SMOLocationData.bullet_bill_maze_regional_coin_4: 2918,
        SMOLocationData.bullet_bill_maze_regional_coin_5: 2919,
        SMOLocationData.bullet_bill_maze_regional_coin_6: 2920,
}

moeeye_invisible_maze_regional_coins = {
        SMOLocationData.moeeye_invisible_maze_regional_coin_1: 2922,
        SMOLocationData.moeeye_invisible_maze_regional_coin_2: 2923,
        SMOLocationData.moeeye_invisible_maze_regional_coin_3: 2924,
        SMOLocationData.moeeye_invisible_maze_regional_coin_4: 2925,
}

ice_cave_regional_coins = {
        SMOLocationData.ice_cave_regional_coin_1: 2927,
        SMOLocationData.ice_cave_regional_coin_2: 2928,
        SMOLocationData.ice_cave_regional_coin_3: 2930,
        SMOLocationData.ice_cave_regional_coin_4: 2931,
}

pyramid_upper_interior_regional_coins = {
        SMOLocationData.pyramid_upper_interior_regional_coin_1: 2933,
        SMOLocationData.pyramid_upper_interior_regional_coin_2: 2934,
        SMOLocationData.pyramid_upper_interior_regional_coin_3: 2935,
}

strange_neighborhood_regional_coins = {
        SMOLocationData.strange_neighborhood_regional_coin_1: 2937,
        SMOLocationData.strange_neighborhood_regional_coin_2: 2938,
        SMOLocationData.strange_neighborhood_regional_coin_3: 2940,
        SMOLocationData.strange_neighborhood_regional_coin_4: 2941,
        SMOLocationData.strange_neighborhood_regional_coin_5: 2942,
}

underground_ruins_regional_coins = {
        SMOLocationData.underground_ruins_regional_coin_1: 2944,
        SMOLocationData.underground_ruins_regional_coin_2: 2945,
        SMOLocationData.underground_ruins_regional_coin_3: 2946,
        SMOLocationData.underground_ruins_regional_coin_4: 2948,
        SMOLocationData.underground_ruins_regional_coin_5: 2949,
        SMOLocationData.underground_ruins_regional_coin_6: 2950,
        SMOLocationData.underground_ruins_regional_coin_7: 2951,
}

jaxi_ruins_regional_coins = {
        SMOLocationData.jaxi_ruins_regional_coin_1: 2953,
        SMOLocationData.jaxi_ruins_regional_coin_2: 2954,
        SMOLocationData.jaxi_ruins_regional_coin_3: 2956,
        SMOLocationData.jaxi_ruins_regional_coin_4: 2957,
        SMOLocationData.jaxi_ruins_regional_coin_5: 2958,
        SMOLocationData.jaxi_ruins_regional_coin_6: 2960,
        SMOLocationData.jaxi_ruins_regional_coin_7: 2961,
        SMOLocationData.jaxi_ruins_regional_coin_8: 2962,
}

wooded_kingdom_regional_groups = {
        SMOLocationData.wooded_kingdom_regional_coin_group_1: 2963,
        SMOLocationData.wooded_kingdom_regional_coin_group_2: 2967,
        SMOLocationData.wooded_kingdom_regional_coin_group_3: 2971,
        SMOLocationData.wooded_kingdom_regional_coin_group_4: 2975,
        SMOLocationData.wooded_kingdom_regional_coin_group_5: 2978,
        SMOLocationData.wooded_kingdom_regional_coin_group_6: 2983,
        SMOLocationData.wooded_kingdom_regional_coin_group_7: 2988,
        SMOLocationData.wooded_kingdom_regional_coin_group_8: 2992,
        SMOLocationData.wooded_kingdom_regional_coin_group_9: 2996,
        SMOLocationData.wooded_kingdom_regional_coin_group_10: 3001,
        SMOLocationData.wooded_kingdom_regional_coin_group_11: 3006,
        SMOLocationData.wooded_kingdom_regional_coin_group_12: 3011,
        SMOLocationData.wooded_kingdom_regional_coin_group_13: 3015,
        SMOLocationData.wooded_kingdom_regional_coin_group_14: 3019,
        SMOLocationData.wooded_kingdom_regional_coin_group_15: 3024,
        SMOLocationData.wooded_kingdom_regional_coin_group_16: 3028,
        SMOLocationData.wooded_kingdom_regional_coin_group_17: 3032,
        SMOLocationData.wooded_kingdom_regional_coin_group_18: 3036,
        SMOLocationData.wooded_kingdom_regional_coin_group_19: 3039,
        SMOLocationData.wooded_kingdom_regional_coin_group_20: 3044,
        SMOLocationData.wooded_kingdom_regional_coin_group_21: 3047,
        SMOLocationData.wooded_kingdom_regional_coin_group_22: 3051,
        SMOLocationData.wooded_kingdom_regional_coin_group_23: 3055,
        SMOLocationData.wooded_kingdom_regional_coin_group_24: 3059,
}

sky_garden_tower_regional_groups = {
        SMOLocationData.sky_garden_tower_regional_coin_group_1: 3063,
}

flooded_pipes_regional_groups = {
        SMOLocationData.flooded_pipes_regional_coin_group_1: 3067,
}

deep_woods_regional_groups = {
        SMOLocationData.deep_woods_regional_coin_group_1: 3071,
        SMOLocationData.deep_woods_regional_coin_group_2: 3075,
        SMOLocationData.deep_woods_regional_coin_group_3: 3079,
}

walking_on_clouds_regional_groups = {
        SMOLocationData.walking_on_clouds_regional_coin_group_1: 3083,
}

wooded_flower_road_regional_groups = {
        SMOLocationData.wooded_flower_road_regional_coin_group_1: 3087,
}

sherm_elevator_regional_groups = {
        SMOLocationData.sherm_elevator_regional_coin_group_1: 3091,
}

wooded_kingdom_regional_coins = {
        SMOLocationData.wooded_kingdom_regional_coin_1: 2964,
        SMOLocationData.wooded_kingdom_regional_coin_2: 2965,
        SMOLocationData.wooded_kingdom_regional_coin_3: 2966,
        SMOLocationData.wooded_kingdom_regional_coin_4: 2968,
        SMOLocationData.wooded_kingdom_regional_coin_5: 2969,
        SMOLocationData.wooded_kingdom_regional_coin_6: 2970,
        SMOLocationData.wooded_kingdom_regional_coin_7: 2972,
        SMOLocationData.wooded_kingdom_regional_coin_8: 2973,
        SMOLocationData.wooded_kingdom_regional_coin_9: 2974,
        SMOLocationData.wooded_kingdom_regional_coin_10: 2976,
        SMOLocationData.wooded_kingdom_regional_coin_11: 2977,
        SMOLocationData.wooded_kingdom_regional_coin_12: 2979,
        SMOLocationData.wooded_kingdom_regional_coin_13: 2980,
        SMOLocationData.wooded_kingdom_regional_coin_14: 2981,
        SMOLocationData.wooded_kingdom_regional_coin_15: 2982,
        SMOLocationData.wooded_kingdom_regional_coin_16: 2984,
        SMOLocationData.wooded_kingdom_regional_coin_17: 2985,
        SMOLocationData.wooded_kingdom_regional_coin_18: 2986,
        SMOLocationData.wooded_kingdom_regional_coin_19: 2987,
        SMOLocationData.wooded_kingdom_regional_coin_20: 2989,
        SMOLocationData.wooded_kingdom_regional_coin_21: 2990,
        SMOLocationData.wooded_kingdom_regional_coin_22: 2991,
        SMOLocationData.wooded_kingdom_regional_coin_23: 2993,
        SMOLocationData.wooded_kingdom_regional_coin_24: 2994,
        SMOLocationData.wooded_kingdom_regional_coin_25: 2995,
        SMOLocationData.wooded_kingdom_regional_coin_26: 2997,
        SMOLocationData.wooded_kingdom_regional_coin_27: 2998,
        SMOLocationData.wooded_kingdom_regional_coin_28: 2999,
        SMOLocationData.wooded_kingdom_regional_coin_29: 3000,
        SMOLocationData.wooded_kingdom_regional_coin_30: 3002,
        SMOLocationData.wooded_kingdom_regional_coin_31: 3003,
        SMOLocationData.wooded_kingdom_regional_coin_32: 3004,
        SMOLocationData.wooded_kingdom_regional_coin_33: 3005,
        SMOLocationData.wooded_kingdom_regional_coin_34: 3007,
        SMOLocationData.wooded_kingdom_regional_coin_35: 3008,
        SMOLocationData.wooded_kingdom_regional_coin_36: 3009,
        SMOLocationData.wooded_kingdom_regional_coin_37: 3010,
        SMOLocationData.wooded_kingdom_regional_coin_38: 3012,
        SMOLocationData.wooded_kingdom_regional_coin_39: 3013,
        SMOLocationData.wooded_kingdom_regional_coin_40: 3014,
        SMOLocationData.wooded_kingdom_regional_coin_41: 3016,
        SMOLocationData.wooded_kingdom_regional_coin_42: 3017,
        SMOLocationData.wooded_kingdom_regional_coin_43: 3018,
        SMOLocationData.wooded_kingdom_regional_coin_44: 3020,
        SMOLocationData.wooded_kingdom_regional_coin_45: 3021,
        SMOLocationData.wooded_kingdom_regional_coin_46: 3022,
        SMOLocationData.wooded_kingdom_regional_coin_47: 3023,
        SMOLocationData.wooded_kingdom_regional_coin_48: 3025,
        SMOLocationData.wooded_kingdom_regional_coin_49: 3026,
        SMOLocationData.wooded_kingdom_regional_coin_50: 3027,
        SMOLocationData.wooded_kingdom_regional_coin_51: 3029,
        SMOLocationData.wooded_kingdom_regional_coin_52: 3030,
        SMOLocationData.wooded_kingdom_regional_coin_53: 3031,
        SMOLocationData.wooded_kingdom_regional_coin_54: 3033,
        SMOLocationData.wooded_kingdom_regional_coin_55: 3034,
        SMOLocationData.wooded_kingdom_regional_coin_56: 3035,
        SMOLocationData.wooded_kingdom_regional_coin_57: 3037,
        SMOLocationData.wooded_kingdom_regional_coin_58: 3038,
        SMOLocationData.wooded_kingdom_regional_coin_59: 3040,
        SMOLocationData.wooded_kingdom_regional_coin_60: 3041,
        SMOLocationData.wooded_kingdom_regional_coin_61: 3042,
        SMOLocationData.wooded_kingdom_regional_coin_62: 3043,
        SMOLocationData.wooded_kingdom_regional_coin_63: 3045,
        SMOLocationData.wooded_kingdom_regional_coin_64: 3046,
        SMOLocationData.wooded_kingdom_regional_coin_65: 3048,
        SMOLocationData.wooded_kingdom_regional_coin_66: 3049,
        SMOLocationData.wooded_kingdom_regional_coin_67: 3050,
        SMOLocationData.wooded_kingdom_regional_coin_68: 3052,
        SMOLocationData.wooded_kingdom_regional_coin_69: 3053,
        SMOLocationData.wooded_kingdom_regional_coin_70: 3054,
        SMOLocationData.wooded_kingdom_regional_coin_71: 3056,
        SMOLocationData.wooded_kingdom_regional_coin_72: 3057,
        SMOLocationData.wooded_kingdom_regional_coin_73: 3058,
        SMOLocationData.wooded_kingdom_regional_coin_74: 3060,
        SMOLocationData.wooded_kingdom_regional_coin_75: 3061,
        SMOLocationData.wooded_kingdom_regional_coin_76: 3062,
}

sky_garden_tower_regional_coins = {
        SMOLocationData.sky_garden_tower_regional_coin_1: 3064,
        SMOLocationData.sky_garden_tower_regional_coin_2: 3065,
        SMOLocationData.sky_garden_tower_regional_coin_3: 3066,
}

flooded_pipes_regional_coins = {
        SMOLocationData.flooded_pipes_regional_coin_1: 3068,
        SMOLocationData.flooded_pipes_regional_coin_2: 3069,
        SMOLocationData.flooded_pipes_regional_coin_3: 3070,
}

deep_woods_regional_coins = {
        SMOLocationData.deep_woods_regional_coin_1: 3072,
        SMOLocationData.deep_woods_regional_coin_2: 3073,
        SMOLocationData.deep_woods_regional_coin_3: 3074,
        SMOLocationData.deep_woods_regional_coin_4: 3076,
        SMOLocationData.deep_woods_regional_coin_5: 3077,
        SMOLocationData.deep_woods_regional_coin_6: 3078,
        SMOLocationData.deep_woods_regional_coin_7: 3080,
        SMOLocationData.deep_woods_regional_coin_8: 3081,
        SMOLocationData.deep_woods_regional_coin_9: 3082,
}

walking_on_clouds_regional_coins = {
        SMOLocationData.walking_on_clouds_regional_coin_1: 3084,
        SMOLocationData.walking_on_clouds_regional_coin_2: 3085,
        SMOLocationData.walking_on_clouds_regional_coin_3: 3086,
}

wooded_flower_road_regional_coins = {
        SMOLocationData.wooded_flower_road_regional_coin_1: 3088,
        SMOLocationData.wooded_flower_road_regional_coin_2: 3089,
        SMOLocationData.wooded_flower_road_regional_coin_3: 3090,
}

sherm_elevator_regional_coins = {
        SMOLocationData.sherm_elevator_regional_coin_1: 3092,
        SMOLocationData.sherm_elevator_regional_coin_2: 3093,
        SMOLocationData.sherm_elevator_regional_coin_3: 3094,
}

lake_kingdom_regional_groups = {
        SMOLocationData.lake_kingdom_regional_coin_group_1: 3095,
        SMOLocationData.lake_kingdom_regional_coin_group_2: 3100,
        SMOLocationData.lake_kingdom_regional_coin_group_3: 3104,
        SMOLocationData.lake_kingdom_regional_coin_group_4: 3109,
        SMOLocationData.lake_kingdom_regional_coin_group_5: 3113,
        SMOLocationData.lake_kingdom_regional_coin_group_6: 3117,
        SMOLocationData.lake_kingdom_regional_coin_group_7: 3121,
        SMOLocationData.lake_kingdom_regional_coin_group_8: 3125,
        SMOLocationData.lake_kingdom_regional_coin_group_9: 3130,
        SMOLocationData.lake_kingdom_regional_coin_group_10: 3135,
        SMOLocationData.lake_kingdom_regional_coin_group_11: 3139,
        SMOLocationData.lake_kingdom_regional_coin_group_12: 3144,
        SMOLocationData.lake_kingdom_regional_coin_group_13: 3148,
        SMOLocationData.lake_kingdom_regional_coin_group_14: 3152,
}

bouncy_flowers_regional_groups = {
        SMOLocationData.bouncy_flowers_regional_coin_group_1: 3156,
}

lake_kingdom_regional_coins = {
        SMOLocationData.lake_kingdom_regional_coin_1: 3096,
        SMOLocationData.lake_kingdom_regional_coin_2: 3097,
        SMOLocationData.lake_kingdom_regional_coin_3: 3098,
        SMOLocationData.lake_kingdom_regional_coin_4: 3099,
        SMOLocationData.lake_kingdom_regional_coin_5: 3101,
        SMOLocationData.lake_kingdom_regional_coin_6: 3102,
        SMOLocationData.lake_kingdom_regional_coin_7: 3103,
        SMOLocationData.lake_kingdom_regional_coin_8: 3105,
        SMOLocationData.lake_kingdom_regional_coin_9: 3106,
        SMOLocationData.lake_kingdom_regional_coin_10: 3107,
        SMOLocationData.lake_kingdom_regional_coin_11: 3108,
        SMOLocationData.lake_kingdom_regional_coin_12: 3110,
        SMOLocationData.lake_kingdom_regional_coin_13: 3111,
        SMOLocationData.lake_kingdom_regional_coin_14: 3112,
        SMOLocationData.lake_kingdom_regional_coin_15: 3114,
        SMOLocationData.lake_kingdom_regional_coin_16: 3115,
        SMOLocationData.lake_kingdom_regional_coin_17: 3116,
        SMOLocationData.lake_kingdom_regional_coin_18: 3118,
        SMOLocationData.lake_kingdom_regional_coin_19: 3119,
        SMOLocationData.lake_kingdom_regional_coin_20: 3120,
        SMOLocationData.lake_kingdom_regional_coin_21: 3122,
        SMOLocationData.lake_kingdom_regional_coin_22: 3123,
        SMOLocationData.lake_kingdom_regional_coin_23: 3124,
        SMOLocationData.lake_kingdom_regional_coin_24: 3126,
        SMOLocationData.lake_kingdom_regional_coin_25: 3127,
        SMOLocationData.lake_kingdom_regional_coin_26: 3128,
        SMOLocationData.lake_kingdom_regional_coin_27: 3129,
        SMOLocationData.lake_kingdom_regional_coin_28: 3131,
        SMOLocationData.lake_kingdom_regional_coin_29: 3132,
        SMOLocationData.lake_kingdom_regional_coin_30: 3133,
        SMOLocationData.lake_kingdom_regional_coin_31: 3134,
        SMOLocationData.lake_kingdom_regional_coin_32: 3136,
        SMOLocationData.lake_kingdom_regional_coin_33: 3137,
        SMOLocationData.lake_kingdom_regional_coin_34: 3138,
        SMOLocationData.lake_kingdom_regional_coin_35: 3140,
        SMOLocationData.lake_kingdom_regional_coin_36: 3141,
        SMOLocationData.lake_kingdom_regional_coin_37: 3142,
        SMOLocationData.lake_kingdom_regional_coin_38: 3143,
        SMOLocationData.lake_kingdom_regional_coin_39: 3145,
        SMOLocationData.lake_kingdom_regional_coin_40: 3146,
        SMOLocationData.lake_kingdom_regional_coin_41: 3147,
        SMOLocationData.lake_kingdom_regional_coin_42: 3149,
        SMOLocationData.lake_kingdom_regional_coin_43: 3150,
        SMOLocationData.lake_kingdom_regional_coin_44: 3151,
        SMOLocationData.lake_kingdom_regional_coin_45: 3153,
        SMOLocationData.lake_kingdom_regional_coin_46: 3154,
        SMOLocationData.lake_kingdom_regional_coin_47: 3155,
}

bouncy_flowers_regional_coins = {
        SMOLocationData.bouncy_flowers_regional_coin_1: 3157,
        SMOLocationData.bouncy_flowers_regional_coin_2: 3158,
        SMOLocationData.bouncy_flowers_regional_coin_3: 3159,
}

lost_kingdom_regional_groups = {
        SMOLocationData.lost_kingdom_regional_coin_group_1: 3160,
        SMOLocationData.lost_kingdom_regional_coin_group_2: 3164,
        SMOLocationData.lost_kingdom_regional_coin_group_3: 3169,
        SMOLocationData.lost_kingdom_regional_coin_group_4: 3173,
        SMOLocationData.lost_kingdom_regional_coin_group_5: 3177,
        SMOLocationData.lost_kingdom_regional_coin_group_6: 3180,
        SMOLocationData.lost_kingdom_regional_coin_group_7: 3185,
        SMOLocationData.lost_kingdom_regional_coin_group_8: 3189,
        SMOLocationData.lost_kingdom_regional_coin_group_9: 3194,
        SMOLocationData.lost_kingdom_regional_coin_group_10: 3197,
        SMOLocationData.lost_kingdom_regional_coin_group_11: 3200,
        SMOLocationData.lost_kingdom_regional_coin_group_12: 3203,
        SMOLocationData.lost_kingdom_regional_coin_group_13: 3206,
        SMOLocationData.lost_kingdom_regional_coin_group_14: 3210,
        SMOLocationData.lost_kingdom_regional_coin_group_15: 3213,
        SMOLocationData.lost_kingdom_regional_coin_group_16: 3216,
        SMOLocationData.lost_kingdom_regional_coin_group_17: 3220,
        SMOLocationData.lost_kingdom_regional_coin_group_18: 3224,
}

lost_kingdom_regional_coins = {
        SMOLocationData.lost_kingdom_regional_coin_1: 3161,
        SMOLocationData.lost_kingdom_regional_coin_2: 3162,
        SMOLocationData.lost_kingdom_regional_coin_3: 3163,
        SMOLocationData.lost_kingdom_regional_coin_4: 3165,
        SMOLocationData.lost_kingdom_regional_coin_5: 3166,
        SMOLocationData.lost_kingdom_regional_coin_6: 3167,
        SMOLocationData.lost_kingdom_regional_coin_7: 3168,
        SMOLocationData.lost_kingdom_regional_coin_8: 3170,
        SMOLocationData.lost_kingdom_regional_coin_9: 3171,
        SMOLocationData.lost_kingdom_regional_coin_10: 3172,
        SMOLocationData.lost_kingdom_regional_coin_11: 3174,
        SMOLocationData.lost_kingdom_regional_coin_12: 3175,
        SMOLocationData.lost_kingdom_regional_coin_13: 3176,
        SMOLocationData.lost_kingdom_regional_coin_14: 3178,
        SMOLocationData.lost_kingdom_regional_coin_15: 3179,
        SMOLocationData.lost_kingdom_regional_coin_16: 3181,
        SMOLocationData.lost_kingdom_regional_coin_17: 3182,
        SMOLocationData.lost_kingdom_regional_coin_18: 3183,
        SMOLocationData.lost_kingdom_regional_coin_19: 3184,
        SMOLocationData.lost_kingdom_regional_coin_20: 3186,
        SMOLocationData.lost_kingdom_regional_coin_21: 3187,
        SMOLocationData.lost_kingdom_regional_coin_22: 3188,
        SMOLocationData.lost_kingdom_regional_coin_23: 3190,
        SMOLocationData.lost_kingdom_regional_coin_24: 3191,
        SMOLocationData.lost_kingdom_regional_coin_25: 3192,
        SMOLocationData.lost_kingdom_regional_coin_26: 3193,
        SMOLocationData.lost_kingdom_regional_coin_27: 3195,
        SMOLocationData.lost_kingdom_regional_coin_28: 3196,
        SMOLocationData.lost_kingdom_regional_coin_29: 3198,
        SMOLocationData.lost_kingdom_regional_coin_30: 3199,
        SMOLocationData.lost_kingdom_regional_coin_31: 3201,
        SMOLocationData.lost_kingdom_regional_coin_32: 3202,
        SMOLocationData.lost_kingdom_regional_coin_33: 3204,
        SMOLocationData.lost_kingdom_regional_coin_34: 3205,
        SMOLocationData.lost_kingdom_regional_coin_35: 3207,
        SMOLocationData.lost_kingdom_regional_coin_36: 3208,
        SMOLocationData.lost_kingdom_regional_coin_37: 3209,
        SMOLocationData.lost_kingdom_regional_coin_38: 3211,
        SMOLocationData.lost_kingdom_regional_coin_39: 3212,
        SMOLocationData.lost_kingdom_regional_coin_40: 3214,
        SMOLocationData.lost_kingdom_regional_coin_41: 3215,
        SMOLocationData.lost_kingdom_regional_coin_42: 3217,
        SMOLocationData.lost_kingdom_regional_coin_43: 3218,
        SMOLocationData.lost_kingdom_regional_coin_44: 3219,
        SMOLocationData.lost_kingdom_regional_coin_45: 3221,
        SMOLocationData.lost_kingdom_regional_coin_46: 3222,
        SMOLocationData.lost_kingdom_regional_coin_47: 3223,
        SMOLocationData.lost_kingdom_regional_coin_48: 3225,
        SMOLocationData.lost_kingdom_regional_coin_49: 3226,
        SMOLocationData.lost_kingdom_regional_coin_50: 3227,
}

night_metro_kingdom_regional_groups = {
        SMOLocationData.metro_kingdom_regional_coin_group_1: 3228,
        SMOLocationData.metro_kingdom_regional_coin_group_2: 3232,
        SMOLocationData.metro_kingdom_regional_coin_group_3: 3236,
        SMOLocationData.metro_kingdom_regional_coin_group_4: 3240,
        SMOLocationData.metro_kingdom_regional_coin_group_5: 3244,
        SMOLocationData.metro_kingdom_regional_coin_group_6: 3248,
}

metro_kingdom_regional_groups = {
        SMOLocationData.metro_kingdom_regional_coin_group_7: 3251,
        SMOLocationData.metro_kingdom_regional_coin_group_8: 3255,
        SMOLocationData.metro_kingdom_regional_coin_group_9: 3259,
        SMOLocationData.metro_kingdom_regional_coin_group_10: 3263,
        SMOLocationData.metro_kingdom_regional_coin_group_11: 3267,
        SMOLocationData.metro_kingdom_regional_coin_group_12: 3271,
        SMOLocationData.metro_kingdom_regional_coin_group_13: 3274,
        SMOLocationData.metro_kingdom_regional_coin_group_14: 3278,
        SMOLocationData.metro_kingdom_regional_coin_group_15: 3281,
        SMOLocationData.metro_kingdom_regional_coin_group_16: 3285,
        SMOLocationData.metro_kingdom_regional_coin_group_17: 3289,
        SMOLocationData.metro_kingdom_regional_coin_group_18: 3293,
        SMOLocationData.metro_kingdom_regional_coin_group_19: 3296,
        SMOLocationData.metro_kingdom_regional_coin_group_20: 3301,
        SMOLocationData.metro_kingdom_regional_coin_group_21: 3305,
        SMOLocationData.metro_kingdom_regional_coin_group_22: 3309,
        SMOLocationData.metro_kingdom_regional_coin_group_23: 3312,
        SMOLocationData.metro_kingdom_regional_coin_group_24: 3316,
        SMOLocationData.metro_kingdom_regional_coin_group_25: 3319,
        SMOLocationData.metro_kingdom_regional_coin_group_26: 3322,
}

sewers_regional_groups = {
        SMOLocationData.sewers_regional_coin_group_1: 3325,
        SMOLocationData.sewers_regional_coin_group_2: 3329,
}

city_hall_regional_groups = {
        SMOLocationData.city_hall_regional_coin_group_1: 3334,
        SMOLocationData.city_hall_regional_coin_group_2: 3338,
        SMOLocationData.city_hall_regional_coin_group_3: 3341,
        SMOLocationData.city_hall_regional_coin_group_4: 3345,
}

bullet_billding_regional_groups = {
        SMOLocationData.bullet_billding_regional_coin_group_1: 3348,
}

high_rise_regional_groups = {
        SMOLocationData.high_rise_regional_coin_group_1: 3352,
}

trex_escape_regional_groups = {
        SMOLocationData.trex_escape_regional_coin_group_1: 3356,
        SMOLocationData.trex_escape_regional_coin_group_2: 3360,
}

night_metro_kingdom_regional_coins = {
        SMOLocationData.metro_kingdom_regional_coin_1: 3229,
        SMOLocationData.metro_kingdom_regional_coin_2: 3230,
        SMOLocationData.metro_kingdom_regional_coin_3: 3231,
        SMOLocationData.metro_kingdom_regional_coin_4: 3233,
        SMOLocationData.metro_kingdom_regional_coin_5: 3234,
        SMOLocationData.metro_kingdom_regional_coin_6: 3235,
        SMOLocationData.metro_kingdom_regional_coin_7: 3237,
        SMOLocationData.metro_kingdom_regional_coin_8: 3238,
        SMOLocationData.metro_kingdom_regional_coin_9: 3239,
        SMOLocationData.metro_kingdom_regional_coin_10: 3241,
        SMOLocationData.metro_kingdom_regional_coin_11: 3242,
        SMOLocationData.metro_kingdom_regional_coin_12: 3243,
        SMOLocationData.metro_kingdom_regional_coin_13: 3245,
        SMOLocationData.metro_kingdom_regional_coin_14: 3246,
        SMOLocationData.metro_kingdom_regional_coin_15: 3247,
        SMOLocationData.metro_kingdom_regional_coin_16: 3249,
        SMOLocationData.metro_kingdom_regional_coin_17: 3250,
}

metro_kingdom_regional_coins = {
        SMOLocationData.metro_kingdom_regional_coin_18: 3252,
        SMOLocationData.metro_kingdom_regional_coin_19: 3253,
        SMOLocationData.metro_kingdom_regional_coin_20: 3254,
        SMOLocationData.metro_kingdom_regional_coin_21: 3256,
        SMOLocationData.metro_kingdom_regional_coin_22: 3257,
        SMOLocationData.metro_kingdom_regional_coin_23: 3258,
        SMOLocationData.metro_kingdom_regional_coin_24: 3260,
        SMOLocationData.metro_kingdom_regional_coin_25: 3261,
        SMOLocationData.metro_kingdom_regional_coin_26: 3262,
        SMOLocationData.metro_kingdom_regional_coin_27: 3264,
        SMOLocationData.metro_kingdom_regional_coin_28: 3265,
        SMOLocationData.metro_kingdom_regional_coin_29: 3266,
        SMOLocationData.metro_kingdom_regional_coin_30: 3268,
        SMOLocationData.metro_kingdom_regional_coin_31: 3269,
        SMOLocationData.metro_kingdom_regional_coin_32: 3270,
        SMOLocationData.metro_kingdom_regional_coin_33: 3272,
        SMOLocationData.metro_kingdom_regional_coin_34: 3273,
        SMOLocationData.metro_kingdom_regional_coin_35: 3275,
        SMOLocationData.metro_kingdom_regional_coin_36: 3276,
        SMOLocationData.metro_kingdom_regional_coin_37: 3277,
        SMOLocationData.metro_kingdom_regional_coin_38: 3279,
        SMOLocationData.metro_kingdom_regional_coin_39: 3280,
        SMOLocationData.metro_kingdom_regional_coin_40: 3282,
        SMOLocationData.metro_kingdom_regional_coin_41: 3283,
        SMOLocationData.metro_kingdom_regional_coin_42: 3284,
        SMOLocationData.metro_kingdom_regional_coin_43: 3286,
        SMOLocationData.metro_kingdom_regional_coin_44: 3287,
        SMOLocationData.metro_kingdom_regional_coin_45: 3288,
        SMOLocationData.metro_kingdom_regional_coin_46: 3290,
        SMOLocationData.metro_kingdom_regional_coin_47: 3291,
        SMOLocationData.metro_kingdom_regional_coin_48: 3292,
        SMOLocationData.metro_kingdom_regional_coin_49: 3294,
        SMOLocationData.metro_kingdom_regional_coin_50: 3295,
        SMOLocationData.metro_kingdom_regional_coin_51: 3297,
        SMOLocationData.metro_kingdom_regional_coin_52: 3298,
        SMOLocationData.metro_kingdom_regional_coin_53: 3299,
        SMOLocationData.metro_kingdom_regional_coin_54: 3300,
        SMOLocationData.metro_kingdom_regional_coin_55: 3302,
        SMOLocationData.metro_kingdom_regional_coin_56: 3303,
        SMOLocationData.metro_kingdom_regional_coin_57: 3304,
        SMOLocationData.metro_kingdom_regional_coin_58: 3306,
        SMOLocationData.metro_kingdom_regional_coin_59: 3307,
        SMOLocationData.metro_kingdom_regional_coin_60: 3308,
        SMOLocationData.metro_kingdom_regional_coin_61: 3310,
        SMOLocationData.metro_kingdom_regional_coin_62: 3311,
        SMOLocationData.metro_kingdom_regional_coin_63: 3313,
        SMOLocationData.metro_kingdom_regional_coin_64: 3314,
        SMOLocationData.metro_kingdom_regional_coin_65: 3315,
        SMOLocationData.metro_kingdom_regional_coin_66: 3317,
        SMOLocationData.metro_kingdom_regional_coin_67: 3318,
        SMOLocationData.metro_kingdom_regional_coin_68: 3320,
        SMOLocationData.metro_kingdom_regional_coin_69: 3321,
        SMOLocationData.metro_kingdom_regional_coin_70: 3323,
        SMOLocationData.metro_kingdom_regional_coin_71: 3324,
}

sewers_regional_coins = {
        SMOLocationData.sewers_regional_coin_1: 3326,
        SMOLocationData.sewers_regional_coin_2: 3327,
        SMOLocationData.sewers_regional_coin_3: 3328,
        SMOLocationData.sewers_regional_coin_4: 3330,
        SMOLocationData.sewers_regional_coin_5: 3331,
        SMOLocationData.sewers_regional_coin_6: 3332,
        SMOLocationData.sewers_regional_coin_7: 3333,
}

city_hall_regional_coins = {
        SMOLocationData.city_hall_regional_coin_1: 3335,
        SMOLocationData.city_hall_regional_coin_2: 3336,
        SMOLocationData.city_hall_regional_coin_3: 3337,
        SMOLocationData.city_hall_regional_coin_4: 3339,
        SMOLocationData.city_hall_regional_coin_5: 3340,
        SMOLocationData.city_hall_regional_coin_6: 3342,
        SMOLocationData.city_hall_regional_coin_7: 3343,
        SMOLocationData.city_hall_regional_coin_8: 3344,
        SMOLocationData.city_hall_regional_coin_9: 3346,
        SMOLocationData.city_hall_regional_coin_10: 3347,
}

bullet_billding_regional_coins = {
        SMOLocationData.bullet_billding_regional_coin_1: 3349,
        SMOLocationData.bullet_billding_regional_coin_2: 3350,
        SMOLocationData.bullet_billding_regional_coin_3: 3351,
}

high_rise_regional_coins = {
        SMOLocationData.high_rise_regional_coin_1: 3353,
        SMOLocationData.high_rise_regional_coin_2: 3354,
        SMOLocationData.high_rise_regional_coin_3: 3355,
}

trex_escape_regional_coins = {
        SMOLocationData.trex_escape_regional_coin_1: 3357,
        SMOLocationData.trex_escape_regional_coin_2: 3358,
        SMOLocationData.trex_escape_regional_coin_3: 3359,
        SMOLocationData.trex_escape_regional_coin_4: 3361,
        SMOLocationData.trex_escape_regional_coin_5: 3362,
        SMOLocationData.trex_escape_regional_coin_6: 3363,
}

seaside_kingdom_regional_groups = {
        SMOLocationData.seaside_kingdom_regional_coin_group_1: 3364,
        SMOLocationData.seaside_kingdom_regional_coin_group_2: 3368,
        SMOLocationData.seaside_kingdom_regional_coin_group_3: 3372,
        SMOLocationData.seaside_kingdom_regional_coin_group_4: 3376,
        SMOLocationData.seaside_kingdom_regional_coin_group_5: 3380,
        SMOLocationData.seaside_kingdom_regional_coin_group_6: 3384,
        SMOLocationData.seaside_kingdom_regional_coin_group_7: 3388,
        SMOLocationData.seaside_kingdom_regional_coin_group_8: 3392,
        SMOLocationData.seaside_kingdom_regional_coin_group_9: 3396,
        SMOLocationData.seaside_kingdom_regional_coin_group_10: 3400,
        SMOLocationData.seaside_kingdom_regional_coin_group_11: 3405,
        SMOLocationData.seaside_kingdom_regional_coin_group_12: 3409,
        SMOLocationData.seaside_kingdom_regional_coin_group_13: 3413,
        SMOLocationData.seaside_kingdom_regional_coin_group_14: 3417,
        SMOLocationData.seaside_kingdom_regional_coin_group_15: 3421,
        SMOLocationData.seaside_kingdom_regional_coin_group_16: 3425,
        SMOLocationData.seaside_kingdom_regional_coin_group_17: 3429,
        SMOLocationData.seaside_kingdom_regional_coin_group_18: 3433,
        SMOLocationData.seaside_kingdom_regional_coin_group_19: 3437,
        SMOLocationData.seaside_kingdom_regional_coin_group_20: 3441,
        SMOLocationData.seaside_kingdom_regional_coin_group_21: 3445,
        SMOLocationData.seaside_kingdom_regional_coin_group_22: 3449,
        SMOLocationData.seaside_kingdom_regional_coin_group_23: 3453,
        SMOLocationData.seaside_kingdom_regional_coin_group_24: 3457,
        SMOLocationData.seaside_kingdom_regional_coin_group_25: 3461,
        SMOLocationData.seaside_kingdom_regional_coin_group_26: 3465,
        SMOLocationData.seaside_kingdom_regional_coin_group_27: 3469,
        SMOLocationData.seaside_kingdom_regional_coin_group_28: 3473,
        SMOLocationData.seaside_kingdom_regional_coin_group_29: 3477,
        SMOLocationData.seaside_kingdom_regional_coin_group_30: 3481,
        SMOLocationData.seaside_kingdom_regional_coin_group_31: 3485,
}

sea_cave_regional_groups = {
        SMOLocationData.sea_cave_regional_coin_group_1: 3489,
        SMOLocationData.sea_cave_regional_coin_group_2: 3493,
}

seaside_kingdom_regional_coins = {
        SMOLocationData.seaside_kingdom_regional_coin_1: 3365,
        SMOLocationData.seaside_kingdom_regional_coin_2: 3366,
        SMOLocationData.seaside_kingdom_regional_coin_3: 3367,
        SMOLocationData.seaside_kingdom_regional_coin_4: 3369,
        SMOLocationData.seaside_kingdom_regional_coin_5: 3370,
        SMOLocationData.seaside_kingdom_regional_coin_6: 3371,
        SMOLocationData.seaside_kingdom_regional_coin_7: 3373,
        SMOLocationData.seaside_kingdom_regional_coin_8: 3374,
        SMOLocationData.seaside_kingdom_regional_coin_9: 3375,
        SMOLocationData.seaside_kingdom_regional_coin_10: 3377,
        SMOLocationData.seaside_kingdom_regional_coin_11: 3378,
        SMOLocationData.seaside_kingdom_regional_coin_12: 3379,
        SMOLocationData.seaside_kingdom_regional_coin_13: 3381,
        SMOLocationData.seaside_kingdom_regional_coin_14: 3382,
        SMOLocationData.seaside_kingdom_regional_coin_15: 3383,
        SMOLocationData.seaside_kingdom_regional_coin_16: 3385,
        SMOLocationData.seaside_kingdom_regional_coin_17: 3386,
        SMOLocationData.seaside_kingdom_regional_coin_18: 3387,
        SMOLocationData.seaside_kingdom_regional_coin_19: 3389,
        SMOLocationData.seaside_kingdom_regional_coin_20: 3390,
        SMOLocationData.seaside_kingdom_regional_coin_21: 3391,
        SMOLocationData.seaside_kingdom_regional_coin_22: 3393,
        SMOLocationData.seaside_kingdom_regional_coin_23: 3394,
        SMOLocationData.seaside_kingdom_regional_coin_24: 3395,
        SMOLocationData.seaside_kingdom_regional_coin_25: 3397,
        SMOLocationData.seaside_kingdom_regional_coin_26: 3398,
        SMOLocationData.seaside_kingdom_regional_coin_27: 3399,
        SMOLocationData.seaside_kingdom_regional_coin_28: 3401,
        SMOLocationData.seaside_kingdom_regional_coin_29: 3402,
        SMOLocationData.seaside_kingdom_regional_coin_30: 3403,
        SMOLocationData.seaside_kingdom_regional_coin_31: 3404,
        SMOLocationData.seaside_kingdom_regional_coin_32: 3406,
        SMOLocationData.seaside_kingdom_regional_coin_33: 3407,
        SMOLocationData.seaside_kingdom_regional_coin_34: 3408,
        SMOLocationData.seaside_kingdom_regional_coin_35: 3410,
        SMOLocationData.seaside_kingdom_regional_coin_36: 3411,
        SMOLocationData.seaside_kingdom_regional_coin_37: 3412,
        SMOLocationData.seaside_kingdom_regional_coin_38: 3414,
        SMOLocationData.seaside_kingdom_regional_coin_39: 3415,
        SMOLocationData.seaside_kingdom_regional_coin_40: 3416,
        SMOLocationData.seaside_kingdom_regional_coin_41: 3418,
        SMOLocationData.seaside_kingdom_regional_coin_42: 3419,
        SMOLocationData.seaside_kingdom_regional_coin_43: 3420,
        SMOLocationData.seaside_kingdom_regional_coin_44: 3422,
        SMOLocationData.seaside_kingdom_regional_coin_45: 3423,
        SMOLocationData.seaside_kingdom_regional_coin_46: 3424,
        SMOLocationData.seaside_kingdom_regional_coin_47: 3426,
        SMOLocationData.seaside_kingdom_regional_coin_48: 3427,
        SMOLocationData.seaside_kingdom_regional_coin_49: 3428,
        SMOLocationData.seaside_kingdom_regional_coin_50: 3430,
        SMOLocationData.seaside_kingdom_regional_coin_51: 3431,
        SMOLocationData.seaside_kingdom_regional_coin_52: 3432,
        SMOLocationData.seaside_kingdom_regional_coin_53: 3434,
        SMOLocationData.seaside_kingdom_regional_coin_54: 3435,
        SMOLocationData.seaside_kingdom_regional_coin_55: 3436,
        SMOLocationData.seaside_kingdom_regional_coin_56: 3438,
        SMOLocationData.seaside_kingdom_regional_coin_57: 3439,
        SMOLocationData.seaside_kingdom_regional_coin_58: 3440,
        SMOLocationData.seaside_kingdom_regional_coin_59: 3442,
        SMOLocationData.seaside_kingdom_regional_coin_60: 3443,
        SMOLocationData.seaside_kingdom_regional_coin_61: 3444,
        SMOLocationData.seaside_kingdom_regional_coin_62: 3446,
        SMOLocationData.seaside_kingdom_regional_coin_63: 3447,
        SMOLocationData.seaside_kingdom_regional_coin_64: 3448,
        SMOLocationData.seaside_kingdom_regional_coin_65: 3450,
        SMOLocationData.seaside_kingdom_regional_coin_66: 3451,
        SMOLocationData.seaside_kingdom_regional_coin_67: 3452,
        SMOLocationData.seaside_kingdom_regional_coin_68: 3454,
        SMOLocationData.seaside_kingdom_regional_coin_69: 3455,
        SMOLocationData.seaside_kingdom_regional_coin_70: 3456,
        SMOLocationData.seaside_kingdom_regional_coin_71: 3458,
        SMOLocationData.seaside_kingdom_regional_coin_72: 3459,
        SMOLocationData.seaside_kingdom_regional_coin_73: 3460,
        SMOLocationData.seaside_kingdom_regional_coin_74: 3462,
        SMOLocationData.seaside_kingdom_regional_coin_75: 3463,
        SMOLocationData.seaside_kingdom_regional_coin_76: 3464,
        SMOLocationData.seaside_kingdom_regional_coin_77: 3466,
        SMOLocationData.seaside_kingdom_regional_coin_78: 3467,
        SMOLocationData.seaside_kingdom_regional_coin_79: 3468,
        SMOLocationData.seaside_kingdom_regional_coin_80: 3470,
        SMOLocationData.seaside_kingdom_regional_coin_81: 3471,
        SMOLocationData.seaside_kingdom_regional_coin_82: 3472,
        SMOLocationData.seaside_kingdom_regional_coin_83: 3474,
        SMOLocationData.seaside_kingdom_regional_coin_84: 3475,
        SMOLocationData.seaside_kingdom_regional_coin_85: 3476,
        SMOLocationData.seaside_kingdom_regional_coin_86: 3478,
        SMOLocationData.seaside_kingdom_regional_coin_87: 3479,
        SMOLocationData.seaside_kingdom_regional_coin_88: 3480,
        SMOLocationData.seaside_kingdom_regional_coin_89: 3482,
        SMOLocationData.seaside_kingdom_regional_coin_90: 3483,
        SMOLocationData.seaside_kingdom_regional_coin_91: 3484,
        SMOLocationData.seaside_kingdom_regional_coin_92: 3486,
        SMOLocationData.seaside_kingdom_regional_coin_93: 3487,
        SMOLocationData.seaside_kingdom_regional_coin_94: 3488,
}

sea_cave_regional_coins = {
        SMOLocationData.sea_cave_regional_coin_1: 3490,
        SMOLocationData.sea_cave_regional_coin_2: 3491,
        SMOLocationData.sea_cave_regional_coin_3: 3492,
        SMOLocationData.sea_cave_regional_coin_4: 3494,
        SMOLocationData.sea_cave_regional_coin_5: 3495,
        SMOLocationData.sea_cave_regional_coin_6: 3496,
}

snow_kingdom_regional_groups = {
        SMOLocationData.snow_kingdom_regional_coin_group_1: 3497,
        SMOLocationData.snow_kingdom_regional_coin_group_2: 3500,
        SMOLocationData.snow_kingdom_regional_coin_group_3: 3503,
        SMOLocationData.snow_kingdom_regional_coin_group_4: 3507,
        SMOLocationData.snow_kingdom_regional_coin_group_5: 3510,
        SMOLocationData.snow_kingdom_regional_coin_group_6: 3513,
}

shiveria_regional_groups = {
        SMOLocationData.shiveria_regional_coin_group_1: 3516,
        SMOLocationData.shiveria_regional_coin_group_2: 3521,
        SMOLocationData.shiveria_regional_coin_group_3: 3525,
        SMOLocationData.shiveria_regional_coin_group_4: 3529,
        SMOLocationData.shiveria_regional_coin_group_5: 3534,
        SMOLocationData.shiveria_regional_coin_group_6: 3538,
        SMOLocationData.shiveria_regional_coin_group_7: 3542,
        SMOLocationData.shiveria_regional_coin_group_8: 3546,
        SMOLocationData.shiveria_regional_coin_group_9: 3551,
}

snowline_regional_groups = {
        SMOLocationData.snowline_regional_coin_group_1: 3555,
        SMOLocationData.snowline_regional_coin_group_2: 3560,
}

snow_kingdom_regional_coins = {
        SMOLocationData.snow_kingdom_regional_coin_1: 3498,
        SMOLocationData.snow_kingdom_regional_coin_2: 3499,
        SMOLocationData.snow_kingdom_regional_coin_3: 3501,
        SMOLocationData.snow_kingdom_regional_coin_4: 3502,
        SMOLocationData.snow_kingdom_regional_coin_5: 3504,
        SMOLocationData.snow_kingdom_regional_coin_6: 3505,
        SMOLocationData.snow_kingdom_regional_coin_7: 3506,
        SMOLocationData.snow_kingdom_regional_coin_8: 3508,
        SMOLocationData.snow_kingdom_regional_coin_9: 3509,
        SMOLocationData.snow_kingdom_regional_coin_10: 3511,
        SMOLocationData.snow_kingdom_regional_coin_11: 3512,
        SMOLocationData.snow_kingdom_regional_coin_12: 3514,
        SMOLocationData.snow_kingdom_regional_coin_13: 3515,
}

shiveria_regional_coins = {
        SMOLocationData.shiveria_regional_coin_1: 3517,
        SMOLocationData.shiveria_regional_coin_2: 3518,
        SMOLocationData.shiveria_regional_coin_3: 3519,
        SMOLocationData.shiveria_regional_coin_4: 3520,
        SMOLocationData.shiveria_regional_coin_5: 3522,
        SMOLocationData.shiveria_regional_coin_6: 3523,
        SMOLocationData.shiveria_regional_coin_7: 3524,
        SMOLocationData.shiveria_regional_coin_8: 3526,
        SMOLocationData.shiveria_regional_coin_9: 3527,
        SMOLocationData.shiveria_regional_coin_10: 3528,
        SMOLocationData.shiveria_regional_coin_11: 3530,
        SMOLocationData.shiveria_regional_coin_12: 3531,
        SMOLocationData.shiveria_regional_coin_13: 3532,
        SMOLocationData.shiveria_regional_coin_14: 3533,
        SMOLocationData.shiveria_regional_coin_15: 3535,
        SMOLocationData.shiveria_regional_coin_16: 3536,
        SMOLocationData.shiveria_regional_coin_17: 3537,
        SMOLocationData.shiveria_regional_coin_18: 3539,
        SMOLocationData.shiveria_regional_coin_19: 3540,
        SMOLocationData.shiveria_regional_coin_20: 3541,
        SMOLocationData.shiveria_regional_coin_21: 3543,
        SMOLocationData.shiveria_regional_coin_22: 3544,
        SMOLocationData.shiveria_regional_coin_23: 3545,
        SMOLocationData.shiveria_regional_coin_24: 3547,
        SMOLocationData.shiveria_regional_coin_25: 3548,
        SMOLocationData.shiveria_regional_coin_26: 3549,
        SMOLocationData.shiveria_regional_coin_27: 3550,
        SMOLocationData.shiveria_regional_coin_28: 3552,
        SMOLocationData.shiveria_regional_coin_29: 3553,
        SMOLocationData.shiveria_regional_coin_30: 3554,

}

snowline_regional_coins = {
        SMOLocationData.snowline_regional_coin_1: 3556,
        SMOLocationData.snowline_regional_coin_2: 3557,
        SMOLocationData.snowline_regional_coin_3: 3558,
        SMOLocationData.snowline_regional_coin_4: 3559,
        SMOLocationData.snowline_regional_coin_5: 3561,
        SMOLocationData.snowline_regional_coin_6: 3562,
        SMOLocationData.snowline_regional_coin_7: 3563,
}

luncheon_kingdom_regional_groups = {
        SMOLocationData.luncheon_kingdom_regional_coin_group_1: 3564,
        SMOLocationData.luncheon_kingdom_regional_coin_group_2: 3568,
        SMOLocationData.luncheon_kingdom_regional_coin_group_3: 3572,
        SMOLocationData.luncheon_kingdom_regional_coin_group_4: 3576,
        SMOLocationData.luncheon_kingdom_regional_coin_group_5: 3580,
        SMOLocationData.luncheon_kingdom_regional_coin_group_6: 3584,
        SMOLocationData.luncheon_kingdom_regional_coin_group_7: 3588,
        SMOLocationData.luncheon_kingdom_regional_coin_group_8: 3591,
        SMOLocationData.luncheon_kingdom_regional_coin_group_9: 3599,
        SMOLocationData.luncheon_kingdom_regional_coin_group_10: 3603,
        SMOLocationData.luncheon_kingdom_regional_coin_group_11: 3607,
        SMOLocationData.luncheon_kingdom_regional_coin_group_12: 3613,
        SMOLocationData.luncheon_kingdom_regional_coin_group_13: 3617,
        SMOLocationData.luncheon_kingdom_regional_coin_group_14: 3622,
        SMOLocationData.luncheon_kingdom_regional_coin_group_15: 3626,
        SMOLocationData.luncheon_kingdom_regional_coin_group_16: 3630,
        SMOLocationData.luncheon_kingdom_regional_coin_group_17: 3634,
        SMOLocationData.luncheon_kingdom_regional_coin_group_18: 3638,
        SMOLocationData.luncheon_kingdom_regional_coin_group_19: 3642,
        SMOLocationData.luncheon_kingdom_regional_coin_group_20: 3646,
}

luncheon_kingdom_post_meat_regional_groups = {
        SMOLocationData.luncheon_kingdom_regional_coin_group_21: 3650,
        SMOLocationData.luncheon_kingdom_regional_coin_group_22: 3655,
        SMOLocationData.luncheon_kingdom_regional_coin_group_23: 3659,
        SMOLocationData.luncheon_kingdom_regional_coin_group_24: 3663,
        SMOLocationData.luncheon_kingdom_regional_coin_group_25: 3667,
        SMOLocationData.luncheon_kingdom_regional_coin_group_26: 3671,
}

cascading_magma_regional_groups = {
        SMOLocationData.cascading_magma_regional_coin_group_1: 3675,
        SMOLocationData.cascading_magma_regional_coin_group_2: 3679,
}

magma_narrow_path_regional_groups = {
        SMOLocationData.magma_narrow_path_regional_coin_group_1: 3683,
}

spinning_athletics_regional_groups = {
        SMOLocationData.spinning_athletics_regional_coin_group_1: 3687,
}

fork_flickin_regional_groups = {
        SMOLocationData.fork_flickin_regional_coin_group_1: 3691,
}

luncheon_kingdom_regional_coins = {
        SMOLocationData.luncheon_kingdom_regional_coin_1: 3565,
        SMOLocationData.luncheon_kingdom_regional_coin_2: 3566,
        SMOLocationData.luncheon_kingdom_regional_coin_3: 3567,
        SMOLocationData.luncheon_kingdom_regional_coin_4: 3569,
        SMOLocationData.luncheon_kingdom_regional_coin_5: 3570,
        SMOLocationData.luncheon_kingdom_regional_coin_6: 3571,
        SMOLocationData.luncheon_kingdom_regional_coin_7: 3573,
        SMOLocationData.luncheon_kingdom_regional_coin_8: 3574,
        SMOLocationData.luncheon_kingdom_regional_coin_9: 3575,
        SMOLocationData.luncheon_kingdom_regional_coin_10: 3577,
        SMOLocationData.luncheon_kingdom_regional_coin_11: 3578,
        SMOLocationData.luncheon_kingdom_regional_coin_12: 3579,
        SMOLocationData.luncheon_kingdom_regional_coin_13: 3581,
        SMOLocationData.luncheon_kingdom_regional_coin_14: 3582,
        SMOLocationData.luncheon_kingdom_regional_coin_15: 3583,
        SMOLocationData.luncheon_kingdom_regional_coin_16: 3585,
        SMOLocationData.luncheon_kingdom_regional_coin_17: 3586,
        SMOLocationData.luncheon_kingdom_regional_coin_18: 3587,
        SMOLocationData.luncheon_kingdom_regional_coin_19: 3589,
        SMOLocationData.luncheon_kingdom_regional_coin_20: 3590,
        SMOLocationData.luncheon_kingdom_regional_coin_21: 3592,
        SMOLocationData.luncheon_kingdom_regional_coin_22: 3593,
        SMOLocationData.luncheon_kingdom_regional_coin_23: 3594,
        SMOLocationData.luncheon_kingdom_regional_coin_24: 3595,
        SMOLocationData.luncheon_kingdom_regional_coin_25: 3596,
        SMOLocationData.luncheon_kingdom_regional_coin_26: 3597,
        SMOLocationData.luncheon_kingdom_regional_coin_27: 3598,
        SMOLocationData.luncheon_kingdom_regional_coin_28: 3600,
        SMOLocationData.luncheon_kingdom_regional_coin_29: 3601,
        SMOLocationData.luncheon_kingdom_regional_coin_30: 3602,
        SMOLocationData.luncheon_kingdom_regional_coin_31: 3604,
        SMOLocationData.luncheon_kingdom_regional_coin_32: 3605,
        SMOLocationData.luncheon_kingdom_regional_coin_33: 3606,
        SMOLocationData.luncheon_kingdom_regional_coin_34: 3608,
        SMOLocationData.luncheon_kingdom_regional_coin_35: 3609,
        SMOLocationData.luncheon_kingdom_regional_coin_36: 3610,
        SMOLocationData.luncheon_kingdom_regional_coin_37: 3611,
        SMOLocationData.luncheon_kingdom_regional_coin_38: 3612,
        SMOLocationData.luncheon_kingdom_regional_coin_39: 3614,
        SMOLocationData.luncheon_kingdom_regional_coin_40: 3615,
        SMOLocationData.luncheon_kingdom_regional_coin_41: 3616,
        SMOLocationData.luncheon_kingdom_regional_coin_42: 3618,
        SMOLocationData.luncheon_kingdom_regional_coin_43: 3619,
        SMOLocationData.luncheon_kingdom_regional_coin_44: 3620,
        SMOLocationData.luncheon_kingdom_regional_coin_45: 3621,
        SMOLocationData.luncheon_kingdom_regional_coin_46: 3623,
        SMOLocationData.luncheon_kingdom_regional_coin_47: 3624,
        SMOLocationData.luncheon_kingdom_regional_coin_48: 3625,
        SMOLocationData.luncheon_kingdom_regional_coin_49: 3627,
        SMOLocationData.luncheon_kingdom_regional_coin_50: 3628,
        SMOLocationData.luncheon_kingdom_regional_coin_51: 3629,
        SMOLocationData.luncheon_kingdom_regional_coin_52: 3631,
        SMOLocationData.luncheon_kingdom_regional_coin_53: 3632,
        SMOLocationData.luncheon_kingdom_regional_coin_54: 3633,
        SMOLocationData.luncheon_kingdom_regional_coin_55: 3635,
        SMOLocationData.luncheon_kingdom_regional_coin_56: 3636,
        SMOLocationData.luncheon_kingdom_regional_coin_57: 3637,
        SMOLocationData.luncheon_kingdom_regional_coin_58: 3639,
        SMOLocationData.luncheon_kingdom_regional_coin_59: 3640,
        SMOLocationData.luncheon_kingdom_regional_coin_60: 3641,
        SMOLocationData.luncheon_kingdom_regional_coin_61: 3643,
        SMOLocationData.luncheon_kingdom_regional_coin_62: 3644,
        SMOLocationData.luncheon_kingdom_regional_coin_63: 3645,
        SMOLocationData.luncheon_kingdom_regional_coin_64: 3647,
        SMOLocationData.luncheon_kingdom_regional_coin_65: 3648,
        SMOLocationData.luncheon_kingdom_regional_coin_66: 3649,
}


luncheon_kingdom_post_meat_regional_coins = {
        SMOLocationData.luncheon_kingdom_regional_coin_67: 3651,
        SMOLocationData.luncheon_kingdom_regional_coin_68: 3652,
        SMOLocationData.luncheon_kingdom_regional_coin_69: 3653,
        SMOLocationData.luncheon_kingdom_regional_coin_70: 3654,
        SMOLocationData.luncheon_kingdom_regional_coin_71: 3656,
        SMOLocationData.luncheon_kingdom_regional_coin_72: 3657,
        SMOLocationData.luncheon_kingdom_regional_coin_73: 3658,
        SMOLocationData.luncheon_kingdom_regional_coin_74: 3660,
        SMOLocationData.luncheon_kingdom_regional_coin_75: 3661,
        SMOLocationData.luncheon_kingdom_regional_coin_76: 3662,
        SMOLocationData.luncheon_kingdom_regional_coin_77: 3664,
        SMOLocationData.luncheon_kingdom_regional_coin_78: 3665,
        SMOLocationData.luncheon_kingdom_regional_coin_79: 3666,
        SMOLocationData.luncheon_kingdom_regional_coin_80: 3668,
        SMOLocationData.luncheon_kingdom_regional_coin_81: 3669,
        SMOLocationData.luncheon_kingdom_regional_coin_82: 3670,
        SMOLocationData.luncheon_kingdom_regional_coin_83: 3672,
        SMOLocationData.luncheon_kingdom_regional_coin_84: 3673,
        SMOLocationData.luncheon_kingdom_regional_coin_85: 3674,
}

cascading_magma_regional_coins = {
        SMOLocationData.cascading_magma_regional_coin_1: 3676,
        SMOLocationData.cascading_magma_regional_coin_2: 3677,
        SMOLocationData.cascading_magma_regional_coin_3: 3678,
        SMOLocationData.cascading_magma_regional_coin_4: 3680,
        SMOLocationData.cascading_magma_regional_coin_5: 3681,
        SMOLocationData.cascading_magma_regional_coin_6: 3682,
}

magma_narrow_path_regional_coins = {
        SMOLocationData.magma_narrow_path_regional_coin_1: 3684,
        SMOLocationData.magma_narrow_path_regional_coin_2: 3685,
        SMOLocationData.magma_narrow_path_regional_coin_3: 3686,
}

spinning_athletics_regional_coins = {
        SMOLocationData.spinning_athletics_regional_coin_1: 3688,
        SMOLocationData.spinning_athletics_regional_coin_2: 3689,
        SMOLocationData.spinning_athletics_regional_coin_3: 3690,
}

fork_flickin_regional_coins = {
        SMOLocationData.fork_flickin_regional_coin_1: 3692,
        SMOLocationData.fork_flickin_regional_coin_2: 3693,
        SMOLocationData.fork_flickin_regional_coin_3: 3694,
}

bowsers_kingdom_regional_groups = {
        SMOLocationData.bowsers_kingdom_regional_coin_group_1: 3695,
        SMOLocationData.bowsers_kingdom_regional_coin_group_2: 3699,
        SMOLocationData.bowsers_kingdom_regional_coin_group_3: 3703,
        SMOLocationData.bowsers_kingdom_regional_coin_group_4: 3707,
        SMOLocationData.bowsers_kingdom_regional_coin_group_5: 3711,
        SMOLocationData.bowsers_kingdom_regional_coin_group_6: 3715,
        SMOLocationData.bowsers_kingdom_regional_coin_group_7: 3720,
        SMOLocationData.bowsers_kingdom_regional_coin_group_8: 3724,
        SMOLocationData.bowsers_kingdom_regional_coin_group_9: 3728,
        SMOLocationData.bowsers_kingdom_regional_coin_group_10: 3732,
        SMOLocationData.bowsers_kingdom_regional_coin_group_11: 3736,
        SMOLocationData.bowsers_kingdom_regional_coin_group_12: 3740,
        SMOLocationData.bowsers_kingdom_regional_coin_group_20: 3772,
        SMOLocationData.bowsers_kingdom_regional_coin_group_21: 3777,
        SMOLocationData.bowsers_kingdom_regional_coin_group_22: 3782,
        SMOLocationData.bowsers_kingdom_regional_coin_group_23: 3786,
        SMOLocationData.bowsers_kingdom_regional_coin_group_24: 3791,
        SMOLocationData.bowsers_kingdom_regional_coin_group_25: 3794,
        SMOLocationData.bowsers_kingdom_regional_coin_group_26: 3797,
        SMOLocationData.bowsers_kingdom_regional_coin_group_27: 3801,
        SMOLocationData.bowsers_kingdom_regional_coin_group_28: 3805,
        SMOLocationData.bowsers_kingdom_regional_coin_group_29: 3809,
        SMOLocationData.bowsers_kingdom_regional_coin_group_30: 3813,
        SMOLocationData.bowsers_kingdom_regional_coin_group_31: 3818,
        SMOLocationData.bowsers_kingdom_regional_coin_group_32: 3822,
}

bowsers_kingdom_peace_regional_groups = {
        SMOLocationData.bowsers_kingdom_regional_coin_group_13: 3744,
        SMOLocationData.bowsers_kingdom_regional_coin_group_14: 3748,
        SMOLocationData.bowsers_kingdom_regional_coin_group_15: 3752,
        SMOLocationData.bowsers_kingdom_regional_coin_group_16: 3756,
        SMOLocationData.bowsers_kingdom_regional_coin_group_17: 3760,
        SMOLocationData.bowsers_kingdom_regional_coin_group_18: 3764,
        SMOLocationData.bowsers_kingdom_regional_coin_group_19: 3768,
}

bowsers_kingdom_regional_coins = {
        SMOLocationData.bowsers_kingdom_regional_coin_1: 3696,
        SMOLocationData.bowsers_kingdom_regional_coin_2: 3697,
        SMOLocationData.bowsers_kingdom_regional_coin_3: 3698,
        SMOLocationData.bowsers_kingdom_regional_coin_4: 3700,
        SMOLocationData.bowsers_kingdom_regional_coin_5: 3701,
        SMOLocationData.bowsers_kingdom_regional_coin_6: 3702,
        SMOLocationData.bowsers_kingdom_regional_coin_7: 3704,
        SMOLocationData.bowsers_kingdom_regional_coin_8: 3705,
        SMOLocationData.bowsers_kingdom_regional_coin_9: 3706,
        SMOLocationData.bowsers_kingdom_regional_coin_10: 3708,
        SMOLocationData.bowsers_kingdom_regional_coin_11: 3709,
        SMOLocationData.bowsers_kingdom_regional_coin_12: 3710,
        SMOLocationData.bowsers_kingdom_regional_coin_13: 3712,
        SMOLocationData.bowsers_kingdom_regional_coin_14: 3713,
        SMOLocationData.bowsers_kingdom_regional_coin_15: 3714,
        SMOLocationData.bowsers_kingdom_regional_coin_16: 3716,
        SMOLocationData.bowsers_kingdom_regional_coin_17: 3717,
        SMOLocationData.bowsers_kingdom_regional_coin_18: 3718,
        SMOLocationData.bowsers_kingdom_regional_coin_19: 3719,
        SMOLocationData.bowsers_kingdom_regional_coin_20: 3721,
        SMOLocationData.bowsers_kingdom_regional_coin_21: 3722,
        SMOLocationData.bowsers_kingdom_regional_coin_22: 3723,
        SMOLocationData.bowsers_kingdom_regional_coin_23: 3725,
        SMOLocationData.bowsers_kingdom_regional_coin_24: 3726,
        SMOLocationData.bowsers_kingdom_regional_coin_25: 3727,
        SMOLocationData.bowsers_kingdom_regional_coin_26: 3729,
        SMOLocationData.bowsers_kingdom_regional_coin_27: 3730,
        SMOLocationData.bowsers_kingdom_regional_coin_28: 3731,
        SMOLocationData.bowsers_kingdom_regional_coin_29: 3733,
        SMOLocationData.bowsers_kingdom_regional_coin_30: 3734,
        SMOLocationData.bowsers_kingdom_regional_coin_31: 3735,
        SMOLocationData.bowsers_kingdom_regional_coin_32: 3737,
        SMOLocationData.bowsers_kingdom_regional_coin_33: 3738,
        SMOLocationData.bowsers_kingdom_regional_coin_34: 3739,
        SMOLocationData.bowsers_kingdom_regional_coin_35: 3741,
        SMOLocationData.bowsers_kingdom_regional_coin_36: 3742,
        SMOLocationData.bowsers_kingdom_regional_coin_37: 3743,
        SMOLocationData.bowsers_kingdom_regional_coin_59: 3773,
        SMOLocationData.bowsers_kingdom_regional_coin_60: 3774,
        SMOLocationData.bowsers_kingdom_regional_coin_61: 3775,
        SMOLocationData.bowsers_kingdom_regional_coin_62: 3776,
        SMOLocationData.bowsers_kingdom_regional_coin_63: 3778,
        SMOLocationData.bowsers_kingdom_regional_coin_64: 3779,
        SMOLocationData.bowsers_kingdom_regional_coin_65: 3780,
        SMOLocationData.bowsers_kingdom_regional_coin_66: 3781,
        SMOLocationData.bowsers_kingdom_regional_coin_67: 3783,
        SMOLocationData.bowsers_kingdom_regional_coin_68: 3784,
        SMOLocationData.bowsers_kingdom_regional_coin_69: 3785,
        SMOLocationData.bowsers_kingdom_regional_coin_70: 3787,
        SMOLocationData.bowsers_kingdom_regional_coin_71: 3788,
        SMOLocationData.bowsers_kingdom_regional_coin_72: 3789,
        SMOLocationData.bowsers_kingdom_regional_coin_73: 3790,
        SMOLocationData.bowsers_kingdom_regional_coin_74: 3792,
        SMOLocationData.bowsers_kingdom_regional_coin_75: 3793,
        SMOLocationData.bowsers_kingdom_regional_coin_76: 3795,
        SMOLocationData.bowsers_kingdom_regional_coin_77: 3796,
        SMOLocationData.bowsers_kingdom_regional_coin_78: 3798,
        SMOLocationData.bowsers_kingdom_regional_coin_79: 3799,
        SMOLocationData.bowsers_kingdom_regional_coin_80: 3800,
        SMOLocationData.bowsers_kingdom_regional_coin_81: 3802,
        SMOLocationData.bowsers_kingdom_regional_coin_82: 3803,
        SMOLocationData.bowsers_kingdom_regional_coin_83: 3804,
        SMOLocationData.bowsers_kingdom_regional_coin_84: 3806,
        SMOLocationData.bowsers_kingdom_regional_coin_85: 3807,
        SMOLocationData.bowsers_kingdom_regional_coin_86: 3808,
        SMOLocationData.bowsers_kingdom_regional_coin_87: 3810,
        SMOLocationData.bowsers_kingdom_regional_coin_88: 3811,
        SMOLocationData.bowsers_kingdom_regional_coin_89: 3812,
        SMOLocationData.bowsers_kingdom_regional_coin_90: 3814,
        SMOLocationData.bowsers_kingdom_regional_coin_91: 3815,
        SMOLocationData.bowsers_kingdom_regional_coin_92: 3816,
        SMOLocationData.bowsers_kingdom_regional_coin_93: 3817,
        SMOLocationData.bowsers_kingdom_regional_coin_94: 3819,
        SMOLocationData.bowsers_kingdom_regional_coin_95: 3820,
        SMOLocationData.bowsers_kingdom_regional_coin_96: 3821,
        SMOLocationData.bowsers_kingdom_regional_coin_97: 3823,
        SMOLocationData.bowsers_kingdom_regional_coin_98: 3824,
        SMOLocationData.bowsers_kingdom_regional_coin_99: 3825,
        SMOLocationData.bowsers_kingdom_regional_coin_100: 3826,
}

bowsers_kingdom_peace_regional_coins = {
        SMOLocationData.bowsers_kingdom_regional_coin_38: 3745,
        SMOLocationData.bowsers_kingdom_regional_coin_39: 3746,
        SMOLocationData.bowsers_kingdom_regional_coin_40: 3747,
        SMOLocationData.bowsers_kingdom_regional_coin_41: 3749,
        SMOLocationData.bowsers_kingdom_regional_coin_42: 3750,
        SMOLocationData.bowsers_kingdom_regional_coin_43: 3751,
        SMOLocationData.bowsers_kingdom_regional_coin_44: 3753,
        SMOLocationData.bowsers_kingdom_regional_coin_45: 3754,
        SMOLocationData.bowsers_kingdom_regional_coin_46: 3755,
        SMOLocationData.bowsers_kingdom_regional_coin_47: 3757,
        SMOLocationData.bowsers_kingdom_regional_coin_48: 3758,
        SMOLocationData.bowsers_kingdom_regional_coin_49: 3759,
        SMOLocationData.bowsers_kingdom_regional_coin_50: 3761,
        SMOLocationData.bowsers_kingdom_regional_coin_51: 3762,
        SMOLocationData.bowsers_kingdom_regional_coin_52: 3763,
        SMOLocationData.bowsers_kingdom_regional_coin_53: 3765,
        SMOLocationData.bowsers_kingdom_regional_coin_54: 3766,
        SMOLocationData.bowsers_kingdom_regional_coin_55: 3767,
        SMOLocationData.bowsers_kingdom_regional_coin_56: 3769,
        SMOLocationData.bowsers_kingdom_regional_coin_57: 3770,
        SMOLocationData.bowsers_kingdom_regional_coin_58: 3771,
}

moon_kingdom_regional_groups = {
        SMOLocationData.moon_kingdom_regional_coin_group_1: 3827,
        SMOLocationData.moon_kingdom_regional_coin_group_2: 3831,
        SMOLocationData.moon_kingdom_regional_coin_group_3: 3836,
        SMOLocationData.moon_kingdom_regional_coin_group_4: 3843,
        SMOLocationData.moon_kingdom_regional_coin_group_5: 3847,
        SMOLocationData.moon_kingdom_regional_coin_group_6: 3852,
        SMOLocationData.moon_kingdom_regional_coin_group_7: 3856,
        SMOLocationData.moon_kingdom_regional_coin_group_8: 3860,
        SMOLocationData.moon_kingdom_regional_coin_group_9: 3864,
}

moon_cave_regional_groups = {
        SMOLocationData.moon_cave_regional_coin_group_1: 3868,
        SMOLocationData.moon_cave_regional_coin_group_2: 3872,
        SMOLocationData.moon_cave_regional_coin_group_3: 3876,
        SMOLocationData.moon_cave_regional_coin_group_4: 3880,
        SMOLocationData.moon_cave_regional_coin_group_5: 3884,
        SMOLocationData.moon_cave_regional_coin_group_6: 3888,
}

moon_kingdom_regional_coins = {
        SMOLocationData.moon_kingdom_regional_coin_1: 3828,
        SMOLocationData.moon_kingdom_regional_coin_2: 3829,
        SMOLocationData.moon_kingdom_regional_coin_3: 3830,
        SMOLocationData.moon_kingdom_regional_coin_4: 3832,
        SMOLocationData.moon_kingdom_regional_coin_5: 3833,
        SMOLocationData.moon_kingdom_regional_coin_6: 3834,
        SMOLocationData.moon_kingdom_regional_coin_7: 3835,
        SMOLocationData.moon_kingdom_regional_coin_8: 3837,
        SMOLocationData.moon_kingdom_regional_coin_9: 3838,
        SMOLocationData.moon_kingdom_regional_coin_10: 3839,
        SMOLocationData.moon_kingdom_regional_coin_11: 3840,
        SMOLocationData.moon_kingdom_regional_coin_12: 3841,
        SMOLocationData.moon_kingdom_regional_coin_13: 3842,
        SMOLocationData.moon_kingdom_regional_coin_14: 3844,
        SMOLocationData.moon_kingdom_regional_coin_15: 3845,
        SMOLocationData.moon_kingdom_regional_coin_16: 3846,
        SMOLocationData.moon_kingdom_regional_coin_17: 3848,
        SMOLocationData.moon_kingdom_regional_coin_18: 3849,
        SMOLocationData.moon_kingdom_regional_coin_19: 3850,
        SMOLocationData.moon_kingdom_regional_coin_20: 3851,
        SMOLocationData.moon_kingdom_regional_coin_21: 3853,
        SMOLocationData.moon_kingdom_regional_coin_22: 3854,
        SMOLocationData.moon_kingdom_regional_coin_23: 3855,
        SMOLocationData.moon_kingdom_regional_coin_24: 3857,
        SMOLocationData.moon_kingdom_regional_coin_25: 3858,
        SMOLocationData.moon_kingdom_regional_coin_26: 3859,
        SMOLocationData.moon_kingdom_regional_coin_27: 3861,
        SMOLocationData.moon_kingdom_regional_coin_28: 3862,
        SMOLocationData.moon_kingdom_regional_coin_29: 3863,
        SMOLocationData.moon_kingdom_regional_coin_30: 3865,
        SMOLocationData.moon_kingdom_regional_coin_31: 3866,
        SMOLocationData.moon_kingdom_regional_coin_32: 3867,
}

moon_cave_regional_coins = {
        SMOLocationData.moon_cave_regional_coin_1: 3869,
        SMOLocationData.moon_cave_regional_coin_2: 3870,
        SMOLocationData.moon_cave_regional_coin_3: 3871,
        SMOLocationData.moon_cave_regional_coin_4: 3873,
        SMOLocationData.moon_cave_regional_coin_5: 3874,
        SMOLocationData.moon_cave_regional_coin_6: 3875,
        SMOLocationData.moon_cave_regional_coin_7: 3877,
        SMOLocationData.moon_cave_regional_coin_8: 3878,
        SMOLocationData.moon_cave_regional_coin_9: 3879,
        SMOLocationData.moon_cave_regional_coin_10: 3881,
        SMOLocationData.moon_cave_regional_coin_11: 3882,
        SMOLocationData.moon_cave_regional_coin_12: 3883,
        SMOLocationData.moon_cave_regional_coin_13: 3885,
        SMOLocationData.moon_cave_regional_coin_14: 3886,
        SMOLocationData.moon_cave_regional_coin_15: 3887,
        SMOLocationData.moon_cave_regional_coin_16: 3889,
        SMOLocationData.moon_cave_regional_coin_17: 3890,
        SMOLocationData.moon_cave_regional_coin_18: 3891,
}

mushroom_kingdom_regional_groups = {
        SMOLocationData.mushroom_kingdom_regional_coin_group_1: 3896,
        SMOLocationData.mushroom_kingdom_regional_coin_group_2: 3901,
        SMOLocationData.mushroom_kingdom_regional_coin_group_3: 3905,
        SMOLocationData.mushroom_kingdom_regional_coin_group_4: 3909,
        SMOLocationData.mushroom_kingdom_regional_coin_group_5: 3914,
        SMOLocationData.mushroom_kingdom_regional_coin_group_6: 3918,
        SMOLocationData.mushroom_kingdom_regional_coin_group_7: 3922,
        SMOLocationData.mushroom_kingdom_regional_coin_group_8: 3926,
        SMOLocationData.mushroom_kingdom_regional_coin_group_9: 3930,
        SMOLocationData.mushroom_kingdom_regional_coin_group_10: 3934,
        SMOLocationData.mushroom_kingdom_regional_coin_group_11: 3938,
        SMOLocationData.mushroom_kingdom_regional_coin_group_12: 3942,
        SMOLocationData.mushroom_kingdom_regional_coin_group_13: 3946,
        SMOLocationData.mushroom_kingdom_regional_coin_group_14: 3950,
        SMOLocationData.mushroom_kingdom_regional_coin_group_15: 3954,
        SMOLocationData.mushroom_kingdom_regional_coin_group_16: 3958,
        SMOLocationData.mushroom_kingdom_regional_coin_group_17: 3962,
        SMOLocationData.mushroom_kingdom_regional_coin_group_18: 3967,
        SMOLocationData.mushroom_kingdom_regional_coin_group_19: 3971,
        SMOLocationData.mushroom_kingdom_regional_coin_group_20: 3974,
        SMOLocationData.mushroom_kingdom_regional_coin_group_21: 3977,
        SMOLocationData.mushroom_kingdom_regional_coin_group_22: 3981,
        SMOLocationData.mushroom_kingdom_regional_coin_group_23: 3985,
        SMOLocationData.mushroom_kingdom_regional_coin_group_24: 3989,
        SMOLocationData.mushroom_kingdom_regional_coin_group_25: 3993,
        SMOLocationData.mushroom_kingdom_regional_coin_group_26: 3997,
        SMOLocationData.mushroom_kingdom_regional_coin_group_27: 4001,
        SMOLocationData.mushroom_kingdom_regional_coin_group_28: 4005,
        SMOLocationData.mushroom_kingdom_regional_coin_group_29: 4009,
        SMOLocationData.mushroom_kingdom_regional_coin_group_30: 4013,
        SMOLocationData.mushroom_kingdom_regional_coin_group_31: 4017,
        SMOLocationData.mushroom_kingdom_regional_coin_group_32: 4021,
}

peachs_castle_regional_groups = {
        SMOLocationData.mushroom_kingdom_regional_coin_group_1: 3892,
}

mushroom_kingdom_regional_coins = {
        SMOLocationData.mushroom_kingdom_regional_coin_1: 3897,
        SMOLocationData.mushroom_kingdom_regional_coin_2: 3898,
        SMOLocationData.mushroom_kingdom_regional_coin_3: 3899,
        SMOLocationData.mushroom_kingdom_regional_coin_4: 3900,
        SMOLocationData.mushroom_kingdom_regional_coin_5: 3902,
        SMOLocationData.mushroom_kingdom_regional_coin_6: 3903,
        SMOLocationData.mushroom_kingdom_regional_coin_7: 3904,
        SMOLocationData.mushroom_kingdom_regional_coin_8: 3906,
        SMOLocationData.mushroom_kingdom_regional_coin_9: 3907,
        SMOLocationData.mushroom_kingdom_regional_coin_10: 3908,
        SMOLocationData.mushroom_kingdom_regional_coin_11: 3910,
        SMOLocationData.mushroom_kingdom_regional_coin_12: 3911,
        SMOLocationData.mushroom_kingdom_regional_coin_13: 3912,
        SMOLocationData.mushroom_kingdom_regional_coin_14: 3913,
        SMOLocationData.mushroom_kingdom_regional_coin_15: 3915,
        SMOLocationData.mushroom_kingdom_regional_coin_16: 3916,
        SMOLocationData.mushroom_kingdom_regional_coin_17: 3917,
        SMOLocationData.mushroom_kingdom_regional_coin_18: 3919,
        SMOLocationData.mushroom_kingdom_regional_coin_19: 3920,
        SMOLocationData.mushroom_kingdom_regional_coin_20: 3921,
        SMOLocationData.mushroom_kingdom_regional_coin_21: 3923,
        SMOLocationData.mushroom_kingdom_regional_coin_22: 3924,
        SMOLocationData.mushroom_kingdom_regional_coin_23: 3925,
        SMOLocationData.mushroom_kingdom_regional_coin_24: 3927,
        SMOLocationData.mushroom_kingdom_regional_coin_25: 3928,
        SMOLocationData.mushroom_kingdom_regional_coin_26: 3929,
        SMOLocationData.mushroom_kingdom_regional_coin_27: 3931,
        SMOLocationData.mushroom_kingdom_regional_coin_28: 3932,
        SMOLocationData.mushroom_kingdom_regional_coin_29: 3933,
        SMOLocationData.mushroom_kingdom_regional_coin_30: 3935,
        SMOLocationData.mushroom_kingdom_regional_coin_31: 3936,
        SMOLocationData.mushroom_kingdom_regional_coin_32: 3937,
        SMOLocationData.mushroom_kingdom_regional_coin_33: 3939,
        SMOLocationData.mushroom_kingdom_regional_coin_34: 3940,
        SMOLocationData.mushroom_kingdom_regional_coin_35: 3941,
        SMOLocationData.mushroom_kingdom_regional_coin_36: 3943,
        SMOLocationData.mushroom_kingdom_regional_coin_37: 3944,
        SMOLocationData.mushroom_kingdom_regional_coin_38: 3945,
        SMOLocationData.mushroom_kingdom_regional_coin_39: 3947,
        SMOLocationData.mushroom_kingdom_regional_coin_40: 3948,
        SMOLocationData.mushroom_kingdom_regional_coin_41: 3949,
        SMOLocationData.mushroom_kingdom_regional_coin_42: 3951,
        SMOLocationData.mushroom_kingdom_regional_coin_43: 3952,
        SMOLocationData.mushroom_kingdom_regional_coin_44: 3953,
        SMOLocationData.mushroom_kingdom_regional_coin_45: 3955,
        SMOLocationData.mushroom_kingdom_regional_coin_46: 3956,
        SMOLocationData.mushroom_kingdom_regional_coin_47: 3957,
        SMOLocationData.mushroom_kingdom_regional_coin_48: 3959,
        SMOLocationData.mushroom_kingdom_regional_coin_49: 3960,
        SMOLocationData.mushroom_kingdom_regional_coin_50: 3961,
        SMOLocationData.mushroom_kingdom_regional_coin_51: 3963,
        SMOLocationData.mushroom_kingdom_regional_coin_52: 3964,
        SMOLocationData.mushroom_kingdom_regional_coin_53: 3965,
        SMOLocationData.mushroom_kingdom_regional_coin_54: 3966,
        SMOLocationData.mushroom_kingdom_regional_coin_55: 3968,
        SMOLocationData.mushroom_kingdom_regional_coin_56: 3969,
        SMOLocationData.mushroom_kingdom_regional_coin_57: 3970,
        SMOLocationData.mushroom_kingdom_regional_coin_58: 3972,
        SMOLocationData.mushroom_kingdom_regional_coin_59: 3973,
        SMOLocationData.mushroom_kingdom_regional_coin_60: 3975,
        SMOLocationData.mushroom_kingdom_regional_coin_61: 3976,
        SMOLocationData.mushroom_kingdom_regional_coin_62: 3978,
        SMOLocationData.mushroom_kingdom_regional_coin_63: 3979,
        SMOLocationData.mushroom_kingdom_regional_coin_64: 3980,
        SMOLocationData.mushroom_kingdom_regional_coin_65: 3982,
        SMOLocationData.mushroom_kingdom_regional_coin_66: 3983,
        SMOLocationData.mushroom_kingdom_regional_coin_67: 3984,
        SMOLocationData.mushroom_kingdom_regional_coin_68: 3986,
        SMOLocationData.mushroom_kingdom_regional_coin_69: 3987,
        SMOLocationData.mushroom_kingdom_regional_coin_70: 3988,
        SMOLocationData.mushroom_kingdom_regional_coin_71: 3990,
        SMOLocationData.mushroom_kingdom_regional_coin_72: 3991,
        SMOLocationData.mushroom_kingdom_regional_coin_73: 3992,
        SMOLocationData.mushroom_kingdom_regional_coin_74: 3994,
        SMOLocationData.mushroom_kingdom_regional_coin_75: 3995,
        SMOLocationData.mushroom_kingdom_regional_coin_76: 3996,
        SMOLocationData.mushroom_kingdom_regional_coin_77: 3998,
        SMOLocationData.mushroom_kingdom_regional_coin_78: 3999,
        SMOLocationData.mushroom_kingdom_regional_coin_79: 4000,
        SMOLocationData.mushroom_kingdom_regional_coin_80: 4002,
        SMOLocationData.mushroom_kingdom_regional_coin_81: 4003,
        SMOLocationData.mushroom_kingdom_regional_coin_82: 4004,
        SMOLocationData.mushroom_kingdom_regional_coin_83: 4006,
        SMOLocationData.mushroom_kingdom_regional_coin_84: 4007,
        SMOLocationData.mushroom_kingdom_regional_coin_85: 4008,
        SMOLocationData.mushroom_kingdom_regional_coin_86: 4010,
        SMOLocationData.mushroom_kingdom_regional_coin_87: 4011,
        SMOLocationData.mushroom_kingdom_regional_coin_88: 4012,
        SMOLocationData.mushroom_kingdom_regional_coin_89: 4014,
        SMOLocationData.mushroom_kingdom_regional_coin_90: 4015,
        SMOLocationData.mushroom_kingdom_regional_coin_91: 4016,
        SMOLocationData.mushroom_kingdom_regional_coin_92: 4018,
        SMOLocationData.mushroom_kingdom_regional_coin_93: 4019,
        SMOLocationData.mushroom_kingdom_regional_coin_94: 4020,
        SMOLocationData.mushroom_kingdom_regional_coin_95: 4022,
        SMOLocationData.mushroom_kingdom_regional_coin_96: 4023,
        SMOLocationData.mushroom_kingdom_regional_coin_97: 4024,
}

peachs_castle_regional_coins = {
        SMOLocationData.peachs_castle_regional_coin_1: 3893,
        SMOLocationData.peachs_castle_regional_coin_2: 3894,
        SMOLocationData.peachs_castle_regional_coin_3: 3895,
}

regional_coin_groups_table = {
        **cap_kingdom_regional_groups,
        **cascade_kingdom_regional_groups,
        **cascade_kingdom_peace_regional_groups,
        **sand_kingdom_regional_groups,
        **sand_kingdom_peace_regional_groups,
        **sand_kingdom_pyramid_over_world_regional_groups,
        **wooded_kingdom_regional_groups,
        **lake_kingdom_regional_groups,
        **lost_kingdom_regional_groups,
        **metro_kingdom_regional_groups,
        **night_metro_kingdom_regional_groups,
        **seaside_kingdom_regional_groups,
        **snow_kingdom_regional_groups,
        **luncheon_kingdom_regional_groups,
        **luncheon_kingdom_post_meat_regional_groups,
        **bowsers_kingdom_regional_groups,
        **bowsers_kingdom_peace_regional_groups,
        **moon_kingdom_regional_groups,
        **mushroom_kingdom_regional_groups,
        **top_hat_tower_regional_groups,
        **frog_pond_regional_groups,
        **pushblocks_regional_groups,
        **poison_tides_regional_groups,
        **chasm_lifts_regional_groups,
        **bullet_bill_maze_regional_groups,
        **jaxi_ruins_regional_groups,
        **strange_neighborhood_regional_groups,
        **moeeye_invisible_maze_regional_groups,
        **ice_cave_regional_groups,
        **pyramid_upper_interior_regional_groups,
        **underground_ruins_regional_groups,
        **sky_garden_tower_regional_groups,
        **flooded_pipes_regional_groups,
        **deep_woods_regional_groups,
        **walking_on_clouds_regional_groups,
        **wooded_flower_road_regional_groups,
        **sherm_elevator_regional_groups,
        **bouncy_flowers_regional_groups,
        **city_hall_regional_groups,
        **sewers_regional_groups,
        **bullet_billding_regional_groups,
        **high_rise_regional_groups,
        **trex_escape_regional_groups,
        **sea_cave_regional_groups,
        **shiveria_regional_groups,
        **snowline_regional_groups,
        **cascading_magma_regional_groups,
        **magma_narrow_path_regional_groups,
        **spinning_athletics_regional_groups,
        **fork_flickin_regional_groups,
        **moon_cave_regional_groups,
        **peachs_castle_regional_groups,
}

regional_coin_table = {
        **cap_kingdom_regional_coins,
        **cascade_kingdom_regional_coins,
        **cascade_kingdom_peace_regional_coins,
        **sand_kingdom_regional_coins,
        **sand_kingdom_peace_regional_coins,
        **sand_kingdom_pyramid_over_world_regional_coins,
        **wooded_kingdom_regional_coins,
        **lake_kingdom_regional_coins,
        **lost_kingdom_regional_coins,
        **metro_kingdom_regional_coins,
        **night_metro_kingdom_regional_coins,
        **seaside_kingdom_regional_coins,
        **snow_kingdom_regional_coins,
        **luncheon_kingdom_regional_coins,
        **luncheon_kingdom_post_meat_regional_coins,
        **bowsers_kingdom_regional_coins,
        **bowsers_kingdom_peace_regional_coins,
        **moon_kingdom_regional_coins,
        **mushroom_kingdom_regional_coins,
        **top_hat_tower_regional_coins,
        **frog_pond_regional_coins,
        **pushblocks_regional_coins,
        **poison_tides_regional_coins,
        **chasm_lifts_regional_coins,
        **bullet_bill_maze_regional_coins,
        **jaxi_ruins_regional_coins,
        **strange_neighborhood_regional_coins,
        **moeeye_invisible_maze_regional_coins,
        **ice_cave_regional_coins,
        **pyramid_upper_interior_regional_coins,
        **underground_ruins_regional_coins,
        **sky_garden_tower_regional_coins,
        **flooded_pipes_regional_coins,
        **deep_woods_regional_coins,
        **walking_on_clouds_regional_coins,
        **wooded_flower_road_regional_coins,
        **sherm_elevator_regional_coins,
        **bouncy_flowers_regional_coins,
        **city_hall_regional_coins,
        **sewers_regional_coins,
        **bullet_billding_regional_coins,
        **high_rise_regional_coins,
        **trex_escape_regional_coins,
        **sea_cave_regional_coins,
        **shiveria_regional_coins,
        **snowline_regional_coins,
        **cascading_magma_regional_coins,
        **magma_narrow_path_regional_coins,
        **spinning_athletics_regional_coins,
        **fork_flickin_regional_coins,
        **moon_cave_regional_coins,
        **peachs_castle_regional_coins,
}

regional_coin_groups = {
        #region Cap Groups
        "CapWorldHomeStage": {
                2700: [2701, 2702, 2703, 2704],
                2705: [2706, 2707, 2708, 2709],
                2710: [2711, 2712, 2713, 2714],
                2715: [2716, 2717, 2718, 2719],
                2720: [2721, 2722, 2723],
                2724: [2725, 2726, 2727],
                2728: [2729, 2730, 2731],
                2732: [2733, 2734, 2735],
                2736: [2737, 2738, 2739],
        },

        "CapWorldTowerStage": {
                2740: [2741, 2742, 2743, 2744],
                2745: [2746, 2747, 2748, 2749, 2750],
        },

        "FrogSearchExStage": {
                2751: [2752, 2753, 2754, 2755],
        },

        "PushBlockExStage": {
                2756: [2757, 2758, 2759],
        },

        "PoisonWaveExStage": {
                2760: [2761, 2762, 2763],
        },
        #endregion

        #region Cascade Groups
        "WaterfallWorldHomeStage": {
                2764: [2765, 2766, 2767],
                2768: [2769, 2770, 2771],
                2772: [2773, 2774, 2775],
                2776: [2777, 2778, 2779],
                2780: [2781, 2782, 2783],
                2784: [2785, 2786, 2787],
                2788: [2789, 2790, 2791],
                2792: [2793, 2794, 2795],
                2796: [2797, 2798, 2799],
                2800: [2801, 2802, 2803, 2804],
                2805: [2806, 2807, 2808],
                2809: [2810, 2811, 2812],
                2813: [2814, 2815, 2816],
                2817: [2818, 2819, 2820],
                2821: [2822, 2823, 2824],
        },

        "Lift2DExStage": {
                2825: [2826, 2827, 2828, 2829],
        },
        #endregion

        #region Sand Groups
        "SandWorldHomeStage": {
                2830: [2831, 2832, 2833],
                2834: [2835, 2836, 2837],
                2838: [2839, 2840, 2841],
                2842: [2843, 2844, 2845],
                2846: [2847, 2848, 2849],
                2850: [2851, 2852],
                2853: [2854, 2855],
                2856: [2857, 2858, 2859],
                2860: [2861, 2862, 2863],
                2864: [2865, 2866, 2867],
                2868: [2869, 2870, 2871],
                2872: [2873, 2874, 2875],
                2876: [2877, 2878, 2879],
                2880: [2881, 2882, 2883],
                2884: [2885, 2886],
                2887: [2888, 2889, 2890, 2891, 2892, 2893],
                2894: [2895, 2896, 2897],
                2898: [2899, 2900, 2901, 2902],
                2903: [2904, 2905, 2906, 2907],
                2908: [2909, 2910, 2912, 2913],
        },

        "SandWorldKillerExStage": {
                2914: [2915, 2916, 2917, 2918, 2919, 2920],
        },

        "SandWorldMeganeExStage": {
                2921: [2922, 2923, 2924, 2925],
        },

        "SandWorldPressExStage": {
                2926: [2927, 2928],
                2929: [2930, 2931],
        },

        "SandWorldPyramid001Stage": {
                2932: [2933, 2934, 2935],
        },

        "SandWorldRotateExStage": {
                2936: [2937, 2938],
                2939: [2940, 2941, 2942],
        },

        "SandWorldUnderground000Stage": {
                2943: [2944, 2945, 2946],
                2947: [2948, 2949, 2950, 2951],
        },

        "SandWorldSphinxExStage": {
                2952: [2953, 2954],
                2955: [2956, 2957, 2958],
                2959: [2960, 2961, 2962],
        },
        #endregion

        #region Wooded Groups
        "ForestWorldHomeStage": {
                2963: [2964, 2965, 2966],
                2967: [2968, 2969, 2970],
                2971: [2972, 2973, 2974],
                2975: [2976, 2977],
                2978: [2979, 2980, 2981, 2982],
                2983: [2984, 2985, 2986, 2987],
                2988: [2989, 2990, 2991],
                2992: [2993, 2994, 2995],
                2996: [2997, 2998, 2999, 3000],
                3001: [3002, 3003, 3004, 3005],
                3006: [3007, 3008, 3009, 3010],
                3011: [3012, 3013, 3014],
                3015: [3016, 3017, 3018],
                3019: [3020, 3021, 3022, 3023],
                3024: [3025, 3026, 3027],
                3028: [3029, 3030, 3031],
                3032: [3033, 3034, 3035],
                3036: [3037, 3038],
                3039: [3040, 3041, 3042, 3043],
                3044: [3045, 3046],
                3047: [3048, 3049, 3050],
                3051: [3052, 3053, 3054],
                3055: [3056, 3057, 3058],
                3059: [3060, 3061, 3062],
        },

        "ForestWorldTowerStage": {
                3063: [3064, 3065, 3066],
        },

        "ForestWorldWaterExStage": {
                3067: [3068, 3069, 3070],
        },

        "ForestWorldWoodsStage": {
                3071: [3072, 3073, 3074],
                3075: [3076, 3077, 3078],
                3079: [3080, 3081, 3082],
        },

        "ForestWorldCloudBonusExStage": {
                3083: [3084, 3085, 3086],
        },

        "RailCollisionExStage": {
                3087: [3088, 3089, 3090],
        },

        "ShootingElevatorExStage": {
                3091: [3092, 3093, 3094],
        },
        #endregion

        #region Lake Groups
        "LakeWorldHomeStage": {
                3095: [3096, 3097, 3098, 3099],
                3100: [3101, 3102, 3103],
                3104: [3105, 3106, 3107, 3108],
                3109: [3110, 3111, 3112],
                3113: [3114, 3115, 3116],
                3117: [3118, 3119, 3120],
                3121: [3122, 3123, 3124],
                3125: [3126, 3127, 3128, 3129],
                3130: [3131, 3132, 3133, 3134],
                3135: [3136, 3137, 3138],
                3139: [3140, 3141, 3142, 3143],
                3144: [3145, 3146, 3147],
                3148: [3149, 3150, 3151],
                3152: [3153, 3154, 3155],
        },

        "TrampolineWallCatchExStage": {
                3156: [3157, 3158, 3159],
        },
        #endregion

        #region Lost Groups
        "ClashWorldHomeStage": {
                3160: [3161, 3162, 3163],
                3164: [3165, 3166, 3167, 3168],
                3169: [3170, 3171, 3172],
                3173: [3174, 3175, 3176],
                3177: [3178, 3179],
                3180: [3181, 3182, 3183, 3184],
                3185: [3186, 3187, 3188],
                3189: [3190, 3191, 3192, 3193],
                3194: [3195, 3196],
                3197: [3198, 3199],
                3200: [3201, 3202],
                3203: [3204, 3205],
                3206: [3207, 3208, 3209],
                3210: [3211, 3212],
                3213: [3214, 3215],
                3216: [3217, 3218, 3219],
                3220: [3221, 3222, 3223],
                3224: [3225, 3226, 3227],
        },
        #endregion

        #region Metro Groups
        "CityWorldHomeStage": {
                3228: [3229, 3230, 3231],
                3232: [3233, 3234, 3235],
                3236: [3237, 3238, 3239],
                3240: [3241, 3242, 3243],
                3244: [3245, 3246, 3247],
                3248: [3249, 3250],
                3251: [3252, 3253, 3254],
                3255: [3256, 3257, 3258],
                3259: [3260, 3261, 3262],
                3263: [3264, 3265, 3266],
                3267: [3268, 3269, 3270],
                3271: [3272, 3273],
                3274: [3275, 3276, 3277],
                3278: [3279, 3280],
                3281: [3282, 3283, 3284],
                3285: [3286, 3287, 3288],
                3289: [3290, 3291, 3292],
                3293: [3294, 3295],
                3296: [3297, 3298, 3299, 3300],
                3301: [3302, 3303, 3304],
                3305: [3306, 3307, 3308],
                3309: [3310, 3311],
                3312: [3313, 3314, 3315],
                3316: [3317, 3318],
                3319: [3320, 3321],
                3322: [3323, 3324],
        },

        "CityWorldFactoryStage": {
                3325: [3326, 3327, 3328],
                3329: [3330, 3331, 3332, 3333],
        },

        "CityWorldMainTowerStage": {
                3334: [3335, 3336, 3337],
                3338: [3339, 3340],
                3341: [3342, 3343, 3344],
                3345: [3346, 3347],
        },

        "PoleKillerExStage": {
                3348: [3349, 3350, 3351],
        },

        "PoleGrabCeilExStage": {
                3352: [3353, 3354, 3355],
        },

        "TrexBikeExStage": {
                3356: [3357, 3358, 3359],
                3360: [3361, 3362, 3363],
        },
        #endregion

        #region Seaside Groups
        "SeaWorldHomeStage": {
                3364: [3365, 3366, 3367],
                3368: [3369, 3370, 3371],
                3372: [3373, 3374, 3375],
                3376: [3377, 3378, 3379],
                3380: [3381, 3382, 3383],
                3384: [3385, 3386, 3387],
                3388: [3389, 3390, 3391],
                3392: [3393, 3394, 3395],
                3396: [3397, 3398, 3399],
                3400: [3401, 3402, 3403, 3404],
                3405: [3406, 3407, 3408],
                3409: [3410, 3411, 3412],
                3413: [3414, 3415, 3416],
                3417: [3418, 3419, 3420],
                3421: [3422, 3423, 3424],
                3425: [3426, 3427, 3428],
                3429: [3430, 3431, 3432],
                3433: [3434, 3435, 3436],
                3437: [3438, 3439, 3440],
                3441: [3442, 3443, 3444],
                3445: [3446, 3447, 3448],
                3449: [3450, 3451, 3452],
                3453: [3454, 3455, 3456],
                3457: [3458, 3459, 3460],
                3461: [3462, 3463, 3464],
                3465: [3466, 3467, 3468],
                3469: [3470, 3471, 3472],
                3473: [3474, 3475, 3476],
                3477: [3478, 3479, 3480],
                3481: [3482, 3483, 3484],
                3485: [3486, 3487, 3488],
        },

        "SeaWorldUtsuboCaveStage": {
                3489: [3490, 3491, 3492],
                3493: [3494, 3495, 3496],
        },
        #endregion

        #region Snow Groups
        "SnowWorldHomeStage": {
                3497: [3498, 3499],
                3500: [3501, 3502],
                3503: [3504, 3505, 3506],
                3507: [3508, 3509],
                3510: [3511, 3512],
                3513: [3514, 3515],
        },

        "SnowWorldTownStage": {
                3516: [3517, 3518, 3519, 3520],
                3521: [3522, 3523, 3524],
                3525: [3526, 3527, 3528],
                3529: [3530, 3531, 3532, 3533],
                3534: [3535, 3536, 3537],
                3538: [3539, 3540, 3541],
                3542: [3543, 3544, 3545],
                3546: [3547, 3548, 3549, 3550],
                3551: [3552, 3553, 3554],
                3555: [3556, 3557, 3558, 3559],
                3560: [3561, 3562, 3563],
        },
        #endregion

        #region Luncheon Groups
        "LavaWorldHomeStage": {
                3564: [3565, 3566, 3567],
                3568: [3569, 3570, 3571],
                3572: [3573, 3574, 3575],
                3576: [3577, 3578, 3579],
                3580: [3581, 3582, 3583],
                3584: [3585, 3586, 3587],
                3588: [3589, 3590],
                3591: [3592, 3593, 3594, 3595, 3596, 3597, 3598],
                3599: [3600, 3601, 3602],
                3603: [3604, 3605, 3606],
                3607: [3608, 3609, 3610, 3611, 3612],
                3613: [3614, 3615, 3616],
                3617: [3618, 3619, 3620, 3621],
                3622: [3623, 3624, 3625],
                3626: [3627, 3628, 3629],
                3630: [3631, 3632, 3633],
                3634: [3635, 3636, 3637],
                3638: [3639, 3640, 3641],
                3642: [3643, 3644, 3645],
                3646: [3647, 3648, 3649],
                3650: [3651, 3652, 3653, 3654],
                3655: [3656, 3657, 3658],
                3659: [3660, 3661, 3662],
                3663: [3664, 3665, 3666],
                3667: [3668, 3669, 3670],
                3671: [3672, 3673, 3674],
                3675: [3676, 3677, 3678],
                3679: [3680, 3681, 3682],
        },

        "LavaWorldBubbleLaneExStage": {
                3683: [3684, 3685, 3686],
        },

        "LavaWorldClockExStage": {
                3687: [3688, 3689, 3690],
        },

        "ForkExStage": {
                3691: [3692, 3693, 3694],
        },
        #endregion

        #region Bowser Groups
        "SkyWorldHomeStage": {
                3695: [3696, 3697, 3698],
                3699: [3700, 3701, 3702],
                3703: [3704, 3705, 3706],
                3707: [3708, 3709, 3710],
                3711: [3712, 3713, 3714],
                3715: [3716, 3717, 3718, 3719],
                3720: [3721, 3722, 3723],
                3724: [3725, 3726, 3727],
                3728: [3729, 3730, 3731],
                3732: [3733, 3734, 3735],
                3736: [3737, 3738, 3739],
                3740: [3741, 3742, 3743],
                3744: [3745, 3746, 3747],
                3748: [3749, 3750, 3751],
                3752: [3753, 3754, 3755],
                3756: [3757, 3758, 3759],
                3760: [3761, 3762, 3763],
                3764: [3765, 3766, 3767],
                3768: [3769, 3770, 3771],
                3772: [3773, 3774, 3775, 3776],
                3777: [3778, 3779, 3780, 3781],
                3782: [3783, 3784, 3785],
                3786: [3787, 3788, 3789, 3790],
                3791: [3792, 3793],
                3794: [3795, 3796],
                3797: [3798, 3799, 3800],
                3801: [3802, 3803, 3804],
                3805: [3806, 3807, 3808],
                3809: [3810, 3811, 3812],
                3813: [3814, 3815, 3816, 3817],
                3818: [3819, 3820, 3821],
                3822: [3823, 3824, 3825, 3826],
        },

        #endregion

        #region Moon Groups
        "MoonWorldHomeStage": {
                3827: [3828, 3829, 3830],
                3831: [3832, 3833, 3834, 3835],
                3836: [3837, 3838, 3839, 3840, 3841, 3842],
                3843: [3844, 3845, 3846],
                3847: [3848, 3849, 3850, 3851],
                3852: [3853, 3854, 3855],
                3856: [3857, 3858, 3859],
                3860: [3861, 3862, 3863],
                3864: [3865, 3866, 3867],
        },

        "MoonWorldCaptureParadeStage": {
                3868: [3869, 3870, 3871],
                3872: [3873, 3874, 3875],
                3876: [3877, 3878, 3879],
                3880: [3881, 3882, 3883],
                3884: [3885, 3886, 3887],
                3888: [3889, 3890, 3891],
        },
        #endregion

        #region Mushroom Groups
        "PeachWorldHomeStage": {
                3892: [3893, 3894, 3895, 3896],
                3897: [3898, 3899, 3900],
                3901: [3902, 3903, 3904],
                3905: [3906, 3907, 3908],
                3909: [3910, 3911, 3912, 3913],
                3914: [3915, 3916, 3917],
                3918: [3919, 3920, 3921],
                3922: [3923, 3924, 3925],
                3926: [3927, 3928, 3929],
                3930: [3931, 3932, 3933],
                3934: [3935, 3936, 3937],
                3938: [3939, 3940, 3941],
                3942: [3943, 3944, 3945],
                3946: [3947, 3948, 3949],
                3950: [3951, 3952, 3953],
                3954: [3955, 3956, 3957],
                3958: [3959, 3960, 3961],
                3962: [3963, 3964, 3965, 3966],
                3967: [3968, 3969, 3970],
                3971: [3972, 3973],
                3974: [3975, 3976],
                3977: [3978, 3979, 3980],
                3981: [3982, 3983, 3984],
                3985: [3986, 3987, 3988],
                3989: [3990, 3991, 3992],
                3993: [3994, 3995, 3996],
                3997: [3998, 3999, 4000],
                4001: [4002, 4003, 4004],
                4005: [4006, 4007, 4008],
                4009: [4010, 4011, 4012],
                4013: [4014, 4015, 4016],
                4017: [4018, 4019, 4020],
        },

        "PeachWorldCastleStage": {
                4021: [4022, 4023, 4024],
        },
        #endregion

}


regional_coins = {
        #region Cap Coins
        "CapWorldHomeStage": {
                "obj2103": 2701,
                "obj2105": 2702,
                "obj2108": 2703,
                "obj2104": 2704,
                "obj2109": 2706,
                "obj2110": 2707,
                "obj2111": 2708,
                "obj2112": 2709,
                "obj2117": 2711,
                "obj2118": 2712,
                "obj2119": 2713,
                "obj2120": 2714,
                "obj2139": 2716,
                "obj2140": 2717,
                "obj2141": 2718,
                "obj2142": 2719,
                "obj2143": 2721,
                "obj2144": 2722,
                "obj2145": 2723,
                "obj2147": 2725,
                "obj2148": 2726,
                "obj2149": 2727,
                "obj2166": 2729,
                "obj2167": 2730,
                "obj2168": 2731,
                "obj2169": 2733,
                "obj2170": 2734,
                "obj2171": 2735,
                "obj2233": 2737,
                "obj2234": 2738,
                "obj2235": 2739,
        },

        "CapWorldTowerStage": {
                "obj1348": 2741,
                "obj1349": 2742,
                "obj1350": 2743,
                "obj1351": 2744,
                "obj1352": 2746,
                "obj1353": 2747,
                "obj1354": 2748,
                "obj1355": 2749,
                "obj1356": 2750,
        },

        "FrogSearchExStage": {
                "obj64": 2752,
                "obj65": 2753,
                "obj66": 2754,
                "obj67": 2755,
        },

        "PushBlockExStage": {
                "obj131": 2757,
                "obj515": 2758,
                "obj516": 2759,
        },

        "PoisonWaveExStage": {
                "obj422": 2761,
                "obj423": 2762,
                "obj424": 2763,
        },
        #endregion

        #region Cascade Coins
        "WaterfallWorldHomeStage": {
                "obj1046": 2765,
                "obj1047": 2766,
                "obj1048": 2767,
                "obj1057": 2769,
                "obj1211": 2770,
                "obj1212": 2771,
                "obj1106": 2773,
                "obj1107": 2774,
                "obj1109": 2775,
                "obj1535": 2777,
                "obj1536": 2778,
                "obj1537": 2779,
                "obj1641": 2781,
                "obj1926": 2782,
                "obj1927": 2783,
                "obj1796": 2785,
                "obj1797": 2786,
                "obj1798": 2787,
                "obj1855": 2789,
                "obj1856": 2790,
                "obj1857": 2791,
                "obj1897": 2793,
                "obj1898": 2794,
                "obj1899": 2795,
                "obj2041": 2797,
                "obj2042": 2798,
                "obj2043": 2799,
                "obj2156": 2801,
                "obj2157": 2802,
                "obj2158": 2803,
                "obj2159": 2804,
                "obj3265": 2806,
                "obj3266": 2807,
                "obj3267": 2808,
                "obj3268": 2810,
                "obj3269": 2811,
                "obj3270": 2812,
                "obj3271": 2814,
                "obj3272": 2815,
                "obj3273": 2816,
                "obj1049": 2818,
                "obj1050": 2819,
                "obj1394": 2820,
                "obj1555": 2822,
                "obj1556": 2823,
                "obj1557": 2824,
        },

        "Lift2DExStage": {
                "obj6460": 2826,
                "obj6461": 2827,
                "obj6462": 2828,
                "obj7621": 2829,
        },
        #endregion

        #region Sand Coins
        "SandWorldHomeStage": {
                "obj1438": 2831,
                "obj3024": 2832,
                "obj3025": 2833,
                "obj1831": 2835,
                "obj1832": 2836,
                "obj1833": 2837,
                "obj1967": 2839,
                "obj1969": 2840,
                "obj1970": 2841,
                "obj1999": 2843,
                "obj2000": 2844,
                "obj2399": 2845,
                "obj2018": 2847,
                "obj3737": 2848,
                "obj2019": 2849,
                "obj2021": 2851,
                "obj3726": 2852,
                "obj2022": 2854,
                "obj3727": 2855,
                "obj2392": 2857,
                "obj2393": 2858,
                "obj2394": 2859,
                "obj2396": 2861,
                "obj2397": 2862,
                "obj2398": 2863,
                "obj3404": 2865,
                "obj3405": 2866,
                "obj3406": 2867,
                "obj3479": 2869,
                "obj3480": 2870,
                "obj3481": 2871,
                "obj3720": 2873,
                "obj3721": 2874,
                "obj3722": 2875,
                "obj3723": 2877,
                "obj3724": 2878,
                "obj3725": 2879,
                "obj3855": 2881,
                "obj3856": 2882,
                "obj3857": 2883,
                "obj3879": 2885,
                "obj3880": 2886,
                "obj4864": 2888,
                "obj4865": 2889,
                "obj4866": 2890,
                "obj4867": 2891,
                "obj4868": 2892,
                "obj4869": 2893,
                "obj6862": 2895,
                "obj6863": 2896,
                "obj6864": 2897,
                "obj3671": 2899,
                "obj3673": 2900,
                "obj3676": 2901,
                "obj3677": 2902,
                "obj4871": 2904,
                "obj4873": 2905,
                "obj4875": 2906,
                "obj4876": 2907,
                "obj134(SandWorldKillerTowerZone[obj2721])": 2909,
                "obj140(SandWorldKillerTowerZone[obj2721])": 2910,
                "obj135(SandWorldKillerTowerZone[obj2721])": 2912,
                "obj141(SandWorldKillerTowerZone[obj2721])": 2913,
        },

        "SandWorldKillerExStage": {
                "obj38": 2915,
                "obj39": 2916,
                "obj40": 2917,
                "obj41": 2918,
                "obj132": 2919,
                "obj133": 2920,
        },

        "SandWorldMeganeExStage": {
                "obj172": 2922,
                "obj203": 2923,
                "obj204": 2924,
                "obj205": 2925,
        },

        "SandWorldPressExStage": {
                "obj44": 2927,
                "obj46": 2928,
                "obj195": 2930,
                "obj196": 2931,
        },

        "SandWorldPyramid001Stage": {
                "obj295": 2933,
                "obj296": 2934,
                "obj297": 2935,
        },

        "SandWorldRotateExStage": {
                "obj201": 2937,
                "obj202": 2938,
                "obj203": 2940,
                "obj225": 2941,
                "obj226": 2942,
        },

        "SandWorldUnderground000Stage": {
                "obj165": 2944,
                "obj166": 2945,
                "obj168": 2946,
                "obj530": 2948,
                "obj531": 2949,
                "obj532": 2950,
                "obj533": 2951,
        },

        "SandWorldSphinxExStage": {
                "obj1136": 2953,
                "obj1139": 2954,
                "obj1137": 2956,
                "obj1141": 2957,
                "obj1142": 2958,
                "obj1138": 2960,
                "obj1143": 2961,
                "obj1144": 2962,
        },
        #endregion

        #region Wooded Coins
        "ForestWorldHomeStage": {
                "obj1727": 2964,
                "obj1729": 2965,
                "obj1730": 2966,
                "obj1752": 2968,
                "obj1753": 2969,
                "obj1754": 2970,
                "obj1757": 2972,
                "obj1758": 2973,
                "obj1759": 2974,
                "obj1772": 2976,
                "obj5937": 2977,
                "obj1783": 2979,
                "obj1784": 2980,
                "obj1785": 2981,
                "obj1786": 2982,
                "obj2294": 2984,
                "obj2295": 2985,
                "obj4826": 2986,
                "obj6989": 2987,
                "obj2305": 2989,
                "obj2306": 2990,
                "obj2307": 2991,
                "obj4918": 2993,
                "obj4919": 2994,
                "obj4920": 2995,
                "obj5000": 2997,
                "obj5001": 2998,
                "obj5002": 2999,
                "obj6990": 3000,
                "obj5036": 3002,
                "obj5037": 3003,
                "obj5038": 3004,
                "obj5039": 3005,
                "obj5119": 3007,
                "obj5120": 3008,
                "obj5121": 3009,
                "obj5122": 3010,
                "obj5616": 3012,
                "obj5617": 3013,
                "obj5618": 3014,
                "obj5950": 3016,
                "obj5952": 3017,
                "obj5963": 3018,
                "obj5953": 3020,
                "obj5954": 3021,
                "obj5955": 3022,
                "obj6145": 3023,
                "obj5957": 3025,
                "obj5958": 3026,
                "obj6150": 3027,
                "obj5964": 3029,
                "obj5965": 3030,
                "obj5966": 3031,
                "obj5970": 3033,
                "obj5971": 3034,
                "obj5972": 3035,
                "obj6024": 3037,
                "obj6025": 3038,
                "obj6146": 3040,
                "obj6147": 3041,
                "obj6148": 3042,
                "obj6149": 3043,
                "obj6402": 3045,
                "obj6985": 3046,
                "obj7317": 3048,
                "obj7318": 3049,
                "obj7319": 3050,
                "obj7320": 3052,
                "obj7321": 3053,
                "obj7322": 3054,
                "obj7704": 3056,
                "obj7705": 3057,
                "obj7706": 3058,
                "obj1748": 3060,
                "obj1745": 3061,
                "obj1750": 3062,
        },

        "ForestWorldTowerStage": {
                "obj216": 3064,
                "obj217": 3065,
                "obj218": 3066,
        },

        "ForestWorldWaterExStage": {
                "obj245": 3068,
                "obj246": 3069,
                "obj247": 3070,
        },

        "ForestWorldWoodsStage": {
                "obj93": 3072,
                "obj94": 3073,
                "obj95": 3074,
                "obj223": 3076,
                "obj224": 3077,
                "obj225": 3078,
                "obj321": 3080,
                "obj322": 3081,
                "obj323": 3082,
        },

        "ForestWorldCloudBonusExStage": {
                "obj1546": 3084,
                "obj1547": 3085,
                "obj1548": 3086,
        },

        "RailCollisionExStage": {
                "obj261": 3088,
                "obj262": 3089,
                "obj263": 3090,
        },

        "ShootingElevatorExStage": {
                "obj253": 3092,
                "obj254": 3093,
                "obj255": 3094,
        },
        #endregion

        #region Lake Coins
        "LakeWorldHomeStage": {
                "obj20(LakeWorld2DZone[obj526])": 3096,
                "obj22(LakeWorld2DZone[obj526])": 3097,
                "obj23(LakeWorld2DZone[obj526])": 3098,
                "obj21(LakeWorld2DZone[obj526])": 3099,
                "obj111": 3101,
                "obj113": 3102,
                "obj112": 3103,
                "obj312": 3105,
                "obj313": 3106,
                "obj314": 3107,
                "obj315": 3108,
                "obj351": 3110,
                "obj353": 3111,
                "obj354": 3112,
                "obj509": 3114,
                "obj511": 3115,
                "obj510": 3116,
                "obj529": 3118,
                "obj530": 3119,
                "obj531": 3120,
                "obj336(LakeWorldTownZone[obj324])": 3122,
                "obj337(LakeWorldTownZone[obj324])": 3123,
                "obj338(LakeWorldTownZone[obj324])": 3124,
                "obj431(LakeWorldTownZone[obj324])": 3126,
                "obj432(LakeWorldTownZone[obj324])": 3127,
                "obj433(LakeWorldTownZone[obj324])": 3128,
                "obj549(LakeWorldTownZone[obj324])": 3129,
                "obj448(LakeWorldTownZone[obj324])": 3131,
                "obj449(LakeWorldTownZone[obj324])": 3132,
                "obj450(LakeWorldTownZone[obj324])": 3133,
                "obj746(LakeWorldTownZone[obj324])": 3134,
                "obj551(LakeWorldTownZone[obj324])": 3136,
                "obj552(LakeWorldTownZone[obj324])": 3137,
                "obj554(LakeWorldTownZone[obj324])": 3138,
                "obj668(LakeWorldTownZone[obj324])": 3140,
                "obj670(LakeWorldTownZone[obj324])": 3141,
                "obj671(LakeWorldTownZone[obj324])": 3142,
                "obj669(LakeWorldTownZone[obj324])": 3143,
                "obj743(LakeWorldTownZone[obj324])": 3145,
                "obj744(LakeWorldTownZone[obj324])": 3146,
                "obj745(LakeWorldTownZone[obj324])": 3147,
                "obj759(LakeWorldTownZone[obj324])": 3149,
                "obj760(LakeWorldTownZone[obj324])": 3150,
                "obj761(LakeWorldTownZone[obj324])": 3151,
                "obj613(LakeWorldTownZone[obj324])": 3153,
                "obj615(LakeWorldTownZone[obj324])": 3154,
                "obj758(LakeWorldTownZone[obj324])": 3155,
        },

        "TrampolineWallCatchExStage": {
                "obj1095": 3157,
                "obj1096": 3158,
                "obj1137": 3159,
        },
        #endregion

        #region Lost Coins
        "ClashWorldHomeStage": {
                "obj553": 3161,
                "obj554": 3162,
                "obj555": 3163,
                "obj587": 3165,
                "obj1030": 3166,
                "obj1035": 3167,
                "obj1122": 3168,
                "obj588": 3170,
                "obj589": 3171,
                "obj1036": 3172,
                "obj727": 3174,
                "obj979": 3175,
                "obj728": 3176,
                "obj729": 3178,
                "obj1121": 3179,
                "obj851": 3181,
                "obj852": 3182,
                "obj853": 3183,
                "obj854": 3184,
                "obj868": 3186,
                "obj964": 3187,
                "obj870": 3188,
                "obj872": 3190,
                "obj874": 3191,
                "obj875": 3192,
                "obj977": 3193,
                "obj873": 3195,
                "obj998": 3196,
                "obj901": 3198,
                "obj1193": 3199,
                "obj902": 3201,
                "obj904": 3202,
                "obj903": 3204,
                "obj1738": 3205,
                "obj978": 3207,
                "obj1740": 3208,
                "obj1739": 3209,
                "obj997": 3211,
                "obj999": 3212,
                "obj1039": 3214,
                "obj1043": 3215,
                "obj1096": 3217,
                "obj1097": 3218,
                "obj1098": 3219,
                "obj1689": 3221,
                "obj1690": 3222,
                "obj1691": 3223,
                "obj1031": 3225,
                "obj1033": 3226,
                "obj1034": 3227,
        },
        #endregion

        #region Metro Coins
        "CityWorldHomeStage": {
                "obj4637": 3229,
                "obj4639": 3230,
                "obj4640": 3231,
                "obj7901": 3233,
                "obj7902": 3234,
                "obj7904": 3235,
                "obj8580": 3237,
                "obj8581": 3238,
                "obj8582": 3239,
                "obj9306": 3241,
                "obj9307": 3242,
                "obj9308": 3243,
                "obj9372": 3245,
                "obj9373": 3246,
                "obj9374": 3247,
                "obj10505": 3249,
                "obj11282": 3250,
                "obj4506": 3252,
                "obj4508": 3253,
                "obj4507": 3254,
                "obj4633": 3256,
                "obj10075": 3257,
                "obj10076": 3258,
                "obj4638": 3260,
                "obj13420": 3261,
                "obj13421": 3262,
                "obj5055": 3264,
                "obj5056": 3265,
                "obj9413": 3266,
                "obj5910": 3268,
                "obj5913": 3269,
                "obj5911": 3270,
                "obj7939": 3272,
                "obj11281": 3273,
                "obj8043": 3275,
                "obj13097": 3276,
                "obj13098": 3277,
                "obj8044": 3279,
                "obj8046": 3280,
                "obj8269": 3282,
                "obj9541": 3283,
                "obj9542": 3284,
                "obj8807": 3286,
                "obj8808": 3287,
                "obj8809": 3288,
                "obj10062": 3290,
                "obj10063": 3291,
                "obj10064": 3292,
                "obj10675": 3294,
                "obj10676": 3295,
                "obj10968": 3297,
                "obj10969": 3298,
                "obj10970": 3299,
                "obj10971": 3300,
                "obj10978": 3302,
                "obj10979": 3303,
                "obj10980": 3304,
                "obj11001": 3306,
                "obj11002": 3307,
                "obj11003": 3308,
                "obj11083": 3310,
                "obj11280": 3311,
                "obj12978": 3313,
                "obj12979": 3314,
                "obj12983": 3315,
                "obj13145": 3317,
                "obj13146": 3318,
                "obj15765": 3320,
                "obj15766": 3321,
                "obj15775": 3323,
                "obj15777": 3324,
        },

        "CityWorldFactoryStage": {
                "obj664(CityWorldFactory01Zone[obj309])": 3326,
                "obj665(CityWorldFactory01Zone[obj309])": 3327,
                "obj666(CityWorldFactory01Zone[obj309])": 3328,
                "obj298": 3330,
                "obj299": 3331,
                "obj301": 3332,
                "obj367": 3333,
        },

        "CityWorldMainTowerStage": {
                "obj546": 3335,
                "obj547": 3336,
                "obj1260": 3337,
                "obj874": 3339,
                "obj1041": 3340,
                "obj882": 3342,
                "obj883": 3343,
                "obj884": 3344,
                "obj966": 3346,
                "obj967": 3347,
        },

        "PoleKillerExStage": {
                "obj1497": 3349,
                "obj1498": 3350,
                "obj1499": 3351,
        },

        "PoleGrabCeilExStage": {
                "obj800": 3353,
                "obj801": 3354,
                "obj802": 3355,
        },

        "TrexBikeExStage": {
                "obj5435": 3357,
                "obj5436": 3358,
                "obj5437": 3359,
                "obj5438": 3361,
                "obj5439": 3362,
                "obj5440": 3363,
        },
        #endregion

        #region Seaside Coins
        "SeaWorldHomeStage": {
                "obj254(SeaWorld2DLargeZone[obj2083])": 3365,
                "obj256(SeaWorld2DLargeZone[obj2083])": 3366,
                "obj371(SeaWorld2DLargeZone[obj2083])": 3367,
                "obj220(SeaWorldDamageBallZone[obj1070])": 3369,
                "obj221(SeaWorldDamageBallZone[obj1070])": 3370,
                "obj223(SeaWorldDamageBallZone[obj1070])": 3371,
                "obj1149": 3373,
                "obj1150": 3374,
                "obj1151": 3375,
                "obj1152": 3377,
                "obj1154": 3378,
                "obj1153": 3379,
                "obj1403": 3381,
                "obj1404": 3382,
                "obj1405": 3383,
                "obj1566": 3385,
                "obj1567": 3386,
                "obj1568": 3387,
                "obj2064": 3389,
                "obj2065": 3390,
                "obj2066": 3391,
                "obj2204": 3393,
                "obj2205": 3394,
                "obj2206": 3395,
                "obj2385": 3397,
                "obj2386": 3398,
                "obj2387": 3399,
                "obj2398": 3401,
                "obj2399": 3402,
                "obj2400": 3403,
                "obj2445": 3404,
                "obj2409": 3406,
                "obj2411": 3407,
                "obj2410": 3408,
                "obj2413": 3410,
                "obj2415": 3411,
                "obj2414": 3412,
                "obj2455": 3414,
                "obj2456": 3415,
                "obj2457": 3416,
                "obj2458": 3418,
                "obj2459": 3419,
                "obj2460": 3420,
                "obj2552": 3422,
                "obj2553": 3423,
                "obj2554": 3424,
                "obj2686": 3426,
                "obj2687": 3427,
                "obj2688": 3428,
                "obj3078": 3430,
                "obj3079": 3431,
                "obj3080": 3432,
                "obj4312": 3434,
                "obj4313": 3435,
                "obj4314": 3436,
                "obj206(SeaWorldLavaZone[obj1399])": 3438,
                "obj207(SeaWorldLavaZone[obj1399])": 3439,
                "obj208(SeaWorldLavaZone[obj1399])": 3440,
                "obj236(SeaWorldLavaZone[obj1399])": 3442,
                "obj237(SeaWorldLavaZone[obj1399])": 3443,
                "obj238(SeaWorldLavaZone[obj1399])": 3444,
                "obj430(SeaWorldLavaZone[obj1399])": 3446,
                "obj431(SeaWorldLavaZone[obj1399])": 3447,
                "obj432(SeaWorldLavaZone[obj1399])": 3448,
                "obj494(SeaWorldLavaZone[obj1399])": 3450,
                "obj495(SeaWorldLavaZone[obj1399])": 3451,
                "obj496(SeaWorldLavaZone[obj1399])": 3452,
                "obj280(SeaWorldLighthouseZone[obj1402])": 3454,
                "obj282(SeaWorldLighthouseZone[obj1402])": 3455,
                "obj281(SeaWorldLighthouseZone[obj1402])": 3456,
                "obj314(SeaWorldLighthouseZone[obj1402])": 3458,
                "obj315(SeaWorldLighthouseZone[obj1402])": 3459,
                "obj316(SeaWorldLighthouseZone[obj1402])": 3460,
                "obj351(SeaWorldLighthouseZone[obj1402])": 3462,
                "obj352(SeaWorldLighthouseZone[obj1402])": 3463,
                "obj353(SeaWorldLighthouseZone[obj1402])": 3464,
                "obj373(SeaWorldLighthouseZone[obj1402])": 3466,
                "obj375(SeaWorldLighthouseZone[obj1402])": 3467,
                "obj374(SeaWorldLighthouseZone[obj1402])": 3468,
                "obj83(SeaWorldLongReefZone[obj1921])": 3470,
                "obj85(SeaWorldLongReefZone[obj1921])": 3471,
                "obj84(SeaWorldLongReefZone[obj1921])": 3472,
                "obj89(SeaWorldSphinxQuizZone[obj2084])": 3474,
                "obj90(SeaWorldSphinxQuizZone[obj2084])": 3475,
                "obj103(SeaWorldSphinxQuizZone[obj2084])": 3476,
                "obj149(SeaWorldUnderGlassZone[obj1898])": 3478,
                "obj150(SeaWorldUnderGlassZone[obj1898])": 3479,
                "obj151(SeaWorldUnderGlassZone[obj1898])": 3480,
                "obj346(SeaWorldUnderGlassZone[obj1898])": 3482,
                "obj347(SeaWorldUnderGlassZone[obj1898])": 3483,
                "obj348(SeaWorldUnderGlassZone[obj1898])": 3484,
                "obj46(SeaWorldWallCaveWestZone[obj1354])": 3486,
                "obj47(SeaWorldWallCaveWestZone[obj1354])": 3487,
                "obj48(SeaWorldWallCaveWestZone[obj1354])": 3488,
        },

        "SeaWorldUtsuboCaveStage": {
                "obj484": 3490,
                "obj485": 3491,
                "obj486": 3492,
                "obj487": 3494,
                "obj488": 3495,
                "obj489": 3496,
        },
        #endregion

# Find way to differentiate Shiveria Coins
# Likely use other placementId information
        #region Snow Coins
        "SnowWorldHomeStage": {
                "obj1150": 3498,
                "obj1151": 3499,
                "obj1348": 3501,
                "obj1350": 3502,
                "obj1426": 3504,
                "obj1427": 3505,
                "obj1428": 3506,
                "obj1432": 3508,
                "obj1433": 3509,
                "obj1533": 3511,
                "obj1534": 3512,
                "obj1535": 3514,
                "obj1536": 3515,
        },

        "SnowWorldTownStage": {
                "obj1097": 3517,
                "obj1098": 3518,
                "obj1099": 3519,
                "obj1100": 3520,
                "obj169(SnowWorldBalconyZone[obj534])": 3522,
                "obj170(SnowWorldBalconyZone[obj534])": 3523,
                "obj251(SnowWorldBalconyZone[obj534])": 3524,
                "obj271(SnowWorldBalconyZone[obj534])": 3526,
                "obj272(SnowWorldBalconyZone[obj534])": 3527,
                "obj273(SnowWorldBalconyZone[obj534])": 3528,
                "obj222(SnowWorldByugoZone[obj420])": 3530,
                "obj223(SnowWorldByugoZone[obj420])": 3531,
                "obj224(SnowWorldByugoZone[obj420])": 3532,
                "obj225(SnowWorldByugoZone[obj420])": 3533,
                "obj273(SnowWorldByugoZone[obj420])": 3535,
                "obj274(SnowWorldByugoZone[obj420])": 3536,
                "obj275(SnowWorldByugoZone[obj420])": 3537,
                "obj273(SnowWorldGabuzouZone[obj419])": 3539,
                "obj274(SnowWorldGabuzouZone[obj419])": 3540,
                "obj275(SnowWorldGabuzouZone[obj419])": 3541,
                "obj281(SnowWorldGabuzouZone[obj419])": 3543,
                "obj282(SnowWorldGabuzouZone[obj419])": 3544,
                "obj283(SnowWorldGabuzouZone[obj419])": 3545,
                "obj380(SnowWorldIcicleZone[obj417])": 3547,
                "obj381(SnowWorldIcicleZone[obj417])": 3548,
                "obj448(SnowWorldIcicleZone[obj417])": 3549,
                "obj449(SnowWorldIcicleZone[obj417])": 3550,
                "obj421(SnowWorldIcicleZone[obj417])": 3552,
                "obj422(SnowWorldIcicleZone[obj417])": 3553,
                "obj423(SnowWorldIcicleZone[obj417])": 3554,

        },

        "SnowWorldLobby000Stage" : {
                "obj923": 3556,
                "obj924": 3557,
                "obj925": 3558,
                "obj926": 3559,
                "obj927": 3561,
                "obj928": 3562,
                "obj929": 3563,
        },
        #endregion

        #region Luncheon Coins
        "LavaWorldHomeStage": {
                "obj2675": 3565,
                "obj2746": 3566,
                "obj2747": 3567,
                "obj2678": 3569,
                "obj2679": 3570,
                "obj2680": 3571,
                "obj2685": 3573,
                "obj2686": 3574,
                "obj2688": 3575,
                "obj2690": 3577,
                "obj2692": 3578,
                "obj2691": 3579,
                "obj2713": 3581,
                "obj2774": 3582,
                "obj2773": 3583,
                "obj2717": 3585,
                "obj2775": 3586,
                "obj4287": 3587,
                "obj2722": 3589,
                "obj2728": 3590,
                "obj2735": 3592,
                "obj2736": 3593,
                "obj2737": 3594,
                "obj2738": 3595,
                "obj2740": 3596,
                "obj2741": 3597,
                "obj2742": 3598,
                "obj2744": 3600,
                "obj5567": 3601,
                "obj5568": 3602,
                "obj2748": 3604,
                "obj2756": 3605,
                "obj2760": 3606,
                "obj2750": 3608,
                "obj2754": 3609,
                "obj2758": 3610,
                "obj2762": 3611,
                "obj2751": 3612,
                "obj2755": 3614,
                "obj2785": 3615,
                "obj2783": 3616,
                "obj2757": 3618,
                "obj2761": 3619,
                "obj3489": 3620,
                "obj3490": 3621,
                "obj2784": 3623,
                "obj2787": 3624,
                "obj3123": 3625,
                "obj3310": 3627,
                "obj3311": 3628,
                "obj3312": 3629,
                "obj3808": 3631,
                "obj3809": 3632,
                "obj3810": 3633,
                "obj3843": 3635,
                "obj3844": 3636,
                "obj3845": 3637,
                "obj5656": 3639,
                "obj5657": 3640,
                "obj5658": 3641,
                "obj6254": 3643,
                "obj6255": 3644,
                "obj6256": 3645,
                "obj6363": 3647,
                "obj6364": 3648,
                "obj6365": 3649,
                "obj2659": 3651,
                "obj2660": 3652,
                "obj2663": 3653,
                "obj2664": 3654,
                "obj2662": 3656,
                "obj3740": 3657,
                "obj3738": 3658,
                "obj2665": 3660,
                "obj2666": 3661,
                "obj3991": 3662,
                "obj3910": 3664,
                "obj3911": 3665,
                "obj3912": 3666,
                "obj6347": 3668,
                "obj6348": 3669,
                "obj6349": 3670,
                "obj6366": 3672,
                "obj6367": 3673,
                "obj6368": 3674,
                "obj185(LavaWorldCaveZone[obj3426])": 3676,
                "obj208(LavaWorldCaveZone[obj3426])": 3677,
                "obj209(LavaWorldCaveZone[obj3426])": 3678,
                "obj187(LavaWorldCaveZone[obj3426])": 3680,
                "obj206(LavaWorldCaveZone[obj3426])": 3681,
                "obj207(LavaWorldCaveZone[obj3426])": 3682,
        },

        "LavaWorldBubbleLaneExStage": {
                "obj807": 3684,
                "obj808": 3685,
                "obj809": 3686,
        },

        "LavaWorldClockExStage": {
                "obj431": 3688,
                "obj432": 3689,
                "obj433": 3690,
        },

        "ForkExStage": {
                "obj5762": 3692,
                "obj5763": 3693,
                "obj5764": 3694,
        },
        #endregion

        #region Bowser Coins
        "SkyWorldHomeStage": {
                "obj653(SkyWorldCastleZone[obj2160])": 3696,
                "obj4377(SkyWorldCastleZone[obj2160])": 3697,
                "obj6733(SkyWorldCastleZone[obj2160])": 3698,
                "obj1692(SkyWorldCastleZone[obj2160])": 3700,
                "obj1694(SkyWorldCastleZone[obj2160])": 3701,
                "obj1693(SkyWorldCastleZone[obj2160])": 3702,
                "obj1800(SkyWorldCastleZone[obj2160])": 3704,
                "obj1918(SkyWorldCastleZone[obj2160])": 3705,
                "obj2450(SkyWorldCastleZone[obj2160])": 3706,
                "obj1801(SkyWorldCastleZone[obj2160])": 3708,
                "obj1917(SkyWorldCastleZone[obj2160])": 3709,
                "obj2451(SkyWorldCastleZone[obj2160])": 3710,
                "obj1955(SkyWorldCastleZone[obj2160])": 3712,
                "obj1958(SkyWorldCastleZone[obj2160])": 3713,
                "obj1956(SkyWorldCastleZone[obj2160])": 3714,
                "obj2306(SkyWorldCastleZone[obj2160])": 3716,
                "obj2307(SkyWorldCastleZone[obj2160])": 3717,
                "obj2308(SkyWorldCastleZone[obj2160])": 3718,
                "obj2309(SkyWorldCastleZone[obj2160])": 3719,
                "obj2501(SkyWorldCastleZone[obj2160])": 3721,
                "obj2512(SkyWorldCastleZone[obj2160])": 3722,
                "obj2514(SkyWorldCastleZone[obj2160])": 3723,
                "obj2726(SkyWorldCastleZone[obj2160])": 3725,
                "obj2727(SkyWorldCastleZone[obj2160])": 3726,
                "obj2728(SkyWorldCastleZone[obj2160])": 3727,
                "obj3233(SkyWorldCastleZone[obj2160])": 3729,
                "obj3234(SkyWorldCastleZone[obj2160])": 3730,
                "obj3235(SkyWorldCastleZone[obj2160])": 3731,
                "obj5680(SkyWorldCastleZone[obj2160])": 3733,
                "obj5681(SkyWorldCastleZone[obj2160])": 3734,
                "obj8169(SkyWorldCastleZone[obj2160])": 3735,
                "obj6735(SkyWorldCastleZone[obj2160])": 3737,
                "obj6736(SkyWorldCastleZone[obj2160])": 3738,
                "obj6737(SkyWorldCastleZone[obj2160])": 3739,
                "obj7322(SkyWorldCastleZone[obj2160])": 3741,
                "obj7324(SkyWorldCastleZone[obj2160])": 3742,
                "obj7323(SkyWorldCastleZone[obj2160])": 3743,
                "obj650": 3745,
                "obj2132": 3746,
                "obj2133": 3747,
                "obj2714": 3749,
                "obj6734": 3750,
                "obj7583": 3751,
                "obj2715": 3753,
                "obj5705": 3754,
                "obj5682": 3755,
                "obj3634": 3757,
                "obj3635": 3758,
                "obj3636": 3759,
                "obj5706": 3761,
                "obj5708": 3762,
                "obj5707": 3763,
                "obj5735": 3765,
                "obj5736": 3766,
                "obj5737": 3767,
                "obj7580": 3769,
                "obj7581": 3770,
                "obj7582": 3771,
                "obj1722": 3773,
                "obj1723": 3774,
                "obj1724": 3775,
                "obj1925": 3776,
                "obj1808": 3778,
                "obj1810": 3779,
                "obj2073": 3780,
                "obj2828": 3781,
                "obj1970": 3783,
                "obj1971": 3784,
                "obj1972": 3785,
                "obj2272": 3787,
                "obj2273": 3788,
                "obj2274": 3789,
                "obj2829": 3790,
                "obj2830": 3792,
                "obj2841": 3793,
                "obj2831": 3795,
                "obj2832": 3796,
                "obj959(SkyWorldWallZone[obj2161])": 3798,
                "obj960(SkyWorldWallZone[obj2161])": 3799,
                "obj1227(SkyWorldWallZone[obj2161])": 3800,
                "obj1503(SkyWorldWallZone[obj2161])": 3802,
                "obj1504(SkyWorldWallZone[obj2161])": 3803,
                "obj2159(SkyWorldWallZone[obj2161])": 3804,
                "obj1612(SkyWorldWallZone[obj2161])": 3806,
                "obj1832(SkyWorldWallZone[obj2161])": 3807,
                "obj1613(SkyWorldWallZone[obj2161])": 3808,
                "obj1872(SkyWorldWallZone[obj2161])": 3810,
                "obj1873(SkyWorldWallZone[obj2161])": 3811,
                "obj2192(SkyWorldWallZone[obj2161])": 3812,
                "obj1964(SkyWorldWallZone[obj2161])": 3814,
                "obj1965(SkyWorldWallZone[obj2161])": 3815,
                "obj1967(SkyWorldWallZone[obj2161])": 3816,
                "obj2238(SkyWorldWallZone[obj2161])": 3817,
                "obj2097(SkyWorldWallZone[obj2161])": 3819,
                "obj2098(SkyWorldWallZone[obj2161])": 3820,
                "obj2099(SkyWorldWallZone[obj2161])": 3821,
                "obj2218(SkyWorldWallZone[obj2161])": 3823,
                "obj2219(SkyWorldWallZone[obj2161])": 3824,
                "obj2220(SkyWorldWallZone[obj2161])": 3825,
                "obj2237(SkyWorldWallZone[obj2161])": 3826,
        },
        #endregion

        #region Moon Coins
        "MoonWorldHomeStage": {
                "obj71(MoonWorldHome2DZone[obj638])": 3828,
                "obj72(MoonWorldHome2DZone[obj638])": 3829,
                "obj73(MoonWorldHome2DZone[obj638])": 3830,
                "obj94": 3832,
                "obj95": 3833,
                "obj96": 3834,
                "obj97": 3835,
                "obj197": 3837,
                "obj198": 3838,
                "obj199": 3839,
                "obj836": 3840,
                "obj837": 3841,
                "obj838": 3842,
                "obj469": 3844,
                "obj470": 3845,
                "obj471": 3846,
                "obj516": 3848,
                "obj517": 3849,
                "obj518": 3850,
                "obj519": 3851,
                "obj672": 3853,
                "obj673": 3854,
                "obj700": 3855,
                "obj805": 3857,
                "obj806": 3858,
                "obj807": 3859,
                "obj811": 3861,
                "obj812": 3862,
                "obj814": 3863,
                "obj859": 3865,
                "obj860": 3866,
                "obj861": 3867,
        },

        "MoonWorldCaptureParadeStage": {
                "obj6104": 3869,
                "obj6105": 3870,
                "obj6106": 3871,
                "obj239": 3873,
                "obj240": 3874,
                "obj412": 3875,
                "obj430": 3877,
                "obj461": 3878,
                "obj462": 3879,
                "obj405": 3881,
                "obj406": 3882,
                "obj407": 3883,
                "obj21": 3885,
                "obj22": 3886,
                "obj23": 3887,
                "obj193": 3889,
                "obj194": 3890,
                "obj195": 3891,
        },
        #endregion

        #region Mushroom Coins
        "PeachWorldHomeStage": {
                "obj740": 3893,
                "obj741": 3894,
                "obj742": 3895,
                "obj847": 3896,
                "obj848": 3898,
                "obj849": 3899,
                "obj850": 3900,
                "obj851": 3902,
                "obj852": 3903,
                "obj853": 3904,
                "obj854": 3906,
                "obj855": 3907,
                "obj856": 3908,
                "obj857": 3910,
                "obj871": 3911,
                "obj872": 3912,
                "obj873": 3913,
                "obj858": 3915,
                "obj859": 3916,
                "obj860": 3917,
                "obj861": 3919,
                "obj862": 3920,
                "obj863": 3921,
                "obj864": 3923,
                "obj865": 3924,
                "obj866": 3925,
                "obj867": 3927,
                "obj877": 3928,
                "obj887": 3929,
                "obj874": 3931,
                "obj875": 3932,
                "obj876": 3933,
                "obj878": 3935,
                "obj879": 3936,
                "obj880": 3937,
                "obj881": 3939,
                "obj882": 3940,
                "obj883": 3941,
                "obj884": 3943,
                "obj885": 3944,
                "obj886": 3945,
                "obj888": 3947,
                "obj889": 3948,
                "obj890": 3949,
                "obj1286": 3951,
                "obj1287": 3952,
                "obj1288": 3953,
                "obj1289": 3955,
                "obj1290": 3956,
                "obj1291": 3957,
                "obj1811": 3959,
                "obj1813": 3960,
                "obj1812": 3961,
                "obj1900": 3963,
                "obj1901": 3964,
                "obj1902": 3965,
                "obj1903": 3966,
                "obj1920": 3968,
                "obj1921": 3969,
                "obj1922": 3970,
                "obj1923": 3972,
                "obj1924": 3973,
                "obj1927": 3975,
                "obj1928": 3976,
                "obj1929": 3978,
                "obj1930": 3979,
                "obj1931": 3980,
                "obj1932": 3982,
                "obj1933": 3983,
                "obj1934": 3984,
                "obj1950": 3986,
                "obj1951": 3987,
                "obj1952": 3988,
                "obj1953": 3990,
                "obj1954": 3991,
                "obj1955": 3992,
                "obj1956": 3994,
                "obj1957": 3995,
                "obj1958": 3996,
                "obj1977": 3998,
                "obj1978": 3999,
                "obj1979": 4000,
                "obj1980": 4002,
                "obj1981": 4003,
                "obj1982": 4004,
                "obj1994": 4006,
                "obj1995": 4007,
                "obj1996": 4008,
                "obj2216": 4010,
                "obj2217": 4011,
                "obj2218": 4012,
                "obj2644": 4014,
                "obj2645": 4015,
                "obj2646": 4016,
                "obj1904": 4018,
                "obj1905": 4019,
                "obj1906": 4020,
        },

        "PeachWorldCastleStage": {
                "obj92": 4022,
                "obj93": 4023,
                "obj94": 4024,
        },
        #endregion
}

regional_sub_area_to_kingdom = {
        "cap": [
                SMORegion.frog_pond_regional_coins,
                SMORegion.frog_pond_regional_groups,
                SMORegion.push_blocks_regional_coins,
                SMORegion.push_blocks_regional_groups,
                SMORegion.poison_tides_regional_coins,
                SMORegion.poison_tides_regional_groups,
                SMORegion.top_hat_tower_regional_coins,
                SMORegion.top_hat_tower_regional_groups,
        ],
        "cascade": [
                SMORegion.chasm_lifts_regional_coins,
                SMORegion.chasm_lifts_regional_groups,
        ],
        "sand": [
                SMORegion.ice_cave_regional_coins,
                SMORegion.ice_cave_regional_groups,
                SMORegion.jaxi_ruins_regional_coins,
                SMORegion.jaxi_ruins_regional_groups,
                SMORegion.strange_neighborhood_regional_coins,
                SMORegion.strange_neighborhood_regional_groups,
                SMORegion.pyramid_upper_interior_regional_coins,
                SMORegion.pyramid_upper_interior_regional_groups,
                SMORegion.bullet_bill_maze_regional_coins,
                SMORegion.bullet_bill_maze_regional_groups,
                SMORegion.moe_eye_invisible_maze_regional_coins,
                SMORegion.moe_eye_invisible_maze_regional_groups,
                SMORegion.underground_ruins_regional_coins,
                SMORegion.underground_ruins_regional_groups,
        ],
        "lake": [
                SMORegion.bouncy_flowers_regional_coins,
                SMORegion.bouncy_flowers_regional_groups
        ],
        "wooded": [
                SMORegion.sky_garden_tower_regional_coins,
                SMORegion.sky_garden_tower_regional_groups,
                SMORegion.deep_woods_regional_coins,
                SMORegion.deep_woods_regional_groups,
                SMORegion.flooded_pipes_regional_coins,
                SMORegion.flooded_pipes_regional_groups,
                SMORegion.sherm_elevator_regional_coins,
                SMORegion.sherm_elevator_regional_groups,
                SMORegion.wooded_flower_road_regional_coins,
                SMORegion.wooded_flower_road_regional_groups,
                SMORegion.walking_on_clouds_regional_coins,
                SMORegion.walking_on_clouds_regional_groups,
        ],
        "metro": [
                SMORegion.city_hall_regional_coins,
                SMORegion.city_hall_regional_groups,
                SMORegion.high_rise_regional_coins,
                SMORegion.high_rise_regional_groups,
                SMORegion.bullet_billding_regional_coins,
                SMORegion.bullet_billding_regional_groups,
                SMORegion.trex_escape_regional_coins,
                SMORegion.trex_escape_regional_groups,
                SMORegion.sewers_regional_coins,
                SMORegion.sewers_regional_groups,

        ],
        "snow": [
                SMORegion.shiveria_regional_coins,
                SMORegion.shiveria_regional_groups,
                SMORegion.snowline_regional_coins,
                SMORegion.snowline_regional_groups,
        ],
        "seaside": [
                SMORegion.sea_cave_regional_coins,
                SMORegion.sea_cave_regional_groups,
        ],
        "luncheon": [
                SMORegion.cascading_magma_regional_coins,
                SMORegion.cascading_magma_regional_groups,
                SMORegion.spinning_athletics_regional_coins,
                SMORegion.spinning_athletics_regional_groups,
                SMORegion.magma_narrow_path_regional_coins,
                SMORegion.magma_narrow_path_regional_groups,
                SMORegion.fork_flickin_regional_coins,
                SMORegion.fork_flickin_regional_groups,
        ],
        "moon": [
                SMORegion.moon_cave_regional_coins,
                SMORegion.moon_cave_regional_groups,
        ],
        "mushroom": [
                SMORegion.peachs_castle_regional_coins,
                SMORegion.peachs_castle_regional_groups,
        ]
}

#endregion

# Contains rule information for sop locations
shop_location_costs = [
        (SMOLocationData.black_top_hat, Goal.option_sand, SMOKingdoms.CAP, 5),
        (SMOLocationData.black_tuxedo, Goal.option_sand,SMOKingdoms.CAP, 10),
        (SMOLocationData.plush_frog, Goal.option_sand,SMOKingdoms.CAP, 5),
        (SMOLocationData.bonneton_tower_model, Goal.option_sand,SMOKingdoms.CAP, 25),
        (SMOLocationData.cap_kingdom_sticker, Goal.option_sand,SMOKingdoms.CAP, 5),
        (SMOLocationData.caveman_headwear, Goal.option_sand,SMOKingdoms.CASCADE, 5),
        (SMOLocationData.caveman_outfit, Goal.option_sand,SMOKingdoms.CASCADE, 10),
        (SMOLocationData.t_rex_model, Goal.option_sand,SMOKingdoms.CASCADE, 5),
        (SMOLocationData.triceratops_trophy, Goal.option_sand,SMOKingdoms.CASCADE, 25),
        (SMOLocationData.cascade_kingdom_sticker, Goal.option_sand,SMOKingdoms.CASCADE, 5),
        (SMOLocationData.sombrero, Goal.option_sand,SMOKingdoms.SAND, 5),
        (SMOLocationData.poncho, Goal.option_sand,SMOKingdoms.SAND, 10),
        (SMOLocationData.cowboy_hat, Goal.option_sand,SMOKingdoms.SAND, 20),
        (SMOLocationData.cowboy_outfit, Goal.option_sand,SMOKingdoms.SAND, 25),
        (SMOLocationData.inverted_pyramid_model, Goal.option_sand,SMOKingdoms.SAND, 25),
        (SMOLocationData.jaxi_statue, Goal.option_sand,SMOKingdoms.SAND, 5),
        (SMOLocationData.sand_kingdom_sticker, Goal.option_sand,SMOKingdoms.SAND, 10),
        (SMOLocationData.swim_goggles, Goal.option_metro,SMOKingdoms.LAKE, 5),
        (SMOLocationData.swimwear, Goal.option_metro,SMOKingdoms.LAKE, 10),
        (SMOLocationData.underwater_dome, Goal.option_metro,SMOKingdoms.LAKE, 25),
        (SMOLocationData.rubber_dorrie, Goal.option_metro,SMOKingdoms.LAKE, 5),
        (SMOLocationData.lake_kingdom_sticker, Goal.option_metro,SMOKingdoms.LAKE, 5),
        (SMOLocationData.explorer_hat, Goal.option_metro,SMOKingdoms.WOODED, 5),
        (SMOLocationData.explorer_outfit, Goal.option_metro,SMOKingdoms.WOODED, 10),
        (SMOLocationData.scientist_visor, Goal.option_metro,SMOKingdoms.WOODED, 20),
        (SMOLocationData.scientist_outfit, Goal.option_metro,SMOKingdoms.WOODED, 25),
        (SMOLocationData.flowers_from_steam_gardens, Goal.option_metro,SMOKingdoms.WOODED, 5),
        (SMOLocationData.steam_gardener_watering_can, Goal.option_metro,SMOKingdoms.WOODED, 25),
        (SMOLocationData.wooded_kingdom_sticker, Goal.option_metro,SMOKingdoms.WOODED, 10),
        (SMOLocationData.aviator_cap, Goal.option_metro,SMOKingdoms.LOST, 5),
        (SMOLocationData.aviator_outfit, Goal.option_metro,SMOKingdoms.LOST, 10),
        (SMOLocationData.potted_palm_tree, Goal.option_metro,SMOKingdoms.LOST, 5),
        (SMOLocationData.butterfly_mobile, Goal.option_metro,SMOKingdoms.LOST, 25),
        (SMOLocationData.lost_kingdom_sticker, Goal.option_metro,SMOKingdoms.LOST, 5),
        (SMOLocationData.builder_helmet, Goal.option_metro,SMOKingdoms.METRO, 5),
        (SMOLocationData.builder_outfit, Goal.option_metro,SMOKingdoms.METRO, 10),
        (SMOLocationData.golf_cap, Goal.option_metro,SMOKingdoms.METRO, 20),
        (SMOLocationData.golf_outfit, Goal.option_metro,SMOKingdoms.METRO, 25),
        (SMOLocationData.pauline_statue, Goal.option_metro,SMOKingdoms.METRO, 25),
        (SMOLocationData.new_donk_city_hall_model, Goal.option_metro,SMOKingdoms.METRO, 5),
        (SMOLocationData.metro_kingdom_sticker, Goal.option_metro,SMOKingdoms.METRO, 10),
        (SMOLocationData.snow_hood, Goal.option_luncheon,SMOKingdoms.SNOW, 5),
        (SMOLocationData.snow_suit, Goal.option_luncheon,SMOKingdoms.SNOW, 10),
        (SMOLocationData.snow_kingdom_sticker, Goal.option_luncheon,SMOKingdoms.SNOW, 5),
        (SMOLocationData.shiverian_rug, Goal.option_luncheon,SMOKingdoms.SNOW, 5),
        (SMOLocationData.shiverian_nesting_dolls, Goal.option_luncheon,SMOKingdoms.SNOW, 25),
        (SMOLocationData.resort_hat, Goal.option_luncheon,SMOKingdoms.SEASIDE, 5),
        (SMOLocationData.resort_outfit, Goal.option_luncheon,SMOKingdoms.SEASIDE, 10),
        (SMOLocationData.sailor_hat, Goal.option_luncheon,SMOKingdoms.SEASIDE, 20),
        (SMOLocationData.sailor_suit, Goal.option_luncheon,SMOKingdoms.SEASIDE, 25),
        (SMOLocationData.seaside_kingdom_sticker, Goal.option_luncheon,SMOKingdoms.SEASIDE, 10),
        (SMOLocationData.sand_jar, Goal.option_luncheon,SMOKingdoms.SEASIDE, 5),
        (SMOLocationData.glass_tower_model, Goal.option_luncheon,SMOKingdoms.SEASIDE, 25),
        (SMOLocationData.chef_hat, Goal.option_luncheon,SMOKingdoms.LUNCHEON, 5),
        (SMOLocationData.chef_suit, Goal.option_luncheon,SMOKingdoms.LUNCHEON, 10),
        (SMOLocationData.painters_cap, Goal.option_luncheon,SMOKingdoms.LUNCHEON, 20),
        (SMOLocationData.painter_outfit, Goal.option_luncheon,SMOKingdoms.LUNCHEON, 25),
        (SMOLocationData.luncheon_kingdom_sticker, Goal.option_luncheon,SMOKingdoms.LUNCHEON, 10),
        (SMOLocationData.souvenir_forks, Goal.option_luncheon,SMOKingdoms.LUNCHEON, 5),
        (SMOLocationData.vegetable_plate, Goal.option_luncheon,SMOKingdoms.LUNCHEON, 25),
        (SMOLocationData.samurai_helmet, Goal.option_moon,SMOKingdoms.BOWSER, 5),
        (SMOLocationData.samurai_armor, Goal.option_moon,SMOKingdoms.BOWSER, 10),
        (SMOLocationData.happi_headband, Goal.option_moon,SMOKingdoms.BOWSER, 20),
        (SMOLocationData.happi_outfit, Goal.option_moon,SMOKingdoms.BOWSER, 25),
        (SMOLocationData.bowsers_kingdom_sticker, Goal.option_moon,SMOKingdoms.BOWSER, 10),
        (SMOLocationData.paper_lantern, Goal.option_moon,SMOKingdoms.BOWSER, 5),
        (SMOLocationData.jizo_statue, Goal.option_moon,SMOKingdoms.BOWSER, 25),
        (SMOLocationData.space_helmet, Goal.option_moon,SMOKingdoms.MOON, 5),
        (SMOLocationData.space_suit, Goal.option_moon,SMOKingdoms.MOON, 10),
        (SMOLocationData.moon_kingdom_sticker, Goal.option_moon,SMOKingdoms.MOON, 5),
        (SMOLocationData.moon_rock_fragment, Goal.option_moon,SMOKingdoms.MOON, 5),
        (SMOLocationData.moon_lamp, Goal.option_moon,SMOKingdoms.MOON, 25),
        (SMOLocationData.mario_64_cap, Goal.option_dark,SMOKingdoms.MUSHROOM, 15),
        (SMOLocationData.mario_64_suit, Goal.option_dark,SMOKingdoms.MUSHROOM, 20),
        (SMOLocationData.mushroom_cushion_set, Goal.option_dark,SMOKingdoms.MUSHROOM, 10),
        (SMOLocationData.peachs_castle_model, Goal.option_dark,SMOKingdoms.MUSHROOM, 25),
        (SMOLocationData.pipe_sticker, Goal.option_dark,SMOKingdoms.MUSHROOM, 5),
        (SMOLocationData.coin_sticker, Goal.option_dark,SMOKingdoms.MUSHROOM, 5),
        (SMOLocationData.block_sticker, Goal.option_dark,SMOKingdoms.MUSHROOM, 5),
        (SMOLocationData.question_block_sticker, Goal.option_dark,SMOKingdoms.MUSHROOM, 5),
        (SMOLocationData.mushroom_kingdom_sticker, Goal.option_dark,SMOKingdoms.MUSHROOM, 10),
]
# possibly add coin outfits
# might need to reorder if purple coin amounts don't match in game (YES)


locations_table = {
        **base_locations_table,
        **post_game_locations_table,
        **special_locations_table,
        **shop_locations_table,
        **loc_Post_Cloud,
        **loc_Moon_Post_Moon,
        **loc_Captures,
        **sub_area_table,
        **regional_coin_groups_table,
        **regional_coin_table
}

locations_list = [
    loc_Cap,
    {**loc_Cascade, **loc_Cascade_Peace, **loc_Cascade_Revisit, **loc_Cascade_Post_Snow, **loc_Cascade_Post_Metro},
    {**loc_Sand, **loc_Sand_Peace, **loc_Sand_Revisit, **loc_Sand_Pyramid},
    {**loc_Lake, **loc_Lake_Post_Seaside},
    {**loc_Wooded, **loc_Wooded_Post_Story1, **loc_Wooded_Peace, **loc_Wooded_Post_Metro},
    loc_Cloud,
    {**loc_Lost, **loc_Lost_Revisit},
    {**loc_Night_Metro, **loc_Metro, **loc_Metro_Sewer_Access, **loc_Metro_Peace, **loc_Metro_Post_Sand},
    {**loc_Snow, **loc_Snow_Peace},
    {**loc_Seaside, **loc_Seaside_Peace},
    {**loc_Luncheon, **loc_Luncheon_Post_Spewart, **loc_Luncheon_Post_Cheese_Rocks, **loc_Luncheon_Peace, **loc_Luncheon_Post_Wooded},
    loc_Ruined,
    {**loc_Bowser, **loc_Bowser_Infiltrate, **loc_Bowser_Post_Bombing, **loc_Bowser_Mecha_Broodal, **loc_Bowser_Peace},
    loc_Moon,
    {**loc_Mushroom, **loc_Mushroom_Post_Luncheon},
    loc_Dark,
    loc_Darker
]

full_moon_locations_list = [
    {**loc_Cap, **loc_Cap_Postgame},
    {**loc_Cascade, **loc_Cascade_Peace, **loc_Cascade_Revisit, **loc_Cascade_Post_Snow, **loc_Cascade_Post_Metro, **loc_Cascade_Postgame},
    {**loc_Sand, **loc_Sand_Peace, **loc_Sand_Revisit, **loc_Sand_Pyramid, **loc_Sand_Underground, **loc_Sand_Postgame},
    {**loc_Wooded, **loc_Wooded_Post_Story1, **loc_Wooded_Peace, **loc_Wooded_Post_Metro, **loc_Wooded_Postgame},
    {**loc_Lake, **loc_Lake_Post_Seaside, **loc_Lake_Postgame},
    {**loc_Cloud, **loc_Cloud_Postgame},
    {**loc_Lost, **loc_Lost_Revisit, **loc_Lost_Postgame},
    {**loc_Night_Metro, **loc_Metro, **loc_Metro_Sewer_Access, **loc_Metro_Peace, **loc_Metro_Post_Sand, **loc_Metro_Postgame},
    {**loc_Seaside, **loc_Seaside_Peace, **loc_Seaside_Postgame},
    {**loc_Snow, **loc_Snow_Peace, **loc_Snow_Postgame},
    {**loc_Luncheon, **loc_Luncheon_Post_Spewart, **loc_Luncheon_Post_Cheese_Rocks, **loc_Luncheon_Peace, **loc_Luncheon_Post_Wooded, **loc_Luncheon_Postgame},
    {**loc_Ruined, **loc_Ruined_Postgame},
    {**loc_Bowser, **loc_Bowser_Infiltrate, **loc_Bowser_Post_Bombing, **loc_Bowser_Mecha_Broodal, **loc_Bowser_Peace, **loc_Bowser_Postgame},
    {**loc_Moon, **loc_Moon_Postgame},
    {**loc_Mushroom, **loc_Mushroom_Post_Luncheon},
    loc_Dark,
    loc_Darker
]

post_game_locations_list = [
    loc_Cap_Postgame,
    loc_Cascade_Postgame,
    loc_Sand_Postgame,
    loc_Lake_Postgame,
    loc_Wooded_Postgame,
    loc_Cloud_Postgame,
    loc_Lost_Postgame,
    loc_Metro_Postgame,
    loc_Snow_Postgame,
    loc_Seaside_Postgame,
    loc_Luncheon_Postgame,
    loc_Ruined_Postgame,
    loc_Bowser_Postgame,
    loc_Moon_Postgame
]

multi_moons = {
    "Cascade" : [SMOLocationData.multi_moon_atop_the_falls],
    "Sand" : [SMOLocationData.showdown_on_the_inverted_pyramid,
    SMOLocationData.the_hole_in_the_desert],
    "Lake" : [SMOLocationData.broodals_over_the_lake],
    "Wooded" : [SMOLocationData.flower_thieves_of_sky_garden,
    SMOLocationData.defend_the_secret_flower_field],
    "Metro" : [SMOLocationData.new_donk_citys_pest_problem,
    SMOLocationData.a_traditional_festival],
    "Snow" : [SMOLocationData.the_bound_bowl_grand_prix],
    "Seaside" : [SMOLocationData.the_glass_is_half_full],
    "Luncheon" : [SMOLocationData.big_pot_on_the_volcano_dive_in,
    SMOLocationData.cookatiel_showdown],
    "Ruined" : [SMOLocationData.battle_with_the_lord_of_lightning],
    "Bowser" : [SMOLocationData.showdown_at_bowsers_castle],
    "Mushroom" : [SMOLocationData.tussle_in_tostarena_rematch,
    SMOLocationData.struggle_in_steam_gardens_rematch,
    SMOLocationData.dust_up_in_new_donk_city_rematch,
    SMOLocationData.battle_in_bubblaine_rematch,
    SMOLocationData.blowup_at_mount_volbono_rematch,
    SMOLocationData.rumble_in_crumbleden_rematch],
    "Dark" : [SMOLocationData.arrival_at_rabbit_ridge],
    "Darker" : [SMOLocationData.a_long_journeys_end]
}

story_moons = {
    "Cascade" : [SMOLocationData.our_first_power_moon],
    "Sand" : [SMOLocationData.atop_the_highest_tower,
    SMOLocationData.moon_shards_in_the_sand],
    "Wooded" : [SMOLocationData.road_to_sky_garden,
    SMOLocationData.path_to_the_secret_flower_field],
    "Metro" : [SMOLocationData.drummer_on_board,
    SMOLocationData.guitarist_on_board,
    SMOLocationData.bassist_on_board,
    SMOLocationData.trumpeter_on_board,
    SMOLocationData.powering_up_the_station],
    "Snow" : [SMOLocationData.the_icicle_barrier,
    SMOLocationData.the_ice_wall_barrier,
    SMOLocationData.the_gusty_barrier,
    SMOLocationData.the_snowy_mountain_barrier],
    "Seaside" : [SMOLocationData.the_stone_pillar_seal,
    SMOLocationData.the_lighthouse_seal,
    SMOLocationData.the_hot_spring_seal,
    SMOLocationData.the_seal_above_the_canyon],
    "Luncheon" : [SMOLocationData.the_broodals_are_after_some_cookin,
    SMOLocationData.under_the_cheese_rocks,
    SMOLocationData.climb_up_the_cascading_magma],
    "Bowser" : [SMOLocationData.infiltrate_bowsers_castle,
    SMOLocationData.smart_bombing,
    SMOLocationData.big_broodal_battle]
}

goals_table = {
    Goal.option_sand : SMOLocationData.the_hole_in_the_desert,
    Goal.option_lake : SMOLocationData.broodals_over_the_lake,
    Goal.option_cloud : SMOLocationData.beat_bowser_in_cloud,
    Goal.option_metro : SMOLocationData.a_traditional_festival,
    Goal.option_luncheon : SMOLocationData.cookatiel_showdown,
    Goal.option_moon : SMOLocationData.beat_the_game,
    Goal.option_dark : SMOLocationData.arrival_at_rabbit_ridge,
    Goal.option_darker : SMOLocationData.a_long_journeys_end
}