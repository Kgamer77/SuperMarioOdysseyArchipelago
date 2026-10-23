#include "Archipelago/WebSocketDataFrame.h"
#include <ctime>
#include <random>
#include <zlib.h>

WebSocketDataFrame::WebSocketDataFrame(WebSocketFragment isFragment, WebSocketPerMessageDeflate isDeflate,
    WebSocketOpcode opcode, WebSocketMask isMasked, u64 length, DataFrameVector payload) {

    mFragment = isFragment;
    mIsDeflate = isDeflate;
    mOpcode = opcode;
    mIsMask = isMasked;
    mPayloadLength = length;
    mPayload = payload;

    if (mIsDeflate == WebSocketPerMessageDeflate::DeflateExtensionEnabled)
        compressPayload();

    if (isMasked == WebSocketMask::MASKED) {
        std::srand(std::time(0));
        mMaskingKey = std::rand();
        maskPayload();
    }
}

WebSocketDataFrame::WebSocketDataFrame(WebSocketFragment isFragment, WebSocketPerMessageDeflate isDeflate,
    WebSocketMask isMasked, u64 length, const char* payload) {
    DataFrameVector payloadBytes(length);
    // Hnadle encountering nuyll char at some point
    for (size_t i = 0; i < length; i++) {
        payloadBytes.push_back(payload[i]);
    }
    WebSocketDataFrame(isFragment, isDeflate, WebSocketOpcode::TEXT, isMasked, length, payloadBytes);
}

WebSocketDataFrame::WebSocketDataFrame(DataFrameVector frameData) {
    deserializeHeader(frameData);
}

u64 WebSocketDataFrame::getExtendedHeaderSize() {
    u64 extendedSize = 0;
    if (mIsMask) {
        extendedSize += 4;
    }
    if (mPayloadLength > WebSocketPayloadLength::NonExtended) {
        extendedSize += 2;

        // 64 bit size
        if (mPayloadLength > WebSocketPayloadLength::Extended16) {
            extendedSize += 6;
        }
    }

    return extendedSize;
}

void WebSocketDataFrame::compressPayload() {
    uLong sourceLen = mPayloadLength;
    uLong destLen = compressBound(sourceLen);

    DataFrameVector compressedData(destLen);

    int result = compress(
        reinterpret_cast<Bytef*>(compressedData.data()), &destLen,
        reinterpret_cast<const Bytef*>(&mPayload), sourceLen);

    if (result != Z_OK) {
        mIsDeflate = WebSocketPerMessageDeflate::DeflateExtensionDisabled;
        mFragment = WebSocketFragment::FAIL;
        return; // Compression failed
    }

    compressedData.resize(destLen);

    mPayload = compressedData;
    mPayloadLength = compressedData.size();
}

void WebSocketDataFrame::decompressPayload() {
    uLong sourceLen = mPayloadLength;
    uLong destLen = 4096;

    DataFrameVector uncompressedData(destLen);

    int result = uncompress(
        reinterpret_cast<Bytef*>(uncompressedData.data()), &destLen,
        reinterpret_cast<const Bytef*>(&mPayload), sourceLen);

    if (result != Z_OK) {
        mIsDeflate = WebSocketPerMessageDeflate::DeflateExtensionDisabled;
        mFragment = WebSocketFragment::FAIL;
        return; // Compression failed
    }

    uncompressedData.resize(destLen);

    mPayload = uncompressedData;
    mPayloadLength = uncompressedData.size();
}

void WebSocketDataFrame::maskPayload() {
    // Split the masking key into bytes
    u8 maskBytes[4];
    for (size_t index = 0; index < 4; index++)
        maskBytes[index] = (mMaskingKey >> (24 - 8 * index)) & 0xFF;
    for (size_t i = 0; i < mPayloadLength; i++) {
        mPayload[i] = mPayload[i] ^ maskBytes[i % 4];
    }
}

// When this returns 0, full length of the payload is in mPayloadLength
u64 WebSocketDataFrame::encodePayloadLength() {
    if (mPayloadLength <= WebSocketPayloadLength::NonExtended) {
        return 0;
    } else if (mPayloadLength < 65535) {
        mPayloadLength = 126;
    } else {
        mPayloadLength = 127;
    }
    return mPayload.size();
}

DataFrameVector WebSocketDataFrame::serialize() {
    int totalFrameSize = mPayloadLength + 4;
    DataFrameVector out(totalFrameSize);
    u64 payloadSize = encodePayloadLength();

    u8 encodedByte = 0;
    encodedByte = mFragment | (mIsDeflate << 1) | (mOpcode << 4);
    out.push_back(encodedByte);
    encodedByte = 0;
    encodedByte = mIsMask | (mPayloadLength << 1);
    out.push_back(encodedByte);
    encodedByte = 0;
    if (payloadSize > 0) {
        int extendedMax = mPayloadLength == WebSocketPayloadLength::Extended16 ? 16 : 64;
        for (size_t index = 0; index < (extendedMax / 8); index++)
            out.push_back((payloadSize >> (extendedMax - 8 * index)) & 0xFF);
    }
    if (mMaskingKey == WebSocketMask::MASKED) {
        for (size_t index = 0; index < 4; index++)
            out.push_back((mMaskingKey >> (24 - 8 * index)) & 0xFF);
    }
    for (size_t i = 0; i < mPayload.size(); i++) {
        out.push_back(mPayload[i]);
    }
    return out;
}

void WebSocketDataFrame::deserializeHeader(DataFrameVector header) {
    // Deserializing the header failed due to header size being below minimum.
    if (header.size() < WebSocketHeaderSizes::MinimumHeader) {
        mFragment = WebSocketFragment::FAIL;
        return;
    }

    u8 curByte = header[0];
    mFragment = curByte & WebSocketFragment::FINAL ? WebSocketFragment::FINAL : WebSocketFragment::MORE;
    mIsDeflate = curByte & (WebSocketPerMessageDeflate::DeflateExtensionEnabled << 1) ? WebSocketPerMessageDeflate::DeflateExtensionEnabled : WebSocketPerMessageDeflate::DeflateExtensionDisabled;
    mOpcode = static_cast<WebSocketOpcode>(curByte >> 4);

    curByte = header[1];
    mIsMask = curByte & WebSocketMask::MASKED ? WebSocketMask::MASKED : WebSocketMask::UNMASKED;
    mPayloadLength = curByte >> 1;
}

void WebSocketDataFrame::deserializeExtendedHeader(DataFrameVector header) {
    // Deserializing the header failed due to header size being below minimum.
    if (header.size() < WebSocketHeaderSizes::MinimumHeader) {
        mFragment = WebSocketFragment::FAIL;
        return;
    }

    size_t curIndex = 0;
    u64 curLength = mPayloadLength;
    mPayloadLength = 0;

    // Extended Payload Length is first in the data frame
    if (curLength > NonExtended) {
        // 64 bits
        if (curLength > Extended16) {
            for (size_t i = 0; i < sizeof(u64); i++)
                mPayloadLength |= static_cast<u64>(header[i + curIndex]) << (8 * i);

            curIndex += sizeof(u64);
        } else {
            // 16 bits
            for (size_t i = 0; i < sizeof(u16); i++)
                mPayloadLength |= static_cast<u64>(header[i + curIndex]) << (8 * i);

            curIndex += sizeof(u16);
        }
    }

    // Followed by the masking key if enabled
    if (mIsMask) {
        for (size_t i = 0; i < sizeof(u32); i++)
            mMaskingKey |= static_cast<u32>(header[i + curIndex]) << (8 * i);

        curIndex += sizeof(u32);
    }
}

void WebSocketDataFrame::deserializePayload(DataFrameVector payload) {
    mPayload = payload;
    // Add unmasking for possible server implementation maybe
    if (mIsDeflate)
        decompressPayload();
}