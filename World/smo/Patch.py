"""
Classes and functions related to creating a romfs patch
"""
import json
import os
import random
import re

from settings import get_settings

from .byml import byml
from .sarc import sarc


from .yaz0 import yaz0
from .MsbtEditor import Msbt

from BaseClasses import ItemClassification
from worlds.Files import APProcedurePatch


class SMOProcedurePatch(APProcedurePatch):
    game = "Super Mario Odyssey"
    patch_file_ending = ".apsmo"
    procedure = [("apply_romfs_patch", ["options.json", "location_data.json", "moon_counts.json", "player_names.json"])]
    hash = "N/A"

    def apply_romfs_patch(self, options_file : str, location_file : str, counts_file : str, names_file : str):
        rom_fs = get_settings().smo_settings.romFS_folder
        if not os.path.exists(rom_fs):
            raise Exception("Super Mario Odyssey romfs is invalid: path to romfs does not exist.")

        patch_data = {}

        options = json.loads(self.get_file(options_file))

        moon_counts = json.loads(self.get_file(counts_file))



        if options["counts"] != 0:
            patch_data["atmosphere/contents/0100000000010000/romfs/SystemData/WorldList.szs"] = set_moon_counts(rom_fs, moon_counts)

        location_data = json.loads(self.get_file(location_file))
        player_names = json.loads(self.get_file(names_file))

        if options["shop_sanity"] != 0:
            patch_data[
                "atmosphere/contents/0100000000010000/romfs/LocalizedData/USen/MessageData/SystemMessage.szs"] = patch_shop_text(rom_fs, location_data, self.player, player_names)

        patch_data["atmosphere/contents/0100000000010000/romfs/SystemData/ItemList.szs"] = patch_items(rom_fs, options)

        patch_dir = os.path.join(self.path[:self.path.rindex("/")], "atmosphere/contents/0100000000010000/romfs/")
        os.makedirs(os.path.join(patch_dir, "LocalizedData/USen/MessageData/"))
        os.mkdir(os.path.join(patch_dir, "SystemData"))
        os.mkdir(os.path.join(patch_dir, "StageData"))
        for archive in patch_data:
            file = open(os.path.join(self.path[:self.path.rindex("/")], archive), "wb")
            file.write(patch_data[archive])
            file.close()

def write_patch(self, patch : SMOProcedurePatch) -> None:
    data = {}
    for location in self.get_locations():
        data[location.name] = [location.item.game , location.item.name, location.item.classification, location.item.player]

    out = json.dumps(data)
    patch.write_file("location_data.json", out.encode())

    data = {}
    for i in range(1, self.multiworld.players + 1):
        data[i] = self.multiworld.get_player_name(i)

    out = json.dumps(data)
    patch.write_file("player_names.json", out.encode())

    out = json.dumps(self.moon_counts)
    patch.write_file("moon_counts.json", out.encode())

    data = {}
    data["counts"] = self.options.counts.value
    data["shop_sanity"] = self.options.shop_sanity.value
    data["colors"] = self.options.colors.value

    out = json.dumps(data)
    patch.write_file("options.json", out.encode())



# class SMOPatch(APContainer):
#     game: str = "Super Mario Odyssey"
#
#     def __init__(self, patch_data : dict, base_path: str, output_directory: str, player=None, player_name: str = "", server: str = ""):
#         self.patch_data = patch_data
#         self.file_path = base_path
#         container_path = os.path.join(output_directory, base_path + ".zip")
#         super().__init__(container_path, player, player_name, server)
#
#     def write_contents(self, opened_zipfile: zipfile.ZipFile) -> None:
#         for filename, bin_io in self.patch_data.items():
#             file = opened_zipfile.open(filename, "w")
#             file.write(bin_io)
#             file.close()
#
#         super().write_contents(opened_zipfile)

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
    "Skip1": "", # Racing
    "Doctor Cap": "Doctor Headwear",
    "Classic Cap": "Classic Cap",
    "Gold Cap": "Gold Mario Cap",
    "Skip2": "", # Link
    "King Cap": "King's Crown",
    "Skip3": "", # Mario
    "Scientist Cap": "Scientist Visor",
    "Primitive Man Cap": "Caveman Headwear",
    "Shopman Cap": "Employee Cap",
    "Pilot Cap": "Aviator Cap",
    "Snow Suit Cap": "Snow Hood",
    "Space Suit Cap": "Space Helmet",
    "Diddy Kong Cap": "Diddy Kong Hat",
    "Skip4": "", # Batting
    "Captain Cap": "Captain's Hat",
    "Wario Cap": "Wario Cap",
    "Waluigi Cap": "Waluigi Cap",
    "Skip5": "", # Satellaview
    "Skip6": "", # conductor
    "Skip7": "", # Santa
    "Skip8": "", # Zombie
    "Clown Cap": "Clown Hat",
    "Pirate Cap": "Pirate Hat",
    "Peach Cap": "Bridal Veil",
    "Koopa Cap": "Bowser's Top Hat",
    "Skip9": "", # Knight
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
    "Skip1": "", # Racing
    "Doctor Clothes": "Doctor Outfit",
    "Hakama Clothes": "Hakama",
    "Classic Clothes": "Classic Suit",
    "Gold Clothes": "Gold Mario Suit",
    "Skip2": "", # Link
    "Bone Clothes": "Skeleton Suit",
    "King Clothes": "King's Outfit",
    "Skip3": "", # Mario
    "Scientist Clothes": "Scientist Outfit",
    "Primitive Man Clothes": "Caveman Outfit",
    "Shopman Clothes": "Employee Uniform",
    "Pilot Clothes": "Aviator Outfit",
    "Snow Suit Clothes": "Snow Suit",
    "Space Suit Clothes": "Space Suit",
    "Diddy Kong Clothes": "Diddy Kong Suit",
    "Skip4": "", # Batting
    "Wario Clothes": "Wario Suit",
    "Waluigi Clothes": "Waluigi Suit",
    "Skip5": "", # Satellaview
    "Skip6": "", # conductor
    "Skip7": "", # Santa
    "Skip8": "", # Zombie
    "Clown Clothes": "Clown Suit",
    "Pirate Clothes": "Pirate Outfit",
    "Peach Clothes": "Bridal Gown",
    "Koopa Clothes": "Bowser's Tuxedo",
    "Skip9": "", # Knight
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
    "Cap",
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

def set_moon_counts(rom_fs : str, moon_counts : dict) -> bytes:
    """ Generates a ByteStream for a .szs (SARC) archive to replace the number of Power Moons required for each kingdom.
        Return:
            The bytes of the yaz0 compressed SARC archive.
    """
    if not os.path.exists(os.path.join(rom_fs, "SystemData/WorldList.szs")):
        raise Exception("Super Mario Odyssey romFS is invalid: SystemData/WorldList.szs does not exist.")
    world_list = sarc.read_file_and_make_sarc(open(os.path.join(rom_fs, "SystemData/WorldList.szs"), "rb"))
    data = world_list.get_file_data("StageLockList.byml")
    root = byml.Byml(data.tobytes()).parse()
    for i in range(14):
        if i == 1:
            root.get("StageLockList")[i]["ShineNumInfo"] = [byml.Int(moon_counts["cascade"])]
        elif i== 2:
            root.get("StageLockList")[i]["ShineNumInfo"] = [byml.Int(moon_counts["sand"])]
        elif i== 3:
            root.get("StageLockList")[i]["ShineNumInfo"] = [byml.Int(moon_counts["wooded"]), byml.Int(moon_counts["lake"])]
        elif i== 5:
            root.get("StageLockList")[i]["ShineNumInfo"] = [byml.Int(moon_counts["lost"])]
        elif i== 6:
            root.get("StageLockList")[i]["ShineNumInfo"] = [byml.Int(moon_counts["metro"])]
        elif i== 7:
            root.get("StageLockList")[i]["ShineNumInfo"] = [byml.Int(moon_counts["seaside"]), byml.Int(moon_counts["snow"])]
        elif i== 8:
            root.get("StageLockList")[i]["ShineNumInfo"] = [byml.Int(moon_counts["luncheon"])]
        elif i== 9:
            root.get("StageLockList")[i]["ShineNumInfo"] = [byml.Int(moon_counts["ruined"])]
        elif i== 10:
            root.get("StageLockList")[i]["ShineNumInfo"] = [byml.Int(moon_counts["bowser"])]
        elif i== 13:
            root.get("StageLockList")[i]["ShineNumInfo"] = [byml.Int(moon_counts["dark"])]
        elif i== 14:
            root.get("StageLockList")[i]["ShineNumInfo"] = [byml.Int(moon_counts["darker"])]

    writer = byml.Writer(root)
    save_world_list = sarc.make_writer_from_sarc(world_list)
    save_world_list.add_file("StageLockList.byml", writer.get_bytes())

    compressed = yaz0.CompressYaz(save_world_list.get_bytes(), 6)
    return compressed

def patch_prices(item_list : sarc.SARC, save_item_list : sarc.SARCWriter) -> None:
    """ Changes in game item prices so none exceed 1000 coins and so regional items are a threshold of the total in their kingdom.
        Args:
            item_list: The SARC (System Archive) of the item list.
            save_item_list: A Sarc writer for saving patch changes.
    """
    data = item_list.get_file_data("ItemList.byml")
    root = byml.Byml(data.tobytes()).parse()
    store_amounts = {}

    for i in root:
        if i["CoinType"] == "Collect":
            if i["StoreName"] in store_amounts:
                if store_amounts[i["StoreName"]] == 5 and i["Price"] - store_amounts[i["StoreName"]] == 10:
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
            i["MoonNum"] = byml.Int(outfit_moon_counts[caps[internalName] if i["ItemType"].strip() == "Cap" else clothes[internalName]])

    writer = byml.Writer(root)
    save_item_list.add_file("ItemList.byml", writer.get_bytes())

def randomize_colors(item_list : sarc.SARC, save_item_list : sarc.SARCWriter) -> None:
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

def read_regionals_from_world(stage_file : sarc.SARC, file_name : str) -> dict:
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


def patch_shop_text(rom_fs : str, location_data : dict, player : int, names : dict) -> bytes:
    """ Generates a ByteStream for a .szs (SARC) archive to replace the English localized text for shops with the respective item at that location.
        Return:
            The bytes of the yaz0 compressed SARC archive.
    """
    if not os.path.exists(os.path.join(rom_fs, "LocalizedData/USen/MessageData/SystemMessage.szs")):
        raise Exception("Super Mario Odyssey romfs is invalid: LocalizedData/USen/MessageData/SystemMessage.szs does not exist.")

    item_text = sarc.read_file_and_make_sarc(open(os.path.join(rom_fs, "LocalizedData/USen/MessageData/SystemMessage.szs"), "rb"))
    save_item_text = sarc.make_writer_from_sarc(item_text)
    for i in file_to_items.keys():
        data = item_text.get_file_data(i + ".msbt")
        root = Msbt.Msbt(data.tobytes())

        for item in file_to_items[i]:
            internal_name = ("MarioColor" + item) if "Luigi" in item or "Wario" in item or "Waluigi" in item or "Classic" in item or "Gold" in item else ("Mario" + item) if i == "ItemCap" or i == "ItemCloth" else item
            if i == "ItemCap" or i == "ItemCloth":
                internal_name = internal_name.replace(" Cap","").replace("Clothes", "")
            item_classification : ItemClassification
            if not "Skip" in item:
                if item in location_data:
                    item_classification = location_data[item][2]
                    root.msbt["labels"][internal_name.replace(" ", "")]["message"] =  location_data[item].item.name.replace("_", " ")
                    root.msbt["labels"][internal_name.replace(" ", "")]["message"] += "\0"
                    item_player = names[location_data[item][3]]
                    item_game = location_data[item][0]
                    if item_game != "Super Mario Odyssey" and location_data[item][3] != player:
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
                        if location_data[item][1] in filler_item_table.keys():
                            root.msbt["labels"][internal_name.replace(" ", "") + "_Explain"][
                                "message"] = filler_item_table[location_data[item][1]] + "\0"
                        else:
                            root.msbt["labels"][internal_name.replace(" ", "") + "_Explain"][
                                "message"] = ("I may need this!" if item_classification == ItemClassification.progression_skip_balancing or item_classification ==
                            ItemClassification.progression or item_classification == ItemClassification.trap
                            else "It looks useful!" if item_classification == ItemClassification.useful else "I don't need this...")
                            root.msbt["labels"][internal_name.replace(" ", "") + "_Explain"]["message"] += "\0"


        save_item_text.add_file(i + ".msbt", root.get_bytes())

    compressed = yaz0.CompressYaz(save_item_text.get_bytes(), 6)
    return compressed

def patch_items(rom_fs, options : dict) -> bytes:
    """ Generates a ByteStream for a .szs (SARC) archive to change data in the Item List like shop prices and moon colors.
            Return:
                The bytes of the yaz0 compressed SARC archive.
    """
    if not os.path.exists(os.path.join(rom_fs, "SystemData/ItemList.szs")):
        raise Exception("Super Mario Odyssey romfs is invalid: SystemData/ItemList.szs does not exist.")
    item_list = sarc.read_file_and_make_sarc(open(os.path.join(rom_fs, "SystemData/ItemList.szs"), "rb"))
    save_item_list = sarc.make_writer_from_sarc(item_list)

    # reapply shop item changes
    # not needed when using mod file as base
    patch_prices(item_list, save_item_list)

    if options["colors"]:
        # Apply moon color changes
        randomize_colors(item_list, save_item_list)

    compressed = yaz0.CompressYaz(save_item_list.get_bytes(), 6)
    return compressed

def patch_stages(rom_fs : str) -> None:
    """ Generates a ByteStream for a .szs (SARC) archive to change stage object data.
            Return:
                The bytes of the yaz0 compressed SARC archive.
    """
    stage_path = os.path.join(rom_fs, "StageData")
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
                    regional_coins[file_name.replace(".szs", "")] = read_regionals_from_world(stage, file_name)
                    break

    out_line : str = ""

    for world in world_prefixes:
        group_num = 0
        out_line = "\t\t\t{"
        out_line +=  "\"" + world + "WorldHomeStage\"" + ", "
        out_line += "new Regionals {\n"
        out_line += "\t\t\t\tobjGroupLookup = new Dictionary<string, int>() {\n"
        for world_stage in regional_coins:
            if world in world_stage:

                if len(regional_coins[world_stage]) > 0:

                    for group in regional_coins[world_stage]:
                        group_num += 1
                        coin_count = 0
                        for coin in regional_coins[world_stage][group]:
                            out_line += "\t\t\t\t\t{\"" + coin + "\", "
                            out_line +=  str(group_num) + "},\n"
                            coin_count += 1

        out_line += "\t\t\t\t}}\n"

        out_line += "\n\t\t\t},"
        print(out_line)


def make_output(patch_file : str):
    """ Generates .zip file containing the RomFS patch for Super Mario Odyssey.
        Args:
            patch_file: The Patch file
    """
    patcher = SMOProcedurePatch(patch_file)
    SMOProcedurePatch.apply_romfs_patch(patcher,"options.json", "location_data.json", "moon_counts.json", "player_names.json")
    print("Patch Complete")
