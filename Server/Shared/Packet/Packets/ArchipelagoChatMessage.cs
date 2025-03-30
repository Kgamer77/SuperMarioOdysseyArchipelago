using System.Runtime.InteropServices;
using System.Text;

namespace Shared.Packet.Packets;

[Packet(PacketType.ArchipelagoChat)]
public struct ArchipelagoChatMessage : IPacket {
    public string message1;
    public string message2;
    public string message3;

    public short Size => Constants.ChatMessageSize * 3;

    public void Serialize(Span<byte> data) {
        Encoding.UTF8.GetBytes(message1).CopyTo(data[..Constants.ChatMessageSize]);
        Encoding.UTF8.GetBytes(message2).CopyTo(data[Constants.ChatMessageSize..(Constants.ChatMessageSize * 2)]);
        Encoding.UTF8.GetBytes(message3).CopyTo(data[(Constants.ChatMessageSize * 2)..Size]);
    }

    public void Deserialize(ReadOnlySpan<byte> data) {
        message1 = Encoding.UTF8.GetString(data[..Constants.ChatMessageSize]).TrimNullTerm();
        message2 = Encoding.UTF8.GetString(data[Constants.ChatMessageSize..(Constants.ChatMessageSize * 2)]).TrimNullTerm();
        message2 = Encoding.UTF8.GetString(data[(Constants.ChatMessageSize * 2)..]).TrimNullTerm();
    }
}