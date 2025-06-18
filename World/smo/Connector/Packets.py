from enum import Enum
from ctypes import c_short as short, c_ushort as ushort, c_int, c_byte as sbyte, c_ubyte as byte
from typing import Any

class PacketType(Enum):
    Unknown : short = 0
    Init : short = 1
    PlayerInfo : short = 2
    HackCapInfo : short = 3
    GameInfo : short = 4
    TagInfo : short = 5
    Connect : short = 6
    Disconnect : short = 7
    CostumeInfo : short = 8
    Shine : short = 9
    CaptureInfo : short = 10
    ChangeStage : short = 11
    Command : short = 12
    Item : short = 13
    Filler : short = 14
    ArchipelagoChat : short = 15
    SlotData : short = 16
    RegionalCollect : short = 18
    Deathlink : short = 19

class ConnectionType(Enum):
    Connect = 0
    Reconnect = 1

#region Check Packets

class ShinePacket:
    id : c_int
    SIZE : short = 4

    def __init__(self, packet_bytes : bytearray = None, shine_id : int = None):
        if packet_bytes:
            self.deserialize(packet_bytes)
        else:
            self.id = c_int(shine_id)

    def serialize(self) -> bytearray:
        data : bytearray = bytearray()
        int_value : int = self.id.value
        data += int_value.to_bytes(4, "little")
        if len(data) > self.SIZE:
            raise f"ShinePacket failed to serialize. bytearray exceeds maximum size {self.SIZE}."
        return data

    def deserialize(self, data : bytes | bytearray) -> None:
        if data is bytes:
            data = bytearray(data)
        self.id  = c_int(int.from_bytes(data[0:self.SIZE], "little"))


class ItemPacket:
    ITEM_NAME_SIZE : c_int = 0x80
    name : str
    item_type : c_int
    SIZE : short = 0x84

    def __init__(self, packet_bytes: bytearray = None, name: str = None, item_type: int = None) -> None:
        if packet_bytes:
            self.deserialize(packet_bytes)
        else:
            self.name = name
            self.item_type = c_int(item_type)

    def serialize(self) -> bytearray:
        data: bytearray = bytearray()
        data += self.name.encode()
        while len(data) < self.ITEM_NAME_SIZE:
            data += b"\x00"
        int_value : int = self.item_type.value
        data += int_value.to_bytes(4, "little")
        if len(data) > self.SIZE:
            raise f"ItemPacket failed to serialize. bytearray exceeds maximum size {self.SIZE}."
        return data

    def deserialize(self, data : bytes | bytearray) -> None:
        if data is bytes:
            data = bytearray(data)
        offset : int = 0
        self.name = data[offset:self.ITEM_NAME_SIZE].decode()
        self.name = self.name.replace("\0", "")
        offset += self.ITEM_NAME_SIZE
        self.item_type = c_int(int.from_bytes(data[offset:offset + 4], "little"))

class RegionalCoinPacket:
    OBJECT_ID_SIZE : c_int = 0x10
    STAGE_NAME_SIZE : c_int = 0x30
    object_id : str
    stage_name : str
    SIZE : short

    def serialize(self) -> bytearray:
        data: bytearray = bytearray()
        data += self.object_id.encode()
        while len(data) < self.OBJECT_ID_SIZE:
            data += b"\x00"
        data += self.stage_name.encode()
        while len(data) < self.STAGE_NAME_SIZE + self.OBJECT_ID_SIZE:
            data += b"\x00"
        if len(data) > self.SIZE:
            raise f"RegionalCoinPacket failed to serialize. bytearray exceeds maximum size {self.SIZE}."
        return data

    def deserialize(self, data : bytes | bytearray) -> None:
        if data is bytes:
            data = bytearray(data)
        offset : int = 0
        self.object_id = data[offset:self.OBJECT_ID_SIZE].decode()
        offset += self.OBJECT_ID_SIZE
        self.stage_name = data[offset:offset + self.STAGE_NAME_SIZE].decode()

class FillerPacket:
    item_type : c_int
    SIZE : short = 4

    def __init__(self, packet_bytes : bytearray = None, item_type : int = None):
        if packet_bytes:
            self.deserialize(packet_bytes)
        else:
            self.item_type = c_int(item_type - 9990)

    def serialize(self) -> bytearray:
        data: bytearray = bytearray()
        int_value : int = self.item_type.value
        data += int_value.to_bytes(4,"little")
        if len(data) > self.SIZE:
            raise f"FillerPacket failed to serialize. bytearray exceeds maximum size {self.SIZE}."
        return data

    def deserialize(self, data : bytes | bytearray) -> None:
        if data is bytes:
            data = bytearray(data)

#endregion

#region Server Packets

class ChatMessagePacket:
    MESSAGE_SIZE : int = 0x4B
    messages : list[str]
    SIZE : short = 0x4B * 3

    def __init__(self, messages : list[str]):
        self.messages = messages

    def serialize(self) -> bytearray:
        data : bytearray = bytearray()
        size : int = 0
        for index in range(len(self.messages)):
            for char in self.messages[index]:
                if size < self.MESSAGE_SIZE:
                    data += char.encode()
                else:
                    raise "Message too long exception"

            while len(data) < self.MESSAGE_SIZE * (index + 1):
                data += b"\x00"
        if len(data) > self.SIZE:
            raise f"ChatMessagePacket failed to serialize. bytearray exceeds maximum size {self.SIZE}."
        return data


    def deserialize(self, data : bytes | bytearray) -> None:
        if data is bytes:
            data = bytearray(data)

class SlotDataPacket:
    clash : ushort
    raid : ushort
    regionals : bool
    SIZE : short = 5

    def __init__(self, clash : int, raid : int, regionals : bool):
        self.clash = short(clash)
        self.raid = short(raid)
        self.regionals = regionals

    def serialize(self) -> bytearray:
        data : bytearray = bytearray()
        int_value : int = self.clash.value
        data += int_value.to_bytes(2, "little")
        int_value = self.raid.value
        data += int_value.to_bytes(2, "little")
        data += self.regionals.to_bytes(1, "little")
        if len(data) > self.SIZE:
            raise f"CountsPacket failed to serialize. bytearray exceeds maximum size {self.SIZE}."
        return data

    # Shouldn't be necessary
    def deserialize(self, data : bytes | bytearray) -> None:
        if data is bytes:
            data = bytearray(data)

#endregion

class ChangeStagePacket:
    ID_SIZE : int  = 0x10
    STAGE_SIZE : int = 0x30
    stage : str
    id : str
    scenario : sbyte
    sub_scenario_type : byte
    SIZE : short = 0x42

    def serialize(self) -> bytearray:
        data : bytearray = bytearray()
        data += self.stage.encode()
        while len(data) < self.STAGE_SIZE:
            data += b"\x00"
        data += self.id.encode()
        while len(data) < self.STAGE_SIZE + self.ID_SIZE:
            data += b"\x00"
        int_value : int =  self.scenario.value
        data += int_value.to_bytes(1, "little")
        int_value = self.sub_scenario_type.value
        data += int_value.to_bytes(1, "little")
        if len(data) > self.SIZE:
            raise f"ChangeStagePacket failed to serialize. bytearray exceeds maximum size {self.SIZE}."
        return data

    def deserialize(self, data : bytes | bytearray) -> None:
        if data is bytes:
            data = bytearray(data)
        offset : int = 0
        self.stage = data[offset:self.STAGE_SIZE].decode()
        offset += self.STAGE_SIZE
        self.id = data[offset:offset + self.ID_SIZE].decode()
        offset += self.ID_SIZE
        self.scenario = sbyte(int.from_bytes(data[offset:offset + 1], "little"))
        offset += 1
        self.sub_scenario_type = byte(int.from_bytes(data[offset:offset + 1], "little"))


class DeathLinkPacket:

    def serialize(self) -> bytearray:
        pass

    def deserialize(self, data : bytes | bytearray) -> None:
        if data is bytes:
            data = bytearray(data)

#region Connection Packets

class ConnectPacket:
    connection_type : ConnectionType

    def serialize(self) -> bytearray:
        pass

    def deserialize(self, data : bytes | bytearray) -> None:
        if data is bytes:
            data = bytearray(data)

class DisconnectPacket:
    # Empty Packet just to signal disconnect
    size : short = 0

class InitPacket:
    max_players : ushort = ushort(4)
    SIZE : short = 2

    def serialize(self) -> bytearray:
        data : bytearray = bytearray()
        as_integer : int = self.max_players.value
        data += as_integer.to_bytes(2, "little")
        if len(data) > self.SIZE:
            raise f"InitPacket failed to serialize. bytearray exceeds maximum size {self.SIZE}."
        return data


    def deserialize(self, data : bytes | bytearray) -> None:
        if data is bytes:
            data = bytearray(data)
        self.max_players = ushort(int.from_bytes(data[0:self.SIZE], "little"))

#endregion

class PacketHeader:
    GUID_SIZE : int = 16
    guid : bytes
    packet_type : PacketType
    packet_size : short
    SIZE : short = 16 + 4

    def __init__(self, header_bytes : bytearray = None , guid : bytes = None, packet_type : PacketType = PacketType.Init):
        if header_bytes:
            self.deserialize(header_bytes)
        else:
            self.guid = guid
            self.packet_type = packet_type

    def serialize(self) -> bytearray:
        data: bytearray = bytearray()
        data += self.guid
        while len(data) < self.GUID_SIZE:
            data += b"\x00"
        int_value: int = self.packet_type.value
        data += int_value.to_bytes(2, "little")
        int_value2 : int = self.packet_size
        data += int_value2.to_bytes(2, "little")
        if len(data) > self.SIZE:
            raise f"PacketHeader failed to serialize. bytearray exceeds maximum size {self.SIZE}."
        return data

    def deserialize(self, data : bytes | bytearray) -> None:
        if data is bytes:
            data = bytearray(data)
        offset = 0
        self.guid = bytes(data[offset:self.GUID_SIZE])
        offset += self.GUID_SIZE
        self.packet_type = PacketType(int.from_bytes(data[offset:offset + 2], "little"))
        offset += 2
        self.packet_size = short(int.from_bytes(data[offset:offset + 2], "little"))

class Packet:
    header : PacketHeader
    packet : Any

    def __init__(self, header_bytes : bytearray = None, guid : bytes = None, packet_type : PacketType = PacketType.Connect, packet_data : list = None):
        if header_bytes:
            self.header = PacketHeader(header_bytes=header_bytes)
        else:
            self.header = PacketHeader(guid=guid, packet_type=packet_type)
            match packet_type:
                case PacketType.Init:
                    self.packet = InitPacket()
                case PacketType.ChangeStage:
                    self.packet = ChangeStagePacket()
                case PacketType.SlotData:
                    self.packet = SlotDataPacket(clash=packet_data[0], raid=packet_data[1], regionals=packet_data[2])
                case PacketType.ArchipelagoChat:
                    self.packet = ChatMessagePacket(messages=packet_data[0])
                case PacketType.Deathlink:
                    self.packet = DeathLinkPacket()
                case PacketType.Shine:
                    self.packet = ShinePacket(shine_id=packet_data[0])
                case PacketType.Item:
                    self.packet = ItemPacket(name=packet_data[0], item_type=packet_data[1])
                case PacketType.RegionalCollect:
                    self.packet = RegionalCoinPacket()
                case PacketType.Filler:
                    self.packet = FillerPacket(packet_data[0])

    def serialize(self) -> bytearray:
        self.header.packet_size = self.packet.SIZE
        data : bytearray = bytearray()
        data += self.header.serialize()
        data += self.packet.serialize()
        return data

    def deserialize(self, data : bytes | bytearray) -> None:
        match self.header.packet_type:
            case PacketType.Connect:
                self.packet = ConnectPacket()
            case PacketType.ChangeStage:
                self.packet = ChangeStagePacket()
            # case PacketType.Command:
            #     self.packet = CommandP()
            case PacketType.Shine:
                self.packet = ShinePacket(packet_bytes=data)
            case PacketType.Item:
                self.packet = ItemPacket(packet_bytes=data)
            case PacketType.Filler:
                self.packet = FillerPacket(packet_bytes=data)
            case PacketType.RegionalCollect:
                self.packet = RegionalCoinPacket()
            case PacketType.ArchipelagoChat:
                raise "Server only packet received from client"
            case PacketType.SlotData:
                raise "Server only packet received from client"
            case PacketType.Deathlink:
                self.packet = DeathLinkPacket()
