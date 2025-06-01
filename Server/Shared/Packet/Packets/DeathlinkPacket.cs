using System.Runtime.InteropServices;
using System.Text;

namespace Shared.Packet.Packets;

[Packet(PacketType.Deathlink)]
public struct DeathlinkPacket : IPacket
{

    public short Size => 0x2;

    public void Serialize(Span<byte> data)
    {
    }

    public void Deserialize(ReadOnlySpan<byte> data)
    {
    }

}