import random
import os
from typing import Mapping, Any

from .Items import item_table, SMOItem, filler_item_table, outfits, shop_items, multi_moons, \
moon_item_table, moon_types, story_moons, world_list
from .Locations import locations_table, SMOLocation, locations_list, post_game_locations_list, \
    special_locations_table
from .Options import SMOOptions
from .Rules import set_rules
from .Regions import create_regions
from BaseClasses import Item, ItemClassification
from worlds.AutoWorld import World

from .Patch import make_output


"""
class MyGameSettings(settings.Group):
    class RomFile(settings.SNESRomPath):
        Insert help text for host.yaml here.

    rom_file: RomFile = RomFile("MyGame.sfc")
"""


class SMOWorld(World):
    """Super Mario Odyssey is a 3-D Platformer where Mario sets off across the world with his companion Cappy to save Princess Peach and Cappy's sister Tiara from Bowser's wedding plans."""
    game = "Super Mario Odyssey"
    # this gives the generator all the definitions for our options
    options_dataclass = SMOOptions
    # this gives us typing hints for all the options we defined
    options: SMOOptions

    topology_present = True  # show path to required location checks in spoiler

    # ID of first item and location, could be hard-coded but code may be easier
    # to read with this as a property.
    # instead of dynamic numbering, IDs could be part of data
    # The following two dicts are required for the generation to know which
    # items exist. They could be generated from json or something else. They can
    # include events, but don't have to since events will be placed manually.
    item_name_to_id = {**item_table, **moon_types}

    location_name_to_id = locations_table
    # Number of Power Moons required to leave each kingdom
    moon_counts = {
        "cascade": 5,
        "sand": 16,
        "lake": 8,
        "wooded": 16,
        "lost": 10,
        "metro": 20,
        "snow": 10,
        "seaside": 10,
        "luncheon": 18,
        "ruined": 3,
        "bowser": 8,
        "dark": 250,
        "darker": 500
    }

    # Number of Power Moons required to unlock post game outfits.
    outfit_moon_counts = {
        "Luigi Cap" : 160,
        "Luigi Clothes" : 180,
        "Doctor Cap" : 220,
        "Doctor Clothes" : 240,
        "Waluigi Cap" : 260,
        "Waluigi Clothes" : 280,
        "Diddy Kong Cap" : 300,
        "Diddy Kong Clothes" : 320,
        "Wario Cap" : 340,
        "Wario Clothes" : 360,
        "Hakama Clothes" : 380,
        "Koopa Cap" : 420,
        "Koopa Clothes" : 440,
        "Peach Cap" : 460,
        "Peach Clothes" : 480,
        "Gold Cap" : 500,
        "Gold Clothes" : 500,
        "64 Metal Cap" : 500,
        "64 Metal Clothes" : 500
    }

    # Maximum number of Power Moons for any given kingdom's progression
    max_counts = {
        "cascade": 19,
        "sand": 65,
        "lake": 28,
        "wooded": 53,
        "lost": 20,
        "metro": 57,
        "snow": 35,
        "seaside": 51,
        "luncheon": 53,
        "ruined": 6,
        "bowser": 40,
        "dark": 375,
        "darker": 750
    }
    # Number of Power Moons in the pool for each kingdom
    pool_counts = {
        "Cap" : 31,
        "Cascade" : 38,
        "Sand" : 85,
        "Lake" : 41,
        "Wooded" : 72,
        "Cloud" : 9,
        "Lost" : 35,
        "Metro" : 74,
        "Snow" : 50,
        "Seaside" : 66,
        "Luncheon" : 63,
        "Ruined" : 9,
        "Bowser" : 58,
        "Moon" : 38,
        "Mushroom" : 37,
        "Dark Side" : 23,
        "Cascade Story" : 1,
        "Sand Story" : 2,
        "Wooded Story" : 2,
        "Metro Story" : 5,
        "Snow Story" : 4,
        "Seaside Story" : 4,
        "Luncheon Story" : 3,
        "Bowser Story" : 3,
        "Cascade Multi" : 1,
        "Sand Multi" : 2,
        "Lake Multi" : 1,
        "Wooded Multi" : 2,
        "Metro Multi" : 2,
        "Snow Multi" : 1,
        "Seaside Multi" : 1,
        "Luncheon Multi" : 2,
        "Ruined Multi" : 1,
        "Bowser Multi" : 1,
        "Mushroom Multi" : 6,
        "Dark Side Multi" : 1,
        "Darker Side Multi" : 1
    }

    placed_counts = {
        "cascade": 0,
        "sand": 0,
        "lake": 0,
        "wooded": 0,
        "lost": 0,
        "metro": 0,
        "snow": 0,
        "seaside": 0,
        "luncheon": 0,
        "ruined": 0,
        "bowser": 0,
        "dark": 0,
        "darker": 0
    }

    # Items can be grouped using their names to allow easy checking if any item
    # from that group has been collected. Group names can also be used for !hint
    item_name_groups = {
        "Cap": ["Cap Power Moon"],
        "Cascade": ["Cascade Power Moon","Cascade Story Moon", "Cascade Multi-Moon"],
        "Sand": ["Sand Power Moon","Sand Story Moon", "Sand Multi-Moon"],
        "Lake": ["Lake Power Moon", "Lake Multi-Moon"],
        "Wooded": ["Wooded Power Moon","Wooded Story Moon", "Wooded Multi-Moon"],
        "Cloud": ["Cloud Power Moon"],
        "Lost": ["Lost Power Moon"],
        "Metro": ["Metro Power Moon","Metro Story Moon", "Metro Multi-Moon"],
        "Snow": ["Snow Power Moon","Snow Story Moon", "Snow Multi-Moon"],
        "Seaside": ["Seaside Power Moon","Seaside Story Moon", "Seaside Multi-Moon"],
        "Luncheon": ["Luncheon Power Moon","Luncheon Story Moon", "Luncheon Multi-Moon"],
        "Ruined": ["Ruined Power Moon", "Ruined Multi-Moon"],
        "Bowser": ["Bowser Power Moon","Bowser Story Moon", "Bowser Multi-Moon"],
        "Moon": ["Moon Power Moon"],
        "Mushroom": ["Power Star", "Mushroom Multi-Moon"],
        "Dark": ["Dark Side Power Moon", "Dark Side Multi-Moon"],
        "Darker": ["Darker Side Multi-Moon"]
    }

    def fill_slot_data(self) -> Mapping[str, Any]:
        return {**(self.options.as_dict("goal")), "clash" : self.moon_counts["lost"], "raid" : self.moon_counts["ruined"]}

    def create_regions(self):
        if self.options.counts > 0:
            self.randomize_moon_amounts()
        create_regions(self, self.multiworld, self.player)

    def generate_early(self):
        self.multiworld.early_items[self.player]["Cascade Multi-Moon"] = 1
        self.multiworld.early_items[self.player]["Cascade Story Moon"] = 1
        self.multiworld.early_items[self.player]["Cascade Power Moon"] = self.moon_counts["cascade"]-4

    def generate_basic(self) -> None:
        pass

    def set_rules(self):
        set_rules(self, self.options)

    def create_item(self, name: str) -> Item:
        item_id = self.item_name_to_id[name]
        classification: ItemClassification = ItemClassification.filler
        if name in filler_item_table.keys():
            classification = ItemClassification.filler
        else:
            if name == "Beat the Game" and self.options.goal == "moon":
                classification = ItemClassification.progression_skip_balancing
            elif name in outfits:
                """self.options.goal > 4 and outfits.index(name) <= 2) or \
                                        (self.options.goal > 5 and outfits.index(name) <= 7) or \
                                        (self.options.goal > 9 and outfits.index(name) < 10) or \
                                        (self.options.goal > 12 and outfits.index(name) <= 17) or \
                                        (self.options.goal > 15 and"""
                if outfits.index(name) <= 33:
                    classification = ItemClassification.progression_skip_balancing
            elif name in shop_items:
                # Until achievements implemented if possible
                classification = ItemClassification.filler

            else:
                # if (self.options.goal == 17 and self.placed_counts["dark"] >= self.moon_counts["dark"]) or (
                #         self.options.goal == 18 and self.placed_counts["darker"] > self.moon_counts["darker"]):
                #     classification = ItemClassification.filler
                # else:
                #     classification = ItemClassification.progression_skip_balancing
                if name in moon_types:
                    if (name in self.item_name_groups["Dark"] and self.options.goal < 18) or name in self.item_name_groups["Darker"]:
                        classification = ItemClassification.filler
                    if "Story" in name or "Multi" in name:
                        # if (self.options.goal >= 4 and "Sand" in name) or \
                        #         (self.options.goal >= 5 and "Lake" in name) or \
                        #         (self.options.goal >= 9 and "Wooded" in name) or \
                        #         (self.options.goal >= 9 and "Metro" in name) or \
                        #         (self.options.goal > 9 and "Snow" in name) or \
                        #         (self.options.goal > 9 and "Seaside" in name) or \
                        #         (self.options.goal >= 12 and "Luncheon" in name) or \
                        #         (self.options.goal >= 15 and "Bowser" in name) or \
                        #         (self.options.goal == 17 and "Dark Side" in name) or \
                        #         (self.options.goal == 18 and "Darker Side" in name):
                        classification = ItemClassification.progression_skip_balancing
                    if self.placed_counts["dark"] < self.moon_counts["dark"] and name not in self.item_name_groups["Dark"]:
                        self.placed_counts["dark"] += 3 if "Multi" in name else 1
                        classification = ItemClassification.progression_skip_balancing
                    if self.placed_counts["darker"] < self.moon_counts["darker"] and name not in self.item_name_groups["Darker"]:
                        self.placed_counts["darker"] += 3 if "Multi" in name else 1
                        classification = ItemClassification.progression_skip_balancing
                    for group in self.item_name_groups.keys():
                        if (group.lower() in self.placed_counts
                                and group != "Dark" and group != "Darker" and name in self.item_name_groups[group]
                                and self.placed_counts[group.lower()] < self.moon_counts[group.lower()]):
                            self.placed_counts[group.lower()] += 3 if "Multi" in name else 1
                            #print(self.placed_counts[group.lower()], " ", group.lower())
                            classification = ItemClassification.progression_skip_balancing

                            break



        item: SMOItem

        # if classification == ItemClassification.progression_skip_balancing and name in self.item_name_groups["Cascade"]:
        #     print(name)
        item = SMOItem(name, classification, self.player, item_id)
        return item

    def create_items(self):
        pool = list(item_table.keys() - filler_item_table.keys())
        pool.remove("Beat the Game")

        if self.options.goal > 14:
            pool.append("1000 Coins")

        pool.remove("Beat Bowser in Cloud")
        if self.options.goal >= 9:
            pool.append("1000 Coins")

        # Remove outfits and items from pool whose locations aren't reached with the current settings
        for key in outfits:
            if not key in self.multiworld.regions.location_cache[self.player]:
                pool.remove(key)

        for key in shop_items:
            if not key in self.multiworld.regions.location_cache[self.player]:
                pool.remove(key)

        if self.options.shop_sanity == "off" or self.options.shop_sanity == "non_outfits":
            for key in outfits:
                if key in pool:
                    pool.remove(key)
                    if key in self.multiworld.regions.location_cache[self.player]:
                        self.multiworld.get_location(key, self.player).place_locked_item(self.create_item(key))

        # Shuffle outfits amongst themselves
        elif self.options.shop_sanity == "shuffle":
            loc_names = outfits
            item_names = []
            for i in range(len(loc_names)):
                if loc_names[i] in pool:
                    pool.remove(loc_names[i])
                    item_names.append(loc_names[i])
            while len(item_names) > 0:
                item = item_names.pop(random.randint(0, len(item_names) - 1))
                self.multiworld.get_location(item , self.player).place_locked_item(
                    self.create_item(item))
        # Non outfits
        if self.options.shop_sanity < 3:
            for key in shop_items:
                if key in pool:
                    pool.remove(key)
                    self.multiworld.get_location(key, self.player).place_locked_item(self.create_item(key))

        for item in self.pool_counts.keys():
            for i in range(self.pool_counts[item]):
                if "Story" in item:
                    pool.append(item + " Moon")
                elif "Multi" in item:
                    pool.append(item + "-Moon")
                else:
                    if item == "Mushroom":
                        pool.append("Power Star")
                    else:
                        pool.append(item + " Power Moon")

        filler = 0

        # Place moons into their own kingdom if not part of goal.
        for index in range(len(locations_list)):
            for location in locations_list[index].keys():
                if (self.options.story == 2 or self.options.story == 0) and world_list[
                    index] in story_moons and location in story_moons[world_list[index]]:
                    suffix = " Story Moon"
                elif (self.options.story == 1 or self.options.story == 0) and world_list[
                    index] in multi_moons and location in multi_moons[world_list[index]]:
                    suffix = " Multi-Moon"
                else:
                    if world_list[index] == "Mushroom":
                        suffix = "Power Star"
                    else:
                        suffix = " Power Moon"
                if "Star" in suffix:
                    item_name = suffix
                else:
                    if "Dark" in world_list[index]:
                        item_name = world_list[index] + " Side" + suffix
                    else:
                        item_name = world_list[index] + suffix

                if not location in self.multiworld.regions.location_cache[self.player] and item_name in pool:
                    pool.remove(item_name)


        # Place moons into their own kingdom if not part of goal.
        for index in range(len(post_game_locations_list)):
            for location in post_game_locations_list[index].keys():
                suffix = " Power Moon"

                if "Dark" in world_list[index]:
                    item_name = world_list[index] + " Side" + suffix
                else:
                    item_name = world_list[index] + suffix

                if not location in self.multiworld.regions.location_cache[self.player] and item_name in pool:
                    pool.remove(item_name)

        # Remove Story and Multi Moons if the respective options aren't enabled
        if self.options.story < 3:
            for item in moon_types.keys():
                if self.options.story == 1 and "Story" in item and item in pool:
                    for moon in story_moons[item.split(" ")[0]]:
                        if moon in self.multiworld.regions.location_cache:
                            self.multiworld.get_location(moon,
                                                         self.player).place_locked_item(self.create_item(item))
                            pool.remove(item)
                if self.options.story == 2 and "Multi" in item and item in pool:
                    for moon in multi_moons[item.split(" ")[0]]:
                        if moon in self.multiworld.regions.location_cache:
                            self.multiworld.get_location(moon,
                                                         self.player).place_locked_item(self.create_item(item))
                            pool.remove(item)

        # Remove possible duplicate goal completion Multi Moons
        if self.options.story > 1:
            if self.options.goal == "sand":
                pool.remove("Sand Multi-Moon")
            if self.options.goal == "lake":
                pool.remove("Lake Multi-Moon")
            if self.options.goal == "metro":
                pool.remove("Metro Multi-Moon")
            if self.options.goal == "luncheon":
                pool.remove("Luncheon Multi-Moon")
            if self.options.goal == "dark":
                pool.remove("Dark Side Multi-Moon")
            if self.options.goal == "darker":
                pool.remove("Darker Side Multi-Moon")

        for i in pool:
            self.multiworld.itempool += [self.create_item(i)]


        # Reset placed counts so multi worlds support more than one SMO instance
        for key in self.placed_counts.keys():
            self.placed_counts[key] = 0

        if filler > 0:
            for i in range(filler):
                if i < filler * 0.45:
                    self.multiworld.itempool += [self.create_item("50 Coins")]
                elif i < filler * 0.70:
                    self.multiworld.itempool += [self.create_item("100 Coins")]
                elif i < filler * 0.85:
                    self.multiworld.itempool += [self.create_item("250 Coins")]
                elif i < filler * 0.95:
                    self.multiworld.itempool += [self.create_item("500 Coins")]
                elif i < filler:
                    self.multiworld.itempool += [self.create_item("1000 Coins")]


    def randomize_moon_amounts(self):
        """ Randomizes the moon requirements for progressing to each kingdom."""
        if self.options.counts == 1:
            for key in self.moon_counts.keys():
                if key != "dark" and key != "darker":
                    self.moon_counts[key] = 1
            kingdoms = list(self.moon_counts.keys())
            kingdoms.remove("dark")
            kingdoms.remove("darker")
            count = 0
            for kingdom in kingdoms:
                count += self.moon_counts[kingdom]
            while count != 124 and len(kingdoms) > 0:
                selected = kingdoms[random.randint(0, len(kingdoms)-1)]
                self.moon_counts[selected] += 1
                count += 1
                if self.moon_counts[selected] == self.max_counts[selected]:
                    kingdoms.remove(selected)
        elif self.options.counts == 2:
            for key in self.moon_counts.keys():
                if key != "dark" and key != "darker":
                    self.moon_counts[key] = 1
            self.moon_counts["ruined"] = 3
            kingdoms = list(self.moon_counts.keys())
            kingdoms.remove("dark")
            kingdoms.remove("darker")
            kingdoms.remove("ruined")
            count = 3
            for kingdom in kingdoms:
                count += self.moon_counts[kingdom]
            while count != 124:
                selected = kingdoms[random.randint(0, len(kingdoms)-1)]
                self.moon_counts[selected] += 1
                count += 1
                if self.moon_counts[selected] == self.max_counts[selected]:
                    kingdoms.remove(selected)
        elif self.options.counts == 3:
            for key in self.moon_counts.keys():
                self.moon_counts[key] = random.randint(int(self.moon_counts[key] * 0.8), int(self.moon_counts[key] * 1.25))

        elif self.options.counts == 4:
            for key in self.moon_counts.keys():
                self.moon_counts[key] = random.randint(int(self.moon_counts[key] * 1.0), int(self.moon_counts[key] * 2.0))
        if self.moon_counts["dark"] > self.moon_counts["darker"]:
            temp = self.moon_counts["darker"]
            self.moon_counts["darker"] = self.moon_counts["dark"]
            self.moon_counts["dark"] = temp
        for key in self.moon_counts.keys():
            if self.moon_counts[key] > self.max_counts[key]:
                self.moon_counts[key] = self.max_counts[key]
        if self.options.counts == 1 or self.options.counts == 2:
            kingdoms = list(self.moon_counts.keys())
            kingdoms.remove("dark")
            kingdoms.remove("darker")
            count = 0
            for kingdom in kingdoms:
                count += self.moon_counts[kingdom]
            if count != 124:
                raise Exception("Moon count exception! Moons required to beat the game is not 124, was " + str(count))
        # Change all outfit moon requirements to a proportion based on random Dark Side count
        for key in self.outfit_moon_counts.keys():
            self.outfit_moon_counts[key] = 124 #int(self.outfit_moon_counts[key] * (self.moon_counts["dark"]/250))
            # if self.outfit_moon_counts[key] > self.moon_counts["dark"]:
            #     self.outfit_moon_counts[key] = self.moon_counts["dark"] - 1



    def generate_output(self, output_directory: str):
        if self.options.romFS.value != "" and os.path.exists(os.path.join(self.options.romFS.value)):
            self.options.romFS.value = os.path.join(self.options.romFS.value)
            make_output(self, output_directory)


