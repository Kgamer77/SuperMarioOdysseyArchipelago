"""
Classes and functions related to creating a romfs patch
"""
import os
import random
import re

from .byml import byml
from .sarc import sarc


from .yaz0 import yaz0
from .MsbtEditor import Msbt

import zipfile

from BaseClasses import ItemClassification
from worlds.Files import APContainer


class SMOPatch(APContainer):
    game: str = "Super Mario Odyssey"

    def __init__(self, patch_data : dict, base_path: str, output_directory: str, player=None, player_name: str = "", server: str = ""):
        self.patch_data = patch_data
        self.file_path = base_path
        container_path = os.path.join(output_directory, base_path + ".zip")
        super().__init__(container_path, player, player_name, server)

    def write_contents(self, opened_zipfile: zipfile.ZipFile) -> None:
        for filename, bin_io in self.patch_data.items():
            file = opened_zipfile.open(filename, "w")
            file.write(bin_io)
            file.close()

        super().write_contents(opened_zipfile)

regular_kingdoms = [
"cascade",
"sand",
"lake",
"wooded",
"lost",
"metro",
"snow",
"seaside",
"luncheon",
"ruined",
"bowser"
]

caps = {
    "Poncho Cap": "Sombrero",
    "Gunman Cap": "Cowboy Hat",
    "Explorer Cap": "Explorer Hat",
    "Tail Coat Cap": "Black Top Hat",
    "Golf Cap": "Golf Cap",
    "Aloha Cap": "Resort Hat",
    "Sailor Cap": "Sailor Hat",
    "Swimwear Cap": "Swim Goggles",
    "Cook Cap": "Chef Hat",
    "Armor Cap": "Samurai Helmet",
    "Happi Cap": "Happi Headband",
    "Tuxedo Cap": "Mario's Top Hat",
    "64 Cap": "Mario 64 Cap",
    "Luigi Cap": "Luigi Cap",
    "Football Cap": "Football Helmet",
    "Mechanic Cap": "Mechanic Cap",
    "New 3DS Cap": "Fashionable Cap",
    "Painter Cap": "Painter's Cap",
    "Suit Cap": "Black Fedora",
    "Maker Cap": "Builder Helmet",
    "Skip": "", # Racing
    "Doctor Cap": "Doctor Headwear",
    "Classic Cap": "Classic Cap",
    "Gold Cap": "Gold Mario Cap",
    "Skip": "", # Link
    "King Cap": "King's Crown",
    "Skip": "", # Mario
    "Scientist Cap": "Scientist Visor",
    "Primitive Man Cap": "Caveman Headwear",
    "Shopman Cap": "Employee Cap",
    "Pilot Cap": "Aviator Cap",
    "Snow Suit Cap": "Snow Hood",
    "Space Suit Cap": "Space Helmet",
    "Diddy Kong Cap": "Diddy Kong Hat",
    "Skip": "", # Batting
    "Captain Cap": "Captain's Hat",
    "Wario Cap": "Wario Cap",
    "Waluigi Cap": "Waluigi Cap",
    "Skip": "", # Satellaview
    "Skip": "", # conductor
    "Skip": "", # Santa
    "Skip": "", # Zombie
    "Clown Cap": "Clown Hat",
    "Pirate Cap": "Pirate Hat",
    "Peach Cap": "Bridal Veil",
    "Koopa Cap": "Bowser's Top Hat",
    "Skip": "", # Knight
    "64 Metal Cap": "Metal Mario Cap",
    "Invisible Cap": "Invisibility Hat"
}

clothes = {
    "Poncho Clothes": "Poncho",
    "Gunman Clothes": "Cowboy Outfit",
    "Explorer Clothes": "Explorer Outfit",
    "Tail Coat Clothes": "Black Tuxedo",
    "Golf Clothes": "Golf Outfit",
    "Aloha Clothes": "Resort Outfit",
    "Sailor Clothes": "Sailor Suit",
    "Swimwear Clothes": "Swimwear",
    "Cook Clothes": "Chef Suit",
    "Armor Clothes": "Samurai Armor",
    "Happi Clothes": "Happi Outfit",
    "Tuxedo Clothes": "Mario's Tuxedo",
    "64 Clothes": "Mario 64 Suit",
    "Luigi Clothes": "Luigi Suit",
    "Football Clothes": "Football Uniform",
    "Underwear": "Boxer Shorts",
    "Mechanic Clothes": "Mechanic Outfit",
    "New 3DS Clothes": "Fashionable Outfit",
    "Painter Clothes": "Painter Outfit",
    "Suit Clothes": "Black Suit",
    "Maker Clothes": "Builder Outfit",
    "Skip": "", # Racing
    "Doctor Clothes": "Doctor Outfit",
    "Hakama Clothes": "Hakama",
    "Classic Clothes": "Classic Suit",
    "Gold Clothes": "Gold Mario Suit",
    "Skip": "", # Link
    "Bone Clothes": "Skeleton Suit",
    "King Clothes": "King's Outfit",
    "Skip": "", # Mario
    "Scientist Clothes": "Scientist Outfit",
    "Primitive Man Clothes": "Caveman Outfit",
    "Shopman Clothes": "Employee Uniform",
    "Pilot Clothes": "Aviator Outfit",
    "Snow Suit Clothes": "Snow Suit",
    "Space Suit Clothes": "Space Suit",
    "Diddy Kong Clothes": "Diddy Kong Suit",
    "Skip": "", # Batting=
    "Wario Clothes": "Wario Suit",
    "Waluigi Clothes": "Waluigi Suit",
    "Skip": "", # Satellaview
    "Skip": "", # conductor
    "Skip": "", # Santa
    "Skip": "", # Zombie
    "Clown Clothes": "Clown Suit",
    "Pirate Clothes": "Pirate Outfit",
    "Peach Clothes": "Bridal Gown",
    "Koopa Clothes": "Bowser's Tuxedo",
    "Skip": "", # Knight
    "64 Metal Clothes": "Metal Mario Suit"
}
# Technically filler until achievements implemented
stickers = {
    "Sticker Cap": "Cap Kingdom Sticker",
    "Sticker Waterfall": "Cascade Kingdom Sticker",
    "Sticker Sand": "Sand Kingdom Sticker",
    "Sticker Forest": "Wooded Kingdom Sticker",
    "Sticker City": "Metro Kingdom Sticker",
    "Sticker Clash": "Lost Kingdom Sticker",
    "Sticker Lake": "Lake Kingdom Sticker",
    "Sticker Sea": "Seaside Kingdom Sticker",
    "Sticker Lava": "Luncheon Kingdom Sticker",
    "Sticker Snow": "Snow Kingdom Sticker",
    "Sticker Sky": "Bowser's Kingdom Sticker",
    "Sticker Moon": "Moon Kingdom Sticker",
    "Sticker Peach": "Mushroom Kingdom Sticker",
    "Sticker Peach Dokan": "Pipe Sticker",
    "Sticker Peach Coin": "Coin Sticker",
    "Sticker Peach Block": "Block Sticker",
    "Sticker Peach Block Question": "? Block Sticker"
}

gifts = {
    "Souvenir Hat 1": "Plush Frog",
    "Souvenir Hat 2": "Bonneton Tower Model",
    "Souvenir Fall 1": "T-Rex Model",
    "Souvenir Fall 2": "Triceratops Trophy",
    "Souvenir Sand 2": "Jaxi Statue",
    "Souvenir Sand 1": "Inverted Pyramid Model",
    "Souvenir Forest 1": "Flowers from Steam Gardens",
    "Souvenir Forest 2": "Steam Gardener Watering Can",
    "Souvenir City 2": "New Donk City Hall Model",
    "Souvenir City 1": "Pauline Statue",
    "Souvenir Crash 1": "Potted Palm Tree",
    "Souvenir Crash 2": "Butterfly Mobile",
    "Souvenir Lake 2": "Rubber Dorrie",
    "Souvenir Lake 1": "Underwater Dome",
    "Souvenir Lava 1": "Souvenir Forks",
    "Souvenir Lava 2": "Vegetable Plate",
    "Souvenir Sea 2": "Glass Tower Model",
    "Souvenir Sea 1": "Sand Jar",
    "Souvenir Snow 1": "Shiverian Rug",
    "Souvenir Snow 2": "Shiverian Nesting Dolls",
    "Souvenir Sky1": "Paper Lantern",
    "Souvenir Sky2": "Jizo Statue",
    "Souvenir Moon 1": "Moon Rock Fragment",
    "Souvenir Moon 2": "Moon Lamp",
    "Souvenir Peach 1": "Mushroom Cushion Set",
    "Souvenir Peach 2": "Peach's Castle Model"
}

filler_item_table = {
    "50 Coins": "Pocket Change",
    "100 Coins": "Worth as much as an additional Mario when life was flat.",
    "250 Coins": "Don't spend it all in one place!",
    "500 Coins": "Your face is beaming!",
    "1000 Coins": "STONKS!"
}

file_to_items = {
    "ItemCap" : caps,
    "ItemCloth" : clothes,
    "ItemGift" : gifts,
    "ItemSticker" : stickers
}

world_prefixes = [
    "Hat",
    "Waterfall",
    "Sand",
    "Lake",
    "Forest",
    "Clash",
    "City",
    "Sea",
    "Snow",
    "Moon",
    "Lava",
    "Sky",
    "Peach"
]

def set_moon_counts(self) -> bytes:
    """ Generates a ByteStream for a .szs (SARC) archive to replace the number of Power Moons required for each kingdom.
        Return:
            The bytes of the yaz0 compressed SARC archive.
    """
    if not os.path.exists(os.path.join(self.options.romFS.value, "SystemData/WorldList.szs")):
        raise Exception("Super Mario Odyssey romfs is invalid: SystemData/WorldList.szs does not exist.")
    world_list = sarc.read_file_and_make_sarc(open(self.options.romFS.value+"/SystemData/WorldList.szs", "rb"))
    data = world_list.get_file_data("StageLockList.byml")
    root = byml.Byml(data.tobytes()).parse()
    for i in range(14):
        if i == 1:
            root.get("StageLockList")[i]["ShineNumInfo"] = [byml.Int(self.moon_counts["cascade"])]
        elif i== 2:
            root.get("StageLockList")[i]["ShineNumInfo"] = [byml.Int(self.moon_counts["sand"])]
        elif i== 3:
            root.get("StageLockList")[i]["ShineNumInfo"] = [byml.Int(self.moon_counts["wooded"]), byml.Int(self.moon_counts["lake"])]
        elif i== 5:
            root.get("StageLockList")[i]["ShineNumInfo"] = [byml.Int(self.moon_counts["lost"])]
        elif i== 6:
            root.get("StageLockList")[i]["ShineNumInfo"] = [byml.Int(self.moon_counts["metro"])]
        elif i== 7:
            root.get("StageLockList")[i]["ShineNumInfo"] = [byml.Int(self.moon_counts["seaside"]), byml.Int(self.moon_counts["snow"])]
        elif i== 8:
            root.get("StageLockList")[i]["ShineNumInfo"] = [byml.Int(self.moon_counts["luncheon"])]
        elif i== 9:
            root.get("StageLockList")[i]["ShineNumInfo"] = [byml.Int(self.moon_counts["ruined"])]
        elif i== 10:
            root.get("StageLockList")[i]["ShineNumInfo"] = [byml.Int(self.moon_counts["bowser"])]
        elif i== 13:
            root.get("StageLockList")[i]["ShineNumInfo"] = [byml.Int(self.moon_counts["dark"])]
        elif i== 14:
            root.get("StageLockList")[i]["ShineNumInfo"] = [byml.Int(self.moon_counts["darker"])]

    writer = byml.Writer(root)
    save_world_list = sarc.make_writer_from_sarc(world_list)
    save_world_list.add_file("StageLockList.byml", writer.get_bytes())

    compressed = yaz0.CompressYaz(save_world_list.get_bytes(),6)
    return compressed

def patch_prices(self, item_list : sarc.SARC, save_item_list : sarc.SARCWriter) -> None:
    """ Changes in game item prices so none exceed 1000 coins and so regional items are a threshold of the total in their kingdom.
        Args:
            self: SMOWorld object for this player's world.
            item_list: The SARC (System Archive) of the item list.
            save_item_list: A Sarc writer for saving patch changes.
    """
    data = item_list.get_file_data("ItemList.byml")
    root = byml.Byml(data.tobytes()).parse()
    store_amounts = {}

    for i in root:
        if i["CoinType"] == "Collect":
            print(i)
            if i["StoreName"] in store_amounts:
                if i["Price"] - store_amounts[i["StoreName"]] == 5:
                    break
                store_amounts[i["StoreName"]] += i["Price"]
                i["Price"] = byml.Int(store_amounts[i["StoreName"]])
            else:
                store_amounts[i["StoreName"]] = byml.Int(i["Price"])
        else:
            if i["Price"] > 1000:
                i["Price"] = byml.Int(1000)
        if "MoonNum" in i:
            internalName = re.sub(r'((?<=[a-z])[A-Z]|(?<=[0-9])[A-Z])', r' \1', ((i["ItemName"].replace("Color", "").replace("Mario", "")) + i["ItemType"]))
            i["MoonNum"] = byml.Int(self.outfit_moon_counts[caps[internalName] if i["ItemType"].strip() == "Cap" else clothes[internalName]])

    writer = byml.Writer(root)
    save_item_list.add_file("ItemList.byml", writer.get_bytes())

def randomize_colors(self, item_list : sarc.SARC, save_item_list : sarc.SARCWriter) -> None:
    """ Generates a ByteStream for a .szs (SARC) archive to replace the color of Power Moons for each kingdom.
            Return:
                The bytes of the yaz0 compressed SARC archive.
    """

    data = item_list.get_file_data("WorldItemTypeList.byml")
    root = byml.Byml(data.tobytes()).parse()
    colors = list(range(10))
    colors.append(random.randint(0,9))
    colors.append(random.randint(0,9))
    colors.append(random.randint(0,9))
    colors.append(random.randint(0,9))
    colors.append(random.randint(0,9))
    colors.append(random.randint(0,9))
    for i in root:
        if i["WorldName"] != "Peach":
            i["Shine"] = byml.Int(colors.pop(random.randint(0, len(colors) - 1)))

    writer = byml.Writer(root)
    save_item_list.add_file("WorldItemTypeList.byml", writer.get_bytes())

def read_regionals_from_world(self, stage_file : sarc.SARC, file_name : str) -> dict:
    data = stage_file.get_file_data(file_name.replace(".szs", ".byml"))
    root = byml.Byml(data.tobytes()).parse()

    regionals =  {}
    added_to_group : bool = False
    group_count : int = 1
    for map_dict in root:
        if not "ObjectList" in map_dict:
            return {}
        for entry in map_dict["ObjectList"]:
            if entry["UnitConfigName"] == "CoinCollect" or entry["UnitConfigName"] == "CoinCollect2D":
                for group in regionals:
                    # if entry["Id"] in regionals[group]:
                    #     added_to_group = True
                    #     break
                    for coin in regionals[group]:
                        if (abs(regionals[group][coin]["Translate"]["X"] - entry["Translate"]["X"]) <= 220.00
                                and abs(regionals[group][coin]["Translate"]["Z"] - entry["Translate"]["Z"]) <= 220.0
                                and abs(regionals[group][coin]["Translate"]["Y"] - entry["Translate"]["Y"] <= 150.0)):
                            regionals[group][entry["Id"]] = {}
                            regionals[group][entry["Id"]]["Translate"] = entry["Translate"]
                            added_to_group = True
                            break
                    if added_to_group:
                        break
                if added_to_group:
                    added_to_group = False
                    continue

                else:
                    regionals["group" + str(group_count)] = {}
                    regionals["group" + str(group_count)][entry["Id"]] = {}
                    regionals["group" + str(group_count)][entry["Id"]]["Translate"] = entry["Translate"]
                    group_count += 1

    groups_to_remove = []

    for group in regionals:
        if len(regionals[group]) < 2:
            groups_to_remove.append(group)

    for group in groups_to_remove:
        regionals.pop(group)

    return regionals


def patch_shop_text(self) -> bytes:
    """ Generates a ByteStream for a .szs (SARC) archive to replace the English localized text for shops with the respective item at that location.
        Return:
            The bytes of the yaz0 compressed SARC archive.
    """
    if not os.path.exists(os.path.join(self.options.romFS.value, "LocalizedData/USen/MessageData/SystemMessage.szs")):
        raise Exception("Super Mario Odyssey romfs is invalid: LocalizedData/USen/MessageData/SystemMessage.szs does not exist.")

    item_text = sarc.read_file_and_make_sarc(open(os.path.join(self.options.romFS.value, "LocalizedData/USen/MessageData/SystemMessage.szs"), "rb"))
    save_item_text = sarc.make_writer_from_sarc(item_text)
    for i in file_to_items.keys():
        data = item_text.get_file_data(i + ".msbt")
        root = Msbt.Msbt(data.tobytes())

        for item in file_to_items[i]:
            internal_name = ("MarioColor" + item) if "Luigi" in item or "Wario" in item or "Waluigi" in item or "Classic" in item or "Gold" in item else ("Mario" + item) if i == "ItemCap" or i == "ItemCloth" else item
            if i == "ItemCap" or i == "ItemCloth":
                internal_name = internal_name.replace(" Cap","").replace("Clothes", "")
            item_classification : ItemClassification
            if item != "Skip":
                if item in self.multiworld.regions.location_cache[self.player]:
                    item_classification = self.multiworld.get_location(file_to_items[i][item], self.player).item.classification
                    root.msbt["labels"][internal_name.replace(" ", "")]["message"] =  self.multiworld.get_location(file_to_items[i][item], self.player).item.name.replace("_", " ")
                    root.msbt["labels"][internal_name.replace(" ", "")]["message"] += "\0"
                    item_player = self.multiworld.get_player_name(self.multiworld.get_location(file_to_items[i][item], self.player).item.player)
                    item_game = self.multiworld.get_location(file_to_items[i][item], self.player).item.game
                    if item_game != "Super Mario Odyssey" and self.multiworld.get_location(file_to_items[i][item], self.player).item.player != self.player:
                        root.msbt["labels"][internal_name.replace(" ", "") + "_Explain"]["message"] = \
                            ("Comes from the world of " + item_game.replace("_", " ") +  ".\nSeems to belong to " + item_player +
                            ".\n")
                        root.msbt["labels"][internal_name.replace(" ", "") + "_Explain"]["message"] += ("It looks really important!"
                            if item_classification == ItemClassification.progression_skip_balancing or
                            item_classification == ItemClassification.progression or item_classification == ItemClassification.trap
                            else "It looks useful!" if item_classification == ItemClassification.useful else "It looks like junk, but may as well ask...")

                        root.msbt["labels"][internal_name.replace(" ", "") + "_Explain"][
                            "message"] += "\0"

                    else:
                        if self.multiworld.get_location(file_to_items[i][item], self.player).item.name in filler_item_table.keys():
                            root.msbt["labels"][internal_name.replace(" ", "") + "_Explain"][
                                "message"] = filler_item_table[self.multiworld.get_location(file_to_items[i][item], self.player).item.name] + "\0"
                        else:
                            root.msbt["labels"][internal_name.replace(" ", "") + "_Explain"][
                                "message"] = ("I may need this!" if item_classification == ItemClassification.progression_skip_balancing or item_classification ==
                            ItemClassification.progression or item_classification == ItemClassification.trap
                            else "It looks useful!" if item_classification == ItemClassification.useful else "I don't need this...")
                            root.msbt["labels"][internal_name.replace(" ", "") + "_Explain"]["message"] += "\0"


        save_item_text.add_file(i + ".msbt", root.get_bytes())

    compressed = yaz0.CompressYaz(save_item_text.get_bytes(),6)
    return compressed

def patch_items(self) -> bytes:
    """ Generates a ByteStream for a .szs (SARC) archive to change data in the Item List like shop prices and moon colors.
            Return:
                The bytes of the yaz0 compressed SARC archive.
    """
    if not os.path.exists(os.path.join(self.options.romFS.value, "SystemData/ItemList.szs")):
        raise Exception("Super Mario Odyssey romfs is invalid: SystemData/ItemList.szs does not exist.")
    item_list = sarc.read_file_and_make_sarc(open(os.path.join(self.options.romFS.value, "SystemData/ItemList.szs"), "rb"))
    save_item_list = sarc.make_writer_from_sarc(item_list)

    # reapply shop item changes
    # not needed when using mod file as base
    #patch_prices(self, item_list, save_item_list)

    if self.options.colors.value:
        # Apply moon color changes
        randomize_colors(self, item_list, save_item_list)

    compressed = yaz0.CompressYaz(save_item_list.get_bytes(),6)
    return compressed

def patch_stages(self) -> None:
    """ Generates a ByteStream for a .szs (SARC) archive to change stage object data.
            Return:
                The bytes of the yaz0 compressed SARC archive.
    """
    stage_path = os.path.join(self.options.romFS.value, "StageData")
    if not os.path.exists(stage_path):
        raise Exception("Super Mario Odyssey romfs is invalid: StageData does not exist.")

    dirs = os.listdir(stage_path)

    regional_coins = {}


    for file_name in dirs:
        for prefix in world_prefixes:
            if file_name.startswith(prefix):
                if "Map.szs" in file_name:
                    stage = sarc.read_file_and_make_sarc(open(os.path.join(stage_path, file_name), "rb"))
                    regional_coins[file_name.replace(".szs", "")] = {}
                    regional_coins[file_name.replace(".szs", "")] = read_regionals_from_world(self, stage, file_name)
                    break

    group_num = 0
    out_line : str = ""

    for world in world_prefixes:
        out_line = "private Dictionary<string, string[]> " + world + "RegionalCoins" + " = new Dictionary<string, string[]>() {\n"
        for world_stage in regional_coins:
            if world in world_stage:

                if len(regional_coins[world_stage]) > 0:

                    for group in regional_coins[world_stage]:
                        out_line += "\t{" + "\"group" + str(group_num) + "\", new string[] {"
                        coin_count = 0
                        for coin in regional_coins[world_stage][group]:
                            out_line += " " + coin
                            coin_count += 1
                            if not coin_count == len(regional_coins[world_stage][group]):
                                out_line += ","
                        out_line += "}},\n"
                else:
                    continue

        print(out_line)


def make_output(self, output_dir : str):
    """ Generates .zip file containing the RomFS patch for Super Mario Odyssey.
        Args:
            self: Patch
            output_dir: The Directory to save the generated zip archive.
    """
    if not os.path.exists(self.options.romFS.value):
        raise Exception("Super Mario Odyssey romfs is invalid: path to romfs does not exist.")

    patch_data = {}

    #patch_stages(self)

    if self.options.counts.value != 0:
        patch_data["atmosphere/contents/0100000000010000/romfs/SystemData/WorldList.szs"] = set_moon_counts(self)


    if self.options.shop_sanity.value != 0:
        patch_data["atmosphere/contents/0100000000010000/romfs/LocalizedData/USen/MessageData/SystemMessage.szs"] = patch_shop_text(self)

    #os.mkdir(output_dir + "atmosphere/contents/0100000000010000/romfs/LocalizedData/USen/MessageData/")
    #os.mkdir(output_dir + "atmosphere/contents/0100000000010000/romfs/SystemData/")

    #os.mkdir(output_dir + "atmosphere/contents/0100000000010000/romfs/SystemData/")
    patch_data["atmosphere/contents/0100000000010000/romfs/SystemData/ItemList.szs"] = patch_items(self)


    mod_dir = os.path.join(output_dir,self.multiworld.get_file_safe_player_name(self.player))
    mod = SMOPatch(patch_data, mod_dir, output_dir, self.player, self.multiworld.get_file_safe_player_name(self.player))
    mod.write()
