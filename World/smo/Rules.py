from worlds.generic.Rules import set_rule
from .Options import SMOOptions
from .Logic import  total_moons

def set_rules(self, options : SMOOptions) -> None:

    # Prevents a softlock in Bowser from not having this moon in the moon list
    #self.multiworld.get_location("Big Broodal Battle" , self.player).place_locked_item(self.create_item("Bowser's Story Moon (3)"))


    # Cascade Story Progress
    #set_rule(self.multiworld.get_location("Multi Moon Atop the Falls", self.player), lambda state: state.has("Cascade Story Moon", self.player))

    if options.goal >= 4:
        # Sand Story Progress
        set_rule(self.multiworld.get_location("Moon Shards in the Sand", self.player), lambda state: state.has("Sand Story Moon (1)", self.player))
        set_rule(self.multiworld.get_location("Showdown on the Inverted Pyramid", self.player), lambda state: state.has("Sand Story Moon (2)", self.player))

    if options.goal > 5:
        # Wooded Story Progress
        set_rule(self.multiworld.get_location("Flower Thieves of Sky Garden", self.player), lambda state: state.has("Wooded Story Moon (1)", self.player))
        set_rule(self.multiworld.get_location("Path to the Secret Flower Field", self.player), lambda state: state.has("Wooded Story Moon (1)", self.player))
        set_rule(self.multiworld.get_location("Defend the Secret Flower Field!", self.player), lambda state: state.has("Wooded Story Moon (2)", self.player))

    if options.goal >= 9:
        # Metro Story Progress
        set_rule(self.multiworld.get_location("Powering Up the Station", self.player), lambda state: state.has("Metro Story Moon (1)", self.player) and state.has("Metro Story Moon (2)", self.player)
            and state.has("Metro Story Moon (3)", self.player) and state.has("Metro Story Moon (4)", self.player))
        set_rule(self.multiworld.get_location("A Traditional Festival!", self.player), lambda state: state.has("Metro Story Moon (5)", self.player) and state.has("Metro Story Moon (1)", self.player) and state.has("Metro Story Moon (2)", self.player)
            and state.has("Metro Story Moon (3)", self.player) and state.has("Metro Story Moon (4)", self.player))

    if options.goal >= 12:
        # Seaside Story Progress
        set_rule(self.multiworld.get_location("The Glass Is Half Full!", self.player), lambda state: state.has("Seaside Story Moon (1)", self.player) and
            state.has("Seaside Story Moon (2)", self.player) and state.has("Seaside Story Moon (3)", self.player) and state.has("Seaside Story Moon (4)", self.player))

        # Snow Story Progress
        set_rule(self.multiworld.get_location("The Bound Bowl Grand Prix", self.player), lambda state: state.has("Snow Story Moon (1)", self.player) and
            state.has("Snow Story Moon (2)", self.player) and state.has("Snow Story Moon (3)", self.player) and state.has("Snow Story Moon (4)", self.player))

        # Luncheon Story Progress
        set_rule(self.multiworld.get_location("Under the Cheese Rocks", self.player), lambda state: state.has("Luncheon Story Moon (1)", self.player))
        set_rule(self.multiworld.get_location("Big Pot on the Volcano: Dive In!", self.player), lambda state: state.has("Luncheon Story Moon (2)", self.player))

    if options.goal >= 15:
        # Bowser Story Progress
        set_rule(self.multiworld.get_location("Smart Bombing", self.player), lambda state: state.has("Bowser's Story Moon (1)", self.player))
        set_rule(self.multiworld.get_location("Big Broodal Battle", self.player), lambda state: state.has("Bowser's Story Moon (2)", self.player))
        set_rule(self.multiworld.get_location("Showdown at Bowser’s Castle", self.player), lambda state: state.has("Bowser's Story Moon (3)", self.player) and
            state.has("Bowser's Story Moon (2)", self.player) and  state.has("Bowser's Story Moon (1)", self.player))


    # Outfit Moons
    set_rule(self.multiworld.get_location("Caveman Cave-Fan", self.player), lambda state: state.has("Primitive Man Hat", self.player) and state.has("Primitive Man Clothes", self.player))
    set_rule(self.multiworld.get_location("Dancing with New Friends", self.player), lambda state: (state.has("Poncho Hat", self.player) and state.has("Poncho Clothes", self.player)) or state.has("Bone Clothes", self.player))
    set_rule(self.multiworld.get_location("I Feel Underdressed", self.player), lambda state: (state.has("Swimwear Hat", self.player) and state.has("Swimwear Clothes", self.player)) or state.has("Underwear", self.player))
    set_rule(self.multiworld.get_location("That Trendy “Pirate” Look", self.player), lambda state: state.has("Pirate Hat", self.player) and state.has("Pirate Clothes", self.player))
    set_rule(self.multiworld.get_location("Space Is “In” Right Now", self.player), lambda state: state.has("Space Suit Hat", self.player) and state.has("Space Suit Clothes", self.player))
    set_rule(self.multiworld.get_location("That “Old West” Style", self.player), lambda state: state.has("Gunman Hat", self.player) and state.has("Gunman Clothes", self.player))
    set_rule(self.multiworld.get_location("Exploring for Treasure", self.player), lambda state: state.has("Explorer Hat", self.player) and state.has("Explorer Clothes", self.player))
    set_rule(self.multiworld.get_location("Rewiring the Neighborhood", self.player), lambda state: state.has("Maker Hat", self.player) and state.has("Maker Clothes", self.player))
    set_rule(self.multiworld.get_location("Surprise Clown!", self.player), lambda state: state.has("Clown Hat", self.player) and state.has("Clown Clothes", self.player))
    set_rule(self.multiworld.get_location("A Relaxing Dance", self.player), lambda state: state.has("Aloha Hat", self.player) and state.has("Aloha Clothes", self.player))
    set_rule(self.multiworld.get_location("Moon Shards in the Cold Room", self.player), lambda state: state.has("Snow Suit Hat", self.player) and state.has("Snow Suit Clothes", self.player))
    set_rule(self.multiworld.get_location("Slip Behind the Ice", self.player), lambda state: state.has("Snow Suit Hat", self.player) and state.has("Snow Suit Clothes", self.player))
    set_rule(self.multiworld.get_location("I’m Not Cold!", self.player), lambda state: state.has("Underwear", self.player))
    set_rule(self.multiworld.get_location("A Strong Simmer", self.player), lambda state: state.has("Cook Hat", self.player) and state.has("Cook Clothes", self.player))
    set_rule(self.multiworld.get_location("An Extreme Simmer", self.player), lambda state: state.has("Cook Hat", self.player) and state.has("Cook Clothes", self.player))
    set_rule(self.multiworld.get_location("Mechanic: Repairs Complete!", self.player), lambda state: state.has("Mechanic Hat", self.player) and state.has("Mechanic Clothes", self.player))
    set_rule(self.multiworld.get_location("Scene of Crossing the Poison Swamp", self.player), lambda state: state.has("Armor Hat", self.player) and state.has("Armor Clothes", self.player))
    set_rule(self.multiworld.get_location("Taking Notes: In the Folding Screen", self.player), lambda state: state.has("Armor Hat", self.player) and state.has("Armor Clothes", self.player))
    set_rule(self.multiworld.get_location("Doctor in the House", self.player), lambda state: state.has("Doctor Hat", self.player) and state.has("Doctor Clothes", self.player))
    set_rule(self.multiworld.get_location("Totally Classic", self.player), lambda state: (state.has("64 Hat", self.player) and state.has("64 Clothes", self.player)) or (state.has("64 Metal Hat", self.player) and state.has("64 Metal Clothes", self.player)))
    set_rule(self.multiworld.get_location("Courtyard Chest Trap", self.player), lambda state: (state.has("64 Hat", self.player) and state.has("64 Clothes", self.player)) or (state.has("64 Metal Hat", self.player) and state.has("64 Metal Clothes", self.player)))

    # Post Game Outfits
    if options.goal > 15:
        set_rule(self.multiworld.get_location("Luigi Cap", self.player), lambda state: total_moons(self, state, self.player) >= 160)
        set_rule(self.multiworld.get_location("Luigi Clothes", self.player), lambda state: total_moons(self, state, self.player) >= 180)
        set_rule(self.multiworld.get_location("Doctor Cap", self.player), lambda state: total_moons(self, state, self.player) >= 220)
        set_rule(self.multiworld.get_location("Doctor Clothes", self.player), lambda state: total_moons(self, state, self.player) >= 240)
        set_rule(self.multiworld.get_location("Waluigi Cap", self.player), lambda state: total_moons(self, state, self.player) >= 260)
        set_rule(self.multiworld.get_location("Waluigi Clothes", self.player), lambda state: total_moons(self, state, self.player) >= 280)
        set_rule(self.multiworld.get_location("Diddy Kong Cap", self.player), lambda state: total_moons(self, state, self.player) >= 300)
        set_rule(self.multiworld.get_location("Diddy Kong Clothes", self.player), lambda state: total_moons(self, state, self.player) >= 320)
        set_rule(self.multiworld.get_location("Wario Cap", self.player), lambda state: total_moons(self, state, self.player) >= 340)
        set_rule(self.multiworld.get_location("Wario Clothes", self.player), lambda state: total_moons(self, state, self.player) >= 360)
        set_rule(self.multiworld.get_location("Hakama Clothes", self.player), lambda state: total_moons(self, state, self.player) >= 380)
        set_rule(self.multiworld.get_location("Koopa Cap", self.player), lambda state: total_moons(self, state, self.player) >= 420)
        set_rule(self.multiworld.get_location("Koopa Clothes", self.player), lambda state: total_moons(self, state, self.player) >= 440)
        set_rule(self.multiworld.get_location("Peach Cap", self.player), lambda state: total_moons(self, state, self.player) >= 460)
        set_rule(self.multiworld.get_location("Peach Clothes", self.player), lambda state: total_moons(self, state, self.player) >= 480)
        set_rule(self.multiworld.get_location("Gold Cap", self.player), lambda state: total_moons(self, state, self.player) >= 500)
        set_rule(self.multiworld.get_location("Gold Clothes", self.player), lambda state: total_moons(self, state, self.player) >= 520)
        set_rule(self.multiworld.get_location("64 Metal Cap", self.player), lambda state: total_moons(self, state, self.player) >= 540)
        set_rule(self.multiworld.get_location("64 Metal Clothes", self.player), lambda state: total_moons(self, state, self.player) >= 560)


    # Completion State
    if options.goal == "sand":
        self.multiworld.completion_condition[self.player] = lambda state: state.has("Sand Multi-Moon (2)", self.player)
        self.multiworld.get_location("The Hole in the Desert", self.player).place_locked_item(self.create_item("Sand Multi-Moon (2)"))
    if options.goal == "lake":
        self.multiworld.completion_condition[self.player] = lambda state: state.has("Lake Multi-Moon", self.player)
        self.multiworld.get_location("Broodals Over the Lake", self.player).place_locked_item(self.create_item("Lake Multi-Moon"))
    if options.goal == "metro":
        self.multiworld.completion_condition[self.player] = lambda state: state.has("Metro Multi-Moon (2)", self.player)
        self.multiworld.get_location("A Traditional Festival!", self.player).place_locked_item(self.create_item("Metro Multi-Moon (2)"))
    if options.goal == "luncheon":
        self.multiworld.completion_condition[self.player] = lambda state: state.has("Luncheon Multi-Moon (2)", self.player)
        self.multiworld.get_location("Cookatiel Showdown!", self.player).place_locked_item(self.create_item("Luncheon Multi-Moon (2)"))
    if options.goal == "moon":
        self.multiworld.completion_condition[self.player] = lambda state: state.has("Beat the Game", self.player)
        self.multiworld.get_location("Beat the Game", self.player).place_locked_item(self.create_item("Beat the Game"))
    if options.goal == "dark":
        self.multiworld.completion_condition[self.player] = lambda state: state.has("Dark Side Multi-Moon", self.player)
        self.multiworld.get_location("Arrival at Rabbit Ridge", self.player).place_locked_item(self.create_item("Dark Side Multi-Moon"))
    if options.goal == "darker":
        self.multiworld.completion_condition[self.player] = lambda state: state.has("Darker Side Multi-Moon", self.player)
        self.multiworld.get_location("A Long Journey's End!", self.player).place_locked_item(self.create_item("Darker Side Multi-Moon"))



# for debugging purposes, you may want to visualize the layout of your world. Uncomment the following code to
# write a PlantUML diagram to the file "my_world.puml" that can help you see whether your regions and locations
# are connected and placed as desired
# from Utils import visualize_regions
# visualize_regions(self.multiworld.get_region("Menu", self.player), "my_world.puml")
