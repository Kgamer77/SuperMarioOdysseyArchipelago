﻿using System.Reflection;
using System.Runtime.InteropServices;
using Shared.Packet;
using Shared.Packet.Packets;

namespace Shared;

public static class Constants {
    public const int CostumeNameSize = 0x20;
    public const int ItemNameSize = 0x80;
    public const int ChatMessageSize = 0x4B;

    // dictionary of packet types to packet
    public static readonly Dictionary<Type, PacketAttribute> PacketMap = Assembly
        .GetExecutingAssembly()
        .GetTypes()
        .Where(type => type.IsAssignableTo(typeof(IPacket)) && type.GetCustomAttribute<PacketAttribute>() != null)
        .ToDictionary(type => type, type => type.GetCustomAttribute<PacketAttribute>()!);
    public static readonly Dictionary<PacketType, Type> PacketIdMap = Assembly
        .GetExecutingAssembly()
        .GetTypes()
        .Where(type => type.IsAssignableTo(typeof(IPacket)) && type.GetCustomAttribute<PacketAttribute>() != null)
        .ToDictionary(type => type.GetCustomAttribute<PacketAttribute>()!.Type, type => type);

    public static int HeaderSize { get; } = PacketHeader.StaticSize;
}
