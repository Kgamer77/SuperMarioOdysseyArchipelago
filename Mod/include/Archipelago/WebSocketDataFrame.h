#pragma once

#include "Archipelago/ArchipelagoSocket.h"
#include "types.h"
#include <vector>

class WebSocketDataFrame {
public:
    enum WebSocketHeaderSizes
    {
        MinimumHeader = 2,
        ExtendedSize16 = 2,
        ExtendedSize64 = 8,
        MaskingKeySize = 4
    };

    enum WebSocketFragment
    {
        MORE = 0,
        FINAL = 1,
        FAIL = 2
    };

    // Possibly add enum for RSV1 permessage-deflate-compression extension

    enum WebSocketPerMessageDeflate
    {
        DeflateExtensionDisabled = 0,
        DeflateExtensionEnabled = 1
    };

    enum WebSocketOpcode
    {
        CONTINUATION = 0x0,
        TEXT = 0x1,
        BINARY = 0x2,
        RESERVEDFUTURE0x3 = 0x3,
        RESERVEDFUTURE0x4 = 0x4,
        RESERVEDFUTURE0x5 = 0x5,
        RESERVEDFUTURE0x6 = 0x6,
        RESERVEDFUTURE0x7 = 0x7,
        CLOSE = 0x8,
        PING = 0x9,
        PONG = 0xA,
        RESERVEDFUTURE0xB = 0xB,
        RESERVEDFUTURE0xC = 0xC,
        RESERVEDFUTURE0xD = 0xD,
        RESERVEDFUTURE0xE = 0xE,
        RESERVEDFUTURE0xF = 0xF
    };

    // Masking Key is completely ommitted from packet if Unmasked
    enum WebSocketMask
    {
        // server-to-client
        UNMASKED = 0,
        // client-to-server
        MASKED = 1
    };

    // Extended portions of length are ommitted entirely
    // when length is less than or equal to 125
    enum WebSocketPayloadLength
    {
        NonExtended = 125,
        Extended16 = 126,
        Extended64 = 127
    };

    // default constructor
    WebSocketDataFrame(WebSocketFragment isFragment, WebSocketPerMessageDeflate isDeflate,
        WebSocketOpcode opcode, WebSocketMask isMasked, u64 length, DataFrameVector payload);

    // Convert text directly to a text dataframe
    WebSocketDataFrame(WebSocketFragment isFragment, WebSocketPerMessageDeflate isDeflate,
        WebSocketMask isMasked, u64 length, const char* payload);

    // Deserialize constructor
    WebSocketDataFrame(DataFrameVector frameData);

    bool isFail() { return mFragment == WebSocketFragment::FAIL; };
    bool isDeflate() { return mIsDeflate; };
    WebSocketOpcode getOpcode() { return mOpcode; };
    bool isMasked() { return mIsMask; };
    u64 getExtendedHeaderSize();
    u64 getPayloadSize() { return mPayloadLength; };
    DataFrameVector getPayload() { return mPayload; };
    const char* getPayloadCstr() { return reinterpret_cast<char*>(mPayload.data()); };

    void compressPayload();
    void decompressPayload();
    void maskPayload();
    u64 encodePayloadLength();

    DataFrameVector serialize();
    void deserializeHeader(DataFrameVector header);
    void deserializeExtendedHeader(DataFrameVector header);

    void deserializePayload(DataFrameVector payload);

private:
    WebSocketFragment mFragment;
    WebSocketPerMessageDeflate mIsDeflate;
    WebSocketOpcode mOpcode;
    WebSocketMask mIsMask;
    u64 mPayloadLength;
    u32 mMaskingKey;
    DataFrameVector mPayload;
};