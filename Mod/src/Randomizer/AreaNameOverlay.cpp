#include "Randomizer/AreaNameOverlay.h"

#include "Randomizer/LogicData.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/WorldMeta.h"

#include "helpers.hpp"

#include "Scene/StageScene.h"
#include "al/Library/Base/StringUtil.h"
#include "al/Library/Player/PlayerHolder.h"
#include "al/Library/Player/PlayerUtil.h"
#include "al/Library/Scene/SceneUtil.h"

#include "imgui.h"
#include "nn/util.h"

#include <cstring>

namespace AreaNameOverlay {

    namespace {

        namespace LD = Randomizer::LogicData;

        // show() fires from StageScene::init, well before the loading screen
        // resolves and the stage actually fades into view. Wait for the player
        // actor to actually exist in the scene (the same "deferred player" gap
        // several other hooks in this mod already guard against) before starting
        // the fade-in, so the banner lines up with the player spawning instead of
        // firing during the load. kMaxWaitFrames is just a safety net in case the
        // player actor never turns up for some reason (so the banner isn't lost).
        constexpr s32 kMaxWaitFrames = 300;
        constexpr s32 kShowFrames = 210;
        constexpr s32 kFadeInFrames = 20;
        constexpr s32 kFadeOutFrames = 40;

        bool sPending = false;
        s32 sWaitFrames = 0;
        s32 sFramesLeft = 0;
        char sAreaName[96] = {};
        char sLastStageName[96] = {};

        bool isEqual(const char* a, const char* b) {
            return a && b && al::isEqualString(a, b);
        }

        bool isPlayerActorReady() {
            StageScene* scene = getStageScene();
            if (!scene)
                return false;
            al::PlayerHolder* holder = al::getScenePlayerHolder(scene);
            return holder && al::tryGetPlayerActor(holder, 0) != nullptr;
        }

        // LD::sStages covers every randomizable stage (kingdom home stages and
        // subareas alike) with a curated human-readable name. Fall back to the
        // kingdom's WorldMeta name, then the raw stage name, for anything missed.
        const char* findDisplayName(const char* stageName) {
            for (s32 i = 0; i < LD::sStageCount; i++) {
                if (isEqual(LD::sStages[i].stage, stageName))
                    return LD::sStages[i].displayName;
            }
            if (const WorldMeta::Entry* entry = WorldMeta::findByMainStage(stageName))
                return entry->displayName;
            return stageName;
        }

        f32 calcAlpha() {
            if (sFramesLeft > kShowFrames - kFadeInFrames) {
                const s32 elapsed = kShowFrames - sFramesLeft;
                return (f32)elapsed / (f32)kFadeInFrames;
            }
            if (sFramesLeft < kFadeOutFrames)
                return (f32)sFramesLeft / (f32)kFadeOutFrames;
            return 1.0f;
        }

    } // namespace

    void show(const char* stageName) {
        if (!stageName || !stageName[0])
            return;
        if (isEqual(stageName, sLastStageName))
            return;

        nn::util::SNPrintf(sLastStageName, sizeof(sLastStageName), "%s", stageName);
        const char* displayName = findDisplayName(stageName);
        nn::util::SNPrintf(sAreaName, sizeof(sAreaName), "%s", displayName ? displayName : stageName);
        sPending = true;
        sWaitFrames = 0;
        sFramesLeft = 0;
    }

    void draw() {
        if (!Randomizer::isAreaNameOverlayEnabled()) {
            sPending = false;
            sFramesLeft = 0;
            return;
        }

        if (sPending) {
            sWaitFrames++;
            if (!isPlayerActorReady() && sWaitFrames < kMaxWaitFrames)
                return;
            sPending = false;
            sFramesLeft = kShowFrames;
        }

        if (sFramesLeft <= 0)
            return;

        const ImGuiIO& io = ImGui::GetIO();
        if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
            return;

        const f32 alpha = calcAlpha();
        ImFont* font = ImGui::GetFont();
        constexpr f32 fontSize = 30.0f;
        const ImVec2 textSize = font->CalcTextSizeA(fontSize, 3.402823e38F, 0.0f, sAreaName);

        const f32 centerX = io.DisplaySize.x * 0.5f;
        constexpr f32 topY = 60.0f;
        const f32 textX = centerX - textSize.x * 0.5f;

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const ImU32 shadowColor = IM_COL32(0, 0, 0, (s32)(200.0f * alpha));
        const ImU32 textColor = IM_COL32(255, 255, 255, (s32)(255.0f * alpha));
        const ImU32 lineColor = IM_COL32(255, 218, 88, (s32)(200.0f * alpha));

        dl->AddText(font, fontSize, ImVec2(textX + 2.0f, topY + 2.0f), shadowColor, sAreaName);
        dl->AddText(font, fontSize, ImVec2(textX, topY), textColor, sAreaName);
        dl->AddLine(ImVec2(centerX - textSize.x * 0.5f, topY + fontSize + 6.0f),
            ImVec2(centerX + textSize.x * 0.5f, topY + fontSize + 6.0f), lineColor, 2.0f);

        sFramesLeft--;
    }

} // namespace AreaNameOverlay
