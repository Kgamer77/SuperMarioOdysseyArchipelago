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

caps = [
    "Poncho Cap",
    "Gunman Cap",
    "Explorer Cap",
    "Tail Coat Cap",
    "Golf Cap",
    "Aloha Cap",
    "Sailor Cap",
    "Swimwear Cap",
    "Cook Cap",
    "Armor Cap",
    "Happi Cap",
    "Tuxedo Cap",
    "64 Cap",
    "Luigi Cap",
    "Football Cap",
    "Mechanic Cap",
    "New 3DS Cap",
    "Painter Cap",
    "Suit Cap",
    "Maker Cap",
    "Skip", # Racing
    "Doctor Cap",
    "Classic Cap",
    "Gold Cap",
    "Skip", # Link
    "King Cap",
    "Skip", # Mario
    "Scientist Cap",
    "Primitive Man Cap",
    "Shopman Cap",
    "Pilot Cap",
    "Snow Suit Cap",
    "Space Suit Cap",
    "Diddy Kong Cap",
    "Skip", # Batting
    "Captain Cap",
    "Wario Cap",
    "Waluigi Cap",
    "Skip", # Satellaview
    "Skip", # conductor
    "Skip", # Santa
    "Skip", # Zombie
    "Clown Cap",
    "Pirate Cap",
    "Peach Cap",
    "Koopa Cap",
    "Skip", # Knight
    "64 Metal Cap",
    "Invisible Cap"
]

clothes = [
    "Poncho Clothes",
    "Gunman Clothes",
    "Explorer Clothes",
    "Tail Coat Clothes",
    "Golf Clothes",
    "Aloha Clothes",
    "Sailor Clothes",
    "Swimwear Clothes",
    "Cook Clothes",
    "Armor Clothes",
    "Happi Clothes",
    "Tuxedo Clothes",
    "64 Clothes",
    "Luigi Clothes",
    "Football Clothes",
    "Underwear",
    "Mechanic Clothes",
    "New 3DS Clothes",
    "Painter Clothes",
    "Suit Clothes",
    "Maker Clothes",
    "Skip", # Racing
    "Doctor Clothes",
    "Hakama Clothes",
    "Classic Clothes",
    "Gold Clothes",
    "Skip", # Link
    "Bone Clothes",
    "King Clothes",
    "Skip", # Mario
    "Scientist Clothes",
    "Primitive Man Clothes",
    "Shopman Clothes",
    "Pilot Clothes",
    "Snow Suit Clothes",
    "Space Suit Clothes",
    "Diddy Kong Clothes",
    "Skip", # Baseball
    "Wario Clothes",
    "Waluigi Clothes",
    "Skip", # Satellaview
    "Skip", # conductor
    "Skip", # Santa
    "Skip", # Zombie
    "Clown Clothes",
    "Pirate Clothes",
    "Peach Clothes",
    "Koopa Clothes",
    "Skip", # Knight
    "64 Metal Clothes"
]
# Technically filler until achievements implemented
stickers = [
    "Sticker Cap",
    "Sticker Waterfall",
    "Sticker Sand",
    "Sticker Forest",
    "Sticker City",
    "Sticker Clash",
    "Sticker Lake",
    "Sticker Sea",
    "Sticker Lava",
    "Sticker Snow",
    "Sticker Sky",
    "Sticker Moon",
    "Sticker Peach",
    "Sticker Peach Dokan",
    "Sticker Peach Coin",
    "Sticker Peach Block",
    "Sticker Peach Block Question"
]

gifts = [
    "Souvenir Hat 1",
    "Souvenir Hat 2",
    "Souvenir Fall 1",
    "Souvenir Fall 2",
    "Souvenir Sand 2",
    "Souvenir Sand 1",
    "Souvenir Forest 1",
    "Souvenir Forest 2",
    "Souvenir City 2",
    "Souvenir City 1",
    "Souvenir Crash 1",
    "Souvenir Crash 1",
    "Souvenir Crash 2",
    "Souvenir Lake 2",
    "Souvenir Lake 1",
    "Souvenir Lava 1",
    "Souvenir Lava 2",
    "Souvenir Sea 2",
    "Souvenir Sea 1",
    "Souvenir Snow 1",
    "Souvenir Snow 2",
    "Souvenir Sky1",
    "Souvenir Sky2",
    "Souvenir Moon 1",
    "Souvenir Moon 2",
    "Souvenir Peach 1",
    "Souvenir Peach 2"
]

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

def set_moon_counts(self) -> bytes:
    """ Generates a ByteStream for a .szs (SARC) archive to replace the number of Power Moons required for each kingdom.
        Return:
            The bytes of the yaz0 compressed SARC archive.
    """
    if not os.path.exists(self.options.romFS.value+"SystemData/WorldList.szs"):
        raise Exception("Super Mario Odyssey romfs is invalid: SystemData/WorldList.szs does not exist.")
    world_list = sarc.read_file_and_make_sarc(open(self.options.romFS.value+"SystemData/WorldList.szs", "rb"))
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
            if i["StoreName"] in store_amounts:
                store_amounts[i["StoreName"]] += i["Price"]
                i["Price"] = byml.Int(store_amounts[i["StoreName"]])
            else:
                store_amounts[i["StoreName"]] = byml.Int(i["Price"])
        else:
            if i["Price"] > 1000:
                i["Price"] = byml.Int(1000)
        if "MoonNum" in i:
            i["MoonNum"] = byml.Int(self.outfit_moon_counts[re.sub(r'((?<=[a-z])[A-Z]|(?<=[0-9])[A-Z])', r' \1', ((i["ItemName"].replace("Color", "").replace("Mario", "")) + i["ItemType"]))])

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



def patch_shop_text(self) -> bytes:
    """ Generates a ByteStream for a .szs (SARC) archive to replace the English localized text for shops with the respective item at that location.
        Return:
            The bytes of the yaz0 compressed SARC archive.
    """
    if not os.path.exists(self.options.romFS.value + "LocalizedData/USen/MessageData/SystemMessage.szs"):
        raise Exception("Super Mario Odyssey romfs is invalid: LocalizedData/USen/MessageData/SystemMessage.szs does not exist.")

    item_text = sarc.read_file_and_make_sarc(open(self.options.romFS.value+"LocalizedData/USen/MessageData/SystemMessage.szs", "rb"))
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
                item_classification = self.multiworld.get_location(item, self.player).item.classification
                root.msbt["labels"][internal_name.replace(" ", "")]["message"] =  self.multiworld.get_location(item, self.player).item.name.replace("_", " ")
                root.msbt["labels"][internal_name.replace(" ", "")]["message"] += "\0"
                item_player = self.multiworld.get_player_name(self.multiworld.get_location(item, self.player).item.player)
                item_game = self.multiworld.get_location(item, self.player).item.game
                if item_game != "Super Mario Odyssey" and self.multiworld.get_location(item, self.player).item.player != self.player:
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
                    if self.multiworld.get_location(item, self.player).item.name in filler_item_table.keys():
                        root.msbt["labels"][internal_name.replace(" ", "") + "_Explain"][
                            "message"] = filler_item_table[self.multiworld.get_location(item, self.player).item.name] + "\0"
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
    if not os.path.exists(self.options.romFS.value+"SystemData/ItemList.szs"):
        raise Exception("Super Mario Odyssey romfs is invalid: SystemData/ItemList.szs does not exist.")
    item_list = sarc.read_file_and_make_sarc(open(self.options.romFS.value+"SystemData/ItemList.szs", "rb"))
    save_item_list = sarc.make_writer_from_sarc(item_list)

    # reapply shop item changes
    patch_prices(self, item_list, save_item_list)

    if self.options.colors.value:
        # Apply moon color changes
        randomize_colors(self, item_list, save_item_list)

    compressed = yaz0.CompressYaz(save_item_list.get_bytes(),6)
    return compressed

def make_output(self, output_dir : str):
    """ Generates .zip file containing the RomFS patch for Super Mario Odyssey.
        Args:
            self: Patch
            output_dir: The Directory to save the generated zip archive.
    """
    if not os.path.exists(self.options.romFS.value):
        raise Exception("Super Mario Odyssey romfs is invalid: path to romfs does not exist.")

    patch_data = {}

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
