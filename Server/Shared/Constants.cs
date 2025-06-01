using System.Reflection;
using System.Runtime.InteropServices;
using Shared.Packet;
using Shared.Packet.Packets;

namespace Shared;

public static class Constants {
    public const int CostumeNameSize = 0x20;
    public const int ItemNameSize = 0x80;
    public const int ChatMessageSize = 0x4B;
    public const int ObjectIdSize = 0x8;
    public const int WorldNameSize = 0x30;

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
