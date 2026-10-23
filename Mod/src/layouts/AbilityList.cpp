#include "layouts/AbilityList.h"

#include "sead/prim/seadStringUtil.h"

#include "al/Library/Layout/LayoutActionFunction.h"
#include "al/Library/Layout/LayoutActorUtil.h"
#include "al/Library/Message/MessageHolder.h"
#include "al/Library/Resource/Resource.h"
#include "al/Library/Resource/ResourceFunction.h"
#include "al/Library/Yaml/ByamlIter.h"
#include "al/Library/Yaml/ByamlUtil.h"

#include "game/System/GameDataFunction.h"

#include "Archipelago/Archipelago.h"
#include "rs/util.hpp"
// #include "server/gamemode/GameModeManager.hpp"

AbilityList* AbilityList::sInstance = nullptr;

AbilityList::AbilityList(const char* name, const al::LayoutInitInfo& initInfo)
    : al::LayoutActor(name) {
    al::initLayoutActor(this, initInfo, "AbilityList", 0);
    initNerve(&NrvAbilityList.End, 0);

    // al::setPaneStringFormat(this, "TxtStatus", "10/10");
    // al::hidePane(this, "DotRed");
    // al::hidePane(this, "DotGreen");

    mCommonHorizontalList = new CommonHorizontalList(this, initInfo, false);

    const char* fileDir = "SystemData/AbilityEntryList";
    al::Resource* abilityEntryList = al::findOrCreateResource(fileDir, 0);
    const u8* data = al::findResourceYaml(abilityEntryList, "AbilityEntryList", 0);
    al::ByamlIter bymalIter = al::ByamlIter(data);
    int iterSize = bymalIter.getSize();
    int numValidElements = 0;
    if (0 < iterSize) {
        numValidElements = 0;
        int numEntries = 0;
        do {
            al::ByamlIter otherIter = al::ByamlIter();
            bymalIter.tryGetIterByIndex(&otherIter, numEntries);
            bool isDebug = al::tryGetByamlKeyBoolOrFalse(otherIter, "IsDebug");
            numValidElements += (int)!isDebug;
            numEntries = numEntries + 1;
        } while (iterSize != numEntries);
    }

    mNumValidEntries = numValidElements;
    mAbilityEntries->allocBuffer(mNumValidEntries, nullptr);
    mAbilityNames->allocBuffer(mNumValidEntries, nullptr);
    mExplainText->allocBuffer(mNumValidEntries, nullptr);
    mIndexLabels->allocBuffer(mNumValidEntries, nullptr);
    mIsEnableData = new bool[mNumValidEntries];

    // Use .pushback to put safestrings in arrays
    for (int i = 0; i < mNumValidEntries; i++) {
        mAbilityEntries->pushBack(new AbilityEntry);
        mAbilityNames->pushBack(new sead::WFixedSafeString<32>);
        mExplainText->pushBack(new sead::WFixedSafeString<64>);
        mIndexLabels->pushBack(new sead::WFixedSafeString<3>);
    }

    kill();
}

void AbilityList::left() {
    if (al::isNerve(this, &NrvAbilityList.Wait))
        this->mUserInput = (int)VerticalListInputs::Left;
}

void AbilityList::right() {
    if (al::isNerve(this, &NrvAbilityList.Wait))
        this->mUserInput = (int)VerticalListInputs::Right;
}

void AbilityList::leftTrigger() {
    if (al::isNerve(this, &NrvAbilityList.Wait))
        this->mUserInput = (int)VerticalListInputs::LeftTrigger;
}

void AbilityList::rightTrigger() {
    if (al::isNerve(this, &NrvAbilityList.Wait))
        this->mUserInput = (int)VerticalListInputs::RightTrigger;
}

void AbilityList::pageUp() {
    if (al::isNerve(this, &NrvAbilityList.Wait))
        this->mUserInput = (int)VerticalListInputs::PageUp;
}

void AbilityList::pageDown() {
    if (al::isNerve(this, &NrvAbilityList.Wait))
        this->mUserInput = (int)VerticalListInputs::PageDown;
}

void AbilityList::activate() {
    this->mCommonHorizontalList->activate();
    al::setNerve(this, &NrvAbilityList.Wait);
}

void AbilityList::deactivate() {
    this->mUserInput = 0;
    this->mCommonHorizontalList->deactivate();
    al::setNerve(this, &NrvAbilityList.Deactive);
}

void AbilityList::cancel() {
    if (al::isNerve(this, &NrvAbilityList.End))
        return;

    al::setNerve(this, &NrvAbilityList.End);
}

void AbilityList::changeIn(bool fromLeft) {
    if (al::isNerve(this, &NrvAbilityList.ChangeIn))
        return;
    const char* action = "LeftIn";
    if (!fromLeft)
        action = "RightIn";

    al::startAction(this, action, "");
    this->updateAbilityPartsData();
    this->mCommonHorizontalList->updateParts();
    this->appear();
    al::setNerve(this, &NrvAbilityList.ChangeIn);
}

void AbilityList::changeOut(bool fromLeft) {
    if (al::isNerve(this, &NrvAbilityList.ChangeOut))
        return;
    const char* action = "LeftOut";
    if (!fromLeft)
        action = "RightOut";

    al::startAction(this, action, "");
    this->mCommonHorizontalList->deactivate();
    al::setNerve(this, &NrvAbilityList.ChangeOut);
}

void AbilityList::appear() {
    al::setNerve(this, &NrvAbilityList.Appear);
    this->updateAbilityPartsData();
    al::startAction(this, "Appear", "");
    this->mCommonHorizontalList->activate();
}

// Needs work
void AbilityList::loadTexture() {
    al::StringTmp<128> textureDir = al::StringTmp<128>();
    textureDir = "ObjectData/AbilityListTexture";
    if (0 < this->mNumValidEntries) {
        int i = 0;
        int j = 0;
        while (i < this->mNumValidEntries) {
            this->mTextureArray->mPtrs[j] = al::createTextureInfo(textureDir.cstr(), abilities[j], abilities[j]);
            i++;
            j++;
        }
    }
}

void AbilityList::updateAbilityPartsData() {
    int numerator = 0;
    int denominator = mNumValidEntries;
    GameDataHolderAccessor accessor(this);
    const char* counterState;
    for (int i = 0; i < denominator; i++) {
        (*this->mIndexLabels)[i]->format(u"%02d", i + 1);
        const char* curAbilityName = (*this->mAbilityEntries)[i]->abilityName->cstr();
        bool hasAbility = Archipelago::isAbilityUnlocked(i);
        mIsEnableData[i] = hasAbility;

        if (!hasAbility) {
            this->mHasAll = hasAbility;

            if (al::isExistLabelInSystemMessage(this, "AbilityList", curAbilityName))
                (*mAbilityNames)[i]->format(u"%s", al::getSystemMessageString(this, "AbilityList", curAbilityName));
            (*mExplainText)[i]->format(u"%s", u"?????");
        } else {
            (*this->mAbilityNames)[i]->format(u"%s", u"?????");
            (*this->mExplainText)[i]->format(u"%s", u"?????");
        }

        // sead::StringUtil::convertUtf8ToUtf16();
    }

    rs::setCounterAndDenominator(this->mCounterParts, numerator, denominator);
    al::IUseLayoutAction* layout = nullptr;
    if (this->mCounterParts) {
        layout = (IUseLayoutAction*)this->mCounterParts;
    }
    counterState = this->mHasAll ? "On" : "Off";

    al::startAction(layout, counterState, "State");
    this->mCommonHorizontalList->initDataNoResetSelected(this->mNumValidEntries);
    this->mCommonHorizontalList->setEnableData(mIsEnableData);
    this->mCommonHorizontalList->addStringData((const sead::WBufferedSafeString**)mIndexLabels->data(), 0);
    this->mCommonHorizontalList->setImageData((nn::ui2d::TextureInfo**)this->mTextureArray, "PicDummy");
}

void AbilityList::control() {
    al::IUseLayout* layout = nullptr;
    if (this->mGuideParts)
        layout = (al::IUseLayout*)this->mGuideParts;

    sead::WFixedSafeString<256>* curAbility = (sead::WFixedSafeString<256>*)(*this->mAbilityNames)[this->mCommonHorizontalList->mCurSelected];
    sead::WFixedSafeString<256>* curExplain = (sead::WFixedSafeString<256>*)(*this->mExplainText)[this->mCommonHorizontalList->mCurSelected];

    al::setPaneString(layout, "TxtContent00", curAbility->cstr());
    al::setPaneString(layout, "TxtContent01", curExplain->cstr());
    this->mCommonHorizontalList->update();
}

bool AbilityList::isClosing() const {
    return al::isNerve(this, &NrvAbilityList.End);
}
bool AbilityList::isEnableInput() const {
    return al::isNerve(this, &NrvAbilityList.Wait);
}
bool AbilityList::isDeactive() const {
    return al::isNerve(this, &NrvAbilityList.Deactive);
}

void AbilityList::exeAppear() {
    if (al::isActionEnd(this, 0))
        al::setNerve(this, &NrvAbilityList.Wait);
}

void AbilityList::exeWait() {
    if (al::isFirstStep(this)) {
        al::startAction(this, "Wait", 0);
    }

    VerticalListInputs input = (VerticalListInputs)mUserInput;

    switch (input) {
    case VerticalListInputs::Right:
        this->mCommonHorizontalList->right();
        break;

    case VerticalListInputs::Left:
        this->mCommonHorizontalList->left();
        break;

    case VerticalListInputs::RightTrigger:
        this->mCommonHorizontalList->jumpRight();
        break;

    case VerticalListInputs::LeftTrigger:
        this->mCommonHorizontalList->jumpLeft();
        break;

    case VerticalListInputs::PageUp:
        this->mCommonHorizontalList->pageUp();
        break;

    case VerticalListInputs::PageDown:
        this->mCommonHorizontalList->pageDown();
        break;
    }

    this->mUserInput = 0;
}

// Missing jumptable
void AbilityList::exeEnd() {
    if (al::isFirstStep(this))
        al::startAction(this, "End", 0);

    if (al::isActionEnd(this, 0))
        kill();
}

// Blank in Ghidra
void AbilityList::exeDeactive() { }

// Unrecovered jumptable
void AbilityList::exeChangeOut() {
    // if (al::isActionEnd(this, 0))
    //         al::setNerve(this, &NrvAbilityList.ChangeOut);
}

// Unrecovered jumptable
void AbilityList::exeChangeIn() {
    // if (al::isActionEnd(this, 0))n32

    //         al::setNerve(this, &NrvAbilityList.ChangeIn);
}

// bool AbilityList::tryEnd() {
//     if (!al::isNerve(this, &NrvAbilityList.End)) {
//         al::setNerve(this, &NrvAbilityList.End);
//         return true;
//     }
//     return false;
// }

// bool AbilityList::tryStart() {
//     if (!al::isNerve(this, &NrvAbilityList.Wait) && !al::isNerve(this, &NrvAbilityList.Appear)) {
//         appear();
//         return true;
//     }
//     return false;
// }

// void AbilityList::exeAppear() {
//     if (al::isActionEnd(this, 0)) {
//         al::setNerve(this, &NrvAbilityList.Wait);
//     }
// }

// void AbilityList::exeWait() {
//     if (al::isFirstStep(this)) {
//         al::startAction(this, "Wait", 0);
//     }
//     Client::instance()->mSocket->isConnected() ? showOnline() : showOffline();
// }

// void AbilityList::exeEnd() {
//     if (al::isFirstStep(this)) {
//         al::startAction(this, "End", 0);
//     }

//     if (al::isActionEnd(this, 0)) {
//         kill();
//     }
// }
