#include "Randomizer/ModConfigInfoOverlay.h"

#include "Randomizer/PauseMenuHooks.h"
#include "Scene/StageSceneStateModConfig.hpp"

#include "imgui.h"

#include <basis/seadTypes.h>

namespace ModConfigInfoOverlay {

    namespace {

        constexpr ImU32 kColPanel = IM_COL32(8, 10, 16, 200);
        constexpr ImU32 kColBorder = IM_COL32(255, 218, 88, 175);
        constexpr ImU32 kColText = IM_COL32(255, 255, 255, 255);
        constexpr float kFontSize = 18.0f;
        constexpr float kPadX = 20.0f;
        constexpr float kPadY = 12.0f;
        constexpr float kLineH = 22.0f;
        constexpr float kSideMargin = 40.0f;
        // Clears the pause menu's own button-guide footer at the bottom of the
        // screen; may need tuning once seen against the real in-game layout.
        constexpr float kBottomMargin = 90.0f;
        constexpr s32 kMaxLineChars = 128;

    } // namespace

    void draw() {
        StageSceneStateModConfig* modConfig = PauseMenuHooks::getModConfigInstance();
        if (!modConfig)
            return;

        const char* text = modConfig->getMiscInfoText();
        if (!text || !text[0])
            return;

        ImGuiIO& io = ImGui::GetIO();
        if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
            return;

        s32 lineCount = 1;
        for (const char* p = text; *p; p++)
            if (*p == '\n')
                lineCount++;

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImFont* font = ImGui::GetFont();

        const float panelW = io.DisplaySize.x - kSideMargin * 2.0f;
        const float panelH = kPadY * 2.0f + static_cast<float>(lineCount) * kLineH;
        const float panelX = kSideMargin;
        const float panelY = io.DisplaySize.y - kBottomMargin - panelH;

        dl->AddRectFilled(ImVec2(panelX, panelY), ImVec2(panelX + panelW, panelY + panelH), kColPanel, 8.0f);
        dl->AddRect(ImVec2(panelX, panelY), ImVec2(panelX + panelW, panelY + panelH), kColBorder, 8.0f, 0, 2.0f);

        float y = panelY + kPadY;
        char line[kMaxLineChars];
        s32 lineLen = 0;
        for (const char* p = text;; p++) {
            if (*p == '\n' || *p == '\0') {
                line[lineLen] = '\0';
                dl->AddText(font, kFontSize, ImVec2(panelX + kPadX, y), kColText, line);
                y += kLineH;
                lineLen = 0;
                if (*p == '\0')
                    break;
                continue;
            }
            if (lineLen < kMaxLineChars - 1)
                line[lineLen++] = *p;
        }
    }

} // namespace ModConfigInfoOverlay
