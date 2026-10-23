#include "Keyboard.hpp"

#include "nn/swkbd/swkbd.h"

#include <cstring>

#include "Multiplayer/MpDebugLog.h"
#include "Multiplayer/main.hpp"

Keyboard::Keyboard()
    : al::AsyncFunctorThread("Swkbd", al::FunctorV0M(this, &Keyboard::threadFunction), 0, 16 * 1024, sead::CoreId::cSub1) {
    // Shared across every Keyboard instance ever constructed, same idea as
    // the header-inline mResBuf/mResString above (both already shared
    // globally) - allocate the nn::swkbd work buffer once and reuse it,
    // rather than a fresh gHeap->alloc() per instance. Confirmed as a real,
    // deterministic, high-impact leak: StageSceneStateModConfig owns a
    // Keyboard and is re-`new`'d on every single stage load with the
    // previous instance never freed (see PauseMenuHooks.cpp's
    // sceneStateModConfig leak) - that alone was leaking a full nn::swkbd
    // work buffer (GetRequiredWorkBufferSize is commonly several hundred KB)
    // out of gHeap's 512KB on every stage transition, solo or multiplayer.
    // Safe to share: only one swkbd session is ever active at a time (every
    // call site blocks synchronously on isDone() before touching another
    // Keyboard), and GetRequiredWorkBufferSize(false) is deterministic - the
    // argument is a hardcoded constant, so every instance needs exactly the
    // same size.
    static char* sSharedWorkBuf = nullptr;
    static int sSharedWorkBufSize = 0;
    if (!sSharedWorkBuf) {
        sSharedWorkBufSize = nn::swkbd::GetRequiredWorkBufferSize(false);
        sSharedWorkBuf = (char*)gHeap->alloc(sSharedWorkBufSize, 0x1000);
    }
    mWorkBuf = sSharedWorkBuf;
    mWorkBufSize = sSharedWorkBufSize;
}

// Step-by-step MpDebugLog markers around every nn::swkbd call: a prior crash
// report (User Break, AbortImpl, Swkbd thread) pointed at SetGuideText, which
// this file never calls directly - it must be firing from inside MakePreset,
// one of the Set*Text calls, or ShowKeyboard itself, misattributed to this
// function's symbol by inlining. Shortening the sub-text string (the first
// guess) did NOT fix it - reproduced again on that build - so the real
// trigger is still unknown. These markers turn "which of these six calls was
// mid-flight" into a fact instead of a guess: whichever WROTE line is the
// last one in sd:/SMO-Randomizer/mp_debug.log is the one that didn't return.
void Keyboard::threadFunction() {
    nn::swkbd::ShowKeyboardArg arg;

    MpDebugLog::write("Keyboard: MakePreset header=%s sub=%s", mHeaderText, mSubText);
    nn::swkbd::MakePreset(&arg.keyboardConfig, nn::swkbd::Preset::Default);
    MpDebugLog::write("Keyboard: MakePreset returned");

    mSetupFunc(arg.keyboardConfig);
    arg.keyboardConfig._isUseTextCheck = false;
    MpDebugLog::write("Keyboard: mSetupFunc returned mode=%d passwordMode=%d textMaxLength=%d textMinLength=%d", (int)arg.keyboardConfig.keyboardMode,
        (int)arg.keyboardConfig.passwordMode, arg.keyboardConfig.textMaxLength, arg.keyboardConfig.textMinLength);

    MpDebugLog::write("Keyboard: SetHeaderTextUtf8 len=%d", (int)strlen(mHeaderText));
    nn::swkbd::SetHeaderTextUtf8(&arg.keyboardConfig, mHeaderText);
    MpDebugLog::write("Keyboard: SetHeaderTextUtf8 returned");

    MpDebugLog::write("Keyboard: SetSubTextUtf8 len=%d", (int)strlen(mSubText));
    nn::swkbd::SetSubTextUtf8(&arg.keyboardConfig, mSubText);
    MpDebugLog::write("Keyboard: SetSubTextUtf8 returned");

    arg.workBufSize = mWorkBufSize;
    arg.workBuf = mWorkBuf;

    if (mInitialText.calcLength() > 0) {
        MpDebugLog::write("Keyboard: SetInitialTextUtf8 len=%d text=%s", mInitialText.calcLength(), mInitialText.cstr());
        nn::swkbd::SetInitialTextUtf8(&arg, mInitialText.cstr());
        MpDebugLog::write("Keyboard: SetInitialTextUtf8 returned");
    } else {
        MpDebugLog::write("Keyboard: SetInitialTextUtf8 skipped (empty)");
    }

    MpDebugLog::write("Keyboard: ShowKeyboard starting");
    mIsCancelled = nn::swkbd::ShowKeyboard(&mResString, arg) == 671; // 671 = exit code for pressing x to cancel keyboard
    MpDebugLog::write("Keyboard: ShowKeyboard returned cancelled=%d", mIsCancelled ? 1 : 0);
}
