using System.Runtime.InteropServices;
using System.Text;

namespace Shared.Packet.Packets;

[Packet(PacketType.Filler)]
public struct FillerPacket : IPacket {
    public int itemType;

    public short Size => 6;

    public void Serialize(Span<byte> data)
    {
        MemoryMarshal.Write(data[..4], ref itemType);
    }

    public void Deserialize(ReadOnlySpan<byte> data)
    {
        itemType = MemoryMarshal.Read<int>(data[..4]);
    }

}