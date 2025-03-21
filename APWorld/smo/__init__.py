import random
import typing
import os
import json
from typing import List
from .Items import item_table, SMOItem, filler_item_table, outfits, shop_items, multi_moons, \
    Cap, Cascade, Sand, Lake, Wooded, Cloud, Lost, Metro, Snow, Seaside, Luncheon, Ruined, \
    Bowser, Moon, Mushroom, Dark, Darker
from .Locations import locations_table, SMOLocation, loc_Cascade, loc_Cascade_Revisit, \
    loc_Cap, loc_Sand, loc_Lake, loc_Wooded, loc_Cloud, loc_Lost, loc_Metro, loc_Snow, \
    loc_Seaside, loc_Luncheon, loc_Ruined, loc_Bowser, post_game_locations_table, \
    loc_Moon, loc_Dark, loc_Darker, loc_Mushroom
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
    """Insert description of the world/game here."""
    game = "Super Mario Odyssey"
    # this gives the generator all the definitions for our options
    options_dataclass = SMOOptions
    # this gives us typing hints for all the options we defined
    options: SMOOptions
    #options_dataclass = MyGameOptions  # options the player can set
    #options: MyGameOptions  # typing hints for option results
    #settings: typing.ClassVar[MyGameSettings]  # will be automatically assigned from type hint
    topology_present = True  # show path to required location checks in spoiler

    # ID of first item and location, could be hard-coded but code may be easier
    # to read with this as a property.
    # instead of dynamic numbering, IDs could be part of data
    # The following two dicts are required for the generation to know which
    # items exist. They could be generated from json or something else. They can
    # include events, but don't have to since events will be placed manually.
    item_name_to_id = item_table

    location_name_to_id = locations_table

    # Items can be grouped using their names to allow easy checking if any item
    # from that group has been collected. Group names can also be used for !hint

    item_name_groups = {
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


    def create_regions(self):
        create_regions(self, self.multiworld, self.player)

    def generate_early(self):
        self.multiworld.early_items[self.player]["Cascade Multi-Moon"] = 1
        self.multiworld.early_items[self.player]["Cascade Story Moon"] = 1
        self.multiworld.early_items[self.player][list(Cascade.keys())[random.randint(2,12)]] = 1


    def set_rules(self):
        set_rules(self, self.options)

    def create_item(self, name: str) -> Item:
        item_id = item_table[name]
        if name in filler_item_table.keys():
            classification = ItemClassification.filler
        else:
            if name == "Beat the Game":
                classification = ItemClassification.progression_skip_balancing
            elif self.options.goal < 17 and name in self.item_name_groups["Darker"]:
                classification = ItemClassification.filler
            elif self.options.goal <= 16 and (name in self.item_name_groups["Dark"] or name in self.item_name_groups["Cloud"]):
                classification = ItemClassification.filler
            elif self.options.goal <= 15 and (name in self.item_name_groups["Moon"] or name in self.item_name_groups["Mushroom"]):
                classification = ItemClassification.filler
            elif self.options.goal <= 12 and (name in self.item_name_groups["Bowser"] or name in self.item_name_groups["Ruined"] or name in self.item_name_groups["Luncheon"]):
                classification = ItemClassification.filler
            elif self.options.goal <= 9 and name in self.item_name_groups["Metro"]:
                classification = ItemClassification.filler
            elif self.options.goal <= 5 and name in self.item_name_groups["Lake"]:
                classification = ItemClassification.filler
            elif self.options.goal <= 4 and name in self.item_name_groups["Sand"]:
                classification = ItemClassification.filler
            else:
                classification = ItemClassification.progression_skip_balancing
        item : SMOItem
        kingdom = ""
        for i in self.item_name_groups.keys():
            if name in self.item_name_groups[i]:
                kingdom = i
                break
        if kingdom == "":
            kingdom = "Post Game"

        item = SMOItem(name, classification, self.player, kingdom, item_id)
        return item

    def create_items(self):
        # Coins
        # Add Life Up Hearts
        pool = item_table.keys() - filler_item_table.keys()
        pool.remove("Beat the Game")
        pool.remove("Bowser's Power Moon (314)")
        pool.remove("Beat Bowser in Cloud")
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
        unrequired_kingdoms = []
        if self.options.replace == 1:
            if self.options.goal < 17:
                unrequired_kingdoms.append("Darker")
            if self.options.goal <= 16:
                unrequired_kingdoms.append("Dark")
                unrequired_kingdoms.append("Cloud")
            if self.options.goal <= 15:
                unrequired_kingdoms.append("Moon")
                unrequired_kingdoms.append("Mushroom")
                unrequired_kingdoms.append("Cap")
            if self.options.goal <= 12:
                unrequired_kingdoms.append("Bowser")
                unrequired_kingdoms.append("Ruined")
                unrequired_kingdoms.append("Luncheon")
            if self.options.goal <= 9:
                unrequired_kingdoms.append("Snow")
                unrequired_kingdoms.append("Seaside")
            if self.options.goal <= 5:
                unrequired_kingdoms.append("Metro")
                unrequired_kingdoms.append("Lake")
                unrequired_kingdoms.append("Wooded")
            if self.options.goal <= 4:
                unrequired_kingdoms.append("Sand")
            for kingdom in unrequired_kingdoms:
                # Removes kingdom's power moons from the pool
                for item in self.item_name_groups[kingdom]:
                    if item == "Bowser's Power Moon (314)":
                        continue
                    pool.remove(item)
                    filler += 1

                if kingdom == "Cap":
                    self.options.exclude_locations += [{loc_Cap[i] : i} for i in loc_Cap.keys()]
                if kingdom == "Lake":
                    self.options.exclude_locations += [{loc_Lake[i]: i} for i in loc_Lake.keys()]
                if kingdom == "Wooded":
                    self.options.exclude_locations += [{loc_Wooded[i]: i} for i in loc_Wooded.keys()]
                if kingdom == "Cloud":
                    self.options.exclude_locations += [{loc_Cloud[i]: i} for i in loc_Cloud.keys()]
                if kingdom == "Lost":
                    self.options.exclude_locations += [{loc_Lost[i]: i} for i in loc_Lost.keys()]
                if kingdom == "Metro":
                    self.options.exclude_locations += [{loc_Metro[i]: i} for i in loc_Metro.keys()]
                if kingdom == "Snow":
                    self.options.exclude_locations += [{loc_Snow[i]: i} for i in loc_Snow.keys()]
                if kingdom == "Seaside":
                    self.options.exclude_locations += [{loc_Seaside[i]: i} for i in loc_Seaside.keys()]
                if kingdom == "Luncheon":
                    self.options.exclude_locations += [{loc_Luncheon[i]: i} for i in loc_Luncheon.keys()]
                if kingdom == "Ruined":
                    self.options.exclude_locations += [{loc_Ruined[i]: i} for i in loc_Ruined.keys()]
                if kingdom == "Bowser":
                    self.options.exclude_locations += [{loc_Bowser[i]: i} for i in loc_Bowser.keys()]
                if kingdom == "Moon":
                    self.options.exclude_locations += [{loc_Moon[i]: i} for i in loc_Moon.keys()]
                if kingdom == "Mushroom":
                    self.options.exclude_locations += [{post_game_locations_table[i]: i} for i in post_game_locations_table.keys()]
                if kingdom == "Dark":
                    self.options.exclude_locations += [{loc_Cap[i]: i} for i in loc_Cap.keys()]
                if kingdom == "Darker":
                    self.options.exclude_locations += [{loc_Cap[i]: i} for i in loc_Cap.keys()]

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

        # filler = 776 - len(pool) -25
        # for i in range(filler):
        #     self.multiworld.itempool += [self.create_item("Coins")]
        # for check in self.unused_locations:
        #     self.multiworld.get_location(check, self.player).place_locked_item(self.create_item(check))


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