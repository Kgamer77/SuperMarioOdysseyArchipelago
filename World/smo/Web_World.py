from BaseClasses import Tutorial
from worlds.AutoWorld import WebWorld

from .Options import option_groups
#, option_presets


# For our game to display correctly on the website, we need to define a WebWorld subclass.
class SMOWebWorld(WebWorld):
    # We need to override the "game" field of the WebWorld superclass.
    # This must be the same string as the regular World class.
    game = "Super Mario Odyssey"

    # Your game pages will have a visual theme (affecting e.g. the background image).
    # You can choose between dirt, grass, grassFlowers, ice, jungle, ocean, partyTime, and stone.
    theme = "grass"

    setup_en = Tutorial(
        "Multiworld Setup Guide",
        "A guide to setting up SMO for MultiWorld.",
        "English",
        "setup_en.md",
        "setup/en",
        ["NewSoupVi"],
    )

    # We add these tutorials to our WebWorld by overriding the "tutorials" field.
    tutorials = [setup_en]

    # If we have option groups and/or option presets, we need to specify these here as well.
    option_groups = option_groups
    #options_presets = option_presets
