import random
from typing import Mapping, Any

from .Items import item_table, SMOItem, filler_item_table, outfits, shop_items, multi_moons, \
    Cap, Cascade, Sand, Lake, Wooded, Cloud, Lost, Metro, Snow, Seaside, Luncheon, Ruined, \
    Bowser, Moon, Mushroom, Dark, Darker, moon_item_list
from .Locations import locations_table, SMOLocation, loc_Cascade, loc_Cascade_Revisit, \
    loc_Cap, loc_Sand, loc_Lake, loc_Wooded, loc_Cloud, loc_Lost, loc_Metro, loc_Snow, \
    loc_Seaside, loc_Luncheon, loc_Ruined, loc_Bowser, post_game_locations_table, \
    loc_Moon, loc_Dark, loc_Darker, loc_Mushroom, locations_list, post_game_locations_list, \
    loc_Lake_Post_Seaside, loc_Cascade_Post_Snow, loc_Mushroom_Post_Luncheon
from .Options import SMOOptions
from .Rules import set_rules
from .Regions import create_regions
from BaseClasses import Item, Region, ItemClassification
from worlds.AutoWorld import World

"""
class MyGameSettings(settings.Group):
    class RomFile(settings.SNESRomPath):
        Insert help text for host.yaml here.

    rom_file: RomFile = RomFile("MyGame.sfc")
"""

class SMOWorld(World):
    """Super Mario Odyssey is a 3-D Plaformer where Mario sets off across the world with his companion Cappy to save Princess Peach and Cappy's sister Tiara from Bowser's wedding plans."""
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
    item_name_to_id = item_table

    location_name_to_id = locations_table

    unrequired_kingdoms = []
    # Items can be grouped using their names to allow easy checking if any item
    # from that group has been collected. Group names can also be used for !hint

    item_name_groups = {
        "Cap": Cap.keys(),
        "Cascade": Cascade.keys(),
        "Sand": Sand.keys(),
        "Lake": Lake.keys(),
        "Wooded": Wooded.keys(),
        "Cloud": Cloud.keys(),
        "Lost": Lost.keys(),
        "Metro": Metro.keys(),
        "Snow": Snow.keys(),
        "Seaside": Seaside.keys(),
        "Luncheon": Luncheon.keys(),
        "Ruined": Ruined.keys(),
        "Bowser": Bowser.keys(),
        "Moon": Moon.keys(),
        "Mushroom":Mushroom.keys(),
        "Dark": Dark.keys(),
        "Darker": Darker.keys()
    }

    def fill_slot_data(self) -> Mapping[str, Any]:
        return self.options.as_dict("goal")

    def create_regions(self):
        create_regions(self, self.multiworld, self.player)

    def generate_early(self):
        self.multiworld.early_items[self.player]["Cascade Multi-Moon"] = 1
        self.multiworld.early_items[self.player]["Cascade Story Moon"] = 1
        self.multiworld.early_items[self.player][list(Cascade.keys())[random.randint(2,12)]] = 1

    def generate_basic(self) -> None:
        pass
    
    def set_rules(self):
        set_rules(self, self.options)

    def create_item(self, name: str) -> Item:
        item_id = item_table[name]
        if name in filler_item_table.keys():
            classification = ItemClassification.filler
        else:
            if name == "Beat the Game" and self.options.goal == 15:
                classification = ItemClassification.progression_skip_balancing
            elif self.options.goal <= 15 and item_id in post_game_locations_table.values():
                classification = ItemClassification.filler
            elif self.options.goal <= 9 and item_id in loc_Cascade_Post_Snow.values():
                classification = ItemClassification.filler
            elif self.options.goal < 17 and name in self.item_name_groups["Darker"]:
                classification = ItemClassification.filler
            elif self.options.goal <= 16 and (name in self.item_name_groups["Dark"] or (name == "Bone Clothes" and (self.options.shops == "off" or self.options.shops == "nonoutfits"))):
                classification = ItemClassification.filler
            elif self.options.goal <= 15 and (name in self.item_name_groups["Moon"] or name in self.item_name_groups["Mushroom"] or name in self.item_name_groups["Cloud"] or name in self.item_name_groups["Cap"]):
                classification = ItemClassification.filler
            elif self.options.goal < 12 and (name in self.item_name_groups["Bowser"] or name in self.item_name_groups["Ruined"] or
              name in self.item_name_groups["Luncheon"]):
                classification = ItemClassification.filler
            elif self.options.goal < 9 and (name in self.item_name_groups["Metro"] or name in self.item_name_groups["Seaside"] or
              name in self.item_name_groups["Snow"] or name == "Sand Power Moon (1096)" or name == "Lake Power Moon (417)"):
                classification = ItemClassification.filler
            elif self.options.goal < 5 and (name in self.item_name_groups["Lake"] or name in self.item_name_groups["Wooded"] or name in self.item_name_groups["Lost"] or name in self.item_name_groups["Cloud"]):
                classification = ItemClassification.filler
            elif self.options.goal < 4 and name in self.item_name_groups["Sand"]:
                classification = ItemClassification.filler
            elif name in outfits:
                if (self.options.goal > 4 and outfits.index(name) <= 2) or \
                (self.options.goal > 5 >= outfits.index(name)) or \
                (self.options.goal > 9 >= outfits.index(name)) or \
                (self.options.goal > 12 and outfits.index(name) <= 15) or \
                (self.options.goal >= 15 and outfits.index(name) <= 17) or \
                (self.options.goal > 15 and outfits.index(name) <= 35):
                    classification = ItemClassification.progression_skip_balancing
                else:
                    classification = ItemClassification.filler
            elif name in shop_items:
                classification = ItemClassification.filler
            else:
                classification = ItemClassification.progression_skip_balancing

        item : SMOItem

        item = SMOItem(name, classification, self.player, item_id)
        return item

    def create_items(self):
        pool = item_table.keys() - filler_item_table.keys()
        if not self.options.goal == 15:
            pool.remove("Beat the Game")
            pool.add("1000 Coins")

        pool.remove("Beat Bowser in Cloud")
        if self.options.goal < 9:
            self.multiworld.get_location("Beat Bowser in Cloud", self.player).place_locked_item(
                self.create_item("1000 Coins"))
        else:
            pool.add("1000 Coins")

        if self.options.shops == "off" or self.options.shops == "nonoutfits":
            for key in outfits:
                pool.remove(key)
                self.multiworld.get_location(key, self.player).place_locked_item(self.create_item(key))
        # Shuffle outfits amongst themselves
        elif self.options.shops == "shuffle":
            loc_names = outfits
            item_names = outfits.copy()
            for i in range(len(loc_names)):
                pool.remove(loc_names[i])
                self.multiworld.get_location(loc_names[i], self.player).place_locked_item(self.create_item(item_names.pop(random.randint(0, len(item_names) - 1))))
        # Non outfits
        if self.options.shops < 3:
            for key in shop_items:
                pool.remove(key)
                self.multiworld.get_location(key, self.player).place_locked_item(self.create_item(key))

        filler = 0
        if self.options.goal < 16:
            self.unrequired_kingdoms.append("Cap")
            self.unrequired_kingdoms.append("Cloud")

        if self.options.goal == 15:
            self.unrequired_kingdoms.append("Moon")

        # Remove all post game checks from the pool
        if self.options.goal <= 15:
            for i in range(len(post_game_locations_list)):
                moon_item_list[i].reverse()
                for location_index in range(len(post_game_locations_list[i].keys())):
                    self.multiworld.get_location(list(post_game_locations_list[i].keys())[location_index],
                                                 self.player).place_locked_item(
                        self.create_item(moon_item_list[i][location_index]))
                    pool.remove(moon_item_list[i][location_index])

        locations_list.reverse()
        moon_item_list.reverse()
        for i in range(18 - self.options.goal):
            if i == 6:
                self.multiworld.get_location("Secret Path to Mount Volbono!", self.player).place_locked_item(self.create_item("Luncheon Power Moon (260)"))
                #pool.remove("Secret Path to Mount Volbono!")
            if i == 8:
                self.multiworld.get_location("Secret Path to Fossil Falls!", self.player).place_locked_item(self.create_item("Cascade Power Moon (207)"))
                pool.remove("Cascade Power Moon (207)")
            if i == 7:
                self.multiworld.get_location("Secret Path to Lake Lamode!", self.player).place_locked_item(
                    self.create_item("Lake Power Moon (417)"))
                pool.remove("Lake Power Moon (417)")
            moon_item_list[i].reverse()
            for location_index in range(len(locations_list[i].keys())):
                self.multiworld.get_location(list(locations_list[i].keys())[location_index], self.player).place_locked_item(self.create_item(moon_item_list[i][location_index]))
                if moon_item_list[i][location_index] in pool:
                    pool.remove(moon_item_list[i][location_index])
        for kingdom in self.unrequired_kingdoms:
            # Removes kingdom's power moons from the pool
            if self.options.replace == 1:
                for item in self.item_name_groups[kingdom]:
                    if item in pool:
                        pool.remove(item)
                        filler += 1

        for i in pool:
            self.multiworld.itempool += [self.create_item(i)]

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

    """
    def generate_output(self, output_directory: str):
        if self.multiworld.players != 1:
            return
        data = {
            "slot_data": self.fill_slot_data(),
            "location_to_item": {self.location_name_to_id[i.name] : item_table[i.item.name] for i in self.multiworld.get_locations()},
            "data_package": {
                "data": {
                    "games": {
                        self.game: {
                            "item_name_to_id": self.item_name_to_id,
                            "location_name_to_id": self.location_name_to_id
                        }
                    }
                }
            }
        }
        filename = f"{self.multiworld.get_out_file_name_base(self.player)}.apsmo"
        with open(os.path.join(output_directory, filename), 'w') as f:
            json.dump(data, f)
    """