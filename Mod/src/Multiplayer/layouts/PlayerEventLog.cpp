#include "Multiplayer/layouts/PlayerEventLog.h"

#include "nn/account.h"

#include "sead/basis/seadTypes.h"
#include "sead/prim/seadSafeString.h"

#include "al/Library/Base/StringUtil.h"

#include <cstdarg>

#include "Multiplayer/MessageMasterList.h"
#include "Multiplayer/puppets/PuppetInfo.h"
#include "Multiplayer/server/Client.hpp"
#include "Randomizer/AbilityRando.h"
#include "Randomizer/CaptureRando.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/PaintingRando.h"
#include "Randomizer/WorldMeta.h"
#include "Scene/StageSceneStateModConfig.hpp"
#include "imgui.h"

SEAD_SINGLETON_DISPOSER_IMPL(PlayerEventLog)
bool PlayerEventLog::mIsShow = true;

namespace {

    // One colour per team, used for the roster panel's team labels. Matched by
    // index to MultiplayerProtocol's 0-based team numbers, so entry 0 is the
    // colour of "Team 1".
    const ImVec4 cTeamColors[MultiplayerProtocol::cTeamCount] = {
        ImVec4(1.00f, 0.40f, 0.38f, 1.0f), // Team 1 - red
        ImVec4(0.42f, 0.68f, 1.00f, 1.0f), // Team 2 - blue
        ImVec4(0.45f, 0.95f, 0.52f, 1.0f), // Team 3 - green
        ImVec4(1.00f, 0.84f, 0.32f, 1.0f), // Team 4 - yellow
    };

    // The same 1px black offset every row in the feed below draws itself with.
    void textShadowed(const ImVec4& color, const char* text) {
        const ImVec2 cursor = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(cursor.x + 1.0f, cursor.y + 1.0f));
        ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), "%s", text);
        ImGui::SetCursorPos(cursor);
        ImGui::TextColored(color, "%s", text);
    }

    // Which teams actually have someone on them, as a bitmask of team numbers,
    // with the count written back through countOut. Empty teams get no line in
    // the panel, and a room that fits on a single team gets no panel at all -
    // both of the caller's questions come from this one pass over the roster.
    u32 collectOccupiedTeams(s32* countOut) {
        u32 mask = 0;
        s32 count = 0;

        const s32 rosterCount = Client::getRosterCount();
        for (s32 i = 0; i < rosterCount; i++) {
            const TeamRosterEntry* entry = Client::getRosterEntry(i);
            if (!entry || entry->team >= MultiplayerProtocol::cTeamCount)
                continue;

            const u32 bit = 1u << entry->team;
            if (mask & bit)
                continue;
            mask |= bit;
            count++;
        }

        *countOut = count;
        return mask;
    }

    // Roster panel: who is on which team, one line per occupied team.
    //
    // Deliberately names only - no moon counts. Which team someone is on is
    // public; what they have collected is not, which is the same line the event
    // feed below draws (it only ever shows your own team's events).
    //
    // Built from Client's roster rather than the puppet array, because with
    // cross-team sync off an opposing player never becomes a puppet at all -
    // the server stops relaying them before they get that far.
    //
    // Height follows the number of occupied teams, so the feed underneath does
    // shift when a team empties out or a fresh one is joined - the caller
    // derives its offset from the same count for exactly that reason.
    void drawTeamPanel(f32 top, f32 windowWidth, f32 lineHeight, const ImGuiStyle& style,
        u32 occupiedMask, s32 occupiedCount) {
        const f32 panelHeight = (lineHeight * (f32)occupiedCount) + (style.WindowPadding.y * 2.0f);

        ImGui::SetNextWindowPos(ImVec2(0.0f, top), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(windowWidth, panelHeight));
        ImGui::Begin("Team Roster", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar
                | ImGuiWindowFlags_NoSavedSettings);

        const s32 rosterCount = Client::getRosterCount();
        const u8 ownTeam = Client::getTeam();
        const nn::account::Uid ownId = Client::getClientId();

        for (u8 team = 0; team < MultiplayerProtocol::cTeamCount; team++) {
            if (!(occupiedMask & (1u << team)))
                continue;

            // Build the whole line first, then draw it in one shadowed pass -
            // per-name colouring would mean a SameLine chain, and the names are
            // the part that has to stay readable rather than decorative.
            sead::FixedSafeString<256> line;
            line.format("%s  ", Client::getTeamName(team));

            s32 members = 0;
            for (s32 i = 0; i < rosterCount; i++) {
                const TeamRosterEntry* entry = Client::getRosterEntry(i);
                if (!entry || entry->team != team)
                    continue;

                const bool isSelf = entry->id[0] == ownId.m_Storage[0]
                    && entry->id[1] == ownId.m_Storage[1];
                if (members > 0)
                    line.append(", ");
                line.append(isSelf ? "You" : entry->name.cstr());
                // Still inside the reconnect grace window - they hold their
                // team (and its progress) until it expires.
                if (!entry->connected)
                    line.append("*");
                members++;
            }

            // Your own team at full strength, the others dimmed: the panel is
            // mostly there to answer "who am I playing with".
            ImVec4 color = cTeamColors[team];
            if (team != ownTeam) {
                color.x *= 0.65f;
                color.y *= 0.65f;
                color.z *= 0.65f;
                color.w = 0.75f;
            }
            textShadowed(color, line.cstr());
        }

        ImGui::End();
    }

} // namespace

PlayerEventLog::PlayerEventLog() { }

PlayerEventLog::ShineExtras::ShineExtras()
    : rewardIsCapture(false) { }

PlayerEventLog::Entry::Entry() {
    mLife = 0;
}

PlayerEventLog::Entry::Entry(nn::account::Uid player, Event event, sead::FixedSafeString<128> text, const ShineExtras& extras) {
    mPlayerId = player;
    mPlayerName = "???";
    mEvent = event;
    mText = text;
    mShineExtras = extras;
    mLife = calculateLife();
}

PlayerEventLog::Entry::Entry(Event event, sead::FixedSafeString<128> text, const ShineExtras& extras) {
    mPlayerName = "You";
    mIsPlayerName = true;
    mEvent = event;
    mText = text;
    mShineExtras = extras;
    mLife = calculateLife();
}

namespace {
    // File-static rather than a member: addEvent/addSelfEvent are static and
    // run before sInstance exists on some paths.
    bool sIsSuppressed = false;

    // A Multi (triple) moon grants three rewards; nothing in the game grants
    // more, and the joined string is sized on that assumption.
    constexpr s32 cMaxRewardSlots = 3;
} // namespace

PlayerEventLog::SuppressScope::SuppressScope(bool active)
    : mPrev(sIsSuppressed) {
    if (active)
        sIsSuppressed = true;
}

PlayerEventLog::SuppressScope::~SuppressScope() {
    sIsSuppressed = mPrev;
}

bool PlayerEventLog::isSuppressed() {
    return sIsSuppressed;
}

void PlayerEventLog::addEvent(nn::account::Uid player, PlayerEventLog::Event event, sead::SafeString text, const ShineExtras& extras, s32 amount) {
    if (!sInstance || sIsSuppressed)
        return;

    if (event == PEACH_QUEST) {
        for (s32 i = 0; i < sNumEntries; i++) {
            if (sInstance->mLog[i].mEvent == PEACH_QUEST
                && sInstance->mLog[i].mPlayerId == player)
                return;
        }
    }

    // prevent duplicate entries for shines
    if (event == SHINE) {
        for (s32 i = 0; i < sNumEntries; i++) {
            if (sInstance->mLog[i].mEvent == SHINE && al::isEqualString(sInstance->mLog[i].mText, text))
                return;
        }
    }

    // if an entry already exists for purples/coins, increase the number
    if (event == PURPLE || event == COIN) {
        for (s32 i = 0; i < sNumEntries; i++) {
            if (sInstance->mLog[i].mEvent == event) {
                if (sInstance->mLog[i].mPlayerId == player && al::isEqualString(sInstance->mLog[i].mText, text)) {
                    s32 numPurples = sInstance->mLog[i].mNumPurples + amount;
                    for (s32 j = i; j > 0; j--) {
                        sInstance->mLog[j] = sInstance->mLog[j - 1];
                    }
                    sInstance->mLog[0] = Entry(player, event, text);
                    sInstance->mLog[0].mNumPurples = numPurples;
                    return;
                }
            }
        }
    }

    for (s32 i = sNumEntries - 1; i > 0; i--) {
        sInstance->mLog[i] = sInstance->mLog[i - 1];
    }

    sInstance->mLog[0] = Entry(player, event, text, extras);
    if (event == PURPLE || event == COIN)
        sInstance->mLog[0].mNumPurples = amount;
}

void PlayerEventLog::addSelfEvent(PlayerEventLog::Event event, sead::SafeString text, const ShineExtras& extras, s32 amount) {
    if (sIsSuppressed)
        return;

    if (!sInstance)
        return;

    if (event == PEACH_QUEST) {
        for (s32 i = 0; i < sNumEntries; i++) {
            if (sInstance->mLog[i].mEvent == PEACH_QUEST
                && sInstance->mLog[i].mPlayerName == "You")
                return;
        }
    }

    // prevent duplicate entries for shines
    if (event == SHINE) {
        for (s32 i = 0; i < sNumEntries; i++) {
            if (sInstance->mLog[i].mEvent == SHINE && al::isEqualString(sInstance->mLog[i].mText, text))
                return;
        }
    }

    // if an entry already exists for purples/coins, increase the number
    if (event == PURPLE || event == COIN) {
        for (s32 i = 0; i < sNumEntries; i++) {
            if (sInstance->mLog[i].mEvent == event) {
                if (sInstance->mLog[i].mPlayerName == "You" && al::isEqualString(sInstance->mLog[i].mText, text)) {
                    s32 numPurples = sInstance->mLog[i].mNumPurples + amount;
                    for (s32 j = i; j > 0; j--) {
                        sInstance->mLog[j] = sInstance->mLog[j - 1];
                    }
                    sInstance->mLog[0] = Entry(event, text);
                    sInstance->mLog[0].mNumPurples = numPurples;
                    return;
                }
            }
        }
    }

    for (s32 i = sNumEntries - 1; i > 0; i--) {
        sInstance->mLog[i] = sInstance->mLog[i - 1];
    }

    sInstance->mLog[0] = Entry(event, text, extras);
    if (event == PURPLE || event == COIN)
        sInstance->mLog[0].mNumPurples = amount;
}

void PlayerEventLog::update() {
    // Small always-on connection status indicator, independent of mIsShow
    // (the moon-log window below) - gated on hasAttemptedConnection() rather
    // than isSocketActive() alone so a solo player who never opened the
    // title's Multiplayer menu never sees it at all. Direct answer to "am I
    // actually connected right now" without needing to check a debug log.
    if (Client::hasAttemptedConnection()) {
        ImGuiStyle& statusStyle = ImGui::GetStyle();
        f32 statusLineHeight = ImGui::GetTextLineHeightWithSpacing();
        ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(180.0f, statusLineHeight + statusStyle.WindowPadding.y * 2.0f));
        ImGui::Begin("MP Status", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
                | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

        const bool connected = Client::isSocketActive();
        const char* statusText = connected ? "MP: Connected" : "MP: Disconnected";
        ImVec2 statusCursor = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(statusCursor.x + 1.0f, statusCursor.y + 1.0f));
        ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), "%s", statusText);
        ImGui::SetCursorPos(statusCursor);
        ImGui::TextColored(
            connected ? ImVec4(0.2f, 1.0f, 0.2f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", statusText);

        ImGui::End();
    }

    // Multiplayer-only, on the same gate the status indicator above uses. The
    // self-event hooks (syncHooks.cpp) fire on the local player's own moons,
    // coins and checkpoints whether or not anyone else is in the room, so
    // without this a solo run gets a running commentary on its own pickups -
    // a multiplayer feature leaking into single player.
    if (mIsShow && Client::hasAttemptedConnection()) {
        tryUpdateNames();

        f32 width = ImGui::GetIO().DisplaySize.x;
        f32 height = ImGui::GetIO().DisplaySize.y;
        ImGuiStyle& style = ImGui::GetStyle();
        ImFont* font = ImGui::GetFont();
        f32 charWidth = font->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0.0f, "X").x;
        f32 lineHeight = ImGui::GetTextLineHeightWithSpacing();
        f32 windowWidth = (charWidth * 128.0f) + (style.WindowPadding.x * 2.0f) + (style.FramePadding.x * 2.0f);
        f32 windowHeight = (lineHeight * 8.0f) + (style.WindowPadding.y * 2.0f); // + ImGui::GetFrameHeight();

        // The roster panel takes the spot the feed used to start at, and the
        // feed slides down by exactly its height - so the pair together still
        // occupies the same band of the left edge the feed alone used to.
        //
        // With everyone on one team - or a roster that has not arrived yet -
        // the panel says nothing the player does not already know, so it is
        // dropped entirely and the feed keeps the whole band to itself.
        s32 occupiedTeams = 0;
        const u32 occupiedMask = collectOccupiedTeams(&occupiedTeams);
        const bool showTeamPanel = occupiedTeams > 1;

        const f32 panelTop = (height - windowHeight) * 0.5f;
        const f32 panelHeight = showTeamPanel
            ? (lineHeight * (f32)occupiedTeams) + (style.WindowPadding.y * 2.0f)
            : 0.0f;
        if (showTeamPanel)
            drawTeamPanel(panelTop, windowWidth, lineHeight, style, occupiedMask, occupiedTeams);

        // Always, not FirstUseEver: the offset below depends on the panel's
        // height, and FirstUseEver would freeze the feed wherever the first
        // frame happened to put it - before the roster had even arrived.
        ImGui::SetNextWindowPos(ImVec2(0, panelTop + panelHeight), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(windowWidth, windowHeight));
        ImGui::Begin("Player Event Log", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);

        for (s32 i = 0; i < sNumEntries; i++) {
            if (mLog[i].mLife != 0) {
                ImVec2 originalCursor = ImGui::GetCursorPos();
                switch (mLog[i].mEvent) {
                case CONNECT: {
                    ImGui::SetCursorPos(ImVec2(originalCursor.x + 1.0f, originalCursor.y + 1.0f));
                    ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), "%s connected",
                        mLog[i].mPlayerName.cstr());

                    ImGui::SetCursorPos(originalCursor);
                    ImGui::Text("%s connected", mLog[i].mPlayerName.cstr());
                    break;
                }
                case DISCONNECT: {
                    ImGui::SetCursorPos(ImVec2(originalCursor.x + 1.0f, originalCursor.y + 1.0f));
                    ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), "%s disconnected",
                        mLog[i].mPlayerName.cstr());

                    ImGui::SetCursorPos(originalCursor);
                    ImGui::Text("%s disconnected", mLog[i].mPlayerName.cstr());
                    break;
                }
                case START: {
                    ImGui::SetCursorPos(ImVec2(originalCursor.x + 1.0f, originalCursor.y + 1.0f));
                    ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), "%s started",
                        mLog[i].mPlayerName.cstr());

                    ImGui::SetCursorPos(originalCursor);
                    ImGui::Text("%s started", mLog[i].mPlayerName.cstr());
                    break;
                }
                case PEACH_QUEST: {
                    ImGui::SetCursorPos(ImVec2(originalCursor.x + 1.0f, originalCursor.y + 1.0f));
                    ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), "%s started the ",
                        mLog[i].mPlayerName.cstr());
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), "Peach Quest");

                    ImGui::SetCursorPos(originalCursor);
                    ImGui::Text("%s started the ", mLog[i].mPlayerName.cstr());
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::TextColored(ImVec4(1.0f, 0.41f, 0.71f, 1.0f), "Peach Quest");
                    break;
                }
                case SHINE: {
                    const ShineExtras& extras = mLog[i].mShineExtras;
                    const bool hasMoonRockKey = !extras.moonRockKeyText.isEmpty();
                    const bool hasPainting = !extras.paintingText.isEmpty();
                    const bool hasReward = !extras.rewardText.isEmpty();

                    ImGui::SetCursorPos(ImVec2(originalCursor.x + 1.0f, originalCursor.y + 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
                    ImGui::Text("%s ", mLog[i].mPlayerName.cstr());
                    ImGui::SameLine(0, 0);
                    ImGui::Text("got the moon ");
                    ImGui::SameLine(0, 0);
                    ImGui::Text("%s", mLog[i].mText.cstr());
                    if (hasMoonRockKey) {
                        ImGui::SameLine(0, 0);
                        ImGui::Text(" %s", extras.moonRockKeyText.cstr());
                    }
                    if (hasPainting) {
                        ImGui::SameLine(0, 0);
                        ImGui::Text(" %s", extras.paintingText.cstr());
                    }
                    if (hasReward) {
                        ImGui::SameLine(0, 0);
                        ImGui::Text(" %s", extras.rewardText.cstr());
                    }
                    ImGui::SameLine(0, 0);
                    ImGui::PopStyleColor();

                    ImGui::SetCursorPos(originalCursor);
                    ImGui::Text("%s ", mLog[i].mPlayerName.cstr());

                    ImGui::SameLine(0, 0);
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "got the moon ");

                    // Moon name/kingdom always yellow; each extra category
                    // gets its own color (agreed with Secret Dev) so they're
                    // distinguishable from each other at a glance, not just
                    // from a plain moon pickup: Moon Rock Key = red, Painting
                    // = blue, Capture = orange, Ability = grey.
                    ImGui::SameLine(0, 0);
                    ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.0f, 1.0f), "%s", mLog[i].mText.cstr());

                    if (hasMoonRockKey) {
                        ImGui::SameLine(0, 0);
                        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), " %s", extras.moonRockKeyText.cstr());
                    }
                    if (hasPainting) {
                        ImGui::SameLine(0, 0);
                        ImGui::TextColored(ImVec4(0.0f, 0.5f, 1.0f, 1.0f), " %s", extras.paintingText.cstr());
                    }
                    if (hasReward) {
                        ImGui::SameLine(0, 0);
                        ImGui::TextColored(extras.rewardIsCapture ? ImVec4(1.0f, 0.6f, 0.0f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                            " %s", extras.rewardText.cstr());
                    }
                    break;
                }
                case PURPLE: {
                    const char* plural = mLog[i].mNumPurples > 1 ? "s" : "";

                    ImGui::SetCursorPos(ImVec2(originalCursor.x + 1.0f, originalCursor.y + 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
                    ImGui::Text("%s ", mLog[i].mPlayerName.cstr());
                    ImGui::SameLine(0, 0);
                    ImGui::Text("got ");
                    ImGui::SameLine(0, 0);
                    ImGui::Text("%d %s ", mLog[i].mNumPurples, mLog[i].mText.cstr());
                    ImGui::SameLine(0, 0);
                    ImGui::Text("regional coin%s", plural);
                    ImGui::PopStyleColor();

                    ImGui::SetCursorPos(originalCursor);
                    ImGui::Text("%s ", mLog[i].mPlayerName.cstr());

                    ImGui::SameLine(0, 0);
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "got ");

                    ImGui::SameLine(0, 0);
                    ImGui::TextColored(ImVec4(0.85f, 0.0f, 1.0f, 1.0f), "%d %s ", mLog[i].mNumPurples,
                        mLog[i].mText.cstr());

                    ImGui::SameLine(0, 0);
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "regional coin%s", plural);
                    break;
                }
                case COIN: {
                    const char* plural = mLog[i].mNumPurples > 1 ? "s" : "";

                    ImGui::SetCursorPos(ImVec2(originalCursor.x + 1.0f, originalCursor.y + 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
                    ImGui::Text("%s ", mLog[i].mPlayerName.cstr());
                    ImGui::SameLine(0, 0);
                    ImGui::Text("got ");
                    ImGui::SameLine(0, 0);
                    ImGui::Text("%d coin%s", mLog[i].mNumPurples, plural);
                    ImGui::PopStyleColor();

                    ImGui::SetCursorPos(originalCursor);
                    ImGui::Text("%s ", mLog[i].mPlayerName.cstr());

                    ImGui::SameLine(0, 0);
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "got ");

                    ImGui::SameLine(0, 0);
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.1f, 1.0f), "%d coin%s", mLog[i].mNumPurples, plural);
                    break;
                }
                case PURPLE_BUY: {
                    ImGui::SetCursorPos(ImVec2(originalCursor.x + 1.0f, originalCursor.y + 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
                    ImGui::Text("%s ", mLog[i].mPlayerName.cstr());
                    ImGui::SameLine(0, 0);
                    ImGui::Text("bought ");
                    ImGui::SameLine(0, 0);
                    ImGui::Text("%s ", mLog[i].mText.cstr());
                    ImGui::SameLine(0, 0);
                    ImGui::Text("(Purple Shop)");
                    ImGui::PopStyleColor();

                    ImGui::SetCursorPos(originalCursor);
                    ImGui::Text("%s ", mLog[i].mPlayerName.cstr());

                    ImGui::SameLine(0, 0);
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "bought ");

                    ImGui::SameLine(0, 0);
                    ImGui::TextColored(ImVec4(0.7f, 0.3f, 1.0f, 1.0f), "%s ", mLog[i].mText.cstr());

                    ImGui::SameLine(0, 0);
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "(Purple Shop)");
                    break;
                }
                case DEATH: {
                    ImGui::SetCursorPos(ImVec2(originalCursor.x + 1.0f, originalCursor.y + 1.0f));
                    ImGui::TextColored(ImVec4(0.0f, 0.0f, 0.0f, 1.0f), "%s died",
                        mLog[i].mPlayerName.cstr());

                    ImGui::SetCursorPos(originalCursor);
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s died", mLog[i].mPlayerName.cstr());
                    break;
                }
                case CHECKPOINT: {
                    ImGui::SetCursorPos(ImVec2(originalCursor.x + 1.0f, originalCursor.y + 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
                    ImGui::Text("%s ", mLog[i].mPlayerName.cstr());
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text("got the checkpoint ");
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text("%s", mLog[i].mText.cstr());
                    ImGui::PopStyleColor();

                    ImGui::SetCursorPos(originalCursor);
                    ImGui::Text("%s ", mLog[i].mPlayerName.cstr());

                    ImGui::SameLine(0, 0);
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "got the checkpoint ");

                    ImGui::SameLine(0, 0);
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%s", mLog[i].mText.cstr());
                    break;
                }
                case MOONROCK: {
                    ImGui::SetCursorPos(ImVec2(originalCursor.x + 1.0f, originalCursor.y + 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
                    ImGui::Text("%s ", mLog[i].mPlayerName.cstr());
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text("hit the moon rock in ");
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::Text("%s", mLog[i].mText.cstr());
                    ImGui::SameLine(0.0f, 0.0f);
                    ImGui::PopStyleColor();

                    ImGui::SetCursorPos(originalCursor);
                    ImGui::Text("%s ", mLog[i].mPlayerName.cstr());

                    ImGui::SameLine(0, 0);
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "hit the moon rock in ");

                    ImGui::SameLine(0, 0);
                    ImGui::TextColored(ImVec4(0.0f, 0.5f, 1.0f, 1.0f), "%s", mLog[i].mText.cstr());
                    break;
                }
                }
            }
        }
        ImGui::End();
    }

    // tick down life even when hidden
    for (s32 i = 0; i < sNumEntries; i++) {
        if (mLog[i].mLife > 0)
            mLog[i].mLife--;
    }
}

void PlayerEventLog::tryUpdateNames() {
    for (s32 i = 0; i < sNumEntries; i++) {
        if (mLog[i].mIsPlayerName)
            continue;

        if (mLog[i].mPlayerId.isEmpty()) {
            mLog[i].mPlayerName = "Server";
            mLog[i].mIsPlayerName = true;
            continue;
        }

        PuppetInfo* player = Client::findPuppetInfo(mLog[i].mPlayerId, false);
        if (!player) {
            continue;
        }

        mLog[i].mPlayerName = player->puppetName;
        mLog[i].mIsPlayerName = true;
    }
}

s32 PlayerEventLog::calculateLife() {
    // 5 seconds at 60fps. The decay loop at the end of update() ticks this
    // down even while the window is hidden, and an entry at 0 stops being
    // drawn, so this is all that separates "scrolls away" from the previous
    // behaviour of every event from the whole session staying on screen.
    return 300;
}

const char* PlayerEventLog::getShineMessage(sead::SafeString stage, sead::SafeString objId) {
    for (const MessageMasterList::MessageData& data : MessageMasterList::shineList) {
        if (al::isEqualString(stage, data.stage) && al::isEqualString(objId, data.objId)) {
            return data.text;
        }
    }
    return "NULL";
}

// Wraps getShineMessage, but reports the moon by where it physically is
// rather than what it's credited as -- e.g. picking up the moon sitting at
// "Push-Block Peril" but remapped to count toward a Snow Kingdom slot shows
// as "Push-Block Peril (Snow)", using the physical location's own name plus
// the kingdom it actually credits. Vanilla and non-remapped slots have no
// placement entry and fall straight through to the plain message, since
// there source and destination are the same place anyway.
// Which kingdom's Moon Rock Key this uid/stage is, or -1 if it isn't one.
// getMoonRockKeyForKingdom() only goes kingdom -> entry; this is the reverse
// lookup, done by scanning every kingdom since there's no direct index.
static s32 findMoonRockKeyKingdom(s32 uid, const char* stage) {
    for (s32 k = 0; k < WorldMeta::sWorldCount; k++) {
        const ShineDB::ShineEntry* key = MoonRando::getMoonRockKeyForKingdom(k);
        if (key && key->uid == uid && al::isEqualString(stage, key->stage))
            return k;
    }
    return -1;
}

const char* PlayerEventLog::getShineMessageWithSource(s32 uid, sead::SafeString stage, sead::SafeString objId) {
    static sead::FixedSafeString<128> sMessageWithSource;

    const char* baseMessage = getShineMessage(stage, objId);

    const MoonRando::Placement* placement = MoonRando::getPlacementForItem(uid, stage.cstr());
    if (!placement || !placement->location || !placement->item
        || placement->location->worldId == placement->item->worldId) {
        return baseMessage;
    }

    const WorldMeta::Entry* destWorld = WorldMeta::getByWorldId(placement->item->worldId);
    const char* locationName = placement->location->name ? placement->location->name : baseMessage;
    sMessageWithSource.format("%s (%s)", locationName, destWorld ? destWorld->displayName : "???");
    return sMessageWithSource.cstr();
}

// See the header comment on ShineExtras/getShineExtras - each category is a
// separate, independently-colored segment (rendered in update()'s SHINE
// case), not appended into the plain moon-name text at all.
PlayerEventLog::ShineExtras PlayerEventLog::getShineExtras(s32 uid, sead::SafeString stage) {
    ShineExtras extras;

    // Moon Rock Keys aren't a real kingdom moon - getShineMessage() above
    // returns whatever generic ShineDB name it happens to carry, not
    // anything that says "this was a key." Say so explicitly, same as the
    // reveal-gate in HintSystem.cpp already treats Moon Rock Key rows as a
    // distinct case. Mutually exclusive with painting/reward below - a Moon
    // Rock Key is never also a painting-unlock or ability/capture moon.
    const s32 keyKingdom = findMoonRockKeyKingdom(uid, stage.cstr());
    if (keyKingdom >= 0) {
        const WorldMeta::Entry* kingdom = WorldMeta::getByWorldId(keyKingdom);
        extras.moonRockKeyText.format("(%s Moon Rock Key)", kingdom ? kingdom->displayName : "???");
        return extras;
    }

    const ShineDB::ShineEntry* entry = ShineDB::findEntry(uid, stage.cstr());
    if (!entry)
        return extras;

    // A painting-unlock moon - same info PaintingUnlockOverlay shows in-game
    // (queueCloudPaintingUnlocks, MoonHooks.cpp) but not otherwise visible in
    // this log. Only the first unlock is shown (a moon unlocking more than
    // one link at once is not a normal case).
    if (PaintingRando::getCloudMoonPaintingUnlockCount(*entry) > 0) {
        const s32 paintingIndex = PaintingRando::getCloudMoonPaintingUnlockIndex(*entry, 0);
        const char* sourceKingdom = nullptr;
        const char* destinationKingdom = nullptr;
        if (PaintingRando::getPaintingUnlockKingdomNames(paintingIndex, &sourceKingdom, &destinationKingdom)
            && sourceKingdom && destinationKingdom)
            extras.paintingText.format("+ Painting: %s -> %s", sourceKingdom, destinationKingdom);
    }

    // Which capture this moon grants, if any - CaptureRando::unlockForMoon
    // is a direct getCaptureForMoon(moon) lookup (CaptureRando.cpp:785), so
    // this static per-moon fact always matches what actually gets granted,
    // to whichever player collects it (seed-level, not save-specific).
    //
    // Deliberately NOT doing the same for abilities here: unlike captures,
    // AbilityRando::unlockForMoon does NOT just look up "this moon's
    // ability" and grant it - it resolves the whole progression chain
    // against what's *already unlocked on that specific save*
    // (findPendingAwardAbility/resolveAwardAbilityFromSnapshot,
    // AbilityRando.cpp) and can award an earlier prerequisite, or a
    // completely different chain member, than this moon's own nominal
    // sMoonAbilities entry. Confirmed via real reports: a moon nominally
    // assigned "Ground Pound" actually granted "Roll Boost". Since the
    // result depends on per-save progress, it's only knowable by asking
    // what unlockForMoon actually just did (see fillLastUnlockedAbility()
    // below), and only for the SAME save that just ran it - never
    // knowable from a network packet describing another player's pickup.
    // Every slot, not just the first. A Multi (triple) moon carries three
    // capture assignments - a Mushroom Kingdom multi is the usual case - and
    // logging only slot 0 hid the other two rewards entirely. Slots are
    // contiguous from 0, so stopping at the first null is the count.
    sead::FixedSafeString<96> captureList;
    for (s32 i = 0; i < cMaxRewardSlots; i++) {
        const CaptureRando::CaptureEntry* capture = CaptureRando::getCaptureForMoon(uid, stage.cstr(), i);
        if (!capture)
            break;
        const char* name = capture->displayName ? capture->displayName : capture->hackName;
        if (captureList.isEmpty())
            captureList.copy(name);
        else
            captureList.appendWithFormat(", %s", name);
    }
    if (!captureList.isEmpty()) {
        extras.rewardText.format("(%s)", captureList.cstr());
        extras.rewardIsCapture = true;
    }

    return extras;
}

// Only valid immediately after AbilityRando::unlockForMoon() has just run
// for this exact moon on THIS save (i.e. only from the local player's own
// collection path, syncHooks.cpp's mpOnSetGotShine - applyUnlockFeedback
// always runs before it in the same call chain). Never call this for a
// remote player's collection: what they got depends on their own save's
// progression state, which this console has no way to know.
void PlayerEventLog::fillLastUnlockedAbility(ShineExtras& extras) {
    if (!extras.rewardText.isEmpty())
        return; // a capture already claimed the one reward slot
    const s32 count = AbilityRando::getLastUnlockedAbilityCount();
    if (count <= 0)
        return;

    // All of them. unlockForMoon grants three for a Multi moon - a Dark or
    // Darker Side multi is the usual case - and the count has always been
    // available here; only index 0 was ever read.
    sead::FixedSafeString<96> abilityList;
    for (s32 i = 0; i < count && i < cMaxRewardSlots; i++) {
        const AbilityRando::AbilityEntry* ability = AbilityRando::getLastUnlockedAbility(i);
        if (!ability)
            continue;
        const char* name = ability->displayName ? ability->displayName : ability->key;
        if (abilityList.isEmpty())
            abilityList.copy(name);
        else
            abilityList.appendWithFormat(", %s", name);
    }
    if (!abilityList.isEmpty())
        extras.rewardText.format("(%s)", abilityList.cstr());
}

void PlayerEventLog::fillAbilityById(ShineExtras& extras, s32 abilityId, const char* extraIds) {
    if (!extras.rewardText.isEmpty())
        return; // a capture already claimed the reward slot
    if (abilityId < 0)
        return;
    const AbilityRando::AbilityEntry* ability = AbilityRando::getAbility(abilityId);
    if (!ability)
        return;

    sead::FixedSafeString<96> list;
    list.copy(ability->displayName ? ability->displayName : ability->key);

    // The rest of a Multi moon's abilities, which only the sender could know.
    // Parsed rather than trusted: this string came off the wire, so a malformed
    // or over-long entry simply stops the walk instead of being followed.
    if (extraIds) {
        s32 taken = 1;
        const char* cursor = extraIds;
        while (*cursor && taken < cMaxRewardSlots) {
            s32 id = 0;
            bool sawDigit = false;
            while (*cursor >= '0' && *cursor <= '9') {
                id = id * 10 + (*cursor - '0');
                if (id > AbilityRando::AbilityId_Count)
                    return; // nonsense id, stop rather than guess
                cursor++;
                sawDigit = true;
            }
            if (!sawDigit)
                break;
            if (const AbilityRando::AbilityEntry* extra = AbilityRando::getAbility(id)) {
                list.appendWithFormat(", %s", extra->displayName ? extra->displayName : extra->key);
                taken++;
            }
            if (*cursor == ',')
                cursor++;
            else
                break;
        }
    }

    extras.rewardText.format("(%s)", list.cstr());
}

const char* PlayerEventLog::getCheckpointMessage(sead::SafeString objId) {
    for (const MessageMasterList::MessageData& data : MessageMasterList::checkpointList) {
        if (al::isEqualString(objId, data.objId)) {
            return data.text;
        }
    }
    return "NULL";
}

const char* PlayerEventLog::getAchievementMessage(sead::SafeString label) {
    for (const MessageMasterList::AchievementMessageData& data : MessageMasterList::achievementList) {
        if (al::isEqualString(label, data.label)) {
            return data.text;
        }
    }
    return "NULL";
}