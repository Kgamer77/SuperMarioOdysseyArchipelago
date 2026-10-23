// #include "Archipelago/ArchipelagoConfigMenu.hpp"

// #include <stdint.h>

// #include "Layout/CommonVerticalList.h"
// #include "Library/Layout/LayoutActionFunction.h"
// #include "Library/Memory/HeapUtil.h"
// #include "Library/Play/Layout/RollParts.h"
// #include "Scene/StageSceneStateModConfig.hpp"
// #include "Archipelago/ArchipelagoInfo.h"
// #include "server/Client.hpp"
// #include "server/gamemode/GameModeManager.hpp"

// ArchipelagoConfigMenu::ArchipelagoConfigMenu() : GameModeConfigMenu() {
//     mIPKeyboard = new Keyboard(15);
//     if (mIPKeyboard) {
//         mIPKeyboard->setHeaderText(u"Set Client IP Address");
//         mIPKeyboard->setSubText(u"");
//     }

//     mHostNameKeyboard = new Keyboard(64);
//     if (mHostNameKeyboard) {
//         mHostNameKeyboard->setHeaderText(u"Set Archipelago Host Name");
//         mHostNameKeyboard->setSubText(u"");
//     }

//     mPortKeyboard = new Keyboard(5);
//     if (mPortKeyboard) {
//         mPortKeyboard->setHeaderText(u"Set Archipelago Port");
//         mPortKeyboard->setSubText(u"0 - 65535");
//     }

//     mSlotKeyboard = new Keyboard(64);
//     if (mSlotKeyboard) {
//         mSlotKeyboard->setHeaderText(u"Set Archipelago Slot Name");
//         mSlotKeyboard->setSubText(u"");
//     }

//     mPasswordKeyboard = new Keyboard(64);
//     if (mPasswordKeyboard) {
//         mPasswordKeyboard->setHeaderText(u"Set Archipelago Password");
//         mPasswordKeyboard->setSubText(u"Leave Blank for No Password");
//     }
//     // mPortKeyboard = new Keyboard(5);
//     // if (mPortKeyboard) {
//     //     mPortKeyboard->setHeaderText(u"Set Client Port");
//     //     mPortKeyboard->setSubText(u"Default 1027");
//     // }
// }

// void ArchipelagoConfigMenu::initMenu() {
//     ArchipelagoInfo* curMode = GameModeManager::instance()->getInfo<ArchipelagoInfo>();
//     StageSceneStateModConfig::setMenuItemBase(mList->mListPartsArr[OptionClientIP]);
//     StageSceneStateModConfig::setMenuItemBase(mList->mListPartsArr[OptionReconnect]);
//     StageSceneStateModConfig::setMenuItemBase(mList->mListPartsArr[OptionHostName]);
//     StageSceneStateModConfig::setMenuItemBase(mList->mListPartsArr[OptionHostPort]);
//     StageSceneStateModConfig::setMenuItemBase(mList->mListPartsArr[OptionSlotName]);
//     StageSceneStateModConfig::setMenuItemBase(mList->mListPartsArr[OptionPassowrd]);
//     StageSceneStateModConfig::setMenuItemCheck(mList->mListPartsArr[OptionDeathlink]);
//     StageSceneStateModConfig::setMenuItemCheck(mList->mListPartsArr[OptionMotionButton]);
//     StageSceneStateModConfig::setMenuItemCheck(mList->mListPartsArr[OptionDefaultMode]);
// }

// const sead::WFixedSafeString<0x200>* ArchipelagoConfigMenu::getStringData() {
//     ArchipelagoInfo* info = GameModeManager::instance()->getInfo<ArchipelagoInfo>();
//     ArchipelagoMode* archipelago = GameModeManager::instance()->getMode<ArchipelagoMode>();

//     mItems[OptionClientIP - 1].copy(u"Client IP");
//     mItems[OptionReconnect - 1].copy(u"Reconnect to Client");
//     mItems[OptionHostName - 1].copy(u"Archipelago Host Name");
//     mItems[OptionHostPort - 1].copy(u"Archipelago Port");
//     mItems[OptionSlotName - 1].copy(u"Archipelago Slot Name");
//     mItems[OptionPassowrd - 1].copy(u"Archipelago Password");
//     mItems[OptionDeathlink - 1].copy(u"Death Link");
//     mItems[OptionMotionButton - 1].copy(u"Bind Motion Input to R");
//     mItems[OptionDefaultMode - 1].copy(u"Default Mode");

//     if (info)
//         al::startAction(mList->mListPartsArr[OptionDefaultMode], info->mIsClientConnected == ArchipelagoState::CLIENT_CONNECTED ? "On" : "Off", "State");

//     if (archipelago) {
//         al::startAction(mList->mListPartsArr[OptionDeathlink], archipelago->isDeathLinkEnabled() ? "On" : "Off", "State");
//         al::startAction(mList->mListPartsArr[OptionMotionButton], archipelago->isMotionRebound() ? "On" : "Off", "State");
//     }

//     return mItems.mBuffer;
// }

// GameModeConfigMenu::UpdateAction ArchipelagoConfigMenu::updateMenu(int selectIndex) {
//     ArchipelagoInfo* curMode = GameModeManager::instance()->getInfo<ArchipelagoInfo>();
//     ArchipelagoMode* archipelago = GameModeManager::instance()->getMode<ArchipelagoMode>();

//     if (!curMode) {
//         return GameModeConfigMenu::UpdateAction::NOOP;
//     }

//     switch (selectIndex + 1) {
//     case OptionClientIP: {
//         // Set Client IP
//         if (mIPKeyboard) {
//             mIPKeyboard->openKeyboard(Client::getApClientIP(), [](nn::swkbd::KeyboardConfig& config) {
//                 config.keyboardMode = nn::swkbd::KeyboardMode::ModeASCII;
//                 config.textMaxLength = 15;
//                 config.textMinLength = 7;
//                 config.isUseUtf8 = true;
//                 config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
//             });

//             while (!mIPKeyboard->isThreadDone()) {
//                 nn::os::YieldThread();
//             }

//             if (!mIPKeyboard->isKeyboardCancelled()) {
//                 const char* result = mIPKeyboard->getResult();
//                 if (result && result[0] != '\0') {
//                     // set APIP memember in client here
//                     Client::setApClientIP(result);
//                     Client::setConnectStatusMsg(u"Connecting to Client...");
//                 }
//             }
//         }
//         return GameModeConfigMenu::UpdateAction::NOOP;
//     }
//     case OptionReconnect: {
//         Client::setConnectStatusMsg(u"Connecting to Client...");
//         Client::instance()->startReconnectThread();
//         return GameModeConfigMenu::UpdateAction::NOOP;
//     }
//     case OptionHostName: {
//         // Set Host
//         if (mHostNameKeyboard) {
//             mHostNameKeyboard->openKeyboard(Client::getArchipelagoHost(), [](nn::swkbd::KeyboardConfig& config) {
//                 config.keyboardMode = nn::swkbd::KeyboardMode::ModeASCII;
//                 config.textMaxLength = 64;
//                 config.textMinLength = 0;
//                 config.isUseUtf8 = true;
//                 config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
//             });

//             while (!mHostNameKeyboard->isThreadDone()) {
//                 nn::os::YieldThread();
//             }

//             if (!mHostNameKeyboard->isKeyboardCancelled()) {
//                 const char* result = mHostNameKeyboard->getResult();
//                 if (result && result[0] != '\0') {
//                     Client::setArchipelagoHost(result);
//                     curMode->isNeedArchipelagoConnect = true;
//                 }
//             }
//         }
//         return GameModeConfigMenu::UpdateAction::NOOP;
//     }
//     case OptionHostPort: {
//         // Set Archipelago Port
//         if (mPortKeyboard) {
//             mPortKeyboard->openKeyboard(intToCstr(Client::getArchipelagoPort()), [](nn::swkbd::KeyboardConfig& config) {
//                 config.keyboardMode = nn::swkbd::KeyboardMode::ModeASCII;
//                 config.textMaxLength = 5;
//                 config.textMinLength = 1;
//                 config.isUseUtf8 = true;
//                 config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
//             });

//             while (!mPortKeyboard->isThreadDone()) {
//                 nn::os::YieldThread();
//             }

//             if (!mPortKeyboard->isKeyboardCancelled()) {
//                 const char* result = mPortKeyboard->getResult();
//                 if (result && result[0] != '\0') {
//                     Client::setArchipelagoPort(::atoi(result));
//                     curMode->isNeedArchipelagoConnect = true;
//                 }
//             }
//         }
//         return GameModeConfigMenu::UpdateAction::NOOP;
//     }
//     case OptionSlotName: {
//         // Set Archipelago Slot
//         if (mSlotKeyboard) {
//             mSlotKeyboard->openKeyboard(Client::getArchipelagoSlot(), [](nn::swkbd::KeyboardConfig& config) {
//                 config.keyboardMode = nn::swkbd::KeyboardMode::ModeASCII;
//                 config.textMaxLength = 64;
//                 config.textMinLength = 1;
//                 config.isUseUtf8 = true;
//                 config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
//             });

//             while (!mSlotKeyboard->isThreadDone()) {
//                 nn::os::YieldThread();
//             }

//             if (!mSlotKeyboard->isKeyboardCancelled()) {
//                 const char* result = mSlotKeyboard->getResult();
//                 if (result && result[0] != '\0') {
//                     Client::setArchipelagoSlot(result);
//                     curMode->isNeedArchipelagoConnect = true;
//                 }
//             }
//         }
//         return GameModeConfigMenu::UpdateAction::NOOP;
//     }
//     case OptionPassowrd: {
//         // Set Archipelago Password
//         if (mPasswordKeyboard) {
//             mPasswordKeyboard->openKeyboard(Client::getArchipelagoPassword(), [](nn::swkbd::KeyboardConfig& config) {
//                 config.keyboardMode = nn::swkbd::KeyboardMode::ModeASCII;
//                 config.textMaxLength = 64;
//                 config.textMinLength = 0;
//                 config.isUseUtf8 = true;
//                 config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
//                 config.passwordMode = nn::swkbd::PasswordMode::Hide;
//             });

//             while (!mPasswordKeyboard->isThreadDone()) {
//                 nn::os::YieldThread();
//             }

//             if (!mPasswordKeyboard->isKeyboardCancelled()) {
//                 const char* result = mPasswordKeyboard->getResult();
//                 if (result) {
//                     Client::setArchipelagoPassword(result);
//                     curMode->isNeedArchipelagoConnect = true;
//                 }
//             }
//         }
//         return GameModeConfigMenu::UpdateAction::NOOP;
//     }
//     case OptionDeathlink: {
//         archipelago->setDeathLinkFlag(!archipelago->isDeathLinkEnabled());
//         al::startAction(mList->mListPartsArr[OptionDeathlink], archipelago->isDeathLinkEnabled() ? "On" : "Off", "State");

//         return GameModeConfigMenu::UpdateAction::REFRESH;
//     }

//     case OptionMotionButton: {
//         archipelago->setMotionRebind(!archipelago->isMotionRebound());
//         al::startAction(mList->mListPartsArr[OptionMotionButton], archipelago->isMotionRebound() ? "On" : "Off", "State");

//         return GameModeConfigMenu::UpdateAction::REFRESH;
//     }

//     case OptionDefaultMode: {
//         Client::setDefaultGameMode(Client::getDefaultGameMode() == GameMode::ARCHIPELAGO ? GameMode::HIDEANDSEEK : GameMode::ARCHIPELAGO);
//         al::startAction(mList->mListPartsArr[OptionDefaultMode], Client::getDefaultGameMode() == GameMode::ARCHIPELAGO ? "On" : "Off", "State");

//         return GameModeConfigMenu::UpdateAction::REFRESH;
//     }
//     default:
//         return GameModeConfigMenu::UpdateAction::NOOP;
//     }
// }