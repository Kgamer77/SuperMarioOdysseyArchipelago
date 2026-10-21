#pragma once

#include <math/seadVector.h>
#include <prim/seadSafeString.h>

#include "Library/Nerve/NerveExecutor.h"

namespace al {
    class LayoutActor;
    class LayoutInitInfo;
    class RollParts;
} // namespace al

namespace nn::ui2d {
    class TextureInfo;
}

struct RollPartsData {
    int mRollMsgCount = 0; // 0x0
    const char16_t* const* mRollMsgList; // 0x8
    int mSelectedIdx = 0; // 0x10
    bool mIsLoop = true; // 0x14
};

class CommonVerticalList : public al::NerveExecutor {
public:
    CommonVerticalList(al::LayoutActor*, const al::LayoutInitInfo&, bool);
    ~CommonVerticalList();

    void activate(void);
    void addGroupAnimData(const sead::FixedSafeString<64>*, const char*);
    void addStringData(const sead::WFixedSafeString<512>*, const char*);
    void appearCursor(void);
    void calcAnimRate(void);
    void calcCursorPos(sead::Vector2f*);
    void deactivate(void);
    void decide(void);
    void down(void);
    void endCursor(void);
    void getListPartsNum(void);
    void* getParts(int) const;
    s32 getRollPartsSelected(int idx);
    void getSelectedParts(void);
    void hideAll(void);
    void hideCursor(void);
    void initData(int);
    void initDataNoResetSelected(int);
    void initDataWithIdx(int, int, int);
    void jumpBottom(void);
    void jumpTop(void);
    void pageDown(void);
    void pageUp(void);
    void reject(void);
    void rollLeft(void);
    void rollRight(void);
    void setEnableData(const bool*);
    void setImageData(nn::ui2d::TextureInfo**, const char*);
    void setRollPartsData(RollPartsData*);
    void setRollPartsSelected(int, int);
    void setSelectedIdx(int, int);
    void startLoopActionAll(const char*, const char*);
    void up(void);
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
    int mListPartsArraySize; // 0x18
    int mListPartsArrayCapacity; // 0x1c
    al::LayoutActor** mListPartsArr; // 0x20
    struct CursorParts* mCursorParts; // 0x28
    struct ScrollBarParts* mScrollBarParts; // 0x30
    int mListPartsNum; // 0x38, visible/update parts count
    int mCurSelected; // 0x3c
    int mTopSelectableIdx; // 0x40
    int field_44;
    void* field_48;
    int field_50;
    int field_54;
    float field_58;
    sead::Vector2f mCursorPos; // 0x5c
    bool field_64;
    bool field_65;
    char field_66[2];
    int field_68;
    int field_6c;
    sead::WFixedSafeString<0x200>** mStringDataArr; // 0x70
    sead::FixedSafeString<0x90>** mPaneNameList; // 0x78
    // setImageData's two fields (it writes nothing else). The array is indexed
    // by DATA ROW and only borrowed, so it must outlive the list. Unlike the
    // string/enable/roll bindings, these survive initDataNoResetSelected.
    nn::ui2d::TextureInfo** mImageData; // 0x80
    void* field_88;
    const bool* mIsEnableData; // 0x90
    int mStringDataCount; // 0x98
    int mDataCount; // 0x9c
    const char* mImagePaneName; // 0xa0, second half of setImageData
    void* field_a8;
    int field_b0;
    int field_b4;
    RollPartsData* RollPartsArr; // 0xb8
    s32* mRollPartsSelected; // 0xc0
    int field_c8;
    // 0xcc: "strings dirty" flag. Navigation ops (setSelectedIdx, up/down, rolls,
    // initDataNoResetSelected, …) set it; updateParts() re-copies each row's bound
    // text into its panes only when it is set, then clears it. A value-only refresh
    // must raise this itself or the visible text stays stale.
    bool mStringsDirty; // 0xcc
};
