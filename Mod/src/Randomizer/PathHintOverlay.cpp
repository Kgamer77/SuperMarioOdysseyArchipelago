#include "Randomizer/PathHintOverlay.h"

#include "Randomizer/MoonRando.h"

#include "imgui.h"
#include "nn/util.h"

#include <cstring>

namespace PathHintOverlay {

    namespace {

        constexpr ImU32 kColPanel = IM_COL32(8, 10, 16, 225);
        constexpr ImU32 kColBorder = IM_COL32(255, 218, 88, 200);
        constexpr ImU32 kColTitle = IM_COL32(255, 218, 88, 255);
        constexpr ImU32 kColText = IM_COL32(255, 255, 255, 255);
        constexpr ImU32 kColDim = IM_COL32(180, 186, 200, 255);

        constexpr f32 kTitleFontSize = 22.0f;
        constexpr f32 kFontSize = 19.0f;
        constexpr f32 kLineH = 24.0f;
        constexpr f32 kPadX = 24.0f;
        constexpr f32 kPadY = 16.0f;

        constexpr s32 kMaxLines = 64;
        // Room for a route step plus the "  > " bullet; steps are already
        // qualified with the area they happen in, so they run long.
        constexpr s32 kLineTextSize = MoonRando::kAreaRouteStepTextSize + 16;

        struct Line {
            char text[kLineTextSize];
            bool dim;
        };

        char sTitle[128] = {};
        Line sLines[kMaxLines] = {};
        s32 sLineCount = 0;
        bool sVisible = false;

        void reset(const char* title) {
            nn::util::SNPrintf(sTitle, sizeof(sTitle), "%s", title ? title : "");
            sLineCount = 0;
        }

        void addLine(const char* text, bool dim = false) {
            if (sLineCount >= kMaxLines)
                return;
            Line& line = sLines[sLineCount++];
            nn::util::SNPrintf(line.text, sizeof(line.text), "%s", text ? text : "");
            line.dim = dim;
        }

        void addFormattedLine(bool dim, const char* fmt, const char* arg) {
            char buffer[kLineTextSize];
            nn::util::SNPrintf(buffer, sizeof(buffer), fmt, arg);
            addLine(buffer, dim);
        }

    } // namespace

    void show(const char* title, const MoonRando::AreaRouteSet& routes) {
        reset(title);

        for (s32 i = 0; i < routes.routeCount; i++) {
            const MoonRando::AreaRoute& route = routes.routes[i];
            // Only label the routes when there is more than one; a single route
            // reads better without a "Route 1 of 1" header above it.
            if (routes.routeCount > 1) {
                char header[kLineTextSize];
                nn::util::SNPrintf(header, sizeof(header), "Route %d of %d", i + 1, routes.routeCount);
                if (i > 0)
                    addLine("");
                addLine(header, true);
            }
            for (s32 step = 0; step < route.stepCount; step++) {
                if (step == 0)
                    addFormattedLine(false, "%s", route.steps[step]);
                else
                    addFormattedLine(false, "  > %s", route.steps[step]);
            }
        }

        if (routes.truncated)
            addLine("(alternative longer routes also exist)", true);

        sVisible = sLineCount > 0;
    }

    void showMessage(const char* title, const char* message) {
        reset(title);
        addLine(message);
        sVisible = true;
    }

    void hide() {
        sVisible = false;
        sLineCount = 0;
        sTitle[0] = '\0';
    }

    bool isVisible() {
        return sVisible;
    }

    void draw() {
        if (!sVisible)
            return;

        ImGuiIO& io = ImGui::GetIO();
        if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
            return;

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        ImFont* font = ImGui::GetFont();

        f32 contentW = font->CalcTextSizeA(kTitleFontSize, 3.402823e38F, 0.0f, sTitle).x;
        for (s32 i = 0; i < sLineCount; i++) {
            const f32 width = font->CalcTextSizeA(kFontSize, 3.402823e38F, 0.0f, sLines[i].text).x;
            if (width > contentW)
                contentW = width;
        }

        const f32 maxPanelW = io.DisplaySize.x * 0.9f;
        f32 panelW = contentW + kPadX * 2.0f;
        if (panelW > maxPanelW)
            panelW = maxPanelW;
        const f32 panelH = kPadY * 2.0f + kLineH + static_cast<f32>(sLineCount) * kLineH;
        const f32 panelX = (io.DisplaySize.x - panelW) * 0.5f;
        const f32 panelY = (io.DisplaySize.y - panelH) * 0.5f;

        drawList->AddRectFilled(
            ImVec2(panelX, panelY), ImVec2(panelX + panelW, panelY + panelH), kColPanel, 10.0f);
        drawList->AddRect(
            ImVec2(panelX, panelY), ImVec2(panelX + panelW, panelY + panelH), kColBorder, 10.0f, 0, 2.0f);

        f32 y = panelY + kPadY;
        drawList->AddText(font, kTitleFontSize, ImVec2(panelX + kPadX, y), kColTitle, sTitle);
        y += kLineH;

        for (s32 i = 0; i < sLineCount; i++) {
            drawList->AddText(font, kFontSize, ImVec2(panelX + kPadX, y),
                sLines[i].dim ? kColDim : kColText, sLines[i].text);
            y += kLineH;
        }
    }

} // namespace PathHintOverlay
