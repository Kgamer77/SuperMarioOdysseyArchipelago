#pragma once

#include <math/seadVector.h>
#include <prim/seadSafeString.h>

#include "Library/Nerve/NerveExecutor.h"

namespace al {
    class LayoutActor;
    class LayoutInitInfo;
} // namespace al

namespace nn::ui2d {
    class TextureInfo;
}

class CommonHorizontalList : public al::NerveExecutor {
public:
    CommonHorizontalList(al::LayoutActor*, const al::LayoutInitInfo&, bool);
    ~CommonHorizontalList();

    void activate(void);
    // void addGroupAnimData(const sead::FixedSafeString<64>*, const char*);
    void addStringData(const sead::WBufferedSafeString**, const char*);
    // void calcAnimRate(void);
    void calcCursorPos(sead::Vector2f*);
    void deactivate(void);
    void decide(const char*);
    void initData(int);
    void initDataNoResetSelected(int);
    void initDataWithIdx(int, int, int);
    void jumpLeft(void);
    void jumpRight(void);
    void left();
    void pageDown(void);
    void pageUp(void);
    void reject(void);
    void right();
    void setEnableData(const bool*);
    void setGroupAnimData(const sead::FixedSafeString<64>*, const char*);
    void setImageData(nn::ui2d::TextureInfo**, const char*);
    void setSelectedIdx(int, int);
    void update(void);
    void updateCursorPos(void);
    void updateParts(void);

    bool isActive(void) const;
    bool isDeactive(void) const;
    bool isDecideEnd(void) const;
    bool isRejectEnd(void) const;

    void exeActive(void);
    void exeDeactive(void);
    void exeDecide(void);
    void exeDecideEnd(void);
    void exeReject(void);
    void exeRejectEnd(void);

    al::LayoutActor* mRootActor; // 0x10
    void* field_18; // 0x18
    al::LayoutActor** mListPartsArr; // 0x20
    al::LayoutActor* mScrollBarParts; // 0x28
    al::LayoutActor* mCursorParts; // 0x30
    int mListPartsNum; // 0x38
    int mCurSelected; // 0x3c
    int field_40; // Gets assigned mListNumParts values at points and long ints
    int field_44; // Gets assigned mListNumParts values at points
    void* field_48; // Cast to float at points
    void* field_50;
    sead::Vector2f mCursorPos;
    void* field_60;
    sead::WFixedSafeString<0x80>** mStringDataArr; // 0x68
    sead::FixedSafeString<0x90>** mPaneNameList; // 0x70
    bool** mIsEnableData; // 0x78
    void* field_80;
    void* field_88; // some sort of integer counter?
    int mStringDataCount;
    const bool* unkBool_98; // 0x98
    int mDataCount;
    void* field_a0;
    void* field_a8;
    void* field_b0;
    void* field_b8;
};

// static_assert(sizeof(CommonHorizontalList) == 0xC0);
