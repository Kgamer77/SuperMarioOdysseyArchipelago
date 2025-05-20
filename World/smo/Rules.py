from worlds.generic.Rules import set_rule
from .Options import SMOOptions
from .Logic import  total_moons

def set_rules(self, options : SMOOptions) -> None:
    """ Sets the placement rules for Super Mario Odyssey.
        Args:
            self: SMOWorld object for this player's world.
            options: The options from this player's yaml.
    """

    # Prevents a softlock in Bowser from not having this moon in the moon list
    #self.multiworld.get_location("Big Broodal Battle" , self.player).place_locked_item(self.create_item("Bowser's Story Moon"))


    # Cascade Story Progress
    #set_rule(self.multiworld.get_location("Multi Moon Atop the Falls", self.player), lambda state: state.has("Cascade Story Moon", self.player))

    # if options.goal >= 4:
    #     # Sand Story Progress
    #     set_rule(self.multiworld.get_location("Moon Shards in the Sand", self.player), lambda state: state.has("Sand Story Moon", self.player))
    #     set_rule(self.multiworld.get_location("Showdown on the Inverted Pyramid", self.player), lambda state: state.has("Sand Story Moon", self.player))
    #
    # if options.goal > 5:
    #     # Wooded Story Progress
    #     set_rule(self.multiworld.get_location("Flower Thieves of Sky Garden", self.player), lambda state: state.has("Wooded Story Moon", self.player))
    #     set_rule(self.multiworld.get_location("Path to the Secret Flower Field", self.player), lambda state: state.has("Wooded Story Moon", self.player))
    #     set_rule(self.multiworld.get_location("Defend the Secret Flower Field!", self.player), lambda state: state.has("Wooded Story Moon", self.player))
    #
    if options.goal >= 9:
        # Metro Story Progress
        set_rule(self.multiworld.get_location("Powering Up the Station", self.player), lambda state: state.count("Metro Story Moon", self.player) >= 4)
        set_rule(self.multiworld.get_location("A Traditional Festival!", self.player), lambda state: state.count("Metro Story Moon", self.player) >= 5)

    # if options.goal >= 12:
    #     # Seaside Story Progress
    #     set_rule(self.multiworld.get_location("The Glass Is Half Full!", self.player), lambda state: state.has("Seaside Story Moon", self.player) and
    #         state.has("Seaside Story Moon", self.player) and state.has("Seaside Story Moon", self.player) and state.has("Seaside Story Moon", self.player))
    #
    #     # Snow Story Progress
    #     set_rule(self.multiworld.get_location("The Bound Bowl Grand Prix", self.player), lambda state: state.has("Snow Story Moon", self.player) and
    #         state.has("Snow Story Moon", self.player) and state.has("Snow Story Moon", self.player) and state.has("Snow Story Moon", self.player))
    #
    #     # Luncheon Story Progress
    #     set_rule(self.multiworld.get_location("Under the Cheese Rocks", self.player), lambda state: state.has("Luncheon Story Moon", self.player))
    #     set_rule(self.multiworld.get_location("Big Pot on the Volcano: Dive In!", self.player), lambda state: state.has("Luncheon Story Moon", self.player))
    #
    # if options.goal >= 15:
    #     # Bowser Story Progress
    #     set_rule(self.multiworld.get_location("Smart Bombing", self.player), lambda state: state.has("Bowser's Story Moon", self.player))
    #     set_rule(self.multiworld.get_location("Big Broodal Battle", self.player), lambda state: state.has("Bowser's Story Moon", self.player))
    #     set_rule(self.multiworld.get_location("Showdown at Bowser's Castle", self.player), lambda state: state.has("Bowser's Story Moon", self.player) and
    #         state.has("Bowser's Story Moon", self.player) and  state.has("Bowser's Story Moon", self.player))


    # Outfit Moons
    set_rule(self.multiworld.get_location("Caveman Cave-Fan", self.player), lambda state: state.has("Primitive Man Cap", self.player) and state.has("Primitive Man Clothes", self.player))

    # if options.goal > 15 or (options.shops != 0 and options.shops != 3):
    set_rule(self.multiworld.get_location("Dancing with New Friends", self.player), lambda state: (state.has("Poncho Cap", self.player) and state.has("Poncho Clothes", self.player)) or state.has("Bone Clothes", self.player))
    # else:
    #     set_rule(self.multiworld.get_location("Dancing with New Friends", self.player), lambda state: state.has("Poncho Cap", self.player) and state.has("Poncho Clothes", self.player))

    set_rule(self.multiworld.get_location("I Feel Underdressed", self.player), lambda state: (state.has("Swimwear Cap", self.player) and state.has("Swimwear Clothes", self.player)) or state.has("Underwear", self.player))
    set_rule(self.multiworld.get_location("That Trendy “Pirate” Look", self.player), lambda state: state.has("Pirate Cap", self.player) and state.has("Pirate Clothes", self.player))
    set_rule(self.multiworld.get_location("Space Is “In” Right Now", self.player), lambda state: state.has("Space Suit Cap", self.player) and state.has("Space Suit Clothes", self.player))
    set_rule(self.multiworld.get_location("That “Old West” Style", self.player), lambda state: state.has("Gunman Cap", self.player) and state.has("Gunman Clothes", self.player))
    set_rule(self.multiworld.get_location("Exploring for Treasure", self.player), lambda state: state.has("Explorer Cap", self.player) and state.has("Explorer Clothes", self.player))
    set_rule(self.multiworld.get_location("Rewiring the Neighborhood", self.player), lambda state: state.has("Maker Cap", self.player) and state.has("Maker Clothes", self.player))
    set_rule(self.multiworld.get_location("Surprise Clown!", self.player), lambda state: state.has("Clown Cap", self.player) and state.has("Clown Clothes", self.player))
    set_rule(self.multiworld.get_location("A Relaxing Dance", self.player), lambda state: state.has("Aloha Cap", self.player) and state.has("Aloha Clothes", self.player))
    set_rule(self.multiworld.get_location("Moon Shards in the Cold Room", self.player), lambda state: state.has("Snow Suit Cap", self.player) and state.has("Snow Suit Clothes", self.player))
    set_rule(self.multiworld.get_location("Slip Behind the Ice", self.player), lambda state: state.has("Snow Suit Cap", self.player) and state.has("Snow Suit Clothes", self.player))
    set_rule(self.multiworld.get_location("I'm Not Cold!", self.player), lambda state: state.has("Underwear", self.player))
    set_rule(self.multiworld.get_location("A Strong Simmer", self.player), lambda state: state.has("Cook Cap", self.player) and state.has("Cook Clothes", self.player))
    set_rule(self.multiworld.get_location("An Extreme Simmer", self.player), lambda state: state.has("Cook Cap", self.player) and state.has("Cook Clothes", self.player))
    set_rule(self.multiworld.get_location("Mechanic: Repairs Complete!", self.player), lambda state: state.has("Mechanic Cap", self.player) and state.has("Mechanic Clothes", self.player))
    set_rule(self.multiworld.get_location("Scene of Crossing the Poison Swamp", self.player), lambda state: state.has("Armor Cap", self.player) and state.has("Armor Clothes", self.player))
    set_rule(self.multiworld.get_location("Taking Notes: In the Folding Screen", self.player), lambda state: state.has("Armor Cap", self.player) and state.has("Armor Clothes", self.player))

    set_rule(self.multiworld.get_location("Doctor in the House", self.player), lambda state: state.has("Doctor Cap", self.player) and state.has("Doctor Clothes", self.player))

    set_rule(self.multiworld.get_location("Totally Classic", self.player), lambda state: (state.has("64 Cap", self.player) and state.has("64 Clothes", self.player)) or (state.has("64 Metal Cap", self.player) and state.has("64 Metal Clothes", self.player)))
    set_rule(self.multiworld.get_location("Courtyard Chest Trap", self.player), lambda state: (state.has("64 Cap", self.player) and state.has("64 Clothes", self.player)) or (state.has("64 Metal Cap", self.player) and state.has("64 Metal Clothes", self.player)))

    # Post Game Outfits
    set_rule(self.multiworld.get_location("Luigi Cap", self.player), lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts["Luigi Cap"] or state.has("Luigi Cap", self.player))
    set_rule(self.multiworld.get_location("Luigi Clothes", self.player), lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts["Luigi Clothes"] or state.has("Luigi Clothes", self.player))
    set_rule(self.multiworld.get_location("Doctor Cap", self.player), lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts["Doctor Cap"] or state.has("Doctor Cap", self.player))
    set_rule(self.multiworld.get_location("Doctor Clothes", self.player), lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts["Doctor Clothes"] or state.has("Doctor Clothes", self.player))
    set_rule(self.multiworld.get_location("Waluigi Cap", self.player), lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts["Waluigi Cap"] or state.has("Waluigi Cap", self.player))
    set_rule(self.multiworld.get_location("Waluigi Clothes", self.player), lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts["Waluigi Clothes"] or state.has("Waluigi Clothes", self.player))
    set_rule(self.multiworld.get_location("Diddy Kong Cap", self.player), lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts["Diddy Kong Cap"] or state.has("Diddy Kong Cap", self.player))
    set_rule(self.multiworld.get_location("Diddy Kong Clothes", self.player), lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts["Diddy Kong Clothes"] or state.has("Diddy Kong Clothes", self.player))
    set_rule(self.multiworld.get_location("Wario Cap", self.player), lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts["Wario Cap"] or state.has("Wario Cap", self.player))
    set_rule(self.multiworld.get_location("Wario Clothes", self.player), lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts["Wario Clothes"] or state.has("Wario Clothes", self.player))
    set_rule(self.multiworld.get_location("Hakama Clothes", self.player), lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts["Hakama Clothes"] or state.has("Hakama Clothes", self.player))
    set_rule(self.multiworld.get_location("Koopa Cap", self.player), lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts["Koopa Cap"] or state.has("Koopa Cap", self.player))
    set_rule(self.multiworld.get_location("Koopa Clothes", self.player), lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts["Koopa Clothes"] or state.has("Koopa Clothes", self.player))
    set_rule(self.multiworld.get_location("Peach Cap", self.player), lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts["Peach Cap"] or state.has("Peach Cap", self.player))
    set_rule(self.multiworld.get_location("Peach Clothes", self.player), lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts["Peach Clothes"] or state.has("Peach Clothes", self.player))
    set_rule(self.multiworld.get_location("Gold Cap", self.player), lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts["Gold Cap"] or state.has("Gold Cap", self.player))
    set_rule(self.multiworld.get_location("Gold Clothes", self.player), lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts["Gold Clothes"] or state.has("Gold Clothes", self.player))
    set_rule(self.multiworld.get_location("64 Metal Cap", self.player), lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts["64 Metal Cap"] or state.has("64 Metal Cap", self.player))
    set_rule(self.multiworld.get_location("64 Metal Clothes", self.player), lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts["64 Metal Clothes"] or state.has("64 Metal Clothes", self.player))


    # Completion State
    if options.goal == "sand":
        self.multiworld.completion_condition[self.player] = lambda state: state.count("Sand Multi-Moon", self.player) >= 2
    if options.goal == "lake":
        self.multiworld.completion_condition[self.player] = lambda state: state.has("Lake Multi-Moon", self.player)
    if options.goal == "metro":
        self.multiworld.completion_condition[self.player] = lambda state: state.count("Metro Multi-Moon", self.player) >= 2
    if options.goal == "luncheon":
        self.multiworld.completion_condition[self.player] = lambda state: state.count("Luncheon Multi-Moon", self.player) >= 2
    if options.goal == "moon":
        self.multiworld.completion_condition[self.player] = lambda state: state.has("Beat the Game", self.player)
        self.multiworld.get_location("Beat the Game", self.player).place_locked_item(self.create_item("Beat the Game"))
    if options.goal == "dark":
        self.multiworld.completion_condition[self.player] = lambda state: state.has("Dark Side Multi-Moon", self.player)
    if options.goal == "darker":
        self.multiworld.completion_condition[self.player] = lambda state: state.has("Darker Side Multi-Moon", self.player)

    if options.story > 1:
        if options.goal == "sand":
            self.multiworld.get_location("The Hole in the Desert", self.player).place_locked_item(
                self.create_item("Sand Multi-Moon"))
        if options.goal == "lake":
            self.multiworld.get_location("Broodals Over the Lake", self.player).place_locked_item(
                self.create_item("Lake Multi-Moon"))
        if options.goal == "metro":
            self.multiworld.get_location("A Traditional Festival!", self.player).place_locked_item(
                self.create_item("Metro Multi-Moon"))
        if options.goal == "luncheon":
            self.multiworld.get_location("Cookatiel Showdown!", self.player).place_locked_item(
                self.create_item("Luncheon Multi-Moon"))
        if options.goal == "dark":
            self.multiworld.get_location("Arrival at Rabbit Ridge!", self.player).place_locked_item(
                self.create_item("Dark Side Multi-Moon"))
        if options.goal == "darker":
            self.multiworld.get_location("A Long Journey's End!", self.player).place_locked_item(self.create_item("Darker Side Multi-Moon"))



# for debugging purposes, you may want to visualize the layout of your world. Uncomment the following code to
# write a PlantUML diagram to the file "my_world.puml" that can help you see whether your regions and locations
# are connected and placed as desired
# from Utils import visualize_regions
# visualize_regions(self.multiworld.get_region("Menu", self.player), "my_world.puml")
