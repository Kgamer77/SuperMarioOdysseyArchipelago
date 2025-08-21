from NetUtils import NetworkItem, NetworkPlayer
from .Data import moon_list, id_to_name, goals

class SMOPlayer:
    player : NetworkPlayer
    moons = {}
    MAX_MOONS = {
        "Cascade Story Moon": 1,
        "Sand Story Moon": 2,
        "Wooded Story Moon": 2,
        "Metro Story Moon": 5,
        "Seaside Story Moon": 4,
        "Snow Story Moon": 4,
        "Luncheon Story Moon": 3,
        "Bowser Story Moon": 3,
        "Cascade Multi-Moon": 2,
        "Sand Multi-Moon": 4,
        "Wooded Multi-Moon": 4,
        "Lake Multi-Moon": 1,
        "Metro Multi-Moon": 7,
        "Seaside Multi-Moon": 5,
        "Snow Multi-Moon": 5,
        "Luncheon Multi-Moon": 5,
        "Ruined Multi-Moon": 1,
        "Bowser Multi-Moon": 4,
        "Mushroom Multi-Moon": 5,
        "Dark Side Multi-Moon": 1,
        "Darker Side Multi-Moon": 1
    }
    messages : list[str] = []
    MAX_MESSAGE_SIZE = 0x42
    item_index : int = 0
    world_scenarios : dict = {
        "Cap": 1,
        "Cascade": 1,
        "Sand": 1,
        "Wooded": 1,
        "Lake": 1,
        "Cloud": 1,
        "Lost": 1,
        "Metro": 1,
        "Seaside": 1,
        "Snow": 1,
        "Luncheon": 1,
        "Ruined": 1,
        "Bowser": 1,
        "Moon": 1,
        "Mushroom": 1,
        "Dark": 1,
        "Darker": 1
    }
    goal : int

    def __init__(self):
        self.reset_moons()

    def reset_moons(self):
        self.moons = {
        "Cap Power Moon": 0,
        "Cascade Power Moon": 2,
        "Sand Power Moon": 4,
        "Wooded Power Moon": 4,
        "Lake Power Moon": 1,
        "Cloud Power Moon": 0,
        "Lost Power Moon": 0,
        "Metro Power Moon": 7,
        "Seaside Power Moon": 5,
        "Snow Power Moon": 5,
        "Luncheon Power Moon": 5,
        "Ruined Power Moon": 1,
        "Bowser Power Moon": 4,
        "Moon Power Moon": 0,
        "Power Star": 6,
        "Dark Side Power Moon": 1,
        "Cascade Story Moon": 0,
        "Sand Story Moon": 0,
        "Wooded Story Moon": 0,
        "Metro Story Moon": 0,
        "Seaside Story Moon": 0,
        "Snow Story Moon": 0,
        "Luncheon Story Moon": 0,
        "Bowser Story Moon": 0,
        "Cascade Multi-Moon": 1,
        "Sand Multi-Moon": 2,
        "Wooded Multi-Moon": 2,
        "Lake Multi-Moon": 0,
        "Metro Multi-Moon": 5,
        "Seaside Multi-Moon": 4,
        "Snow Multi-Moon": 4,
        "Luncheon Multi-Moon": 3,
        "Ruined Multi-Moon": 0,
        "Bowser Multi-Moon": 3,
        "Mushroom Multi-Moon": 0,
        "Dark Side Multi-Moon": 0,
        "Darker Side Multi-Moon": 0,
        "Beat the Game": -1
    }

    def get_next_moon(self, item : int) -> int:
        """
        Args:
            item: id of the respective Archipelago Item
        Returns:
            next moon id to send to SMO
        """
        item_name : str = id_to_name[item]


        if item_name in self.MAX_MOONS:
            if self.moons[item_name] >= self.MAX_MOONS[item_name]:
                return -1
        moon_id : int = moon_list["Mushroom" if item_name == "Power Star" else item_name.split(" ")[0]][self.moons[item_name]]
        self.moons[item_name] += 1
        return moon_id

    def add_message(self, message : str) -> None:
        """
        Adds message to the player's messages list automatically subdividing the message to fit in a single ChatMessagePacket.
        Args:
            message: The message to add. Must be UTF-8 compatible.
        """
        try:
            message.encode()
        except UnicodeEncodeError:
            raise f"The message ({message}) cannot be UTF-8 encoded."

        if len(message) <= self.MAX_MESSAGE_SIZE:
            self.messages.append(message)
        else:
            message_parts = message.split()
            current_message : str = message_parts.pop(0)
            while len(message_parts) > 0:
                if len(message_parts[0]) + len(current_message) < self.MAX_MESSAGE_SIZE:
                    current_message += f" {message_parts.pop(0)}"
                else:
                    if len(current_message.replace("\t", "")) > 0:
                        self.messages.append(current_message)
                        current_message = "\t"
                    if len(message_parts[0]) > self.MAX_MESSAGE_SIZE:
                        cut_part = message_parts[0][0:self.MAX_MESSAGE_SIZE - 2] + "-"
                        current_message += cut_part
                        message_parts[0] = message_parts[0][self.MAX_MESSAGE_SIZE - 2:]

            self.messages.append(current_message)

    def next_messages(self) -> list[str]:
        match len(self.messages):
            case 0:
                return ["", "", ""]
            case 1:
                return ["", "", self.messages.pop(0)]
            case 2:
                return ["", self.messages.pop(0), self.messages[0]]
            case _:
                return [self.messages.pop(0), self.messages[0], self.messages[1]]

    def check_goal(self, item : NetworkItem) -> bool:
        if goals[self.goal].item == item.item:
            if goals[self.goal].item_index == self.moons[id_to_name[item.item]]:
                return True
        return False

    def get_scenario_dict(self) -> dict:
        return self.world_scenarios