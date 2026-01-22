from BaseClasses import Location

class SMOLocation(Location):
    game: str = "Super Mario Odyssey"

# Cap
loc_Cap = {
    "Cap Kingdom - Frog-jumping above the Fog": 1019,
    "Cap Kingdom - Frog-jumping from the Top Deck": 815,
    "Cap Kingdom - Cap Kingdom Timer Challenge 1": 861,
    "Cap Kingdom - Good Evening, Captain Toad!": 227,
    "Cap Kingdom - Shopping in Bonneton": 230,
    "Cap Kingdom - Skimming the Poison Tide": 236,
    "Cap Kingdom - Slipping through the Poison Tide": 237,
    "Cap Kingdom - Push-Block Peril": 234,
    "Cap Kingdom - Hidden Among the Push-Blocks": 235,
    "Cap Kingdom - Searching the Frog Pond": 238,
    "Cap Kingdom - Secrets of the Frog Pond": 239,
}

sub_area_frog = {
    "Searching the Frog Pond": 238,
    "Secrets of the Frog Pond": 239,
}

sub_area_poison_tide = {
    "Skimming the Poison Tide": 236,
    "Slipping through the Poison Tide": 237,
}

sub_area_push_block = {
    "Push-Block Peril": 234,
    "Hidden Among the Push-Blocks": 235,
}

loc_Cap_Captures = {
    "Frog": 3701,
    "Spark Pylon": 3702,
}

loc_Cap_Captures_Revisit = {
    "Paragoomba": 3703,
}


loc_Cap_Shop = {
    "Cap Kingdom Sticker": 2582,
    "Plush Frog": 2599,
    "Bonneton Tower Model": 2600,
    "Black Top Hat": 2501,
    "Black Tuxedo": 2539
}

loc_Cap_Postgame = {
    "Cap Kingdom - The Forgotten Treasure": 228,
    "Cap Kingdom - Taxi Flying Through Bonneton": 1073,
    "Cap Kingdom - Bonnetter Blockade": 1038,
    "Cap Kingdom - Cap Kingdom Regular Cup": 1033,
    "Cap Kingdom - Peach in the Cap Kingdom": 229,
    "Moon Kingdom - Found with Cap Kingdom Art": 1086,
    "Cap Kingdom - Next to Glasses Bridge": 233,
    "Cap Kingdom - Danger Sign": 816,
    "Cap Kingdom - Under the Big One's Brim": 817,
    "Cap Kingdom - Fly to the Edge of the Fog": 1158,
    "Cap Kingdom - Spin the Hat, Get a Prize": 1025,
    "Cap Kingdom - Hidden in a Sunken Hat": 231,
    "Cap Kingdom - Fog-Shrouded Platform": 813,
    "Cap Kingdom - Bird Traveling in the Fog": 1156,
    "Cap Kingdom - Caught Hopping Near the Ship!": 232,
    "Cap Kingdom - Taking Notes: In the Fog": 1018,
    "Cap Kingdom - Cap Kingdom Timer Challenge 2": 981,
    "Cap Kingdom - Cap Kingdom Master Cup": 1034,
    "Cap Kingdom - Roll On and On": 950,
    "Cap Kingdom - Precision Rolling": 951
}

sub_area_rolling = {
    "Roll On and On": 950,
    "Precision Rolling": 951
}

# Cascade
loc_Cascade = {
    "Cascade Kingdom - Our First Power Moon" : 205,
    "Cascade Kingdom - Multi Moon Atop the Falls": 218,
    "Cascade Kingdom - Chomp Through the Rocks": 206,
    "Cascade Kingdom - Behind the Waterfall": 212,
}

loc_Cascade_Peace = {
    "Cascade Kingdom - On Top of the Rubble": 1145,
    "Cascade Kingdom - Treasure of the Waterfall Basin": 216,
    "Cascade Kingdom - Above a High Cliff": 210,
    "Cascade Kingdom - Across the Floating Isles": 208,
    "Cascade Kingdom - Cascade Kingdom Timer Challenge 1": 669,
    "Cascade Kingdom - Cascade Kingdom Timer Challenge 2": 670,
    "Cascade Kingdom - Good Morning, Captain Toad!": 204,
    "Cascade Kingdom - Dinosaur Nest: Big Cleanup!": 1116,
    "Cascade Kingdom - Dinosaur Nest: Running Wild!": 1115,
    "Cascade Kingdom - Nice Shot with the Chain Chomp!": 225,
    "Cascade Kingdom - Very Nice Shot with the Chain Chomp!": 226,
    "Cascade Kingdom - Past the Chasm Lifts": 221,
    "Cascade Kingdom - Hidden Chasm Passage": 222
}

loc_Cascade_Revisit = {
    "Cascade Kingdom - Rolling Rock by the Falls": 1004,
    "Cascade Kingdom - Shopping in Fossil Falls": 211,
}

loc_Cascade_Full = {
    **loc_Cascade,
    **loc_Cascade_Peace,
    **loc_Cascade_Revisit
}

sub_area_chain_chomp = {
    "Nice Shot with the Chain Chomp!": 225,
    "Very Nice Shot with the Chain Chomp!": 226,
}

sub_area_trex_nest = {
    "Dinosaur Nest: Big Cleanup!": 1116,
    "Dinosaur Nest: Running Wild!": 1115,
}

sub_area_cascade_2d = {
    "Past the Chasm Lifts": 221,
    "Hidden Chasm Passage": 222
}

loc_Cascade_Captures = {
    "Chain Chomp": 3704,
    "Big Chain Chomp": 3705,
    "Broode's Chain Chomp": 3706,
    "T-Rex": 3707,
}

loc_Cascade_Shop = {
    "Caveman Headwear": 2502,
    "Caveman Outfit": 2540,
    "Cascade Kingdom Sticker": 2583,
    "T-Rex Model": 2601,
    "Triceratops Trophy": 2602
}

loc_Cascade_Post_Metro = {
    "Cascade Kingdom - A Tourist in the Cascade Kingdom!": 906
}

loc_Cascade_Post_Snow = {
    "Cascade Kingdom - Secret Path to Fossil Falls!": 207
}

loc_Cascade_Postgame = {
    "Cascade Kingdom - Peach in the Cascade Kingdom": 209,
    "Cascade Kingdom - Cascade Kingdom Regular Cup": 708,
    "Cascade Kingdom - Caveman Cave-Fan": 866,
    "Cascade Kingdom - Sphynx Traveling to the Waterfall": 1041,
    "Cascade Kingdom - Bottom of the Waterfall Basin": 213,
    "Cascade Kingdom - Just a Hat, Skip, and a Jump": 826,
    "Cascade Kingdom - Treasure Under the Cliff": 214,
    "Cascade Kingdom - Next to the Stone Arch": 215,
    "Cascade Kingdom - Guarded by a Colossal Fossil": 825,
    "Cascade Kingdom - Under the Old Electrical Pole": 823,
    "Cascade Kingdom - Under the Ground": 822,
    "Cascade Kingdom - Inside the Busted Fossil": 1167,
    "Cascade Kingdom - Caught Hopping at the Waterfall!": 217,
    "Cascade Kingdom - Taking Notes: Hurry Upward": 770,
    "Cascade Kingdom - Across the Mysterious Clouds": 224,
    "Cascade Kingdom - Cascade Kingdom Master Cup": 894,
    "Cascade Kingdom - Atop a Wall Among the Clouds": 223,
    "Cascade Kingdom - Across the Gusty Bridges": 673,
    "Cascade Kingdom - Flying Far Away from Gusty Bridges": 674
}

sub_area_gusty_bridges = {
    "Across the Gusty Bridges": 673,
    "Flying Far Away from Gusty Bridges": 674
}

# Sand
loc_Sand = {
    "Sand Kingdom - Atop the Highest Tower": 497,
    "Sand Kingdom - Moon Shards in the Sand": 496,
    "Sand Kingdom - Overlooking the Desert Town": 517,
    "Sand Kingdom - Alcove in the Ruins": 518,
    "Sand Kingdom - On the Leaning Pillar": 516,
    "Sand Kingdom - Hidden Room in the Flowing Sands": 523,
    "Sand Kingdom - Secret of the Mural": 505,
    "Sand Kingdom - On Top of Stone Archway": 520,
    "Sand Kingdom - From a Crate In The Ruins": 507,
    "Sand Kingdom - On the Lone Pillar": 522,
    "Sand Kingdom - Where the Birds Gather": 511,
    "Sand Kingdom - Top of a Dune": 506,
    "Sand Kingdom - Lost in the Luggage": 510,
    "Sand Kingdom - Bullet Bill Breakthrough": 519,
    "Sand Kingdom - Inside a Block Is a Hard Place": 508,
    "Sand Kingdom - Bird Traveling the Desert": 526,
    "Sand Kingdom - The Treasure of Jaxi Ruins": 530,
    "Sand Kingdom - Desert Gardening: Plaza Seed": 1112,
    "Sand Kingdom - Desert Gardening: Ruins Seed": 1113,
    "Sand Kingdom - Desert Gardening: Seed on the Cliff": 1114,
    "Sand Kingdom - Taking Notes: Jump on the Palm": 525,
    "Sand Kingdom - Among the Five Cactuses": 893,
    "Sand Kingdom - Wandering Cactus": 509,
    "Sand Kingdom - Sand Quiz: Wonderful!": 498,
    "Sand Kingdom - Shopping in Tostarena": 565,
    "Sand Kingdom - Employees Only": 564,
    "Sand Kingdom - Sand Kingdom Slots": 1047,
    "Sand Kingdom - Ice Cave Treasure": 567,
    "Sand Kingdom - Sphynx's Treasure Vault": 566,
    "Sand Kingdom - A Rumble from the Sandy Floor": 1136,
    "Sand Kingdom - Dancing with New Friends": 569,
    "Sand Kingdom - The Invisible Maze": 562,
    "Sand Kingdom - Skull Sign in the Transparent Maze": 561,
    "Sand Kingdom - The Bullet Bill Maze: Break Through!": 555,
    "Sand Kingdom - The Bullet Bill Maze: Side Path": 554,
    "Sand Kingdom - Jaxi Driver": 557,
    "Sand Kingdom - Jaxi Stunt Driving": 556,
}

loc_Sand_Pyramid = {
    "Sand Kingdom - Showdown on the Inverted Pyramid": 495,
    "Sand Kingdom - Secret of the Inverted Mural": 504,
    "Sand Kingdom - On the Statue's Tail": 513,
    "Sand Kingdom - Hidden Room in the Inverted Pyramid": 563,
}

loc_Sand_Underground = {
    "Sand Kingdom - The Hole in the Desert": 560,
    "Sand Kingdom - Underground Treasure Chest": 953,
    "Sand Kingdom - Goomba Tower Assembly": 558,
}

loc_Sand_Peace = {
    "Sand Kingdom - Herding Sheep in the Dunes": 532,
    "Sand Kingdom - Walking the Desert!": 533,
    "Sand Kingdom - Hang Your Hat on the Fountain": 534,
    "Sand Kingdom - Found in the Sand! Good Dog!": 512,
    "Sand Kingdom - Sand Kingdom Timer Challenge 1": 531,
    "Sand Kingdom - Sand Kingdom Timer Challenge 2": 536,
    "Sand Kingdom - Sand Kingdom Timer Challenge 3": 539,
    "Sand Kingdom - Fishing in the Oasis": 502,
    "Sand Kingdom - Love in the Heart of the Desert": 493,
    "Sand Kingdom - You're Quite a Catch, Captain Toad!": 692,
    "Sand Kingdom - Jaxi Reunion!": 1059,
    "Sand Kingdom - Welcome Back, Jaxi!": 501,
    "Sand Kingdom - Bird Traveling Wastes": 552,
    "Sand Kingdom - The Lurker Under the Stone": 538,
    "Sand Kingdom - Under the Mummy's Curse": 995,
    "Sand Kingdom - Strange Neighborhood": 570,
    "Sand Kingdom - Above a Strange Neighborhood": 571
}

loc_Sand_Full = {
    **loc_Sand,
    **loc_Sand_Pyramid,
    **loc_Sand_Underground,
    **loc_Sand_Peace
}

sub_area_invisible_maze = {
    "The Invisible Maze": 562,
    "Skull Sign in the Transparent Maze": 561,
}

sub_area_bullet_bill_maze = {
    "The Bullet Bill Maze: Break Through!": 555,
    "The Bullet Bill Maze: Side Path": 554,
}

sub_area_jaxi = {
    "Jaxi Driver": 557,
    "Jaxi Stunt Driving": 556,
}

sub_area_strange_neighborhood = {
    "Strange Neighborhood": 570,
    "Above a Strange Neighborhood": 571
}

sub_area_sand_outfit = {
    "Dancing with New Friends": 569,
}

sub_area_sand_rumbling_floor = {
    "A Rumble from the Sandy Floor": 1136,
}

sub_area_sand_employee = {
    "Employees Only": 564,
}

sub_area_jaxi_ruins = {
    "Ice Cave Treasure": 567,
}

sub_area_sand_sphinx = {
    "Sphynx's Treasure Vault": 566,
}

sub_area_sand_slots = {
    "Sand Kingdom Slots": 1047,
}

sub_area_sand_underground = {
    "Underground Treasure Chest": 953,
    "Goomba Tower Assembly": 558,
}

sub_area_sand_arena = {
    "The Hole in the Desert": 560,
}

sub_area_sand_arena_peace = {
    "Under the Mummy's Curse": 995,
}

loc_Sand_Captures = {
    "Binoculars": 3708,
    "Bullet Bill": 3709,
    "Moe-Eye": 3710,
    "Cactus": 3711,
}

loc_Sand_Captures_Underground = {
    "Goomba": 3712,
    "Knucklotec's Fist": 3713,
}

loc_Sand_Captures_Peace = {
    "Mini Rocket": 3714,
    "Glydon": 3715,
    "Lakitu": 3716,
}

# Sand Underground and Jaxi Ruins Crusher cave, sphynx room
# Continue new souvenir ids here

loc_Sand_Shop = {
    "Captain's Hat": 2577,
    "Sombrero": 2503,
    "Poncho": 2541,
    "Cowboy Hat": 2504,
    "Cowboy Outfit": 2542,
    "Sand Kingdom Sticker": 2584,
    "Jaxi Statue": 2604,
    "Inverted Pyramid Model": 2603,
    "Employee Cap": 2520,
    "Employee Uniform": 2558,
    "Boxer Shorts": 2578
}

loc_Sand_Postgame = {
    "Bowser Kingdom - Found with Sand Kingdom Art": 1096,
    "Sand Kingdom - Jammin' in the Sand Kingdom": 910,
    "Sand Kingdom - Hat-and-Seek: In the Sand": 924,
    "Sand Kingdom - Sand Kingdom Regular Cup": 1045,
    "Sand Kingdom - Binding Band Returned": 887,
    "Sand Kingdom - Round-the-World Tourist": 535,
    "Sand Kingdom - Peach in the Sand Kingdom": 544,
    "Sand Kingdom - Mighty Leap from the Palm Tree!": 547,
    "Sand Kingdom - On the North Pillar": 546,
    "Sand Kingdom - Into the Flowing Sands": 799,
    "Sand Kingdom - In the Skies Above the Canyon": 550,
    "Sand Kingdom - Island in the Poison Swamp": 521,
    "Sand Kingdom - An invisible Gleam": 542,
    "Sand Kingdom - On the Eastern Pillar": 540,
    "Sand Kingdom - Caught Hopping in the Desert!": 553,
    "Sand Kingdom - Poster Cleanup": 545,
    "Sand Kingdom - Taking Notes: Running Down": 515,
    "Sand Kingdom - Taking Notes: In the Wall Painting": 783,
    "Sand Kingdom - Love at the Edge of the Desert": 965,
    "Sand Kingdom - More Walking in the Desert": 712,
    "Sand Kingdom - Sand Kingdom Master Cup": 1046,
    "Sand Kingdom - Where the Transparent Platforms End": 572,
    "Sand Kingdom - Jump Onto the Transparent Lift": 573,
    "Sand Kingdom - Colossal Ruins: Dash Jump!": 575,
    "Sand Kingdom - Sinking Colossal Ruins: Hurry!": 574,
    "Sand Kingdom - Through the Freezing Waterway": 35,
    "Sand Kingdom - Freezing Waterway: Hidden Room": 36
}

loc_Sand_Revisit = {
    "Sand Kingdom - Secret Path to Tostarena!": 524,
}

sub_area_transparent_platform = {
    "Where the Transparent Platforms End": 572,
    "Jump Onto the Transparent Lift": 573,
}

sub_area_colossal_ruins = {
    "Colossal Ruins: Dash Jump!": 575,
    "Sinking Colossal Ruins: Hurry!": 574,
}

sub_area_freezing_waterway = {
    "Through the Freezing Waterway": 35,
    "Freezing Waterway: Hidden Room": 36
}

sub_area_sand_arena_post = {
    "Binding Band Returned": 887,
}

# Lake
loc_Lake = {
    "Lake Kingdom - Broodals Over the Lake": 424,
    "Lake Kingdom - Dorrie-Back Rider": 411,
    "Lake Kingdom - Cheep Cheep Crossing": 412,
    "Lake Kingdom - End of the Hidden Passage": 410,
    "Lake Kingdom - What's in the Box?": 404,
    "Lake Kingdom - On the Lakeshore": 401,
    "Lake Kingdom - From the Broken Pillar": 405,
    "Lake Kingdom - Treasure in the Spiky Waterway": 415,
    "Lake Kingdom - Lake Gardening: Spiky Passage Seed": 1166,
    "Lake Kingdom - Lake Kingdom Timer Challenge 1": 715,
    "Lake Kingdom - Lake Kingdom Timer Challenge 2": 420,
    "Lake Kingdom - Moon Shards in the Lake": 413,
    "Lake Kingdom - Taking Notes: Dive and Swim": 402,
    "Lake Kingdom - Taking Notes: In the Cliffside": 407,
    "Lake Kingdom - Lake Fishing": 416,
    "Lake Kingdom - I Met a Lake Cheep Cheep!": 409,
    "Lake Kingdom - Our Secret Little Room": 414,
    "Lake Kingdom - Let's Go Swimming, Captain Toad!": 403,
    "Lake Kingdom - Shopping in Lake Lamode": 430,
    "Lake Kingdom - A Successful Repair Job": 971,
    "Lake Kingdom - I Feel Underdressed": 406,
    "Lake Kingdom - Unzip the Chasm": 1008,
    "Lake Kingdom - Super-Secret Zipper": 1009,
    "Lake Kingdom - Jump, Grab, Cling, and Climb": 425,
    "Lake Kingdom - Jump, Grab, and Climb Some More": 426,
    "Cascade Kingdom - Found with Lake Kingdom Art": 1094,
}

sub_area_repair = {
    "A Successful Repair Job": 971,
}

sub_area_zipper = {
    "Unzip the Chasm": 1008,
    "Super-Secret Zipper": 1009,
}

sub_area_jump_grab_climb = {
    "Jump, Grab, Cling, and Climb": 425,
    "Jump, Grab, and Climb Some More": 426,
}


loc_Lake_Captures = {
    "Zipper": 3717,
    "Cheep Cheep": 3718,
    "Puzzle Part (Lake Kingdom)": 3719,
}

loc_Lake_Shop = {
    "Swim Goggles": 2505,
    "Swimwear": 2543,
    "Lake Kingdom Sticker": 2585,
    "Rubber Dorrie": 2606,
    "Underwater Dome": 2605,
    "Fashionable Cap": 2521,
    "Fashionable Outfit": 2559
}

loc_Lake_Post_Seaside = {
    "Lake Kingdom - Secret Path to Lake Lamode!": 417
}

loc_Lake_Postgame = {
    "Lake Kingdom - Taxi Flying Through Lake Lamode": 1078,
    "Lake Kingdom - That Trendy “Pirate” Look": 870,
    "Lake Kingdom - Space Is “In” Right Now": 869,
    "Lake Kingdom - That “Old West” Style": 871,
    "Lake Kingdom - Lake Kingdom Regular Cup": 709,
    "Lake Kingdom - Peach in the Lake Kingdom": 408,
    "Lake Kingdom - Behind the Floodgate": 422,
    "Lake Kingdom - High-Flying Leap": 771,
    "Lake Kingdom - Deep, Deep Down": 719,
    "Lake Kingdom - Rooftop of the Water Plaza": 1015,
    "Lake Kingdom - Bird Traveling Over the Lake": 423,
    "Lake Kingdom - Love by the Lake": 1017,
    "Lake Kingdom - Lake Kingdom Master Cup": 720,
    "Lake Kingdom - Waves of Poison: Hoppin' Over": 434,
    "Lake Kingdom - Waves of Poison: Hop to It!": 431
}

sub_area_waves_poison ={
    "Waves of Poison: Hoppin' Over": 434,
    "Waves of Poison: Hop to It!": 431
}

loc_Wooded = {
    "Wooded Kingdom - Road to Sky Garden": 129,
    "Wooded Kingdom - Rolling Rock in the Woods": 149,
    "Wooded Kingdom - Caught Hopping in the Forest!": 148,
    "Wooded Kingdom - Atop a Tall Tree": 139,
    "Wooded Kingdom - Tucked Away Inside a Tunnel": 143,
    "Wooded Kingdom - The Nut ‘Round The Corner": 145,
    "Wooded Kingdom - Climb the Cliff to Get the Nut": 140,
    "Wooded Kingdom - The Nut in the Red Maze": 141,
    "Wooded Kingdom - The Nut at the Dead End": 142,
    "Wooded Kingdom - Fire in the Cave": 136,
    "Wooded Kingdom - Shopping in Steam Gardens": 138,
    "Wooded Kingdom - Nut Planted in the Tower": 180,
    "Wooded Kingdom - Stretching Your Legs": 179,
    "Wooded Kingdom - Rolling Rock in the Deep Woods": 183,
    "Wooded Kingdom - Glowing in the Deep Woods": 1137,
    "Wooded Kingdom - Past The Peculiar Pipes": 1159,
    "Wooded Kingdom - By the Babbling Brook in the Deep Woods": 185,
    "Wooded Kingdom - The Hard Rock in Deep Woods": 186,
    "Wooded Kingdom - A Treasure Made of Coins": 1153,
    "Wooded Kingdom - Beneath the Roots of a Moving Tree": 184,
    "Wooded Kingdom - Deep Woods Treasure Trap": 188,
    "Wooded Kingdom - Exploring for Treasure": 187,
    "Wooded Kingdom - Flooding Pipeway": 196,
    "Wooded Kingdom - Flooding Pipeway Ceiling Secret": 197,
}

loc_Wooded_Post_Metro = {
    "Wooded Kingdom - Secret Path to the Steam Gardens": 137
}

loc_Wooded_Post_Story1 = {
    "Wooded Kingdom - Flower Thieves of Sky Garden": 130,
    "Wooded Kingdom - Path to the Secret Flower Field": 159,
    "Wooded Kingdom - Cracked Nut on a Crumbling Tower": 144,
    "Wooded Kingdom - The Nut that Grew on the Tall Fence": 147,
    "Wooded Kingdom - Love in the Forest Ruins": 131,
    "Wooded Kingdom - Thanks for the Charge!": 135,
    "Wooded Kingdom - Spinning-Platforms Treasure": 195,
    "Wooded Kingdom - Over the Cliff's Edge": 146,
    "Wooded Kingdom - Behind the Rock Wall": 134,
    "Wooded Kingdom - Back Way Up the Mountain": 150,
    "Wooded Kingdom - Flower Road Run": 191,
    "Wooded Kingdom - Flower Road Reach": 192,
    "Wooded Kingdom - Elevator Escalation": 190,
    "Wooded Kingdom - Elevator Blind Spot": 189
}

loc_Wooded_Peace = {
    "Wooded Kingdom - Defend the Secret Flower Field!": 181,
    "Wooded Kingdom - Inside the Rock in the Forest": 1026,
    "Wooded Kingdom - Hey Out There, Captain Toad!": 133,
    "Wooded Kingdom - Make the Secret Flower Field Bloom": 182,
    "Wooded Kingdom - Wooded Kingdom Timer Challenge 1": 157,
    "Wooded Kingdom - Wooded Kingdom Timer Challenge 2": 676,
    "Wooded Kingdom - Wandering in the Fog": 193,
    "Wooded Kingdom - Nut Hidden in the Fog": 194,
    "Wooded Kingdom - Walking on Clouds": 198,
    "Wooded Kingdom - Above the Clouds": 199,
    "Sand Kingdom - Found with Wooded Kingdom Art": 1089
}

sub_area_woods_treasure_trap = {
    "Deep Woods Treasure Trap": 188,
}

sub_area_explorer = {
    "Exploring for Treasure": 187,
}

sub_area_flooding_pipe = {
    "Flooding Pipeway": 196,
    "Flooding Pipeway Ceiling Secret": 197,
}

sub_area_flower_road = {
    "Flower Road Run": 191,
    "Flower Road Reach": 192,
}

sub_area_elevator_escalation = {
    "Elevator Escalation": 190,
    "Elevator Blind Spot": 189
}

sub_area_wooded_fog = {
    "Wandering in the Fog": 193,
    "Nut Hidden in the Fog": 194,
}

sub_area_wooded_clouds = {
    "Walking on Clouds": 198,
    "Above the Clouds": 199,
}

sub_area_flower_field = {
    "Defend the Secret Flower Field!": 181,
}

sub_area_flower_field_peace = {
    "Make the Secret Flower Field Bloom": 182,
}

loc_Wooded_Captures = {
    "Uproot": 3721,
    "Fire Bro": 3722,
    "Coin Coffer": 3724,
    "Tree": 3725,
    "Boulder": 3726,
}

loc_Wooded_Captures_Post_Story1 = {
    "Sherm": 3723,
}
loc_Wooded_Captures_Postgame = {
    "Poison Piranha Plant": 3720,
}

loc_Wooded_Shop = {
    "Explorer Hat": 2506,
    "Explorer Outfit": 2544,
    "Scientist Visor": 2507,
    "Scientist Outfit": 2545,
    "Wooded Kingdom Sticker": 2586,
    "Flowers from Steam Gardens": 2607,
    "Steam Gardener Watering Can": 2608,
    "Mechanic Cap": 2522,
    "Mechanic Outfit": 2560
}

loc_Wooded_Postgame = {
    "Wooded Kingdom - Swing Around Secret Flower Field": 1011,
    "Wooded Kingdom - Jammin' in the Wooded Kingdom": 905,
    "Wooded Kingdom - Wooded Kingdom Regular Cup": 153,
    "Wooded Kingdom - Peach in the Wooded Kingdom": 155,
    "Wooded Kingdom - High Up in the Cave": 168,
    "Wooded Kingdom - Lost in the Tall Trees": 892,
    "Wooded Kingdom - Looking Down on the Goombas": 160,
    "Wooded Kingdom - High Up on a Rock Wall": 177,
    "Wooded Kingdom - The Nut in the Robot Storeroom": 169,
    "Wooded Kingdom - Above the Iron Mountain Path": 161,
    "Wooded Kingdom - The Nut Under the Observation Deck": 174,
    "Wooded Kingdom - Bird Traveling the Forest": 166,
    "Wooded Kingdom - Invader in the Sky Garden": 156,
    "Wooded Kingdom - Hot, Hot, Hot from the Campfire": 163,
    "Wooded Kingdom - Wooded Kingdom Timer Challenge 3": 1010,
    "Wooded Kingdom - Moon Shards in the Forest": 152,
    "Wooded Kingdom - Taking Notes: On Top of the Wall": 170,
    "Wooded Kingdom - Taking Notes: Stretching": 151,
    "Wooded Kingdom - Wooded Kingdom Master Cup": 901,
    "Wooded Kingdom - I Met an Uproot!": 1001,
    "Wooded Kingdom - Invisible Road: Danger!": 202,
    "Wooded Kingdom - Invisible Road: Hidden Room": 203,
    "Wooded Kingdom - Herding Sheep Above the Forest Fog": 200,
    "Wooded Kingdom - Herding Sheep on the Iron Bridge": 201,
    "Wooded Kingdom - Down and Back Breakdown Road": 882,
    "Wooded Kingdom - Below Breakdown Road": 883
}

sub_area_wooded_invisible_road = {
    "Invisible Road: Danger!": 202,
    "Invisible Road: Hidden Room": 203,
}

sub_area_sheep = {
    "Herding Sheep Above the Forest Fog": 200,
    "Herding Sheep on the Iron Bridge": 201,
}

sub_area_wooded_breakdown_road = {
    "Down and Back Breakdown Road": 882,
    "Below Breakdown Road": 883
}

loc_Cloud = {
    "Cloud Kingdom - Picture Match: Basically a Goomba" : 982
}

loc_Post_Cloud = {
    "Beat Bowser in Cloud": 2500
}


loc_Cloud_Captures = {
    "Picture Match Part (Goomba)": 3727,
}

loc_Cloud_Postgame = {
    "Cloud Kingdom - Peach in the Cloud Kingdom": 1118,
    "Cloud Kingdom - Digging in the… Cloud?": 851,
    "Cloud Kingdom - High, High Above the Clouds": 852,
    "Cloud Kingdom - Crossing the Cloud Sea": 853,
    "Cloud Kingdom - Taking Notes: Up and Down": 1160,
    "Cloud Kingdom - Picture Match: A Stellar Goomba!": 983,
    "Cloud Kingdom - King of the Cube!": 914,
    "Cloud Kingdom - The Sixth Face": 915
}

sub_area_cloud_picture = {
    "Picture Match: Basically a Goomba" : 982
}

sub_area_cloud_picture_post = {
    "Picture Match: A Stellar Goomba!": 983,
}

sub_area_cube = {
    "King of the Cube!": 914,
    "The Sixth Face": 915
}

loc_Lost = {
    "Lost Kingdom - Atop a Propeller Pillar": 376,
    "Lost Kingdom - Below the Cliff's Edge": 380,
    "Lost Kingdom - Inside the Stone Cage": 374,
    "Lost Kingdom - On a Tree in the Swamp": 377,
    "Lost Kingdom - Over the Fuzzies, Above the Swamp": 384,
    "Lost Kingdom - Avoiding Fuzzies Inside the Wall": 387,
    "Lost Kingdom - Inside the Rising Stone Pillar": 375,
    "Lost Kingdom - Enjoying the View of Forgotten Isle": 385,
    "Lost Kingdom - On the Mountain Road": 379,
    "Lost Kingdom - A Propeller Pillar's Secret": 373,
    "Lost Kingdom - Wrecked Rock Block": 942,
    "Lost Kingdom - A Butterfly's Treasure": 388,
    "Lost Kingdom - Cave Gardening": 378,
    "Lost Kingdom - Moon Shards in the Jungle": 386,
    "Lost Kingdom - Peeking Out from Under the Bridge": 382,
    "Lost Kingdom - Twist ‘n' Turn-Up Treasure": 1075,
    "Lost Kingdom - Soaring Over the Forgotten Isle!": 381,
    "Lost Kingdom - The Caged Gold": 383,
    "Lost Kingdom - Get Some Rest, Captain Toad": 372,
    "Lost Kingdom - Shopping on Forgotten Isle": 398,
}

loc_Lost_Revisit = {
    "Lost Kingdom - Caught Hopping in the Jungle!": 390
}

loc_Lost_Captures = {
    "Tropical Wiggler": 3728,
}

loc_Lost_Shop = {
    "Aviator Cap": 2508,
    "Aviator Outfit": 2546,
    "Lost Kingdom Sticker": 2587,
    "Potted Palm Tree": 2609,
    "Butterfly Mobile": 2610
}

loc_Lost_Postgame = {
    "Lost Kingdom - Taxi Flying Through Forgotten Isle": 1077,
    "Lost Kingdom - I met a Tropical Wiggler!": 943,
    "Lost Kingdom - Lost Kingdom Regular Cup": 832,
    "Lost Kingdom - Peach in the Lost Kingdom": 394,
    "Lost Kingdom - The Shining Fruit": 395,
    "Lost Kingdom - Jump Down to the Top of a Tree": 396,
    "Lost Kingdom - Line It Up, Blow It Up": 392,
    "Lost Kingdom - Taking Notes: Stretch and Shrink": 393,
    "Lost Kingdom - Lost Kingdom Master Cup": 897,
    "Lost Kingdom - Lost Kingdom Timer Challenge": 855,
    "Lost Kingdom - Stretch and Traverse the Jungle": 399,
    "Lost Kingdom - Aglow in the Jungle": 400,
    "Lost Kingdom - Chasing Klepto": 491,
    "Lost Kingdom - Extremely Hot Bath": 492
}

sub_area_jungle = {
    "Stretch and Traverse the Jungle": 399,
    "Aglow in the Jungle": 400,
}

sub_area_klepto = {
    "Chasing Klepto": 491,
    "Extremely Hot Bath": 492
}

loc_Night_Metro = {
    "Metro Kingdom - New Donk City's Pest Problem": 37,
    "Metro Kingdom - Swaying in the Breeze": 39,
    "Metro Kingdom - Girder Sandwich": 54,
    "Metro Kingdom - City Hall Lost & Found": 100,
}

loc_Metro = {
    "Metro Kingdom - Drummer on Board!": 42,
    "Metro Kingdom - Guitarist on Board!": 41,
    "Metro Kingdom - Bassist on Board!": 44,
    "Metro Kingdom - Trumpeter on Board!": 43,
    "Metro Kingdom - Inside an Iron Girder": 38,
    "Metro Kingdom - Glittering Above the Pool": 65,
    "Metro Kingdom - Dizzying Heights": 61,
    "Metro Kingdom - Secret Girder Tunnel!": 59,
    "Metro Kingdom - Who Piled Garbage on This?": 49,
    "Metro Kingdom - Hidden in the Scrap": 62,
    "Metro Kingdom - Left at the Cafe?": 77,
    "Metro Kingdom - How Do They Take Out the Trash?": 68,
    "Metro Kingdom - City Gardening: Building Planter": 70,
    "Metro Kingdom - City Gardening: Plaza Planter": 71,
    "Metro Kingdom - City Gardening: Rooftop Planter": 69,
    "Metro Kingdom - How You Doin', Captain Toad?": 46,
    "Metro Kingdom - Free Parking: Rooftop Hop": 809,
    "Metro Kingdom - Bench Friends": 50,
    "Metro Kingdom - Shopping in New Donk City": 101,
    "Metro Kingdom - Metro Kingdom Slots": 1040,
    "Metro Kingdom - Jump-Rope Hero": 60,
    "Metro Kingdom - Jump-Rope Genius": 51,
    "Metro Kingdom - Remotely Captured Car": 53,
    "Metro Kingdom - RC Car Pro!": 105,
    "Metro Kingdom - Taking Notes: In the Private Room": 102,
    "Metro Kingdom - Pushing Through the Crowd": 1022,
    "Metro Kingdom - High Over the Crowd": 1023,
    "Metro Kingdom - Rewiring the Neighborhood": 122,
    "Metro Kingdom - Off the Beaten Wire": 121,
    "Metro Kingdom - Moon Shards Under Siege": 114,
    "Metro Kingdom - Sharpshooting Under Siege": 113,
    "Metro Kingdom - Inside the Rotating Maze": 118,
    "Metro Kingdom - Outside the Rotating Maze": 119,
    "Metro Kingdom - Hanging from a High-Rise": 103,
    "Metro Kingdom - Vaulting Up a High-Rise": 104,
    "Metro Kingdom - Bullet Billding": 115,
    "Metro Kingdom - One Man's Trash…": 116,
    "Metro Kingdom - Motor Scooter: Escape!": 1139,
    "Metro Kingdom - Big Jump: Escape!": 1140,
    "Lake Kingdom - Found with Metro Kingdom Art": 1088
}

loc_Metro_Post_Sand = {
    "Metro Kingdom - Secret Path to New Donk City!": 40
}

loc_Metro_Sewer_Access = {
    "Metro Kingdom - Powering Up the Station": 107,
    "Metro Kingdom - Sewer Treasure": 1100,
    "Metro Kingdom - A Traditional Festival!": 95,
}

loc_Metro_Peace = {
    "Metro Kingdom - Celebrating in the Streets!": 875,
    "Metro Kingdom - Caught Hopping on a Building!": 99,
    "Metro Kingdom - Metro Kingdom Timer Challenge 1": 1054,
    "Metro Kingdom - Metro Kingdom Timer Challenge 2": 839,
    "Metro Kingdom - A Tourist in the Metro Kingdom!": 881
}

sub_area_metro_slots = {
    "Metro Kingdom Slots": 1040,
}

sub_area_rc = {
    "RC Car Pro!": 105,
}

sub_area_private_room = {
    "Taking Notes: In the Private Room": 102,
}

sub_area_city_hall = {
    "City Hall Lost & Found": 100,
}

sub_area_crowd = {
    "Pushing Through the Crowd": 1022,
    "High Over the Crowd": 1023,
}

sub_area_rewiring = {
    "Rewiring the Neighborhood": 122,
    "Off the Beaten Wire": 121,
}

sub_area_siege = {
    "Moon Shards Under Siege": 114,
    "Sharpshooting Under Siege": 113,
}

sub_area_rotating_maze = {
    "Inside the Rotating Maze": 118,
    "Outside the Rotating Maze": 119,
}

sub_area_high_rise = {
    "Hanging from a High-Rise": 103,
    "Vaulting Up a High-Rise": 104,
}

sub_area_bullet_billding = {
    "Bullet Billding": 115,
    "One Man's Trash…": 116,
}

sub_area_motor_scooter = {
    "Motor Scooter: Escape!": 1139,
    "Big Jump: Escape!": 1140,
}

loc_Metro_Captures = {
    "Pole": 3729,
    "Manhole": 3730,
    "Taxi": 3731,
    "RC Car": 3732,
}

loc_Metro_Captures_Postgame = {
    "Letter": 3749,
    "Puzzle Part (Metro Kingdom)": 3750,
}

loc_Metro_Shop = {
    "Builder Helmet": 2509,
    "Builder Outfit": 2547,
    "Golf Cap": 2510,
    "Golf Outfit": 2548,
    "Metro Kingdom Sticker": 2588,
    "New Donk City Hall Model": 2612,
    "Pauline Statue": 2611,
    "Black Fedora": 2523,
    "Black Suit": 2561
}

loc_Metro_Postgame = {
    "Metro Kingdom - Bird Traveling in the City": 1150,
    "Metro Kingdom - Mario Signs His Name": 97,
    "Metro Kingdom - Surprise Clown!": 874,
    "Metro Kingdom - A Request from the Mayor": 96,
    "Metro Kingdom - Jammin' in the Metro Kingdom": 904,
    "Metro Kingdom - Sphynx in the City": 1037,
    "Metro Kingdom - Free Parking: Leap of Faith": 979,
    "Metro Kingdom - Metro Kingdom Regular Cup": 939,
    "Metro Kingdom - Hat-and-Seek: In the City": 81,
    "Metro Kingdom - Powering Up the Power Plant": 848,
    "Metro Kingdom - Up on the Big Screen": 928,
    "Metro Kingdom - Down Inside the Big Screen": 929,
    "Metro Kingdom - Peach in the Metro Kingdom": 75,
    "Metro Kingdom - Hanging Between Buildings": 84,
    "Metro Kingdom - Crossing Lines": 78,
    "Metro Kingdom - Out of a Crate in the City": 74,
    "Metro Kingdom - Bird Traveling in the Park": 1155,
    "Metro Kingdom - Metro Kingdom Timer Challenge 3": 938,
    "Metro Kingdom - Found in the Park! Good Dog!": 82,
    "Metro Kingdom - RC Car Champ!": 106,
    "Metro Kingdom - Metro Kingdom Master Cup": 940,
    "Metro Kingdom - Hat-and-Seek: In the Crowd": 1021,
    "Metro Kingdom - Scaling Pitchblack Mountain": 949,
    "Metro Kingdom - Reaching Pitchblack Island": 948,
    "Metro Kingdom - Swinging Scaffolding: Jump!": 125,
    "Metro Kingdom - Swinging Scaffolding: Break!": 126,
    "Metro Kingdom - Moto Scooter: Daredevil!": 123,
    "Metro Kingdom - Full-Throttle Scooting!": 124
}

sub_area_big_screen = {
    "Up on the Big Screen": 928,
    "Down Inside the Big Screen": 929,
}

sub_area_pitch_black = {
    "Scaling Pitchblack Mountain": 949,
    "Reaching Pitchblack Island": 948,
}

sub_area_swinging_scaffolding = {
    "Swinging Scaffolding: Jump!": 125,
    "Swinging Scaffolding: Break!": 126,
}

sub_area_motor_daredevil = {
    "Moto Scooter: Daredevil!": 123,
    "Full-Throttle Scooting!": 124
}

sub_area_crowd_post_game = {
    "Hat-and-Seek: In the Crowd": 1021,
}

sub_area_sewer_post_game = {
    "Powering Up the Power Plant": 848,
}

sub_area_sewer = {
    "Powering Up the Station": 107,
    "Sewer Treasure": 1100,
}

loc_Seaside = {
    "Seaside Kingdom - The Stone Pillar Seal": 438,
    "Seaside Kingdom - The Lighthouse Seal": 441,
    "Seaside Kingdom - The Hot Spring Seal": 440,
    "Seaside Kingdom - The Seal Above the Canyon": 439,
    "Seaside Kingdom - On the Cliff Overlooking the Beach": 449,
    "Seaside Kingdom - Ride the Jetstream": 455,
    "Seaside Kingdom - Ocean-Bottom Maze: Treasure": 466,
    "Seaside Kingdom - Ocean-Bottom Maze: Hidden Room": 467,
    "Seaside Kingdom - Underwater Highway Tunnel": 450,
    "Seaside Kingdom - Shh! It's a Shortcut!": 451,
    "Seaside Kingdom - Gap in the Ocean Trench": 458,
    "Seaside Kingdom - Slip Through the Nesting Spot": 459,
    "Seaside Kingdom - Merci, Dorrie!": 453,
    "Seaside Kingdom - Under a Dangerous Ceiling": 445,
    "Seaside Kingdom - What the Waves Left Behind": 447,
    "Seaside Kingdom - The Back Canyon: Excavate!": 444,
    "Seaside Kingdom - Bubblaine Northern Reaches": 1151,
    "Seaside Kingdom - Wriggling on the Sandy Bottom": 1024,
    "Seaside Kingdom - Glass Palace Treasure Chest": 1057,
    "Seaside Kingdom - Treasure Trap Hidden in the Inlet": 448,
    "Seaside Kingdom - Sea Gardening: Inlet Seed": 1103,
    "Seaside Kingdom - Sea Gardening Canyon Seed": 1104,
    "Seaside Kingdom - Sea Gardening: Hot-Spring Seed": 1106,
    "Seaside Kingdom - Sea Gardening: Ocean Trench Seed": 1105,
    "Seaside Kingdom - Seaside Kingdom Timer Challenge 1": 468,
    "Seaside Kingdom - Moon Shards in the Sea": 454,
    "Seaside Kingdom - Taking Notes: Ocean Surface Dash": 442,
    "Seaside Kingdom - Love by the Seaside": 446,
    "Seaside Kingdom - Good Job, Captain Toad!": 443,
    "Seaside Kingdom - Ocean Quiz: Good!": 457,
    "Seaside Kingdom - Shopping in Bubblaine": 460,
    "Seaside Kingdom - Looking Back in the Dark Waterway": 1107,
    "Seaside Kingdom - The Sphynx's Underwater Vault": 483,
    "Seaside Kingdom - A Rumble on the Seaside Floor": 1135,
    "Seaside Kingdom - A Relaxing Dance": 484,
    "Seaside Kingdom - Wading in the Cloud Sea": 485,
    "Seaside Kingdom - Sunken Treasure in the Cloud Sea": 486,
    "Seaside Kingdom - Fly Through the Narrow Valley": 478,
    "Seaside Kingdom - Treasure Chest in the Narrow Valley": 479,
    "Seaside Kingdom - Hurry and Stretch": 482,
    "Seaside Kingdom - Stretch on the Side Path": 481,
    "Metro Kingdom - Found with Seaside Kingdom Art": 1095,
}

loc_Seaside_Peace = {
    "Seaside Kingdom - The Glass Is Half Full!": 437,
    "Seaside Kingdom - Bonjour, Dorrie!": 452,
    "Seaside Kingdom - Seaside Kingdom Timer Challenge 2": 863,
    "Seaside Kingdom - Found on the Beach! Good Dog!": 471,
    "Seaside Kingdom - Lighthouse Leaper": 474,
    "Seaside Kingdom - Beach Volleyball: Champ": 472,
    "Seaside Kingdom - Beach Volleyball: Hero of the Beach!": 473
}

loc_Seaside_Captures = {
    "Gushen": 3736,
}

loc_Seaside_Shop = {
    "Resort Hat": 2512,
    "Resort Outfit": 2550,
    "Sailor Hat": 2513,
    "Sailor Suit": 2551,
    "Seaside Kingdom Sticker": 2590,
    "Glass Tower Model": 2616,
    "Sand Jar": 2615,
    "Pirate Hat": 2524,
    "Pirate Outfit": 2562
}

sub_area_sandy_bottom = {
    "Wriggling on the Sandy Bottom": 1024,
}

sub_area_seaside_waterway = {
    "Looking Back in the Dark Waterway": 1107,
}

sub_area_seaside_sphynx = {
    "The Sphynx's Underwater Vault": 483,
}

sub_area_seaside_rumble = {
    "A Rumble on the Seaside Floor": 1135,
}

sub_area_resort = {
    "A Relaxing Dance": 484,
}

sub_area_cloud_sea = {
    "Wading in the Cloud Sea": 485,
    "Sunken Treasure in the Cloud Sea": 486,
}

sub_area_valley = {
    "Fly Through the Narrow Valley": 478,
    "Treasure Chest in the Narrow Valley": 479,
}

sub_area_seaside_stretch = {
    "Hurry and Stretch": 482,
    "Stretch on the Side Path": 481,
}

loc_Seaside_Postgame = {
    "Seaside Kingdom - Secret Path to Bubblaine": 464,
    "Seaside Kingdom - Seaside Kingdom Regular Cup": 1028,
    "Seaside Kingdom - Peach in the Seaside Kingdom": 477,
    "Seaside Kingdom - Above the Parasol: Catch!": 780,
    "Seaside Kingdom - What Shines Inside the Glass": 779,
    "Seaside Kingdom - A Fine Detail on the Glass": 920,
    "Seaside Kingdom - Underwater Highway West: Explore!": 476,
    "Seaside Kingdom - Underwater Highway East: Explore!": 773,
    "Seaside Kingdom - Rapid Ascent on Hot Spring Island": 1043,
    "Seaside Kingdom - A Light Next to the Lighthouse": 776,
    "Seaside Kingdom - The Tall Rock Shell in the Deep Ocean": 778,
    "Seaside Kingdom - At the Base of the Lighthouse": 775,
    "Seaside Kingdom - Bird Traveling Over the Ocean": 1120,
    "Seaside Kingdom - Caught Hopping at Glass Palace!": 475,
    "Seaside Kingdom - Seaside Kingdom Timer Challenge 3": 864,
    "Seaside Kingdom - Taking Notes: Ocean-Bottom Maze": 790,
    "Seaside Kingdom - Taking Notes in the Sea": 1163,
    "Seaside Kingdom - Seaside Kingdom Master Cup": 1029,
    "Seaside Kingdom - Aim! Poke!": 488,
    "Seaside Kingdom - Poke! Roll!": 487,
    "Seaside Kingdom - The Spinning Maze: Search!": 489,
    "Seaside Kingdom - The Spinning Maze: Open!": 490
}

sub_area_seaside_pokio = {
    "Aim! Poke!": 488,
    "Poke! Roll!": 487,
}

sub_area_seaside_maze = {
    "The Spinning Maze: Search!": 489,
    "The Spinning Maze: Open!": 490
}

loc_Snow = {
    "Snow Kingdom - The Icicle Barrier": 17,
    "Snow Kingdom - The Ice Wall Barrier": 22,
    "Snow Kingdom - The Gusty Barrier": 18,
    "Snow Kingdom - The Snowy Mountain Barrier": 25,
    "Snow Kingdom - Entrance to Shiveria": 1081,
    "Snow Kingdom - Behind the Snowy Mountain": 21,
    "Snow Kingdom - Shining in the Snow in Town": 1080,
    "Snow Kingdom - Atop a Blustery Arch": 16,
    "Snow Kingdom - The Shiverian Treasure Chest": 23,
    "Snow Kingdom - Treasure in the Ice Wall": 24,
    "Snow Kingdom - Ice-Dodging Goomba Stack": 20,
    "Snow Kingdom - Captain Toad is Chilly!": 694,
    "Snow Kingdom - Shopping in Shiveria": 868,
    "Snow Kingdom - Moon Shards in the Cold Room": 1030,
    "Snow Kingdom - Slip Behind the Ice": 1031,
    "Lost Kingdom - Found with Snow Kingdom Art": 1087
}

sub_area_icicle = {
    "The Icicle Barrier": 17,
    "Ice-Dodging Goomba Stack": 20,
}

sub_area_ice_wall = {
    "The Ice Wall Barrier": 22,
    "Treasure in the Ice Wall": 24,
}

sub_area_gusty_barrier = {
    "The Gusty Barrier": 18,
    "Atop a Blustery Arch": 16,
}

sub_area_snowy_mountain = {
    "The Snowy Mountain Barrier": 25,
    "Behind the Snowy Mountain": 21,
}

loc_Snow_Peace = {
    "Snow Kingdom - The Bound Bowl Grand Prix": 1020,
    "Snow Kingdom - Caught Hopping in the Snow!": 1002,
    "Snow Kingdom - Snow Kingdom Timer Challenge 1": 678,
    "Snow Kingdom - Snow Kingdom Timer Challenge 2": 686,
    "Snow Kingdom - Moon Shards in the Snow": 2,
    "Snow Kingdom - Taking Notes: Snow Path Dash": 3,
    "Snow Kingdom - Fishing in the Glacier!": 4,
    "Snow Kingdom - I'm Not Cold!": 873,
    "Snow Kingdom - Walking on Ice!": 877,
    "Snow Kingdom - Snowline Circuit Class S": 879,
    "Snow Kingdom - Dashing Over Cold Water!": 12,
    "Snow Kingdom - Dashing Above and Beyond!": 13,
    "Snow Kingdom - Jump ‘n' Swim in the Freezing Water": 14,
    "Snow Kingdom - Freezing Water Near the Ceiling": 15,
    "Snow Kingdom - Blowing and Sliding": 33,
    "Snow Kingdom - Spinning Above the Clouds": 830,
    "Snow Kingdom - High-Altitude Spinning": 831,
}

loc_Snow_Captures = {
    "Ty-foo": 3733,
}

loc_Snow_Captures_Peace = {
    "Shiverian Racer": 3734,
    "Cheep Cheep (Snow Kingdom)": 3735,
}

loc_Snow_Shop = {
    "Snow Hood": 2511,
    "Snow Suit": 2549,
    "Snow Kingdom Sticker": 2589,
    "Shiverian Rug": 2613,
    "Shiverian Nesting Dolls": 2614
}

sub_area_snow_outfit = {
    "Moon Shards in the Cold Room": 1030,
    "Slip Behind the Ice": 1031,
}

sub_area_snow_dashing = {
    "Dashing Over Cold Water!": 12,
    "Dashing Above and Beyond!": 13,
}

sub_area_snow_freezing_water = {
    "Jump ‘n' Swim in the Freezing Water": 14,
    "Freezing Water Near the Ceiling": 15,
}

sub_area_blowing = {
    "Blowing and Sliding": 33,
}

sub_area_snow_spinning = {
    "Spinning Above the Clouds": 830,
    "High-Altitude Spinning": 831,
}

loc_Snow_Postgame = {
    "Snow Kingdom - Secret Path to Shiveria!": 937,
    "Snow Kingdom - Snow Kingdom Regular Cup": 695,
    "Snow Kingdom - Hat-and-Seek in the Snow": 903,
    "Snow Kingdom - Peach in the Snow Kingdom": 19,
    "Snow Kingdom - Shining on High": 8,
    "Snow Kingdom - Above the Freezing Fish Pond": 6,
    "Snow Kingdom - Ice Floe Swimming": 7,
    "Snow Kingdom - Icy Jump Challenge": 703,
    "Snow Kingdom - Forgotten in the Holding Room": 843,
    "Snow Kingdom - It Popped Out of the Ice": 10,
    "Snow Kingdom - Deep in the Cold, Cold Water": 9,
    "Snow Kingdom - Water Pooling in the Crevasse": 701,
    "Snow Kingdom - Squirming Under the Ice": 704,
    "Snow Kingdom - Snow Kingdom Timer Challenge 3": 1003,
    "Snow Kingdom - Stacked-Up Ice Climb": 699,
    "Snow Kingdom - I Met a Snow Cheep Cheep!": 973,
    "Snow Kingdom - Even More Walking on Ice!": 878,
    "Snow Kingdom - Snow Kingdom Master Cup": 900,
    "Snow Kingdom - Iceburn Circuit Class A": 998,
    "Snow Kingdom - Iceburn Circuit Class S": 997,
    "Snow Kingdom - Running the Flower Road": 31,
    "Snow Kingdom - Looking Back on the Flower Road": 32
}

sub_area_icicle_post = {
    "The Icicle Barrier": 17,
}

sub_area_ice_wall_post = {
    "The Icicle Barrier": 17,
}

sub_area_gusty_barrier_post = {
    "The Icicle Barrier": 17,
}

sub_area_snowy_mountain_post = {
    "The Icicle Barrier": 17,
}

loc_Luncheon = {
    "Luncheon Kingdom - The Broodals Are After Some Cookin'": 291,
    "Luncheon Kingdom - Piled on the Salt": 256,
    "Luncheon Kingdom - Lurking in the Pillar's Shadow": 250,
    "Luncheon Kingdom - Overlooking a Bunch of Ingredients": 246,
    "Luncheon Kingdom - Luncheon Kingdom Timer Challenge 1": 264,
    "Luncheon Kingdom - Love Above the Lava": 803,
    "Luncheon Kingdom - Magma Swamp: Floating and Sinking": 302,
    "Luncheon Kingdom - Corner of the Magma Swamp": 303
}

loc_Luncheon_Post_Wooded = {
    "Luncheon Kingdom - Secret Path to Mount Volbono!": 260
}

loc_Luncheon_Post_Spewart = {
    "Luncheon Kingdom - Under the Cheese Rocks": 251,
    "Luncheon Kingdom - Atop the Jutting Crag": 253,
    "Luncheon Kingdom - Is This an Ingredient Too?!": 240,
    "Luncheon Kingdom - Atop a Column in a Row": 247,
    "Luncheon Kingdom - Island of Salt Floating in the Lava": 245,
    "Luncheon Kingdom - Golden Turnip Recipe 1": 242,
    "Luncheon Kingdom - Golden Turnip Recipe 2": 241,
    "Luncheon Kingdom - Shopping in Mount Volbono": 294,
    "Luncheon Kingdom - Luncheon Kingdom Slots": 1042,
    "Luncheon Kingdom - A Strong Simmer": 991,
    "Luncheon Kingdom - An Extreme Simmer": 990,
    "Luncheon Kingdom - Fork Flickin' to the Summit": 296,
    "Luncheon Kingdom - Fork Flickin' Detour": 297,
    "Luncheon Kingdom - Excavate ‘n' Search the Cheese Rocks": 300,
    "Luncheon Kingdom - Climb the Cheese Rocks": 301
}

loc_Luncheon_Post_Cheese_Rocks = {
    "Luncheon Kingdom - Big Pot on the Volcano: Dive In!": 292,
    "Luncheon Kingdom - Climb Up the Cascading Magma": 257,
    "Luncheon Kingdom - Surrounded by Tall Mountains": 248,
    "Luncheon Kingdom - Light the Lantern on the Small Island": 254,
    "Luncheon Kingdom - Golden Turnip Recipe 3": 243,
    "Luncheon Kingdom - Beneath the Rolling Vegetables": 263,
    "Luncheon Kingdom - All the Cracks Are Fixed": 970,
    "Luncheon Kingdom - Taking Notes: Swimming in Magma": 244,
    "Luncheon Kingdom - Magma Narrow Path": 299,
    "Luncheon Kingdom - Crossing to the Magma": 298,
    "Luncheon Kingdom - Spinning Athletics End Goal": 307,
    "Luncheon Kingdom - Taking Notes: Spinning Athletics": 306,
    "Luncheon Kingdom - Luncheon Kingdom Timer Challenge 2": 249,
    "Luncheon Kingdom - Treasure Beneath the Cheese Rocks": 952,
    "Luncheon Kingdom - Light the Two Flames": 255,
    "Luncheon Kingdom - The Treasure Chest in the Veggies": 992,
    "Seaside Kingdom - Found with Luncheon Kingdom Art": 1090,
    "Luncheon Kingdom - Alcove Behind the Pillars of Magma": 259,

}

loc_Luncheon_Peace = {
    "Luncheon Kingdom - Cookatiel Showdown!": 290,
    "Luncheon Kingdom - Luncheon Kingdom Timer Challenge 3": 711,
    "Luncheon Kingdom - Caught Hopping at the Volcano!": 289,
    "Luncheon Kingdom - Light the Far-Off Lanterns": 261,
    "Luncheon Kingdom - Bon Appetit, Captain Toad!": 265,
    "Luncheon Kingdom - Taking Notes: Big Pot Swim": 833,
    "Luncheon Kingdom - A Tourist in the Luncheon Kingdom!": 908
}

loc_Luncheon_Captures = {
    "Lava Bubble": 3737,
    "Volbonan": 3738,
    "Hammer Bro": 3739,
}

loc_Luncheon_Captures_Post_Cheese_Rocks = {
    "Meat": 3740,
    "Fire Piranha Plant": 3741,
}

loc_Luncheon_Shop = {
    "Chef Hat": 2514,
    "Chef Suit": 2552,
    "Painter's Cap": 2515,
    "Painter Outfit": 2553,
    "Luncheon Kingdom Sticker": 2591,
    "Souvenir Forks": 2617,
    "Vegetable Plate": 2618,
    "Clown Hat": 2525,
    "Clown Suit": 2563
}

sub_area_magma_swamp = {
    "Magma Swamp: Floating and Sinking": 302,
    "Corner of the Magma Swamp": 303
}

sub_area_veggies = {
    "The Treasure Chest in the Veggies": 992,
}

sub_area_cook = {
    "A Strong Simmer": 991,
    "An Extreme Simmer": 990,
}

sub_area_forks = {
    "Fork Flickin' to the Summit": 296,
    "Fork Flickin' Detour": 297,
}

sub_area_cheese = {
    "Excavate ‘n' Search the Cheese Rocks": 300,
    "Climb the Cheese Rocks": 301
}

sub_area_lava_bubble = {
    "Magma Narrow Path": 299,
    "Crossing to the Magma": 298,
}

sub_area_spinning_athletics = {
    "Spinning Athletics End Goal": 307,
    "Taking Notes: Spinning Athletics": 306,
}

sub_area_luncheon_story = {
    "Climb Up the Cascading Magma": 257,
    "Alcove Behind the Pillars of Magma": 259,
}

sub_area_luncheon_slots = {
    "Luncheon Kingdom Slots": 1042,
}

loc_Luncheon_Postgame = {
    "Luncheon Kingdom - The Rooftop Lantern": 269,
    "Luncheon Kingdom - Jammin' in the Luncheon Kingdom": 907,
    "Luncheon Kingdom - Mechanic: Repairs Complete!": 886,
    "Luncheon Kingdom - Diving from the Big Pot!": 1005,
    "Luncheon Kingdom - Hat-and-Seek: Among the Food": 280,
    "Luncheon Kingdom - Luncheon Kingdom: Regular Cup": 266,
    "Luncheon Kingdom - Peach in the Luncheon Kingdom": 268,
    "Luncheon Kingdom - From Inside a Bright Stone": 271,
    "Luncheon Kingdom - Under the Meat Plateau": 829,
    "Luncheon Kingdom - On Top of a Tall, Tall Roof": 287,
    "Luncheon Kingdom - From a Crack in the Hard Ground": 276,
    "Luncheon Kingdom - By the Cannon Pointed at the Big Pot": 277,
    "Luncheon Kingdom - Luncheon Kingdom: Master Cup": 895,
    "Luncheon Kingdom - Stepping Over the Gears and Lanterns on the Gear Steps": 313,
    "Luncheon Kingdom - Lanterns on the Gear Steps": 312,
    "Luncheon Kingdom - Volcano Cave Cruisin'": 310,
    "Luncheon Kingdom - Volcano Cave and Mysterious Clouds": 311,
    "Luncheon Kingdom - Treasure of the Lava Islands": 1101,
    "Luncheon Kingdom - Flying Over the Lava Islands": 308
}

sub_area_gear_steps = {
    "Stepping Over the Gears and Lanterns on the Gear Steps": 313,
    "Lanterns on the Gear Steps": 312,
}

sub_area_volcano_cave = {
    "Volcano Cave Cruisin'": 310,
    "Volcano Cave and Mysterious Clouds": 311,
}

sub_area_lava_islands = {
    "Treasure of the Lava Islands": 1101,
    "Flying Over the Lava Islands": 308
}

loc_Ruined = {
    "Ruined Kingdom - Battle with the Lord of Lightning!": 795,
    "Ruined Kingdom - In the Ancient Treasure Chest": 793,
    "Ruined Kingdom - Roulette Tower: Climbed": 888,
    "Ruined Kingdom - Roulette Tower: Stopped": 889
}

sub_area_roulette_tower = {
    "Roulette Tower: Climbed": 888,
    "Roulette Tower: Stopped": 889
}

loc_Ruined_Postgame = {
    "Ruined Kingdom - Peach in the Ruined Kingdom": 1117,
    "Ruined Kingdom - Caught in a Big Horn": 834,
    "Ruined Kingdom - Upon the Broken Arch": 835,
    "Ruined Kingdom - Rolling Rock on the Battlefield": 909,
    "Ruined Kingdom - Charging Through an Army": 891,
    "Ruined Kingdom - The Mummy's Curse": 890
}

sub_area_ruined_charging = {
    "Charging Through an Army": 891,
    "The Mummy's Curse": 890
}

loc_Bowser = {
    "Bowser Kingdom - Infiltrate Bowser's Castle!": 325,
    "Bowser Kingdom - Behind the Big Wall": 321,
    "Bowser Kingdom - Caught on the Iron Fence": 345,
    "Bowser Kingdom - Taking notes: Between spinies": 315,
    "Bowser Kingdom - Stack up above the wall": 324,
}

loc_Bowser_Infiltrate = {
    "Bowser Kingdom - Smart Bombing": 334,
    "Bowser Kingdom - Treasure Inside the Turret": 333,
    "Bowser Kingdom - Poking Your Nose in the Plaster Wall": 323,
    "Bowser Kingdom - Poking the Turret Wall": 337,
}

loc_Bowser_Post_Bombing = {
    "Bowser Kingdom - Big Broodal Battle": 314,
    "Bowser Kingdom - From the Side above the Castle Gate": 335,
    "Bowser Kingdom - Exterminate the Ogres!": 320,
    "Bowser Kingdom - Jizo All in a Row": 316,
    "Bowser Kingdom - Underground Jizo": 317,
    "Bowser Kingdom - Shopping at Bowser's Castle": 360,
    "Bowser Kingdom - Scene of Crossing the Poison Swamp": 987,
    "Bowser Kingdom - Taking Notes: In the Folding Screen": 988,
}

loc_Bowser_Mecha_Broodal = {
    "Bowser Kingdom - Showdown at Bowser's Castle": 332,
}

loc_Bowser_Peace = {

    "Bowser Kingdom - Sunken Treasure in the Moat": 327,
    "Bowser Kingdom - On the Giant Bowser Statue's Nose": 339,
    "Bowser Kingdom - Inside a Block in the Castle": 319,
    "Bowser Kingdom - Hidden Corridor Under the Floor": 1092,
    "Bowser Kingdom - Poking Your Nose by the Great Gate": 328,
    "Bowser Kingdom - Found Behind Bars!": 331,
    "Bowser Kingdom - Good to See You, Captain Toad!": 318,
    "Bowser Kingdom - Dashing Above the Clouds": 365,
    "Bowser Kingdom - Dashing Through the Clouds": 366,
    "Bowser Kingdom - Past the Moving Wall": 340,
    "Bowser Kingdom - Above the Poison Swamp": 326,
    "Bowser Kingdom - Knocking Down the Nice Frame": 1133,
    "Bowser Kingdom - Caught Hopping at Bowser's Castle!": 343,
    "Bowser Kingdom - Bowser's Kingdom Timer Challenge 1": 691,
    "Bowser Kingdom - Fishing(?) in Bowser's Castle": 690,
    "Bowser Kingdom - Bowser's Castle Treasure Vault": 999,
    "Bowser Kingdom - On Top of the Spinning Tower": 368,
    "Bowser Kingdom - Down and Up the Spinning Tower": 367,
    "Bowser Kingdom - Jizo's Big Adventure": 363,
    "Bowser Kingdom - Jizo and the Hidden Room": 364,
}

loc_Bowser_Captures = {
    "Pokio": 3742,
}

loc_Bowser_Captures_Post_Bombing = {
    "Jizo": 3743,
}

loc_Bowser_Shop = {
    "Samurai Helmet": 2516,
    "Samurai Armor": 2554,
    "Happi Headband": 2517,
    "Happi Outfit": 2555,
    "Bowser's Kingdom Sticker": 2592,
    "Paper Lantern": 2619,
    "Jizo Statue": 2620
}

sub_area_samurai = {
    "Scene of Crossing the Poison Swamp": 987,
    "Taking Notes: In the Folding Screen": 988,
}

sub_area_bowser_vault = {
    "Bowser's Castle Treasure Vault": 999,
}

sub_area_jizo_adventure = {
    "Jizo's Big Adventure": 363,
    "Jizo and the Hidden Room": 364,
}

sub_area_spinning_tower = {
    "On Top of the Spinning Tower": 368,
    "Down and Up the Spinning Tower": 367,
}

loc_Bowser_Postgame = {
    "Bowser Kingdom - Sphynx Over Bowser's Castle": 1039,
    "Bowser Kingdom - I Met a Pokio!": 941,
    "Bowser Kingdom - Bowser's Kingdom Regular Cup": 859,
    "Bowser Kingdom - A Rumble Under the Arena Floor": 1161,
    "Bowser Kingdom - Secret Path to Bowser's Castle": 322,
    "Bowser Kingdom - Peach in Bowser's Kingdom": 338,
    "Sand Kingdom - Found with Bowser's Kingdom Art": 1091,
    "Bowser Kingdom - Behind the Tall Wall: Poke, Poke!": 350,
    "Bowser Kingdom - From Crates in the Moat": 1032,
    "Bowser Kingdom - Caught on the Giant Horn": 1016,
    "Bowser Kingdom - Inside a Block at the Gate": 356,
    "Bowser Kingdom - Small Bird in Bowser's Castle": 1014,
    "Bowser Kingdom - Invader in Bowser's Castle": 349,
    "Bowser Kingdom - Jumping from Flag to Flag": 355,
    "Bowser Kingdom - Bowser's Kingdom Timer Challenge 2": 963,
    "Bowser Kingdom - Taking Notes: On the Wall": 351,
    "Bowser Kingdom - Taking Notes with a Spinning Throw": 1102,
    "Bowser Kingdom - Third Courtyard Outskirts": 348,
    "Bowser Kingdom - Stone Wall Circuit": 1143,
    "Bowser Kingdom - Bowser's Kingdom Master Cup": 896,
    "Bowser Kingdom - Searching Hexagon Tower": 801,
    "Bowser Kingdom - Center of Hexagon Tower": 802,
    "Bowser Kingdom - Climb the Wooden Tower": 370,
    "Bowser Kingdom - Poke the Wooden Tower": 369
}

sub_area_hexagon_tower = {
    "Searching Hexagon Tower": 801,
    "Center of Hexagon Tower": 802,
}

sub_area_wooden_tower = {
    "Climb the Wooden Tower": 370,
    "Poke the Wooden Tower": 369
}

loc_Moon = {
    "Moon Kingdom - Shining Above the Moon": 579,
    "Moon Kingdom - Along the Cliff Face": 586,
    "Moon Kingdom - The Tip of a White Spire": 577,
    "Moon Kingdom - Rolling Rock on the Moon": 583,
    "Moon Kingdom - Caught Hopping on the Moon!": 585,
    "Moon Kingdom - Cliffside Treasure Chest": 582,
    "Moon Kingdom - Moon Kingdom Timer Challenge 1": 584,
    "Moon Kingdom - Taking Notes: On the Moon's Surface": 576,
    "Moon Kingdom - Under the Bowser Statue": 595,
    "Moon Kingdom - In a Hole in the Magma": 596,
    "Moon Kingdom - Around the Barrier Wall": 597,
    "Moon Kingdom - On Top of the Cannon": 594,
    "Moon Kingdom - Fly to the Treasure Chest and Back": 598,
    "Moon Kingdom - Up in the Rafters": 593,
}

loc_Moon_Captures = {
    "Bowser": 3748,
}

loc_Moon_Cave_Captures = {
    "Bowser statue": 3744,
    "Parabones": 3745,
    "Banzai Bill": 3746,
    "Chargin' Chuck": 3747,
}

loc_Moon_Shop = {
    "Space Helmet": 2518,
    "Space Suit": 2556,
    "Moon Kingdom Sticker": 2593,
    "Moon Rock Fragment": 2621,
    "Moon Lamp": 2622,
    "Football Helmet": 2526,
    "Football Uniform": 2564,

}

loc_Moon_Post_Moon = {
    "Beat the Game": 2499
}

loc_Moon_Outfit = {
    "Mario's Top Hat": 2538,
    "Mario's Tuxedo": 2576
}

# Church sub area?
# Moon Cave sub area?

loc_Moon_Postgame = {
    "Moon Kingdom - Sneaking Around in the Crater": 693,
    "Moon Kingdom - Found on the Moon, Good Dog!": 672,
    "Moon Kingdom - Moon Shards on the Moon": 671,
    "Moon Kingdom - Moon Quiz: Amazing!": 580,
    "Moon Kingdom - Thanks, Captain Toad!": 1000,
    "Moon Kingdom - Shopping in Honeylune Ridge": 1157,
    "Moon Kingdom - Walking on the Moon!": 578,
    "Moon Kingdom - Moon Kingdom Regular Cup": 707,
    "Moon Kingdom - Doctor in the House": 1164,
    "Moon Kingdom - Sphynx's Hidden Vault": 599,
    "Moon Kingdom - A Tourist in the Moon Kingdom": 911,
    "Moon Kingdom - Peach in the Moon Kingdom": 581,
    "Wooded Kingdom - Found with Moon Kindgom Art": 1165,
    "Moon Kingdom - Mysterious Flying Object": 1049,
    "Moon Kingdom - Hidden on the Side of the Cliff": 810,
    "Moon Kingdom - Jumping High as a Frog": 811,
    "Moon Kingdom - Moon Kingdom Timer Challenge 2": 798,
    "Moon Kingdom - Walking on the Moon: Again!": 714,
    "Moon Kingdom - Moon Kingdom Master Cup": 899,
    "Moon Kingdom - Taking Notes: In Low Gravity": 828,
    "Moon Kingdom - Center of the Galaxy": 603,
    "Moon Kingdom - Edge of the Galaxy": 604,
    "Moon Kingdom - Navigating Giant Swings": 601,
    "Moon Kingdom - A Swing on Top of a Swing": 602
}

sub_area_galaxy = {
    "Edge of the Galaxy": 604,
    "Navigating Giant Swings": 601,
}

sub_area_swings = {
    "Navigating Giant Swings": 601,
    "A Swing on Top of a Swing": 602
}

loc_Mushroom = {
    "Mushroom Kingdom - A Tourist in the Mushroom Kingdom": 912,
    "Mushroom Kingdom - Perched on the Castle Roof": 1134,
    "Mushroom Kingdom - Pops out of the Tail": 1027,
    "Mushroom Kingdom - Caught Hopping at Peach's Castle!": 613,
    "Mushroom Kingdom - Gardening for Toad: Garden Seed": 1108,
    "Mushroom Kingdom - Gardening for Toad: Field Seed": 1109,
    "Mushroom Kingdom - Gardening for Toad: Pasture Seed": 1111,
    "Mushroom Kingdom - Gardening for Toad: Lake Seed": 1110,
    "Mushroom Kingdom - Grow a Flower Garden": 959,
    "Mushroom Kingdom - Mushroom Kingdom Timer Challenge": 977,
    "Mushroom Kingdom - Found at Peach's Castle! Good Dog!": 840,
    "Mushroom Kingdom - Taking Notes: Around the Well": 1121,
    "Mushroom Kingdom - Herding Sheep at Peach's Castle": 607,
    "Mushroom Kingdom - Gobbling Fruit with Yoshi": 856,
    "Mushroom Kingdom - Yoshi's Second Helping!": 857,
    "Mushroom Kingdom - Yoshi's All Filled Up!": 858,
    "Mushroom Kingdom - Love at Peach's Castle": 606,
    "Mushroom Kingdom - Toad Defender": 984,
    "Mushroom Kingdom - Forever Onward, Captain Toad!": 605,
    "Mushroom Kingdom - Jammin' in the Mushroom Kingdom": 913,
    "Mushroom Kingdom - Shopping Near Peach's Castle": 933,
    "Mushroom Kingdom - Mushroom Kingdom Regular Cup": 967,
    "Mushroom Kingdom - Mushroom Kingdom Master Cup": 968,
    "Mushroom Kingdom - Picture Match: Basically Mario": 985,
    "Mushroom Kingdom - Picture Match: A Stellar Mario!": 986,
    "Mushroom Kingdom - Light from the Ceiling": 807,
    "Mushroom Kingdom - Loose-Tile Trackdown": 956,
    "Mushroom Kingdom - Totally Classic": 934,
    "Mushroom Kingdom - Courtyard Chest Trap": 1144,
    "Mushroom Kingdom - Yoshi's Feast in the Sea of Clouds": 974,
    "Mushroom Kingdom - Sunken Star in the Sea of Clouds": 975,
    "Mushroom Kingdom - Secret 2D Treasure": 1050,
    "Mushroom Kingdom - 2D Boost from Bullet Bill": 1051,
    "Mushroom Kingdom - Tussle in Tostarena: Rematch": 1148,
    "Mushroom Kingdom - Struggle in Steam Gardens: Rematch": 1149,
    "Mushroom Kingdom - Dust-Up in New Donk City: Rematch": 1141,
    "Mushroom Kingdom - Battle in Bubblaine: Rematch": 1147,
    "Mushroom Kingdom - Blowup at Mount Volbono: Rematch": 1142,
    "Mushroom Kingdom - Rumble in Crumbleden: Rematch": 1146,
    "Cap Kingdom - Found with Mushroom Kingdom Art": 1152,
    "Mushroom Kingdom - Hat-and-Seek: Mushroom Kingdom": 978,
    "Mushroom Kingdom - Princess Peach, Home Again!": 1119,
}

loc_Mushroom_Post_Luncheon = {
    "Mushroom Kingdom - Secret Path to Peach's Castle!": 608
}

loc_Mushroom_Captures = {
    "Yoshi": 3752
}

loc_picture_match_mario = {
    "Picture Match Part (Mario)": 3751,
}

loc_Mushroom_Shop = {
    "Mario 64 Cap": 2519,
    "Mario 64 Suit": 2557,
    "Mushroom Cushion Set": 2623,
    "Peach's Castle Model": 2624,
    "Pipe Sticker": 2594,
    "Coin Sticker": 2595,
    "Block Sticker": 2596,
    "? Block Sticker": 2597,
    "Mushroom Kingdom Sticker": 2598,
    "Classic Cap": 2527,
    "Classic Suit": 2565,
    "Skeleton Suit": 2580
}

sub_area_mushroom_picture = {
    "Picture Match: Basically Mario": 985,
    "Picture Match: A Stellar Mario!": 986,
}

sub_area_castle = {
    "Light from the Ceiling": 807,
    "Loose-Tile Trackdown": 956,
}

sub_area_64 = {
    "Totally Classic": 934,
    "Courtyard Chest Trap": 1144,
}

sub_area_mushroom_well = {
    "Secret 2D Treasure": 1050,
    "2D Boost from Bullet Bill": 1051,
}

sub_area_yoshi_clouds = {
    "Yoshi's Feast in the Sea of Clouds": 974,
    "Sunken Star in the Sea of Clouds": 975,
}

sub_area_rematch_tostarena = {
    "Tussle in Tostarena: Rematch": 1148,
}

sub_area_rematch_steam_gardens = {
    "Struggle in Steam Gardens: Rematch": 1149,
}

sub_area_rematch_metro = {
    "Dust-Up in New Donk City: Rematch": 1141,
}

sub_area_rematch_bubblaine = {
    "Battle in Bubblaine: Rematch": 1147,
}

sub_area_rematch_volbono = {
    "Blowup at Mount Volbono: Rematch": 1142,
}

sub_area_rematch_crumbleden = {
    "Rumble in Crumbleden: Rematch": 1146,
}


loc_Postgame_Shop = {
    "Luigi Cap": 2528,
    "Luigi Suit": 2566,
    "Doctor Headwear": 2532,
    "Doctor Outfit": 2570,
    "Waluigi Cap": 2530,
    "Waluigi Suit": 2568,
    "Diddy Kong Hat": 2533,
    "Diddy Kong Suit": 2571,
    "Wario Cap": 2529,
    "Wario Suit": 2567,
    "Hakama": 2579,
    "Bowser's Top Hat": 2534,
    "Bowser's Tuxedo": 2572,
    "Bridal Veil": 2535,
    "Bridal Gown": 2573,
    "Gold Mario Cap": 2531,
    "Gold Mario Suit": 2569,
    "Metal Mario Cap": 2536,
    "Metal Mario Suit": 2574
}

loc_Dark = {
    "Dark Side - Arrival at Rabbit Ridge!": 1055,
    "Dark Side - Captain Toad on the Dark Side!": 1122,
    "Dark Side - Breakdown Road: Hurry": 1062,
    "Dark Side - Breakdown Road: Final Challenge!": 1063,
    "Dark Side - Invisible Road: Rush!": 1069,
    "Dark Side - Invisible Road: Secret!": 1068,
    "Dark Side - Vanishing Road Rush": 1065,
    "Dark Side - Vanishing Road Challenge": 1064,
    "Dark Side - Yoshi Under Siege": 1066,
    "Dark Side - Fruit Feast Under Siege": 1067,
    "Dark Side - Yoshi on the Sinking Island": 1070,
    "Dark Side - Fruit Feast on the Sinking Island": 1071,
    "Dark Side - Yoshi's Magma Swamp": 1082,
    "Dark Side - Fruit Feast in the Magma Swamp!": 1083,
    "Cascade Kingdom - Found with Dark Side Art 1": 1132,
    "Metro Kingdom - Found with Dark Side Art 2": 1130,
    "Mushroom Kingdom - Found with Dark Side Art 3": 1131,
    "Cloud Kingdom - Found with Dark Side Art 4": 1124,
    "Snow Kingdom - Found with Dark Side Art 5": 1129,
    "Seaside Kingdom - Found with Dark Side Art 6": 1127,
    "Lost Kingdom - Found with Dark Side Art 7": 1126,
    "Luncheon Kingdom - Found with Dark Side Art 8": 1123,
    "Lake Kingdom - Found with Dark Side Art 9": 1128,
    "Ruined Kingdom - Found with Dark Side Art 10": 1125,
}

loc_Dark_Outfit = {
    "King's Crown": 2537,
    "King's Outfit": 2575
}

sub_area_darker_breakdown = {
    "Breakdown Road: Hurry": 1062,
    "Breakdown Road: Final Challenge!": 1063,
}

sub_area_darker_invisible = {
    "Invisible Road: Rush!": 1069,
    "Invisible Road: Secret!": 1068,
}

sub_area_darker_vanishing = {
    "Vanishing Road Rush": 1065,
    "Vanishing Road Challenge": 1064,
}

sub_area_darker_yoshi_siege = {
    "Yoshi Under Siege": 1066,
    "Fruit Feast Under Siege": 1067,
}

sub_area_darker_yoshi_sinking = {
    "Yoshi on the Sinking Island": 1070,
    "Fruit Feast on the Sinking Island": 1071,
}

sub_area_darker_yoshi_magma = {
    "Yoshi's Magma Swamp": 1082,
    "Fruit Feast in the Magma Swamp!": 1083,
}


loc_Darker = {
    "Darker Side - A Long Journey's End!": 1061,
}

loc_Darker_Outfit = {
    "Invisibility Hat": 2581
}

loc_Captures = {
    "Frog": 3701,
    "Spark Pylon": 3702,
    "Paragoomba": 3703,
    "Chain Chomp": 3704,
    "Big Chain Chomp": 3705,
    "Broode's Chain Chomp": 3706,
    "T-Rex": 3707,
    "Binoculars": 3708,
    "Bullet Bill": 3709,
    "Moe-Eye": 3710,
    "Cactus": 3711,
    "Goomba": 3712,
    "Knucklotec's Fist": 3713,
    "Mini Rocket": 3714,
    "Glydon": 3715,
    "Lakitu": 3716,
    "Zipper": 3717,
    "Cheep Cheep": 3718,
    "Puzzle Part (Lake Kingdom)": 3719,
    "Poison Piranha Plant": 3720,
    "Uproot": 3721,
    "Fire Bro": 3722,
    "Sherm": 3723,
    "Coin Coffer": 3724,
    "Tree": 3725,
    "Boulder": 3726,
    "Picture Match Part (Goomba)": 3727,
    "Tropical Wiggler": 3728,
    "Pole": 3729,
    "Manhole": 3730,
    "Taxi": 3731,
    "RC Car": 3732,
    "Ty-foo": 3733,
    "Shiverian Racer": 3734,
    "Cheep Cheep (Snow Kingdom)": 3735,
    "Gushen": 3736,
    "Lava Bubble": 3737,
    "Volbonan": 3738,
    "Hammer Bro": 3739,
    "Meat": 3740,
    "Fire Piranha Plant": 3741,
    "Pokio": 3742,
    "Jizo": 3743,
    "Bowser statue": 3744,
    "Parabones": 3745,
    "Banzai Bill": 3746,
    "Chargin' Chuck": 3747,
    "Bowser": 3748,
    "Letter": 3749,
    "Puzzle Part (Metro Kingdom)": 3750,
    "Picture Match Part (Mario)": 3751,
    "Yoshi": 3752
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
    **loc_Sand_Underground,
    **loc_Sand_Peace,
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
    **loc_Lake_Shop,
    **loc_Wooded_Shop,
    **loc_Lost_Shop,
    **loc_Metro_Shop,
    **loc_Snow_Shop,
    **loc_Seaside_Shop,
    **loc_Luncheon_Shop,
    **loc_Bowser_Shop,
    **loc_Moon_Outfit,
    **loc_Moon_Shop,
    **loc_Mushroom_Shop,
    **loc_Postgame_Shop,
    **loc_Dark_Outfit,
    **loc_Darker_Outfit
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

locations_table = {
    **base_locations_table,
    **post_game_locations_table,
    **special_locations_table,
    **shop_locations_table,
    **loc_Post_Cloud,
    **loc_Moon_Post_Moon,
    **loc_Captures
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
    {**loc_Lake, **loc_Lake_Post_Seaside, **loc_Lake_Postgame},
    {**loc_Wooded, **loc_Wooded_Post_Story1, **loc_Wooded_Peace, **loc_Wooded_Post_Metro, **loc_Wooded_Postgame},
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

goals_table = {
    2 : "Sand Kingdom - The Hole in the Desert",
    4 : "Lake Kingdom - Broodals Over the Lake",
    7 : "Metro Kingdom - A Traditional Festival!",
    10 : "Luncheon Kingdom - Cookatiel Showdown!",
    13 : "Beat the Game",
    15 : "Dark Side - Arrival at Rabbit Ridge!",
    16 : "Darker Side - A Long Journey's End!"
}