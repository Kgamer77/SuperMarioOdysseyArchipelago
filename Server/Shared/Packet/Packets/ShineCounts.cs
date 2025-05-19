using System.Runtime.InteropServices;
using System.Text;

namespace Shared.Packet.Packets;

[Packet(PacketType.ShineCounts)]
public struct ShineCounts : IPacket {
    public ushort clash;
    public ushort raid;

    public short Size => 4 * 2;

    public void Serialize(Span<byte> data)
    {
        MemoryMarshal.Write(data, ref clash);
        MemoryMarshal.Write(data[4..], ref raid);
    }

    public void Deserialize(ReadOnlySpan<byte> data)
    {
        raid = MemoryMarshal.Read<ushort>(data);
        clash = MemoryMarshal.Read<ushort>(data[4..]);
    }
}