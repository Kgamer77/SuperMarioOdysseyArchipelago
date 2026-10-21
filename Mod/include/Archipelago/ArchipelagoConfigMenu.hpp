#pragma once

#include "sead/container/seadSafeArray.h"

#include "Keyboard.hpp"
#include "server/gamemode/GameModeConfigMenu.hpp"

enum ArchipelagoConfigParts
{
    OptionClientIP = 1,
    OptionReconnect = 2,
    OptionHostName = 3,
    OptionHostPort = 4,
    OptionSlotName = 5,
    OptionPassowrd = 6,
    OptionDeathlink = 7,
    OptionMotionButton = 8,
    OptionDefaultMode = 9,
};

// Forward declaration
struct ArchipelagoInfo;

class ArchipelagoConfigMenu : public GameModeConfigMenu, public sead::IDisposer {
public:
    ArchipelagoConfigMenu();
    ~ArchipelagoConfigMenu() override = default;

    const sead::WFixedSafeString<0x200>* getStringData() override;
    GameModeConfigMenu::UpdateAction updateMenu(int selectIndex) override;

    const int getMenuSize() override { return 9; } // Fixed size for now

    void initMenu() override;

private:
    static constexpr int mItemCount = 9;
    sead::SafeArray<sead::WFixedSafeString<0x200>, mItemCount> mItems;
    Keyboard* mIPKeyboard = nullptr;
    Keyboard* mHostNameKeyboard = nullptr;
    Keyboard* mPortKeyboard = nullptr;
    Keyboard* mSlotKeyboard = nullptr;
    Keyboard* mPasswordKeyboard = nullptr;
};