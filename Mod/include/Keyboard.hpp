#pragma once

#include "nn/swkbd/swkbd.h"

#include "sead/prim/seadSafeString.h"

#include "al/Library/Thread/AsyncFunctorThread.h"

#include <cstddef>

typedef void (*KeyboardSetup)(nn::swkbd::KeyboardConfig&);
const u8 MAX_HOSTNAME_LENGTH = 50;
typedef sead::FixedSafeString<MAX_HOSTNAME_LENGTH + 1> hostname;

inline char* mResBuf = (char*)malloc(0x7d4);
inline nn::swkbd::String mResString = nn::swkbd::String(0x7d4, mResBuf);

class Keyboard : public al::AsyncFunctorThread {
public:
    Keyboard();
    void threadFunction();

    void openKeyboard(const char* initialText, KeyboardSetup setupFunc) {
        mInitialText = initialText;
        mSetupFunc = setupFunc;

        start();
    }

    const char* getResult() {
        if (isDone()) {
            return mResString.cstr();
        }
        return nullptr;
    };

    bool isCancelled() const { return mIsCancelled; }

    void setHeaderText(const char* text) { mHeaderText = text; }
    void setSubText(const char* text) { mSubText = text; }

private:
    hostname mInitialText = hostname();
    KeyboardSetup mSetupFunc = KeyboardSetup();

    const char* mHeaderText = "Enter Server IP Here!";
    const char* mSubText = "Must be a Valid Address.";

    bool mIsCancelled = false;

    char* mWorkBuf = nullptr;
    int mWorkBufSize = 0;
};
