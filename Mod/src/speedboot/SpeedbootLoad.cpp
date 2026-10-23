#include "speedboot/SpeedbootLoad.hpp"

#include "al/Library/Controller/InputFunction.h"
#include "al/Library/Layout/LayoutActionFunction.h"
#include "al/Library/Layout/LayoutActorUtil.h"
#include "al/Library/Layout/LayoutInitInfo.h"
#include "al/Library/Nerve/NerveUtil.h"

#include "game/Sequence/HakoniwaSequence.h"
#include "game/Sequence/WorldResourceLoader.h"
#include "game/System/GameDataFunction.h"

#include "Randomizer/Randomizer.h"
#include "Randomizer/WorldMeta.h"
#include "gfx/seadColor.h"
#include "math/seadMathCalcCommon.h"
#include "nn/os.h"
#include "types.h"

#include "nn/util.h"
#include <cstdlib>

namespace {

    constexpr f32 FALLBACK_DURATION = 13.0f;
    constexpr f32 FPS = 60.0f;

} // namespace

SpeedbootLoad::SpeedbootLoad(WorldResourceLoader* resourceLoader, const al::LayoutInitInfo& initInfo, HakoniwaSequence* sequence, float autoCloseAfter)
    : al::LayoutActor("SpeedbootLoad")
    , worldResourceLoader(resourceLoader)
    , mSequence(sequence) {
    al::initLayoutActor(this, initInfo, "SpeedbootLoad", nullptr);
    initNerve(&NrvSpeedbootLoad.Appear, 0);

    wipe = new al::WipeSimple("黒フェードシーン情報", "FadeBlack", initInfo, 0);
    wipeSkip = new al::WipeSimple("スキップワイプ", "WipeSkip", initInfo, 0);

    al::setPaneLocalScale(this, "TxtCurModName", { 0.6f, 0.6f });

    appear();
}

// ============================================================================
// Nerve states
// ============================================================================

void SpeedbootLoad::exeAppear() {
    if (al::isFirstStep(this))
        al::startAction(this, "Appear", nullptr);
    if (al::isActionEnd(this, nullptr))
        al::setNerve(this, &NrvSpeedbootLoad.Wait);
}

void SpeedbootLoad::exeWait() {
    if (al::isFirstStep(this)) {
    }

    if (al::isActionEnd(this, nullptr))
        al::setNerve(this, &NrvSpeedbootLoad.Decrease);
}

void SpeedbootLoad::exeDecrease() {
    updateProgressBar();
    updateUIElements();
    updateTextElements();

    if (mProgression >= 1.0f) {
        wipeSkip->tryStartClose(-1);
        wipe->tryStartClose(60);

        if (wipe->isCloseEnd())
            al::setNerve(this, &NrvSpeedbootLoad.End);
    }
}

void SpeedbootLoad::exeEnd() {
    if (al::isFirstStep(this)) {
        kill();
        wipeSkip->startOpen(-1);
        wipe->startOpen(30);
    }
}

// ============================================================================
// Stage / kingdom helpers
// ============================================================================

const char* SpeedbootLoad::getStageName() const {
    if (!mSequence)
        return nullptr;

    if (!mSequence->mStageName.isEmpty())
        return mSequence->mStageName.cstr();

    const char* name = GameDataFunction::getNextStageName(mSequence->mGameDataHolderAccessor);
    if (name && name[0] != '\0')
        return name;

    return GameDataFunction::getCurrentStageName(mSequence->mGameDataHolderAccessor);
}

const char16_t* SpeedbootLoad::getKingdomName(const char* stageName) const {
    if (!stageName)
        return u"Unknown Kingdom";

    const WorldMeta::Entry* entry = WorldMeta::findByMainStage(stageName);
    return entry ? entry->displayName16 : nullptr;
}

bool SpeedbootLoad::isKnownStage(const char* stageName) const {
    if (!stageName)
        return false;

    return WorldMeta::findByMainStage(stageName) != nullptr;
}

// ============================================================================
// Per-frame update helpers
// ============================================================================

void SpeedbootLoad::updateProgressBar() {
    const char* stageName = getStageName();

    if (!isKnownStage(stageName) && !mUsingFallback)
        mUsingFallback = true;

    if (mUsingFallback) {
        mFallbackTimer += 1.0f / FPS;
        mProgression = sead::Mathf::clamp(mFallbackTimer / FALLBACK_DURATION, 0.0f, 1.0f);
    } else {
        f32 loadPercent = worldResourceLoader->calcLoadPercent();
        mProgression = sead::Mathf::clamp(loadPercent / 100.0f, 0.0f, 1.0f);
    }
}

void SpeedbootLoad::updateUIElements() {
    mRotTime += 0.03f;

    if (mProgression < 1.0f) {
        f32 moonRotation = cosf(mRotTime) * 5.0f;
        al::setPaneLocalRotate(this, "PicBG", { 0.0f, 0.0f, mRotTime * -3.0f });

        f32 barOffset = mRotTime * 30.0f;
        al::setPaneLocalTrans(this, "BloodMoonLoadingBar", { -352.0f + barOffset, -342.0f, 0.0f });
        al::setPaneLocalTrans(this, "BloodMoonLoadingBar2", { -352.0f + barOffset - 1920.0f, -342.0f, 0.0f });
    }
}

void SpeedbootLoad::updateTextElements() {
    const char* stageName = getStageName();
    const char16_t* kingdom = getKingdomName(stageName);

    if (kingdom) {
        al::setPaneString(this, "TxtKingdom", kingdom, 0);
    } else {
        sead::WFormatFixedSafeString<0x100> fallback(u"");
        if (stageName) {
            for (const char* c = stageName; *c != '\0'; c++)
                fallback.appendWithFormat(u"%c", (char16_t)(*c));
        }
        al::setPaneString(this, "TxtKingdom", fallback.cstr(), 0);
    }

    s32 dotCount = ((s32)(mRotTime * 0.8f)) % 4;
    sead::WFormatFixedSafeString<0x20> loadingStr(u"Loading");
    for (s32 i = 0; i < dotCount; i++)
        loadingStr.append(u".");
    al::setPaneString(this, "TxtLoading", loadingStr.cstr(), 0);

    f32 displayPercent = sead::Mathf::clamp(mProgression * 100.0f, 0.0f, 100.0f);
    sead::WFormatFixedSafeString<0x20> percentStr(u"");
    percentStr.appendWithFormat(u"%.0f%%", displayPercent);
    al::setPaneString(this, "TxtLoadingPercent", percentStr.cstr(), 0);

    sead::WFormatFixedSafeString<0x100> debugStr(u"Progress: %.1f%%", displayPercent);
    al::setPaneString(this, "TxtDebug", debugStr.cstr(), 0);

    al::setPaneString(this, "TxtCurModName", u"Randomizer", 0);
    al::setPaneStringFormat(this, "TxtCurModVersion", "%s (%s)\n", TOSTRING(BUILDVERSTR), TOSTRING(BUILDDATESTR));
    al::setPaneString(this, "TxtCredits", u"CONTRIBUTORS", 0);
    al::setPaneString(this, "TxtTip", u"  ", 0); // useless rn i think

    // Contributors list is 6 individually-colorable panes (TxtContrib0..5),
    // split out of the original single TxtContributors pane so each name can
    // get its own exact-hex al::setPaneVtxColor tint.
    const sead::Color4u8 kColorOnijuiku(0x80, 0x80, 0x00, 255); // #808000
    const sead::Color4u8 kColorKarateman(0x0d, 0xc6, 0x26, 255); // #0dc626
    const sead::Color4u8 kColorAJ(0x8d, 0x60, 0xc4, 255); // #8d60c4
    const sead::Color4u8 kColorBaconFearMC(0x34, 0x98, 0xdb, 255); // #3498db
    const sead::Color4u8 kColorFireRising(0xeb, 0x3a, 0x0e, 255); // #eb3a0e
    const sead::Color4u8 kColorMrKatzenGaming(0x86, 0x0d, 0xff, 255); // #860dff

    al::setPaneString(this, "TxtContrib0", u"Onijuiku_", 0);
    al::setPaneVtxColor(this, "TxtContrib0", kColorOnijuiku);
    al::setPaneString(this, "TxtContrib1", u"Karateman", 0);
    al::setPaneVtxColor(this, "TxtContrib1", kColorKarateman);
    al::setPaneString(this, "TxtContrib2", u"1tsAJ", 0);
    al::setPaneVtxColor(this, "TxtContrib2", kColorAJ);
    al::setPaneString(this, "TxtContrib3", u"BaconFearMC", 0);
    al::setPaneVtxColor(this, "TxtContrib3", kColorBaconFearMC);
    al::setPaneString(this, "TxtContrib4", u"FireRising", 0);
    al::setPaneVtxColor(this, "TxtContrib4", kColorFireRising);
    al::setPaneString(this, "TxtContrib5", u"MrKatzenGaming", 0);
    al::setPaneVtxColor(this, "TxtContrib5", kColorMrKatzenGaming);
}
