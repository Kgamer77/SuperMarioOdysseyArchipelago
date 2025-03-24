import typing
from BaseClasses import Region
#from .Options import SMOOptions
from .Locations import SMOLocation, loc_Cap, loc_Cascade, loc_Cascade_Revisit,  \
    loc_Sand, loc_Lake, loc_Wooded, loc_Cloud, loc_Lost, loc_Lost_Revisit, loc_Metro, \
    loc_Snow, loc_Seaside, loc_Luncheon, loc_Ruined, loc_Bowser, loc_Moon, \
    locations_table, post_game_locations_table, loc_Dark, loc_Darker, special_locations_table, \
    loc_Cap_Shop, loc_Cascade_Shop, loc_Sand_Shop, loc_Lake_Shop, loc_Wooded_Shop, \
    loc_Lost_Shop, loc_Metro_Shop, loc_Snow_Shop, loc_Seaside_Shop, loc_Luncheon_Shop, \
    loc_Bowser_Shop, loc_Moon_Shop, loc_Mushroom_Shop, loc_Dark_Outfit, loc_Darker_Outfit, \
    loc_Sand_Revisit, loc_Lake_Post_Seaside, loc_Wooded_Post_Metro, loc_Metro_Post_Sand, \
    loc_Cascade_Post_Metro, loc_Cascade_Post_Snow, loc_Post_Cloud, loc_Moon_Post_Moon, \
    loc_Luncheon_Post_Wooded, loc_Mushroom_Post_Luncheon, loc_Sand_Peace

from .Logic import count_moons, total_moons

class SMORegion(Region):
    subregions: typing.List[Region] = []

def create_regions(self, world, player):
    # Cascade Regions
    regCascade = Region("Menu", player, world, "Cascade Kingdom")
    create_locs(regCascade, *loc_Cascade.keys())
    world.regions.append(regCascade)

    regCascadeRe = Region("Cascade Revisit", player, world, "Cascade Kingdom 2")
    create_locs(regCascadeRe, *loc_Cascade_Revisit.keys())
    world.regions.append(regCascadeRe)

    # Cap
    regCap = Region("Cap", player, world, "Cap Kingdom")
    create_locs(regCap, *loc_Cap.keys())
    world.regions.append(regCap)

    # Sand Regions
    regSand = Region("Sand", player, world, "Sand Kingdom")
    create_locs(regSand, *loc_Sand.keys())
    world.regions.append(regSand)
    regSandPeace = Region("Sand Peace", player, world, "Sand Kingdom Peace")
    create_locs(regSandPeace, *loc_Sand_Peace.keys())
    world.regions.append(regSandPeace)

    # Lake Regions
    regLake = Region("Lake", player, world, "Lake Kingdom")
    create_locs(regLake, *loc_Lake.keys())
    world.regions.append(regLake)

    # Wooded
    regWooded = Region("Wooded" , player, world, "Wooded Kingdom")
    create_locs(regWooded, *loc_Wooded.keys())
    world.regions.append(regWooded)

    # Cloud
    regCloud = Region("Cloud", player, world, "Cloud Kingdom")
    create_locs(regCloud, *loc_Cloud.keys())
    world.regions.append(regCloud)

    # Lost
    regLost = Region("Lost", player, world, "Lost Kingdom")
    create_locs(regLost, *loc_Lost.keys())
    world.regions.append(regLost)

    # Metro
    regMetro = Region("Metro", player, world, "Metro Kingdom")
    create_locs(regMetro, *loc_Metro.keys())
    world.regions.append(regMetro)

    # Snow
    regSnow = Region("Snow", player, world, "Snow Kingdom")
    create_locs(regSnow, *loc_Snow.keys())
    world.regions.append(regSnow)

    # Seaside
    regSeaside = Region("Seaside", player, world, "Seaside Kingdom")
    create_locs(regSeaside, *loc_Seaside.keys())
    world.regions.append(regSeaside)

    # Luncheon
    regLuncheon = Region("Luncheon", player, world, "Luncheon Kingdom")
    create_locs(regLuncheon, *loc_Luncheon.keys())
    world.regions.append(regLuncheon)

    # Ruined
    regRuined = Region("Ruined", player, world, "Ruined Kingdom")
    create_locs(regRuined, *loc_Ruined.keys())
    world.regions.append(regRuined)

    # Bowser
    regBowser = Region("Bowser", player, world, "Bowser Kingdom")
    create_locs(regBowser, *loc_Bowser.keys())
    world.regions.append(regBowser)

    # Moon
    regMoon = Region("Moon", player, world, "Moon Kingdom")
    create_locs(regMoon, *loc_Moon.keys())
    world.regions.append(regMoon)

    # Post Game
    regPostGame = Region("Post Game", player, world, "Post Game Moons")
    create_locs(regPostGame, *post_game_locations_table.keys(), locs_table= post_game_locations_table)
    world.regions.append(regPostGame)

    # Dark Side
    regDark = Region("Dark", player, world, "Dark Side")
    create_locs(regDark, *loc_Dark.keys(),  locs_table= special_locations_table)
    world.regions.append(regDark)

    # Darker Side
    regDarker = Region("Darker", player, world, "Darker Side")
    create_locs(regDarker, *loc_Darker.keys(), locs_table=special_locations_table)
    world.regions.append(regDarker)

    # Revisits
    regCascadeMetro = Region("Cascade Post Metro", player, world, "Cascade Wanderer")
    regCascadeSnow = Region("Cascade Painting", player, world, "Cascade Painting")
    regSandRe = Region("Sand Revisit", player, world, "Sand Revisit")
    regLakeSeaside = Region("Lake Painting", player, world, "Lake Painting")
    regWoodedMetro = Region("Wooded Painting", player, world, "Wooded Painting")
    regLostRe = Region("Lost Revisit", player, world, "Lost Revisit")
    regPostCloud = Region("Post Cloud", player, world, "Post Cloud")
    regMetroSand = Region("Metro Painting", player, world, "Metro Painting")
    regLuncheonWooded = Region("Luncheon Painting", player, world, "Luncheon Painting")
    regPostMoon = Region("Post Moon", player, world, "Post Moon")
    regMushroomLuncheon = Region("Mushroom Painting", player, world, "Mushroom Painting")
    create_locs(regCascadeMetro, *loc_Cascade_Post_Metro.keys())
    create_locs(regCascadeSnow, *loc_Cascade_Post_Snow.keys())
    create_locs(regSandRe, *loc_Sand_Revisit.keys())
    create_locs(regLakeSeaside, *loc_Lake_Post_Seaside.keys())
    create_locs(regWoodedMetro, *loc_Wooded_Post_Metro.keys())
    create_locs(regLostRe, *loc_Lost_Revisit.keys())
    create_locs(regPostCloud, *loc_Post_Cloud.keys())
    create_locs(regMetroSand, *loc_Metro_Post_Sand.keys())
    create_locs(regLuncheonWooded, *loc_Luncheon_Post_Wooded.keys())
    create_locs(regPostMoon, *loc_Moon_Post_Moon.keys())
    create_locs(regMushroomLuncheon, *loc_Mushroom_Post_Luncheon.keys())
    world.regions.append(regCascadeMetro)
    world.regions.append(regCascadeSnow)
    world.regions.append(regSandRe)
    world.regions.append(regLakeSeaside)
    world.regions.append(regWoodedMetro)
    world.regions.append(regLostRe)
    world.regions.append(regPostCloud)
    world.regions.append(regMetroSand)
    world.regions.append(regLuncheonWooded)
    world.regions.append(regPostMoon)
    world.regions.append(regMushroomLuncheon)

    # Shops
    regCapShop = Region("Cap Shop", player, world, "Shop")
    regCascadeShop = Region("Cascade Shop", player, world, "Shop")
    regSandShop = Region("Sand Shop", player, world, "Shop")
    regLakeShop = Region("Lake Shop", player, world, "Shop")
    regWoodedShop = Region("Wooded Shop", player, world, "Shop")
    regLostShop = Region("Lost Shop", player, world, "Shop")
    regMetroShop = Region("Metro Shop", player, world, "Shop")
    regSnowShop = Region("Snow Shop", player, world, "Shop")
    regSeasideShop = Region("Seaside Shop", player, world, "Shop")
    regLuncheonShop = Region("Luncheon Shop", player, world, "Shop")
    regBowserShop = Region("Bowser Shop", player, world, "Shop")
    regMoonShop = Region("Moon Shop", player, world, "Shop")
    regMushroomShop = Region("Mushroom Shop", player, world, "Shop")
    regDarkOutfit = Region("Dark Outfit", player, world, "Shop")
    regDarkerOutfit = Region("Darker Outfit", player, world, "Shop")
    create_locs(regCapShop, *loc_Cap_Shop.keys())
    create_locs(regCascadeShop, *loc_Cascade_Shop.keys())
    create_locs(regSandShop, *loc_Sand_Shop.keys())
    create_locs(regLakeShop, *loc_Lake_Shop.keys())
    create_locs(regWoodedShop, *loc_Wooded_Shop.keys())
    create_locs(regLostShop, *loc_Lost_Shop.keys())
    create_locs(regMetroShop, *loc_Metro_Shop.keys())
    create_locs(regSnowShop, *loc_Snow_Shop.keys())
    create_locs(regSeasideShop, *loc_Seaside_Shop.keys())
    create_locs(regLuncheonShop, *loc_Luncheon_Shop.keys())
    create_locs(regBowserShop, *loc_Bowser_Shop.keys())
    create_locs(regMoonShop, *loc_Moon_Shop.keys())
    create_locs(regMushroomShop, *loc_Mushroom_Shop.keys())
    create_locs(regDarkOutfit, *loc_Dark_Outfit.keys())
    create_locs(regDarkerOutfit, *loc_Darker_Outfit.keys())
    world.regions.append(regCapShop)
    world.regions.append(regCascadeShop)
    world.regions.append(regSandShop)
    world.regions.append(regLakeShop)
    world.regions.append(regWoodedShop)
    world.regions.append(regLostShop)
    world.regions.append(regMetroShop)
    world.regions.append(regSnowShop)
    world.regions.append(regSeasideShop)
    world.regions.append(regLuncheonShop)
    world.regions.append(regBowserShop)
    world.regions.append(regMoonShop)
    world.regions.append(regMushroomShop)
    world.regions.append(regDarkOutfit)
    world.regions.append(regDarkerOutfit)

    # Progression Connections
    regCascade.connect(regSand, "Sand Enter", lambda state: count_moons(self, state, "Cascade", player) >= 5)
    regSand.connect(regSandPeace, "Sand World Peace", lambda state: state.has("Sand Story Moon (1)", player) and state.has("Sand Story Moon (2)", player))
    regSand.connect(regCap)
    regSand.connect(regCascadeRe)
    regSandPeace.connect(regMetroSand)
    regSand.connect(regLake, "Lake Enter", lambda state: count_moons(self, state, "Sand", player) >= 16)
    regLake.connect(regWooded, "Wooded Enter", lambda state: count_moons(self, state, "Lake", player) >= 8)
    regLake.connect(regSandRe)
    regWooded.connect(regLost, "Lost Enter", lambda state: count_moons(self, state, "Wooded", player) >= 16)
    regWooded.connect(regLuncheonWooded)
    regCloud.connect(regPostCloud)
    regLost.connect(regCloud, "Cloud Available", lambda state: count_moons(self, state, "Lost", player) >= 10)
    regLost.connect(regMetro, "Metro Enter", lambda state: count_moons(self, state, "Lost", player) >= 10)

    regMetro.connect(regSnow, "Snow Enter", lambda state: count_moons(self, state, "Metro", player) >= 20)
    regMetro.connect(regCascadeMetro)
    regMetro.connect(regWoodedMetro)
    regSnow.connect(regSeaside, "Seaside Enter", lambda state: count_moons(self, state, "Snow", player) >= 10)
    regSnow.connect(regCascadeSnow)
    regSeaside.connect(regLuncheon, "Enter Luncheon", lambda state: count_moons(self, state, "Seaside", player) >= 10)
    regLuncheon.connect(regRuined, "Enter Ruined", lambda state: count_moons(self, state, "Luncheon", player) >= 18)
    regLuncheon.connect(regMushroomLuncheon)
    regRuined.connect(regBowser,"Enter Bowser", lambda state: count_moons(self, state, "Ruined", player) >= 3)
    regBowser.connect(regMoon, "Enter Moon", lambda state: count_moons(self, state, "Bowser", player) >= 8)
    regMoon.connect(regPostMoon)
    regMoon.connect(regPostGame)
    regPostGame.connect(regDark, "Dark Access", lambda state: total_moons(self, state, player) >= 250)
    regPostGame.connect(regDarker, "Darker Access", lambda state: total_moons(self, state, player) >= 500)

    # Shop Connections
    regCap.connect(regCapShop)
    regCascadeRe.connect(regCascadeShop)
    regSand.connect(regSandShop)
    regLake.connect(regLakeShop)
    regWooded.connect(regWoodedShop)
    regLost.connect(regLostShop)
    regMetro.connect(regMetroShop)
    regSnow.connect(regSnowShop)
    regSeaside.connect(regSeasideShop)
    regLuncheon.connect(regLuncheonShop)
    regBowser.connect(regBowserShop)
    regMoon.connect(regMoonShop)
    regPostGame.connect(regMushroomShop)
    regDark.connect(regDarkOutfit)
    regDarker.connect(regDarkerOutfit)

def create_locs(reg: Region, *locs: str, locs_table = locations_table):
    reg.locations += ([SMOLocation(reg.player, loc_name, locs_table[loc_name], reg) for loc_name in locs])

