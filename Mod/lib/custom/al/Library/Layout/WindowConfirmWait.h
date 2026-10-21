#pragma once

#include "Library/Layout/LayoutActor.h"
#include "Library/Layout/LayoutInitInfo.h"

namespace al {

    class WindowConfirmWait : public al::LayoutActor {
    public:
        WindowConfirmWait(const char*, const char*, const al::LayoutInitInfo&);

        void setTxtMessage(const char16_t*);
        void setTxtMessageConfirm(const char16_t*);

        void appear(void);
        bool tryEnd(void);
        bool tryEndForce(void);
        void playLoop(void);
        void endLoop(void);
        void tryPageIn(void);
        void tryPageOut(void);
        void showPaneConfirm(void);
        void tryConfirmDecide(void);
        void updateHardKey(void);

        void exeHide(void);
        void exeAppear(void);
        void exeKeepWait(void);
        void exeWait(void);
        void exeWaitEnd(void);
        void exeEnd(void);
        void exePageIn(void);
        void exePageOut(void);
        void exePageOutEnd(void);
        void exeConfirmDecide(void);

        al::LayoutActor* mPartsHardKey;
    };

} // namespace al

static_assert(sizeof(al::WindowConfirmWait) == 0x138, "Size of WindowConfirmWait");
