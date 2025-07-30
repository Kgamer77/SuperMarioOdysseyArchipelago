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
    set_rule(self.multiworld.get_location("Multi Moon Atop the Falls", self.player), lambda state: state.has("Cascade Story Moon", self.player))

    # if options.goal >= 4:
    #     # Sand Story Progress
    #     set_rule(self.multiworld.get_location("Moon Shards in the Sand", self.player), lambda state: state.has("Sand Story Moon", self.player))
    #     set_rule(self.multiworld.get_location("Showdown on the Inverted Pyramid", self.player), lambda state: state.has("Sand Story Moon", self.player))
    #
    # if options.goal > 5:
    #     # Wooded Story Progress
    #     set_rule(self.multiworld.get_location("Flower Thieves of Sky Garden", self.player), lambda state: state.has("Wooded Story Moon", self.player))
    #     set_rule(self.multiworld.get_location("Path to the Secret Flower Field", self.player), lambda state: state.count("Wooded Story Moon", self.player) >= 1)
    #     set_rule(self.multiworld.get_location("Defend the Secret Flower Field!", self.player), lambda state: state.count("Wooded Story Moon", self.player) >= 2)
    #
    # if options.goal >= 9:
    #     # Metro Story Progress
    #     set_rule(self.multiworld.get_location("Powering Up the Station", self.player), lambda state: state.count("Metro Story Moon", self.player) >= 4)
    #     set_rule(self.multiworld.get_location("A Traditional Festival!", self.player), lambda state: state.count("Metro Story Moon", self.player) >= 5)
    #
    # if options.goal >= 12:
    #     # Seaside Story Progress
    #     # set_rule(self.multiworld.get_location("The Glass Is Half Full!", self.player), lambda state: state.has("Seaside Story Moon", self.player) and
    #     #     state.has("Seaside Story Moon", self.player) and state.has("Seaside Story Moon", self.player) and state.has("Seaside Story Moon", self.player))
    #     #
    #     # # Snow Story Progress
    #     # set_rule(self.multiworld.get_location("The Bound Bowl Grand Prix", self.player), lambda state: state.has("Snow Story Moon", self.player) and
    #     #     state.has("Snow Story Moon", self.player) and state.has("Snow Story Moon", self.player) and state.has("Snow Story Moon", self.player))
    #
    #     # Luncheon Story Progress
    #     set_rule(self.multiworld.get_location("Under the Cheese Rocks", self.player), lambda state: state.has("Luncheon Story Moon", self.player))
    #     set_rule(self.multiworld.get_location("Big Pot on the Volcano: Dive In!", self.player), lambda state: state.count("Luncheon Story Moon", self.player) >= 2)
    #     set_rule(self.multiworld.get_location("Cookatiel Showdown!", self.player), lambda state: state.count("Luncheon Story Moon", self.player) >= 3)
    #
    # if options.goal >= 15:
    #     # Bowser Story Progress
    #     set_rule(self.multiworld.get_location("Smart Bombing", self.player), lambda state: state.has("Bowser Story Moon", self.player))
    #     set_rule(self.multiworld.get_location("Big Broodal Battle", self.player), lambda state: state.count("Bowser Story Moon", self.player) >= 2)
    #     set_rule(self.multiworld.get_location("Showdown at Bowser's Castle", self.player), lambda state: state.count("Bowser Story Moon", self.player) >= 3)


    # Outfit Moons
    if self.options.goal > 14:
        set_rule(self.multiworld.get_location("Caveman Cave-Fan", self.player),
                 lambda state: state.has("Caveman Headwear", self.player) and state.has("Caveman Outfit", self.player))
        set_rule(self.multiworld.get_location("That Trendy “Pirate” Look", self.player),
                 lambda state: state.has("Pirate Hat", self.player) and state.has("Pirate Outfit", self.player))
        set_rule(self.multiworld.get_location("Space Is “In” Right Now", self.player),
                 lambda state: state.has("Space Helmet", self.player) and state.has("Space Suit", self.player))
        set_rule(self.multiworld.get_location("That “Old West” Style", self.player),
                 lambda state: state.has("Cowboy Hat", self.player) and state.has("Cowboy Outfit", self.player))
        set_rule(self.multiworld.get_location("Mechanic: Repairs Complete!", self.player),
                 lambda state: state.has("Mechanic Cap", self.player) and state.has("Mechanic Outfit", self.player))
        set_rule(self.multiworld.get_location("Doctor in the House", self.player),
                 lambda state: state.has("Doctor Headwear", self.player) and state.has("Doctor Outfit", self.player))
        set_rule(self.multiworld.get_location("Totally Classic", self.player),
                 lambda state: (state.has("Mario 64 Cap", self.player) and state.has("Mario 64 Suit", self.player)) or (
                             state.has("Metal Mario Cap", self.player) and state.has("Metal Mario Clothes", self.player)))
        set_rule(self.multiworld.get_location("Courtyard Chest Trap", self.player),
                 lambda state: (state.has("Mario 64 Cap", self.player) and state.has("Mario 64 Suit", self.player)) or (
                             state.has("Metal Mario Cap", self.player) and state.has("Metal Mario Clothes", self.player)))
        set_rule(self.multiworld.get_location("Surprise Clown!", self.player),
                 lambda state: state.has("Clown Hat", self.player) and state.has("Clown Suit", self.player))

    # if options.goal > 15 or (options.shops != 0 and options.shops != 3):
    set_rule(self.multiworld.get_location("Dancing with New Friends", self.player), lambda state: (state.has("Sombrero", self.player) and state.has("Poncho", self.player)) or state.has("Skeleton Suit", self.player))
    if self.options.goal > 5:
        set_rule(self.multiworld.get_location("I Feel Underdressed", self.player), lambda state: (state.has(
            "Swim Goggles", self.player) and state.has("Swimwear", self.player)) or state.has("Boxer Shorts",
                                                                                                      self.player))
        set_rule(self.multiworld.get_location("Exploring for Treasure", self.player),
                 lambda state: state.has("Explorer Hat", self.player) and state.has("Explorer Outfit", self.player))


    if self.options.goal > 9:
        set_rule(self.multiworld.get_location("Rewiring the Neighborhood", self.player),
                 lambda state: state.has("Builder Helmet", self.player) and state.has("Builder Outfit", self.player))
        set_rule(self.multiworld.get_location("A Relaxing Dance", self.player),
                 lambda state: state.has("Resort Hat", self.player) and state.has("Resort Outfit", self.player))
        set_rule(self.multiworld.get_location("Moon Shards in the Cold Room", self.player),
                 lambda state: state.has("Snow Hood", self.player) and state.has("Snow Suit", self.player))
        set_rule(self.multiworld.get_location("Slip Behind the Ice", self.player),
                 lambda state: state.has("Snow Hood", self.player) and state.has("Snow Suit", self.player))
        set_rule(self.multiworld.get_location("I'm Not Cold!", self.player),
                 lambda state: state.has("Boxer Shorts", self.player))

    if self.options.goal > 12:
        set_rule(self.multiworld.get_location("A Strong Simmer", self.player),
                 lambda state: state.has("Chef Hat", self.player) and state.has("Chef Suit", self.player))
        set_rule(self.multiworld.get_location("An Extreme Simmer", self.player),
                 lambda state: state.has("Chef Hat", self.player) and state.has("Chef Suit", self.player))

    if self.options.goal > 14:
        set_rule(self.multiworld.get_location("Scene of Crossing the Poison Swamp", self.player),
                 lambda state: state.has("Samurai Helmet", self.player) and state.has("Samurai Armor", self.player))
        set_rule(self.multiworld.get_location("Taking Notes: In the Folding Screen", self.player),
                 lambda state: state.has("Samurai Helmet", self.player) and state.has("Samurai Armor", self.player))

    # Post Game Outfits
    if self.options.goal > 14:
        set_rule(self.multiworld.get_location("Luigi Cap", self.player),
                 lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts[
                     "Luigi Cap"] or state.has("Luigi Cap", self.player))
        set_rule(self.multiworld.get_location("Luigi Suit", self.player),
                 lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts[
                     "Luigi Suit"] or state.has("Luigi Suit", self.player))
        set_rule(self.multiworld.get_location("Doctor Headwear", self.player),
                 lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts[
                     "Doctor Headwear"] or state.has("Doctor Headwear", self.player))
        set_rule(self.multiworld.get_location("Doctor Outfit", self.player),
                 lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts[
                     "Doctor Outfit"] or state.has("Doctor Outfit", self.player))
        set_rule(self.multiworld.get_location("Waluigi Cap", self.player),
                 lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts[
                     "Waluigi Cap"] or state.has("Waluigi Cap", self.player))
        set_rule(self.multiworld.get_location("Waluigi Suit", self.player),
                 lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts[
                     "Waluigi Suit"] or state.has("Waluigi Suit", self.player))
        set_rule(self.multiworld.get_location("Diddy Kong Hat", self.player),
                 lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts[
                     "Diddy Kong Hat"] or state.has("Diddy Kong Hat", self.player))
        set_rule(self.multiworld.get_location("Diddy Kong Suit", self.player),
                 lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts[
                     "Diddy Kong Suit"] or state.has("Diddy Kong Suit", self.player))
        set_rule(self.multiworld.get_location("Wario Cap", self.player),
                 lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts[
                     "Wario Cap"] or state.has("Wario Cap", self.player))
        set_rule(self.multiworld.get_location("Wario Suit", self.player),
                 lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts[
                     "Wario Suit"] or state.has("Wario Suit", self.player))
        set_rule(self.multiworld.get_location("Hakama", self.player),
                 lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts[
                     "Hakama"] or state.has("Hakama", self.player))
        set_rule(self.multiworld.get_location("Bowser's Top Hat", self.player),
                 lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts[
                     "Bowser's Top Hat"] or state.has("Bowser's Top Hat", self.player))
        set_rule(self.multiworld.get_location("Bowser's Tuxedo", self.player),
                 lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts[
                     "Bowser's Tuxedo"] or state.has("Bowser's Tuxedo", self.player))
        set_rule(self.multiworld.get_location("Bridal Veil", self.player),
                 lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts[
                     "Bridal Veil"] or state.has("Bridal Veil", self.player))
        set_rule(self.multiworld.get_location("Bridal Gown", self.player),
                 lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts[
                     "Bridal Gown"] or state.has("Bridal Gown", self.player))
        set_rule(self.multiworld.get_location("Gold Mario Cap", self.player),
                 lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts[
                     "Gold Mario Cap"] or state.has("Gold Mario Cap", self.player))
        set_rule(self.multiworld.get_location("Gold Mario Suit", self.player),
                 lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts[
                     "Gold Mario Suit"] or state.has("Gold Mario Suit", self.player))
        set_rule(self.multiworld.get_location("Metal Mario Cap", self.player),
                 lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts[
                     "Metal Mario Cap"] or state.has("Metal Mario Cap", self.player))
        set_rule(self.multiworld.get_location("Metal Mario Suit", self.player),
                 lambda state: total_moons(self, state, self.player) >= self.outfit_moon_counts[
                     "Metal Mario Suit"] or state.has("Metal Mario Suit", self.player))

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

    # Place Goal moon at location
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
#     from Utils import visualize_regions
#     visualize_regions(self.multiworld.get_region("Menu", self.player), "my_world.puml")
