#pragma once

#include "sead/basis/seadTypes.h"

#include <cstring>

// A bounds-checked cursor over one ActorSync payload.
//
// PacketWriter/PacketReader cannot serve here despite looking like the right
// tool: both are header-coupled - their constructors read or write the user id,
// packet type and size - so they only work on a whole packet, not on a payload
// nested inside one. This is the small piece they do not cover.
//
// Overruns are latched rather than throwing: a truncated or hostile payload
// leaves mFailed set and every subsequent read returns zero, so a subclass that
// forgets to check still cannot read past the buffer.
class SyncPayload {
public:
    SyncPayload(u8* buffer, u16 capacity)
        : mBuffer(buffer)
        , mCapacity(capacity) { }

    template <typename T>
    void write(const T& value) {
        if (mFailed || mOffset + sizeof(T) > mCapacity) {
            mFailed = true;
            return;
        }
        std::memcpy(mBuffer + mOffset, &value, sizeof(T));
        mOffset += sizeof(T);
    }

    template <typename T>
    void read(T& value) {
        if (mFailed || mOffset + sizeof(T) > mCapacity) {
            mFailed = true;
            value = T();
            return;
        }
        std::memcpy(&value, mBuffer + mOffset, sizeof(T));
        mOffset += sizeof(T);
    }

    u16 getUsed() const { return mOffset; }
    bool isFailed() const { return mFailed; }

private:
    u8* mBuffer = nullptr;
    u16 mCapacity = 0;
    u16 mOffset = 0;
    bool mFailed = false;
};
