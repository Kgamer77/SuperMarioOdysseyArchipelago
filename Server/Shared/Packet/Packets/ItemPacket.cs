using System.Runtime.InteropServices;
using System.Text;

namespace Shared.Packet.Packets;

[Packet(PacketType.Item)]
public struct ItemPacket : IPacket {
    public string name;
    public int itemType;


    public short Size => 0x86;

    public void Serialize(Span<byte> data)
    {
        Encoding.UTF8.GetBytes(name).CopyTo(data[..Constants.ItemNameSize]);
        MemoryMarshal.Write(data[Constants.ItemNameSize..], ref itemType);
    }

    public void Deserialize(ReadOnlySpan<byte> data)
    {
        int offset = 0;
        name = Encoding.UTF8.GetString(data[..(offset += Constants.ItemNameSize)]).TrimNullTerm();
        itemType = MemoryMarshal.Read<int>(data[offset..(offset+4)]);
    }

}