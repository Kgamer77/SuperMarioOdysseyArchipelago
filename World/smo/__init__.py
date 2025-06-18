import random
import os
from typing import Mapping, Any
from .Items import item_table, SMOItem, filler_item_table, outfits, shop_items, multi_moons, \
    moon_item_table, moon_types, story_moons, world_list
from .Locations import locations_table, SMOLocation, locations_list, post_game_locations_list, \
    special_locations_table, full_moon_locations_list, goals_table
from .Options import SMOOptions
from .Rules import set_rules
from .Regions import create_regions
from BaseClasses import Item, ItemClassification
from worlds.AutoWorld import World
from worlds.LauncherComponents import (Component, components, Type as component_type, SuffixIdentifier, launch as launch_component)
from .Patch import SMOProcedurePatch, make_output, write_patch
from settings import Group, UserFolderPath
from Utils import output_path


def launch_client(*args: str):
    from .Connector.Client import launch
    print(len(args))
    if len(args) > 0:
        make_output(args[0])
        launch_component(launch, name="SMOClient", args=args[1:])
    else:
        launch_component(launch, name="SMOClient", args=args)

component = Component("Super Mario Odyssey Client", component_type=component_type.CLIENT,
                      game_name="Super Mario Odyssey", file_identifier=SuffixIdentifier(".apsmo"), func=launch_client)
components.append(component)

class SMOSettings(Group):
    class SMORomFS(UserFolderPath):
        """Folder location of your dumped Super Mario Odyssey RomFS."""
        description = "Super Mario Odyssey RomFS"
        copy_to = "SMO_RomFs"

    romFS_folder: SMORomFS = SMORomFS(SMORomFS.copy_to)


class SMOWorld(World):
    """Super Mario Odyssey is a 3-D Platformer where Mario sets off across the world with his companion Cappy to save Princess Peach and Cappy's sister Tiara from Bowser's wedding plans."""
    game = "Super Mario Odyssey"

    settings_key = "smo_settings"
    settings : SMOSettings

    options_dataclass = SMOOptions
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
        "Luigi Suit" : 180,
        "Doctor Headwear" : 220,
        "Doctor Outfit" : 240,
        "Waluigi Cap" : 260,
        "Waluigi Suit" : 280,
        "Diddy Kong Hat" : 300,
        "Diddy Kong Suit" : 320,
        "Wario Cap" : 340,
        "Wario Suit" : 360,
        "Hakama" : 380,
        "Bowser's Top Hat" : 420,
        "Bowser's Tuxedo" : 440,
        "Bridal Veil" : 460,
        "Bridal Gown" : 480,
        "Gold Mario Cap" : 500,
        "Gold Mario Suit" : 500,
        "Metal Mario Cap" : 500,
        "Metal Mario Suit" : 500
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
    # pool_counts = {
    #     "Cap" : 31,
    #     "Cascade" : 38,
    #     "Sand" : 85,
    #     "Lake" : 41,
    #     "Wooded" : 72,
    #     "Cloud" : 9,
    #     "Lost" : 35,
    #     "Metro" : 74,
    #     "Snow" : 50,
    #     "Seaside" : 66,
    #     "Luncheon" : 63,
    #     "Ruined" : 9,
    #     "Bowser" : 58,
    #     "Moon" : 38,
    #     "Mushroom" : 37,
    #     "Dark Side" : 23,
    #     "Cascade Story" : 1,
    #     "Sand Story" : 2,
    #     "Wooded Story" : 2,
    #     "Metro Story" : 5,
    #     "Snow Story" : 4,
    #     "Seaside Story" : 4,
    #     "Luncheon Story" : 3,
    #     "Bowser Story" : 3,
    #     "Cascade Multi" : 1,
    #     "Sand Multi" : 2,
    #     "Lake Multi" : 1,
    #     "Wooded Multi" : 2,
    #     "Metro Multi" : 2,
    #     "Snow Multi" : 1,
    #     "Seaside Multi" : 1,
    #     "Luncheon Multi" : 2,
    #     "Ruined Multi" : 1,
    #     "Bowser Multi" : 1,
    #     "Mushroom Multi" : 6,
    #     "Dark Side Multi" : 1,
    #     "Darker Side Multi" : 1
    # }

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

    # Change regionals to be dependent on the option
    def fill_slot_data(self) -> Mapping[str, Any]:
        return {**(self.options.as_dict("goal")), "clash" : self.moon_counts["lost"], "raid" : self.moon_counts["ruined"], "regionals" : False}

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
                if outfits.index(name) <= 33:
                    classification = ItemClassification.progression_skip_balancing
            elif name in shop_items:
                # Until achievements implemented if possible
                classification = ItemClassification.filler
            else:
                if name in moon_types:
                    if (name in self.item_name_groups["Dark"] and self.options.goal < 18) or name in self.item_name_groups["Darker"]:
                        classification = ItemClassification.filler
                    if "Story" in name or "Multi" in name:
                        classification = ItemClassification.progression_skip_balancing
                    if self.placed_counts["dark"] < self.moon_counts["dark"] and name not in self.item_name_groups["Dark"] and name not in self.item_name_groups["Darker"]:
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
        pool : list = []

        # Beat the Game
        if self.options.goal > 14:
            pool.append("1000 Coins")

        # Beat Bowser in Cloud
        if self.options.goal >= 9:
            pool.append("1000 Coins")

        # Additively build pool
        # moons


        locations: list = []
        for location in self.get_locations():
            if location.name in outfits or location.name in shop_items:
                continue
            else:
                locations += [location.name]
        # print(locations)

        for location in locations:
            # found : bool = False
            for index in range(len(world_list)):
                if location in full_moon_locations_list[index]:
                    # found = True
                    item: str = world_list[index]
                    place : bool = False
                    if "Dark" in item:
                        item += " Side"
                    # Multi
                    if world_list[index] in multi_moons and location in multi_moons[world_list[index]]:
                        item += " Multi-Moon"
                        # Prevent placement of duplicate goal Multi-Moon
                        if location == goals_table[self.options.goal.value]:
                            break
                        place = not self.options.story >= 2
                    elif world_list[index] in story_moons and location in story_moons[world_list[index]]:
                        item += " Story Moon"
                        place = not (self.options.story == 1 or self.options.story == 3)
                    else:
                        if world_list[index] == "Mushroom":
                            item = "Power Star"
                        else:
                            item += " Power Moon"

                    if place:
                        self.get_location(location).place_locked_item(self.create_item(item))
                        break

                    pool.append(item)
                    break
            # if not found:
            #     print(location)

        locations : list = []

        for location in self.get_locations():
            if location.name in outfits or location.name in shop_items:
                locations += [location.name]

        # shops
        item_names : list = []
        # Outfits
        for location in outfits:
            if location in locations:
                if self.options.shop_sanity == "outfits" or self.options.shop_sanity == "all":
                    pool.append(location)
                elif self.options.shop_sanity == "shuffle":
                    item_names.append(location)
                else:
                    self.get_location(location).place_locked_item(self.create_item(location))

        # Souvenirs and stickers
        for location in shop_items:
            if location in locations:
                if self.options.shop_sanity == "non_outfits" or self.options.shop_sanity == "all":
                    pool.append(location)
                else:
                    self.get_location(location).place_locked_item(self.create_item(location))

        # Shop sanity shuffle
        if self.options.shop_sanity == "shuffle":
            while len(item_names) > 0:
                item = item_names.pop(random.randint(0, len(item_names) - 1))
                self.get_location(item).place_locked_item(self.create_item(item))

        for i in pool:
            self.multiworld.itempool += [self.create_item(i)]

        # Reset placed counts so multi worlds support more than one SMO instance
        for key in self.placed_counts.keys():
            self.placed_counts[key] = 0


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
        # for key in self.outfit_moon_counts.keys():
        #     self.outfit_moon_counts[key] = int(self.outfit_moon_counts[key] * (self.moon_counts["dark"]/250))
            # if self.outfit_moon_counts[key] > self.moon_counts["dark"]:
            #     self.outfit_moon_counts[key] = self.moon_counts["dark"] - 1



    def generate_output(self, output_directory: str):
        if self.options.colors.value == "true" or self.options.counts != "off" or self.options.shop_sanity != "off":
            out_base = output_path(output_directory, self.multiworld.get_out_file_name_base(self.player))
            patch = SMOProcedurePatch(player=self.player, player_name=self.multiworld.get_player_name(self.player))
            write_patch(self, patch)
            patch.write(os.path.join(output_directory, f"{out_base}{patch.patch_file_ending}"))


