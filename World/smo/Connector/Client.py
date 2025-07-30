import asyncio
import functools
import typing
from copy import deepcopy

import Utils
from NetUtils import decode, encode, NetworkPlayer, NetworkItem, JSONtoTextParser, JSONMessagePart, ClientStatus
from MultiServer import Endpoint
from CommonClient import get_base_parser, gui_enabled, logger, CommonContext, ClientCommandProcessor
from typing import List, Any, Iterable
from .Packets import PacketHeader, PacketType, Packet

from .Data import inverse_shop_items, shop_items, get_item_type, worlds, world_alias, valid_warps, inverse_worlds, \
    multi_moon_locations
from .Player import SMOPlayer

from websockets import WebSocketServerProtocol

message_types = [
    "ItemSend",
    "Hint",
    "Join",
    "Part",
    "Chat"
]

class SMOJSONToTextParser(JSONtoTextParser):
    def _handle_color(self, node: JSONMessagePart):
        return self._handle_text(node)  # No colors for the in-game text

# Add Debug Commands like send_to and shine, etc
class SMOCommandProcessor(ClientCommandProcessor):
    def _cmd_smo(self):
        """Check SMO Connection State"""
        if isinstance(self.ctx, SMOContext):
            logger.info(f"SMO Status: {self.ctx.get_smo_status()}")

    def _cmd_sync(self):
        """Attempt to resync received items"""
        if isinstance(self.ctx, SMOContext):
            logger.info(f"SMO Status: Syncing")
            self.ctx.player_data.item_index = 0
            self.ctx.server_msgs.append({"cmd" : "Sync"})
            # Add the sending locations part here if necessary.

    def _cmd_warp(self, kingdom : str, scenario : int = -1):
        """Warp Mario to another kingdom.
        Change kingdom scenario only if you know what you're doing as it can break the game."""
        if isinstance(self.ctx, SMOContext):
            while kingdom.lower() in world_alias:
                kingdom = world_alias[kingdom]
            if not kingdom in valid_warps:
                warps : str = ""
                for value in valid_warps.values():
                    warps += value + ", "
                warps = warps.removesuffix(", ")
                logger.info(f"{kingdom} is not a valid warp.\nValid warps: {warps}")
            else:
                logger.info(f"Sending Mario to {valid_warps[kingdom]}")
                self.ctx.player_data.add_message(f"Sending Mario to {valid_warps[kingdom]}")
                self.ctx.proxy_msgs.append(Packet(guid=self.ctx.proxy_guid, packet_type=PacketType.ChangeStage,
                                                 packet_data=[kingdom, scenario]))

# Change send message and related calls to send packet and serialize and deserialize using the packet of the respective packet type.
# Make sure to receive packets on the connection to send checks through to the AP Server from this client.

class SMOContext(CommonContext):
    command_processor = SMOCommandProcessor
    game = "Super Mario Odyssey"

    def __init__(self, server_address, password):
        super().__init__(server_address, password)
        self.proxy : asyncio.Server
        self.proxy_chat = None
        self.gamejsontotext = SMOJSONToTextParser(self)
        self.autoreconnect_task = None
        self.endpoint = None
        self.items_handling = 0b111
        self.room_info = None
        self.connected_msg = None
        self.game_connected : bool = False
        self.awaiting_info : bool = False
        self.full_inventory: List[Any] = []
        self.server_msgs: List[Any] = []
        self.server_comm_task = None
        self.proxy_msgs : List[Packet] = []
        self.proxy_guid : bytes = bytes()
        self.player_data : SMOPlayer = SMOPlayer()
        self.slot_data : dict = {}
        self.ping_task = None
        self.awaiting_connection : bool = False
        self.disconnect_timer : int = 120
        self.logged_in : bool = False
        self.multi_moon_anim : bool = False
        self.death_link_enabled : bool = False

    async def server_auth(self, password_requested: bool = False):
        if password_requested and not self.password:
            await super(SMOContext, self).server_auth(password_requested)

        await self.get_username()
        await self.send_connect()

    def get_smo_status(self) -> str:
        if not self.game_connected:
            return "Not connected to Super Mario Odyssey"

        return "Connected to Super Mario Odyssey"

    async def disconnect(self, allow_autoreconnect: bool = False):
        await super().disconnect(allow_autoreconnect)

    async def disconnect_proxy(self):
        if self.endpoint and not self.endpoint.socket.closed:
            await self.endpoint.socket.close()

    def is_connected(self) -> bool:
        return self.server and self.server.socket.open

    # Handle APChatMessage here
    def on_print_json(self, args: dict):
        text = self.gamejsontotext(deepcopy(args["data"]))
        if "type" in args and args["type"] in message_types:
            self.player_data.add_message(text)

        if self.ui:
            self.ui.print_json(args["data"])
        else:
            text = self.jsontotextparser(args["data"])
            logger.info(text)

    def update_items(self):
        # just to be safe - we might still have an inventory from a different room
        if not self.is_connected():
            return

        self.server_msgs.append({"cmd": "ReceivedItems", "index": 0, "items": self.full_inventory})

    def on_deathlink(self, data: typing.Dict[str, typing.Any]) -> None:
        if self.death_link_enabled:
            super().on_deathlink(data)
            death_link_packet : Packet = Packet(guid=self.proxy_guid, packet_type=PacketType.Deathlink)
            self.proxy_msgs.append(death_link_packet)
            self.last_death_link = data["time"]

    # Handle sending packets to SMO here
    def on_package(self, cmd: str, args: dict):
        match cmd:
            case "Connected":
                json = args
                me: NetworkPlayer
                if "slot_info" in json.keys():
                    json["slot_info"] = {}
                if "players" in json.keys():

                    for n in json["players"]:
                        if n.slot == json["slot"] and n.team == json["team"]:
                            me = n
                            break

                    # Only put our player info in there as we actually need it
                    json["players"] = [me]
                    self.slot_data = json["slot_data"]
                self.player_data.player = me
                self.player_data.add_message(f"Connected to Archipelago as {me.name} playing Super Mario Odyssey")
                # Send slot data to SMO
                self.proxy_msgs.append(Packet(guid=self.proxy_guid, packet_type=PacketType.SlotData,
                                                  packet_data=[self.slot_data["clash"], self.slot_data["raid"], self.slot_data["regionals"]]))
                self.player_data.goal = self.slot_data["goal"]
                self.death_link_enabled = self.slot_data["death_link"]
                self.logged_in = True

                if self.death_link_enabled:
                    self.server_msgs.append({"cmd" : "ConnectUpdate", "tags" : ["AP", "DeathLink"]})
                self.server_msgs.append({"cmd" : "Get", "keys" : [f"{self.player_data.player.name}_scenarios"]})
                # if DEBUG:
                #     print(json)
                self.connected_msg = encode([json])
                if self.awaiting_info:
                    self.server_msgs.append(self.room_info)
                    self.update_items()
                    self.awaiting_info = False

            case "RoomUpdate":
                # Same story as above
                json = args
                if "players" in json.keys():
                    json["players"] = []

                self.server_msgs.append(json)

            case "ReceivedItems":
                # Handle Sending various collect packets to SMO here
                if args["index"] == 0:
                    self.full_inventory.clear()
                    # not sure if this is needed?
                    self.player_data.reset_moons()
                    self.player_data.item_index = 0
                    print("Accept full inventory.")

                if args["index"] != self.player_data.item_index:
                    print("Next index mismatch, syncing.")
                    self.server_msgs.append({"cmd" : "Sync"})
                else:
                    self.player_data.item_index += 1

                for item in args["items"]:
                    net_item = NetworkItem(*item)
                    self.full_inventory.append(net_item)

                    if self.player_data.check_goal(net_item):
                        self.server_msgs.append({"cmd" : "StatusUpdate", "status" : ClientStatus.CLIENT_GOAL})
                        print("Goal achieved")

                    packet = None
                    match get_item_type(net_item.item):
                        # Flag items
                        case 0:
                            pass

                        # Moons
                        case -1:
                            packet = Packet(guid=self.proxy_guid, packet_type=PacketType.Shine,
                                            packet_data=[self.player_data.get_next_moon(net_item.item)])
                        # Regional Coins
                        case -2:
                            pass
                        # Filler
                        case -3:
                            packet = Packet(guid=self.proxy_guid, packet_type=PacketType.Filler,
                                    packet_data=[net_item.item - 9990])

                        case _:
                            packet = Packet(guid=self.proxy_guid, packet_type=PacketType.Item,
                                            packet_data=[inverse_shop_items[net_item.item].replace("Cap", "").replace("Clothes", ""), get_item_type(net_item.item)])
                    if packet:
                        self.proxy_msgs.append(packet)

                self.server_msgs.append(args)

            case "RoomInfo":
                self.seed_name = args["seed_name"]
                self.room_info = args

            case "Retrieved":
                if f"{self.player_data.player.name}_scenarios" in args["keys"] and args["keys"][f"{self.player_data.player.name}_scenarios"] is dict:
                    self.player_data.world_scenarios = args["keys"][f"{self.player_data.player.name}_scenarios"]
                    for world in self.player_data.world_scenarios.keys():
                        self.proxy_msgs.append(Packet(guid=self.proxy_guid, packet_type=PacketType.Progress,
                                                      packet_data=[inverse_worlds[world], self.player_data.world_scenarios[world]]))
                else:
                    self.server_msgs.append({"cmd" : "Set", "key" : f"{self.player_data.player.name}_scenarios",
                                            "operations" : [{ "operation" : "replace", "value" : self.player_data.world_scenarios}]})

            case _:
                if cmd != "PrintJSON":
                    self.server_msgs.append(args)

    def run_gui(self):
        from kvui import GameManager

        class SMOManager(GameManager):
            logging_pairs = [
                ("Client", "Archipelago")
            ]
            base_title = "Archipelago Super Mario Odyssey Client"

        self.ui = SMOManager(self)
        self.ui_task = asyncio.create_task(self.ui.async_run(), name="UI")

async def ping_loop(ctx : SMOContext):
    while not ctx.exit_event.is_set():
        if ctx.endpoint:
            if ctx.disconnect_timer == 0:
                ctx.game_connected = False
            ctx.disconnect_timer -= 1
        await asyncio.sleep(1.0)



async def proxy_chat(ctx : SMOContext):
    try:
        clear_msgs : bool = False
        while not ctx.exit_event.is_set():
            if (len(ctx.player_data.messages) > 0 or clear_msgs) and ctx.game_connected:
                msg_packet : Packet = Packet(guid=ctx.proxy_guid, packet_type=PacketType.ArchipelagoChat,
                                             packet_data=[ctx.player_data.next_messages()])
                ctx.proxy_msgs.append(msg_packet)
                if len(ctx.player_data.messages) == 0 and not clear_msgs:
                    clear_msgs = True
                else:
                    clear_msgs = False
            if ctx.multi_moon_anim:
                await asyncio.sleep(27.0)
            else:
                await asyncio.sleep(5.0)
    except Exception as e:
        logger.exception(e)


async def handle_proxy(reader : asyncio.StreamReader, writer : asyncio.StreamWriter, ctx : SMOContext) -> None:
    data : bytearray
    packet : Packet
    ctx.endpoint = Endpoint(writer.transport.get_extra_info("socket"))
    ctx.awaiting_connection = True
    while True:
        data : bytearray = bytearray(await reader.read(PacketHeader.SIZE))
        packet = Packet(header_bytes=data)
        if not ctx.proxy_guid:
            ctx.proxy_guid = packet.header.guid
        packet_size : int = packet.header.packet_size.value
        data = bytearray(await reader.read(packet_size))
        packet.deserialize(data)
        if packet.header.packet_type != PacketType.Unknown:
            ctx.disconnect_timer = 30
        if ctx.proxy_guid and ctx.awaiting_connection:
            ctx.game_connected = True
            ctx.awaiting_connection = False
            print("SMO Connected")
        match packet.header.packet_type:
            case PacketType.Connect:
                if ctx.proxy_guid != packet.header.guid:
                    ctx.proxy_guid = packet.header.guid
                init_packet = Packet(guid=ctx.proxy_guid, packet_type=PacketType.Init)
                # Insert init packet at 0 in queue so other packets added before aren't dropped.
                ctx.proxy_msgs.insert(0, init_packet)

            case PacketType.Disconnect:
                ctx.game_connected = False
                break
            case PacketType.Shine:
                shine_id : int = packet.packet.id.value
                print(f"Got {shine_id}")
                ctx.server_msgs.append({"cmd": "LocationChecks", "locations" : [shine_id]})
                if shine_id in multi_moon_locations:
                    ctx.multi_moon_anim = True
            case PacketType.Item:
                item_type : int = packet.packet.item_type.value
                item_name = packet.packet.name
                item_name += "Cap" if item_type == 1 else "Clothes" if item_type == 0 else ""
                print(f"Got {item_name}")
                item_id: int = shop_items[item_name]
                ctx.server_msgs.append({"cmd": "LocationChecks", "locations": [item_id]})
            case PacketType.RegionalCollect:
                pass
                # shine_id: int = packet.packet.id.value
            case PacketType.Progress:
                ctx.player_data.world_scenarios[inverse_worlds[packet.packet.world_id]] = packet.packet.scenario
                ctx.server_msgs.append({"cmd": "Set", "operations": [{"operation" : "replace", "value" :
                                                                     ctx.player_data.world_scenarios}]})

            case PacketType.Deathlink:
                if ctx.death_link_enabled:
                    await ctx.send_death()

        if len(ctx.proxy_msgs) > 0 and ctx.game_connected:
            for i in range(len(ctx.proxy_msgs)):
                response : Packet = ctx.proxy_msgs.pop(0)
                b = response.serialize()
                writer.write(b)
                await writer.drain()
        if not ctx.game_connected and not ctx.awaiting_connection:
            break
    print("SMO Disconnected")
    ctx.player_data.item_index = 0
    ctx.awaiting_connection = True
    writer.close()


async def comm_loop(ctx : SMOContext):
    while not ctx.exit_event.is_set():
        if not ctx.is_connected():
            ctx.logged_in = False
        if len(ctx.server_msgs) > 0 and ctx.logged_in:
            await ctx.send_msgs(ctx.server_msgs)
            ctx.server_msgs.clear()
        await asyncio.sleep(0.1)


def launch(*launch_args: str):
    async def main():
        parser = get_base_parser()
        args = parser.parse_args(launch_args)

        ctx = SMOContext(args.connect, args.password)
        logger.info("Starting Super Mario Odyssey proxy server")

        ctx.proxy = asyncio.start_server(functools.partial(handle_proxy, ctx=ctx), "0.0.0.0", 1027)
        ctx.proxy_chat = asyncio.create_task(proxy_chat(ctx) , name="ChatLoop")
        ctx.ping_task = asyncio.create_task(ping_loop(ctx), name="PingLoop")
        ctx.server_comm_task = asyncio.create_task(comm_loop(ctx), name="CommLoop")

        if gui_enabled:
            ctx.run_gui()
        ctx.run_cli()

        await ctx.proxy
        await ctx.proxy_chat
        await ctx.ping_task
        await ctx.server_comm_task
        # Make ping task wait 1-second intervals
        # Add counter member to ctx
        # if packet of any kind is read from stream,
        # reset counter to 5
        # if counter is 0 when ping task runs,
        # send ping packet to SMO reset counter, set awaiting_ping member of ctx true
        # when ping packet received, set awaiting_ping to false
        # if counter reaches 0 and awaiting_ping is true,
        # set game_connected to false.
        # test without ping packet to see if enough packets
        # saturate the stream to keep connection open with Mario idle
        # if not then implement the above.

        await ctx.exit_event.wait()


    Utils.init_logging("SMOClient")
    # options = Utils.get_options()

    import colorama
    colorama.just_fix_windows_console()
    asyncio.run(main())
    colorama.deinit()