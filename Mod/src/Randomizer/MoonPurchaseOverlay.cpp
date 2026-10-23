#include "Randomizer/MoonPurchaseOverlay.h"

#include "Randomizer/WorldMeta.h"
#include "ShineDB.h"

#include "imgui.h"
#include "nn/util.h"

namespace MoonPurchaseOverlay {

    namespace {

        static constexpr s32 kFadeInFrames = 24;
        static constexpr s32 kHoldFrames = 90;
        static constexpr s32 kFadeOutFrames = 24;
        static constexpr s32 kTotalFrames = kFadeInFrames + kHoldFrames + kFadeOutFrames;

        static char sMoonText[192] = {};
        static s32 sTimer = 0;
        static bool sActive = false;
        static bool sHasCompanionOverlay = false;

        static f32 clamp01(f32 value) {
            if (value < 0.0f)
                return 0.0f;
            if (value > 1.0f)
                return 1.0f;
            return value;
        }

        static ImU32 colorWithAlpha(s32 r, s32 g, s32 b, s32 baseAlpha, f32 alpha) {
            s32 a = static_cast<s32>(static_cast<f32>(baseAlpha) * clamp01(alpha));
            if (a < 0)
                a = 0;
            if (a > 255)
                a = 255;
            return IM_COL32(r, g, b, a);
        }

        static f32 calcAlpha() {
            if (sTimer < kFadeInFrames)
                return clamp01(static_cast<f32>(sTimer) / static_cast<f32>(kFadeInFrames));
            if (sTimer < kFadeInFrames + kHoldFrames)
                return 1.0f;

            const s32 fadeFrame = sTimer - kFadeInFrames - kHoldFrames;
            return 1.0f - clamp01(static_cast<f32>(fadeFrame) / static_cast<f32>(kFadeOutFrames));
        }

        static void addCenteredText(ImDrawList* drawList, const ImVec2& center, const char* text,
            f32 fontSize, f32 maxWidth, ImU32 color) {
            if (!text || !text[0])
                return;

            ImFont* font = ImGui::GetFont();
            ImVec2 textSize = font->CalcTextSizeA(fontSize, 3.402823e38F, 0.0f, text);
            if (textSize.x > maxWidth && textSize.x > 1.0f) {
                fontSize *= maxWidth / textSize.x;
                if (fontSize < 24.0f)
                    fontSize = 24.0f;
                textSize = font->CalcTextSizeA(fontSize, 3.402823e38F, 0.0f, text);
            }

            drawList->AddText(font, fontSize,
                ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f),
                color, text);
        }

        static void drawOverlay(f32 alpha) {
            ImGuiIO& io = ImGui::GetIO();
            if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
                return;

            ImDrawList* drawList = ImGui::GetForegroundDrawList();
            const f32 boxWidth = io.DisplaySize.x < 1160.0f ? io.DisplaySize.x - 120.0f : 1040.0f;
            const f32 boxHeight = 118.0f;
            const f32 centerX = io.DisplaySize.x * 0.5f;
            const f32 centerY = io.DisplaySize.y * (sHasCompanionOverlay ? 0.70f : 0.86f);
            const ImVec2 min(centerX - boxWidth * 0.5f, centerY - boxHeight * 0.5f);
            const ImVec2 max(centerX + boxWidth * 0.5f, centerY + boxHeight * 0.5f);

            drawList->AddRectFilled(min, max, colorWithAlpha(6, 8, 14, 232, alpha), 18.0f);
            drawList->AddRect(min, max, colorWithAlpha(255, 218, 88, 220, alpha),
                18.0f, 0, 3.0f);
            drawList->AddRectFilled(
                ImVec2(min.x + 22.0f, min.y + 18.0f),
                ImVec2(max.x - 22.0f, min.y + 24.0f),
                colorWithAlpha(255, 218, 88, 170, alpha), 4.0f);

            addCenteredText(drawList, ImVec2(centerX, min.y + 43.0f), "YOU BOUGHT",
                20.0f, boxWidth - 80.0f, colorWithAlpha(255, 230, 130, 210, alpha));
            addCenteredText(drawList, ImVec2(centerX, min.y + 79.0f), sMoonText,
                38.0f, boxWidth - 90.0f, colorWithAlpha(255, 255, 255, 255, alpha));
        }

    } // namespace

    void queue(const ShineDB::ShineEntry& moon, const ShineDB::ShineEntry* location,
        bool hasCompanionOverlay) {
        // Show where the moon was randomized to (the hint reveal) followed by the
        // original kingdom it was purchased from: "<location> (<original kingdom>)".
        const char* displayName = (location && location->name) ? location->name
                                                               : (moon.name ? moon.name : "Power Moon");
        nn::util::SNPrintf(sMoonText, sizeof(sMoonText), "%s (%s)",
            displayName,
            WorldMeta::getDisplayNameForWorldId(moon.worldId));
        sTimer = 0;
        sActive = true;
        sHasCompanionOverlay = hasCompanionOverlay;
    }

    void queueText(const char* text) {
        nn::util::SNPrintf(sMoonText, sizeof(sMoonText), "%s",
            text && text[0] ? text : "Power Moon");
        sTimer = 0;
        sActive = true;
        sHasCompanionOverlay = false;
    }

    void clear() {
        sMoonText[0] = '\0';
        sTimer = 0;
        sActive = false;
        sHasCompanionOverlay = false;
    }

    void draw() {
        if (!sActive)
            return;
        if (sTimer >= kTotalFrames) {
            clear();
            return;
        }

        const f32 alpha = calcAlpha();
        if (alpha > 0.0f)
            drawOverlay(alpha);
        sTimer++;
    }

} // namespace MoonPurchaseOverlay
