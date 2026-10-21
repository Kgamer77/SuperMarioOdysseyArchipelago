#pragma once

// #include "al/Library/Layout/LayoutActor.h"
#include "game/Layout/CommonSelectParts.h"

namespace al {
    class LiveActor;
    class LayoutInitInfo;
    class MessageTagDataHolder;
    class EventFlowChoiceInfo;
    class LayoutActor;
} // namespace al

class TalkMessage : al::LayoutActor {
public:
    TalkMessage(const char*);
    void initLayoutTalk(const al::LayoutInitInfo&, const char*);
    void initLayoutWithArchiveName(const al::LayoutInitInfo&, const char*, const char*);
    void initLayoutImportant(const al::LayoutInitInfo&, const char*);
    void initLayoutOver(const al::LayoutInitInfo&, const char*);
    void initLayoutForEventTalk(const al::LayoutInitInfo&);
    void initLayoutForEventImportant(const al::LayoutInitInfo&);
    void startForNpc(const al::LiveActor*, const char16_t*, const char16_t*, const al::MessageTagDataHolder*, bool);
    void reset();
    void startForSystem(const char16_t*, const al::MessageTagDataHolder*, bool);
    void end();
    bool isIconWait() const;
    void kill();
    bool isWait() const;
    void startSelectWithChoiceTable(const char16_t**, int, int);
    void startSelectWithChoiceInfo(const al::EventFlowChoiceInfo*);
    bool isSelectDecide() const;
    int getSelectedChoiceIndex() const;
    void exeAppear();
    void exeAppearWithText();
    void exeTextAnim();
    void exeIconAppearDelay();
    void exeIconAppear();
    void exeIconWait();
    void exeIconWaitTriggered();
    void exeIconPageNext();
    void exeIconPageNextAndPlayNextPage();
    void exeIconPageNextAndLoadNextMessage();
    void exeIconPageEnd();
    void exeWait();
    void exeEnd();
    void appear();
    void control();
    void startIconPageNext();

    void* _130 = 0;
    void* _138 = 0;
    void* _140 = 0;
    CommonSelectParts* mCommonSelectParts = nullptr;
    void* _150 = 0;
    void* _158 = 0;
    int _160 = -1;
    int _164 = 0;
    short _168 = 0;
};