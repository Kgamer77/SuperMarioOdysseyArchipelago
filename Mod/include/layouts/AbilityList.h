#pragma once

#include "sead/math/seadVector.h"

#include "al/Library/Layout/LayoutActor.h"
#include "al/Library/Layout/LayoutInitInfo.h"
#include "al/Library/Nerve/NerveSetupUtil.h"
#include "al/Library/Nerve/NerveUtil.h"

#include "game/Layout/CommonHorizontalList.h"

#include "container/seadPtrArray.h"

namespace nn::ui2d {
    class TextureInfo;
}

struct AbilityEntry {
    sead::FixedSafeString<64>* abilityName = new sead::FixedSafeString<64>();
    bool hasAbility;
};

enum class VerticalListInputs : int
{
    None = 0,
    Right = 1,
    RightTrigger = 2,
    Left = 3,
    LeftTrigger = 4,
    PageUp = 5,
    PageDown = 6
};

class AbilityList : public al::LayoutActor {
public:
    AbilityList(const char* name, const al::LayoutInitInfo&); // stub
    // ~AbilityList();

    void loadTexture();
    void updateAbilityPartsData(); // stub
    // (al::LayoutInitInfo const&); //stub
    void appear() override; // Needs testing
    void control();
    void calcCursorPos(sead::Vector2f* pos) const { this->mCommonHorizontalList->calcCursorPos(pos); };
    void deactivate();
    void right();
    void left();
    void rightTrigger();
    void leftTrigger();
    void pageUp();
    void pageDown();
    void cancel();
    void changeOut(bool fromLeft);
    void changeIn(bool fromLeft);
    void activate();

    // Convenience function for setting the buffer of a WFixedSafeString to a given system message
    // label
    void setSystemMessageString(al::IUseMessageSystem* messageSystem, const char* archiveName, const char* label, sead::WSafeString* str);

    bool isClosing() const;
    bool isEnableInput() const;
    bool isDeactive() const;

    void exeAppear();
    void exeWait();
    void exeEnd();
    void exeDeactive();
    void exeChangeOut();
    void exeChangeIn();

    static AbilityList* sInstance;

private:
    // Parent Layout Actor ends at 0x130
    // theoretical sead:SafeArray usages might be some for mof sead::PtrArray needs more research
    CommonHorizontalList* mCommonHorizontalList = nullptr; // 0x130
    sead::PtrArray<AbilityEntry>* mAbilityEntries = nullptr; // 0x138 Some sort of array of ptrs? Maybe array of sead::SafeString? (Possibbly
                                                             // array of texture names) saed::SafeArray
    int mNumValidEntries = -1; // 0x140 // Array length // An entry is valid if the flag isDebug is false
    sead::PtrArray<sead::WBufferedSafeString>* mIndexLabels = nullptr; // 0x148 // An array list numbering text?
                                                                       // sead::SafeArray<sead::WFixedSafeString<>>
    sead::PtrArray<sead::WBufferedSafeString>* mAbilityNames = nullptr; // 0x150 // Array of capture readable names?
                                                                        // sead::SafeArray<sead::WFixedSafeString<>> (unsure of list type or string size)
    sead::PtrArray<sead::WBufferedSafeString>* mExplainText = nullptr; // 0x158  // Array of tutorial text? sead::SafeArray<sead::WFixedSafeString<>>
                                                                       // (unsure of list type or string size)
    bool* mIsEnableData = nullptr; // 0x160 // Array of bool ptrs for whether data is enabled ofr the given index
    int _168 = 0; // 0x168
    int mUserInput = 0; // 0x16c
    bool mHasAll = 0; // 0x170 // determines whether the star on the counter appears
    sead::PtrArray<nn::ui2d::TextureInfo>* mTextureArray = nullptr; // 0x178 // array? (Array of Loaded Textures/ just a texture array)
                                                                    // sead::SafeArray
    LayoutActor* mCounterParts = nullptr; // 0x180
    LayoutActor* mGuideParts = nullptr; // 0x188
    LayoutActor* mFooterParts = nullptr; // 0x190
};

namespace {
    NERVE_IMPL(AbilityList, Appear)
    NERVE_IMPL(AbilityList, Wait)
    NERVE_IMPL(AbilityList, End)
    NERVE_IMPL(AbilityList, Deactive)
    NERVE_IMPL(AbilityList, ChangeOut)
    NERVE_IMPL(AbilityList, ChangeIn)

    NERVES_MAKE_STRUCT(AbilityList, Appear, Wait, End, Deactive, ChangeOut, ChangeIn)
} // namespace