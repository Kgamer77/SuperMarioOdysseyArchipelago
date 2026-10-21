#pragma once

namespace al {
    class LayoutInitInfo;
    class LayoutActor;
    class EventFlowChoiceInfo;
} // namespace al

class CommonSelectParts {
public:
    CommonSelectParts(const char*, al::LayoutActor*, const al::LayoutInitInfo&, int, bool);
    void activateAll();
    void startSelect2(const char16_t*, const char16_t*, int);
    void startSelectWithChoiceTable(const char16_t**, int, int);
    void startSelectWithChoiceTableWithoutPosAnim(const char16_t**, int, int);
    void startSelectWithChoiceInfo(const al::EventFlowChoiceInfo*);
    bool isActive() const;
    bool isDecideEnd() const;
    void setSelectPartsString(const char16_t*, int);
    void deactivate(int);
    void kill();
    void reset();
    void exeHide();
    void exeAppearBefore();
    void exeAppear();
    void exeAppearAfter();
    void exeAppearCursor();
    void exeSelect();
    void exeDecideParts();
    void exeDecideDeactiveParts();
    void exeDecide();
    void exeDecideAfter();
    void exeDecideEnd();
};