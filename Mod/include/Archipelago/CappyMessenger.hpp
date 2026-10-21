#pragma once
#include "hk/hook/Replace.h"
#include "hk/hook/Trampoline.h"

#include "sead/prim/seadSafeString.h"

#include "al/Library/Message/MessageHolder.h"

#include "Archipelago/ArchipelagoCappyMessage.h"
// #include "server/gamemode/GameModeManager.hpp"

// ----- Cappy Messenger: text-system intercept -----
//
// Four trampolines on al's per-mstxt-file message accessors. When
// CapMessageLayout::exeDelay (called from rs::tryShowCapMessagePriorityLow
// downstream) asks for ArchipelagoMode::kArchipelagoCappyLabel and a Cappy
// buffer is currently live, return our UTF-16 buffer and synthesize the
// "label exists" probe. All four are hooked because exeDelay dispatches
// through either the System or Stage variant based on
// CapMessageShowInfo::isStageMessage; rs::tryShowCapMessagePriorityLow uses
// the System path but defensive hooking of both costs little and protects
// against future code that uses the Stage path.

static bool isExistCappyLabelInSystemMessageHook(const al::IUseMessageSystem* sys, const char* mstxt, const char* label) {
    if (Archipelago::isArchipelagoActive()) {
        if (Archipelago::lookupCappyMessageSubstitution(label) != nullptr) {
            return true;
        }
    }
    return al::isExistLabelInSystemMessage(sys, mstxt, label);
}

static const char16_t* getSystemMessageCappyStringHook(const al::IUseMessageSystem* sys, const char* mstxt, const char* label) {
    if (Archipelago::isArchipelagoActive()) {
        const char16_t* sub = Archipelago::lookupCappyMessageSubstitution(label);
        if (sub)
            return sub;
    }
    return al::getSystemMessageString(sys, mstxt, label);
}

static bool isExistCappyLabelInStageMessageHook(const al::IUseMessageSystem* sys, const char* mstxt, const char* label) {
    if (Archipelago::isArchipelagoActive()) {
        if (Archipelago::lookupCappyMessageSubstitution(label) != nullptr) {
            return true;
        }
    }
    return al::isExistLabelInStageMessage(sys, mstxt, label);
}

static const char16_t* getStageMessageCappyStringHook(const al::IUseMessageSystem* sys, const char* mstxt, const char* label) {
    if (Archipelago::isArchipelagoActive()) {
        const char16_t* sub = Archipelago::lookupCappyMessageSubstitution(label);
        if (sub)
            return sub;
    }
    return al::getStageMessageString(sys, mstxt, label);
}