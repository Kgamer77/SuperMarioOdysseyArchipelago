using System.Runtime.InteropServices;
using System.Text;

namespace Shared.Packet.Packets;

[Packet(PacketType.UnlockWorld)]
public struct UnlockWorld : IPacket {
    public int worldID;

    public short Size => 4;

    public void Serialize(Span<byte> data)
    {
        MemoryMarshal.Write(data, ref worldID);
    }

    public void Deserialize(ReadOnlySpan<byte> data)
    {
        worldID = MemoryMarshal.Read<int>(data);
    }
}