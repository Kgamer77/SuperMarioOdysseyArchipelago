import typing
from BaseClasses import Region
#from .Options import SMOOptions
from .Locations import SMOLocation, loc_Cap, loc_Cascade, loc_Cascade_Revisit, \
    loc_Sand, loc_Lake, loc_Wooded, loc_Cloud, loc_Lost, loc_Lost_Revisit, loc_Metro, \
    loc_Snow, loc_Seaside, loc_Luncheon, loc_Ruined, loc_Bowser, loc_Moon, \
    locations_table, post_game_locations_table, loc_Dark, loc_Darker, special_locations_table, \
    loc_Cap_Shop, loc_Cascade_Shop, loc_Sand_Shop, loc_Lake_Shop, loc_Wooded_Shop, \
    loc_Lost_Shop, loc_Metro_Shop, loc_Snow_Shop, loc_Seaside_Shop, loc_Luncheon_Shop, \
    loc_Bowser_Shop, loc_Moon_Shop, loc_Mushroom_Shop, loc_Dark_Outfit, loc_Darker_Outfit, \
    loc_Sand_Revisit, loc_Lake_Post_Seaside, loc_Wooded_Post_Metro, loc_Metro_Post_Sand, \
    loc_Cascade_Post_Metro, loc_Cascade_Post_Snow, loc_Post_Cloud, loc_Moon_Post_Moon, \
    loc_Luncheon_Post_Wooded, loc_Mushroom_Post_Luncheon, loc_Sand_Peace, loc_Wooded_Post_Story1, \
    loc_Wooded_Peace, loc_Metro_Sewer_Access, loc_Metro_Peace, loc_Snow_Peace, loc_Seaside_Peace, \
    loc_Luncheon_Post_Spewart, loc_Luncheon_Post_Cheese_Rocks, loc_Luncheon_Peace, \
    loc_Bowser_Infiltrate, loc_Bowser_Post_Bombing, loc_Bowser_Peace, loc_Postgame_Shop, loc_Sand_Pyramid, \
    loc_Sand_Underground, loc_Bowser_Mecha_Broodal

from .Logic import count_moons, total_moons

class SMORegion(Region):
    subregions: typing.List[Region] = []

def create_regions(self, world, player):
    """ Creates the regions for Super Mario Odyssey.
            Args:
                self: SMOWorld object for this player's world.
                world: The MultiWorld instance.
                player: The index of this player in the multiworld.
    """
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
    regSandPyramid = Region("Sand Pyramid", player, world, "Sand Kingdom Pyramid")
    create_locs(regSandPyramid, *loc_Sand_Pyramid.keys())
    world.regions.append(regSandPyramid)
    regSandUnderground = Region("Sand Underground", player, world, "Sand Kingdom Underground")
    create_locs(regSandUnderground, *loc_Sand_Underground.keys())
    world.regions.append(regSandUnderground)
    regSandPeace = Region("Sand Peace", player, world, "Sand Kingdom Peace")
    if self.options.goal > 4:
        create_locs(regSandPeace, *loc_Sand_Peace.keys())
    world.regions.append(regSandPeace)

    # Lake Regions
    regLake = Region("Lake", player, world, "Lake Kingdom")
    if self.options.goal > 4:
        create_locs(regLake, *loc_Lake.keys())
    world.regions.append(regLake)

    # Wooded
    regWooded = Region("Wooded" , player, world, "Wooded Kingdom")

    if self.options.goal > 5:
        create_locs(regWooded, *loc_Wooded.keys())
    world.regions.append(regWooded)
    regWoodedStory1 = Region("Wooded Post Road to Sky Garden", player, world, "Wooded Kingdom Story 1")
    if self.options.goal > 5:
        create_locs(regWoodedStory1, * loc_Wooded_Post_Story1.keys())
    world.regions.append(regWoodedStory1)
    regWoodedPeace = Region("Wooded Peace", player, world, "Wooded Kingdom Peace")
    if self.options.goal > 5:
        create_locs(regWoodedPeace, * loc_Wooded_Peace.keys())
    world.regions.append(regWoodedPeace)

    # Cloud
    regCloud = Region("Cloud", player, world, "Cloud Kingdom")
    if self.options.goal > 9:
        create_locs(regCloud, *loc_Cloud.keys())
    world.regions.append(regCloud)

    # Lost
    regLost = Region("Lost", player, world, "Lost Kingdom")
    if self.options.goal > 5:
        create_locs(regLost, *loc_Lost.keys())
    world.regions.append(regLost)

    # Metro
    regMetro = Region("Metro", player, world, "Metro Kingdom")
    if self.options.goal > 5:
        create_locs(regMetro, *loc_Metro.keys())
    world.regions.append(regMetro)
    regMetroSewer = Region("Metro Sewer", player, world, "Metro Kingdom Story 1")
    if self.options.goal > 5:
        create_locs(regMetroSewer, *loc_Metro_Sewer_Access.keys())
    world.regions.append(regMetroSewer)
    regMetroPeace = Region("Metro World Peace", player, world, "Metro Kingdom Peace")
    if self.options.goal > 5:
        create_locs(regMetroPeace, *loc_Metro_Peace.keys())
    world.regions.append(regMetroPeace)


    # Snow
    regSnow = Region("Snow", player, world, "Snow Kingdom")
    if self.options.goal > 9:
        create_locs(regSnow, *loc_Snow.keys())
    world.regions.append(regSnow)
    regSnowPeace = Region("Snow World Peace", player, world, "Snow Kingdom Peace")
    if self.options.goal > 9:
        create_locs(regSnowPeace, *loc_Snow_Peace.keys())
    world.regions.append(regSnowPeace)

    # Seaside
    regSeaside = Region("Seaside", player, world, "Seaside Kingdom")
    if self.options.goal > 9:
        create_locs(regSeaside, *loc_Seaside.keys())
    world.regions.append(regSeaside)
    regSeasidePeace = Region("Seaside World Peace", player, world, "Seaside Kingdom Peace")
    if self.options.goal > 9:
        create_locs(regSeasidePeace, *loc_Seaside_Peace.keys())
    world.regions.append(regSeasidePeace)

    # Luncheon
    regLuncheon = Region("Luncheon", player, world, "Luncheon Kingdom")
    if self.options.goal > 9:
        create_locs(regLuncheon, *loc_Luncheon.keys())
    world.regions.append(regLuncheon)
    regLuncheonSpewart = Region("Luncheon Post Spewart", player, world, "Luncheon Kingdom Story 1")
    if self.options.goal > 9:
        create_locs(regLuncheonSpewart, *loc_Luncheon_Post_Spewart.keys())
    world.regions.append(regLuncheonSpewart)
    regLuncheonCheese = Region("Luncheon Post Cheese Rocks", player, world, "Luncheon Kingdom Story 2")
    if self.options.goal > 9:
        create_locs(regLuncheonCheese, *loc_Luncheon_Post_Cheese_Rocks.keys())
    world.regions.append(regLuncheonCheese)
    regLuncheonPeace = Region("Luncheon World Peace", player, world, "Luncheon Kingdom Peace")
    if self.options.goal > 12:
        create_locs(regLuncheonPeace, *loc_Luncheon_Peace.keys())
    world.regions.append(regLuncheonPeace)

    # Ruined
    regRuined = Region("Ruined", player, world, "Ruined Kingdom")
    if self.options.goal > 12:
        create_locs(regRuined, *loc_Ruined.keys())
    world.regions.append(regRuined)

    # Bowser
    regBowser = Region("Bowser", player, world, "Bowser Kingdom")
    if self.options.goal > 12:
        create_locs(regBowser, *loc_Bowser.keys())
    world.regions.append(regBowser)
    regBowserInfiltrate = Region("Bowser Infiltrate", player, world, "Bowser Kingdom Story 1")
    if self.options.goal > 12:
        create_locs(regBowserInfiltrate, *loc_Bowser_Infiltrate.keys())
    world.regions.append(regBowserInfiltrate)
    regBowserBombing = Region("Bowser Post Bombing", player, world, "Bowser Kingdom Story 2")
    if self.options.goal > 12:
        create_locs(regBowserBombing, *loc_Bowser_Post_Bombing.keys())
    world.regions.append(regBowserBombing)
    regBowserMecha = Region("Bowser Mecha Broodal", player, world, "Bowser Kingdom Story 3")
    if self.options.goal > 12:
        create_locs(regBowserMecha, *loc_Bowser_Mecha_Broodal.keys())
    world.regions.append(regBowserMecha)
    regBowserPeace = Region("Bowser World Peace", player, world, "Bowser Kingdom Peace")
    if self.options.goal > 12:
        create_locs(regBowserPeace, *loc_Bowser_Peace.keys())
    world.regions.append(regBowserPeace)

    # Moon
    regMoon = Region("Moon", player, world, "Moon Kingdom")
    if self.options.goal > 14:
        create_locs(regMoon, *loc_Moon.keys())
    world.regions.append(regMoon)

    # Post Game
    regPostGame = Region("Post Game", player, world, "Post Game Moons")
    if self.options.goal > 14:
        create_locs(regPostGame, *post_game_locations_table.keys(), locs_table= post_game_locations_table)
    world.regions.append(regPostGame)

    # Dark Side
    regDark = Region("Dark", player, world, "Dark Side")
    if self.options.goal > 14:
        create_locs(regDark, *loc_Dark.keys(),  locs_table=special_locations_table)
    world.regions.append(regDark)

    # Darker Side
    regDarker = Region("Darker", player, world, "Darker Side")
    if self.options.goal > 16:
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

    if self.options.goal > 9:
        create_locs(regCascadeMetro, *loc_Cascade_Post_Metro.keys())
        create_locs(regCascadeSnow, *loc_Cascade_Post_Snow.keys())

    if self.options.goal > 5:
        create_locs(regSandRe, *loc_Sand_Revisit.keys())
        create_locs(regMetroSand, *loc_Metro_Post_Sand.keys())


    if self.options.goal > 9:
        create_locs(regLakeSeaside, *loc_Lake_Post_Seaside.keys())
        create_locs(regWoodedMetro, *loc_Wooded_Post_Metro.keys())
        create_locs(regLostRe, *loc_Lost_Revisit.keys())
        create_locs(regPostCloud, *loc_Post_Cloud.keys())
        create_locs(regLuncheonWooded, *loc_Luncheon_Post_Wooded.keys())

    if self.options.goal > 12:
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
    regPostGameShop = Region("Postgame Shop", player, world, "Shop")
    regDarkOutfit = Region("Dark Outfit", player, world, "Shop")
    regDarkerOutfit = Region("Darker Outfit", player, world, "Shop")

    create_locs(regCapShop, *loc_Cap_Shop.keys())
    create_locs(regCascadeShop, *loc_Cascade_Shop.keys())
    create_locs(regSandShop, *loc_Sand_Shop.keys())

    if self.options.goal > 4:
        create_locs(regLakeShop, *loc_Lake_Shop.keys())

    if self.options.goal > 5:
        create_locs(regWoodedShop, *loc_Wooded_Shop.keys())
        create_locs(regLostShop, *loc_Lost_Shop.keys())
        create_locs(regMetroShop, *loc_Metro_Shop.keys())
    if self.options.goal > 9:
        create_locs(regSnowShop, *loc_Snow_Shop.keys())
        create_locs(regSeasideShop, *loc_Seaside_Shop.keys())
        create_locs(regLuncheonShop, *loc_Luncheon_Shop.keys())
    if self.options.goal > 12:
        create_locs(regBowserShop, *loc_Bowser_Shop.keys())

    if self.options.goal > 14:
        create_locs(regMoonShop, *loc_Moon_Shop.keys())
        create_locs(regMushroomShop, *loc_Mushroom_Shop.keys())
        create_locs(regPostGameShop, *loc_Postgame_Shop.keys())

    if self.options.goal > 16:
        create_locs(regDarkOutfit, *loc_Dark_Outfit.keys())
    if self.options.goal > 17:
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
    world.regions.append(regPostGameShop)
    world.regions.append(regDarkOutfit)
    world.regions.append(regDarkerOutfit)

    # Progression Connections
    regCascade.connect(regSand, "Sand Enter", lambda state: count_moons(self, state, "Cascade", player) >= self.moon_counts["cascade"])
    regSand.connect(regSandPyramid, "Sand Pyramid Access", lambda state: state.count("Sand Story Moon", player) >= 1)
    regSandPyramid.connect(regSandUnderground, "Sand Story Subarea", lambda state: state.count("Sand Story Moon", player) >= 1)
    regSandUnderground.connect(regSandPeace, "Sand World Peace", lambda state: state.count("Sand Story Moon", player) >= 1)
    regSand.connect(regCap)
    regSand.connect(regCascadeRe)
    regSandPeace.connect(regMetroSand)
    regSand.connect(regLake, "Lake Enter", lambda state: count_moons(self, state, "Sand", player) >= self.moon_counts["sand"])

    regLake.connect(regWooded, "Wooded Enter")
    regLake.connect(regSandRe)

    regWooded.connect(regWoodedStory1, "Wooded Story 1", lambda state: state.has("Wooded Story Moon", player))
    regWoodedStory1.connect(regWoodedPeace, "Wooded World Peace", lambda state: state.count("Wooded Story Moon", player) >= 2)
    regWoodedPeace.connect(regLuncheonWooded)
    regWooded.connect(regLost, "Lost Enter", lambda state: count_moons(self, state, "Lake", player) >= self.moon_counts["lake"] and count_moons(self, state, "Wooded", player) >= self.moon_counts["wooded"])
    regCloud.connect(regPostCloud)
    regLost.connect(regCloud, "Cloud Available", lambda state: count_moons(self, state, "Lost", player) >= self.moon_counts["lost"])
    regCloud.connect(regMetro, "Metro Enter", lambda state: count_moons(self, state, "Lost", player) >= self.moon_counts["lost"])

    regMetro.connect(regMetroSewer, "Metro Sewer", lambda state: state.count("Metro Story Moon", player) >= 4)
    regMetroSewer.connect(regMetroPeace, "Metro World Peace", lambda state: state.count("Metro Story Moon", player) >= 5)
    regMetro.connect(regSnow, "Snow Enter", lambda state: count_moons(self, state, "Metro", player) >= self.moon_counts["metro"])
    regMetro.connect(regCascadeMetro)
    regMetro.connect(regWoodedMetro)
    regMetro.connect(regLostRe)

    regSnow.connect(regSnowPeace, "Snow World Peace")
    regSnow.connect(regSeaside, "Seaside Enter")
    regSnow.connect(regCascadeSnow)

    regSeaside.connect(regSeasidePeace, "Seaside World Peace")
    regSeaside.connect(regLuncheon, "Enter Luncheon", lambda state: count_moons(self, state, "Snow", player) >= self.moon_counts["snow"] and count_moons(self, state, "Seaside", player) >= self.moon_counts["seaside"])
    regSeasidePeace.connect(regLakeSeaside)

    regLuncheon.connect(regLuncheonSpewart, "Luncheon Town", lambda state: state.has("Luncheon Story Moon", player))
    regLuncheonSpewart.connect(regLuncheonCheese, "Luncheon Meat Plateau", lambda state: state.count("Luncheon Story Moon", player) >= 2)
    regLuncheonCheese.connect(regLuncheonPeace, "Luncheon World Peace", lambda state: state.count("Luncheon Story Moon", player) >= 3)
    regLuncheon.connect(regRuined, "Enter Ruined", lambda state: count_moons(self, state, "Luncheon", player) >= self.moon_counts["luncheon"])
    regLuncheonCheese.connect(regMushroomLuncheon)
    regRuined.connect(regBowser,"Enter Bowser", lambda state: count_moons(self, state, "Ruined", player) >= self.moon_counts["ruined"])

    regBowser.connect(regBowserInfiltrate, "Bowser Infiltrate", lambda state: state.has("Bowser Story Moon", player))
    regBowserInfiltrate.connect(regBowserBombing, "Bowser Bombing", lambda state: state.count("Bowser Story Moon", player) >= 2)

    regBowserBombing.connect(regBowserMecha, "Bowser Mecha Fight", lambda state: state.count("Bowser Story Moon", player) >= 3)
    regBowserMecha.connect(regBowserPeace, "Bowser World Peace")
    regBowserPeace.connect(regMoon, "Enter Moon", lambda state: count_moons(self, state, "Bowser", player) >= self.moon_counts["bowser"])
    regMoon.connect(regPostMoon)
    regMoon.connect(regPostGame)
    regPostGame.connect(regDark, "Dark Access", lambda state: total_moons(self, state, player) >= self.moon_counts["dark"])
    regPostGame.connect(regDarker, "Darker Access", lambda state: total_moons(self, state, player) >= self.moon_counts["darker"])

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
    regPostGame.connect(regMoonShop)
    regPostGame.connect(regMushroomShop)
    regPostGame.connect(regPostGameShop)

    # if self.options.shops == "outfits" or self.options.shops == "all":
    #     regSand.connect(regPostGameShop)

    regDark.connect(regDarkOutfit)
    regDarker.connect(regDarkerOutfit)

def create_locs(reg: Region, *locs: str, locs_table = locations_table):
    reg.locations += ([SMOLocation(reg.player, loc_name, locs_table[loc_name], reg) for loc_name in locs])
