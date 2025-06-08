using System.Runtime.InteropServices;
using System.Text;

namespace Shared.Packet.Packets;

[Packet(PacketType.RegionalCollect)]
public struct RegionalCoinPacket : IPacket
{
    public string objId;
    public string worldName;


    public short Size => 0x52;

    public void Serialize(Span<byte> data)
    {
        Encoding.UTF8.GetBytes(objId).CopyTo(data[..Constants.ObjectIdSize]);
        Encoding.UTF8.GetBytes(worldName).CopyTo(data[Constants.ObjectIdSize..Constants.WorldNameSize]);
    }

    public void Deserialize(ReadOnlySpan<byte> data)
    {
        int offset = 0;
        objId = Encoding.UTF8.GetString(data[..(offset += Constants.ObjectIdSize)]).TrimNullTerm();
        worldName = Encoding.UTF8.GetString(data[offset..]).TrimNullTerm();
    }

}