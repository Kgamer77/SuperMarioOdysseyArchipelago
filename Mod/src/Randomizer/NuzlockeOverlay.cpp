#include "Randomizer/NuzlockeOverlay.h"

#include "Randomizer/HudTheme.h"
#include "Randomizer/Nuzlocke.h"
#include "Randomizer/SeedGenerator.h"
#include "Randomizer/TrackerOverlay.h"

#include "imgui.h"
#include "nn/util.h"

#include <basis/seadTypes.h>
#include <cfloat>

namespace NuzlockeOverlay {

    namespace {

        // Deliberately the same panel fill and dim/text greys as TrackerOverlay so
        // the death screen reads as the same HUD family; the accents differ because
        // this panel is reporting a loss rather than progress.
        constexpr ImU32 kColPanel = IM_COL32(8, 10, 16, 216);
        constexpr ImU32 kColShade = IM_COL32(0, 0, 0, 150);
        constexpr ImU32 kColText = IM_COL32(255, 255, 255, 255);
        constexpr ImU32 kColDim = IM_COL32(150, 156, 170, 255);
        constexpr ImU32 kColLoss = IM_COL32(232, 86, 86, 255);
        constexpr ImU32 kColBarBack = IM_COL32(60, 64, 74, 255);

        ImU32 applyAlpha(ImU32 color, float alpha) {
            if (alpha < 0.0f)
                alpha = 0.0f;
            if (alpha > 1.0f)
                alpha = 1.0f;
            const ImU32 baseAlpha = (color >> IM_COL32_A_SHIFT) & 0xFFu;
            const ImU32 scaledAlpha = static_cast<ImU32>(baseAlpha * alpha);
            return (color & ~(0xFFu << IM_COL32_A_SHIFT))
                | (scaledAlpha << IM_COL32_A_SHIFT);
        }

        ImU32 lerpColor(ImU32 from, ImU32 to, float t) {
            if (t <= 0.0f)
                return from;
            if (t >= 1.0f)
                return to;
            ImU32 out = 0;
            for (s32 shift = 0; shift < 32; shift += 8) {
                const float a = static_cast<float>((from >> shift) & 0xFFu);
                const float b = static_cast<float>((to >> shift) & 0xFFu);
                out |= static_cast<ImU32>(a + (b - a) * t) << shift;
            }
            return out;
        }

        void addText(ImDrawList* dl, ImFont* font, float size, float x, float y,
            ImU32 col, ImU32 shadow, const char* text) {
            dl->AddText(font, size, ImVec2(x + 1.0f, y + 1.0f), shadow, text);
            dl->AddText(font, size, ImVec2(x, y), col, text);
        }

        // Names that do not fit their column are cut rather than allowed to run
        // into the next one.  The ellipsis is two plain dots so it stays inside the
        // ASCII range the HUD font is guaranteed to carry.
        void addTextClipped(ImDrawList* dl, ImFont* font, float size, float x, float y,
            ImU32 col, ImU32 shadow, const char* text, float maxWidth) {
            const char* remaining = nullptr;
            font->CalcTextSizeA(size, maxWidth, 0.0f, text, nullptr, &remaining);
            if (!remaining || !*remaining) {
                addText(dl, font, size, x, y, col, shadow, text);
                return;
            }

            char buf[64];
            s32 length = static_cast<s32>(remaining - text);
            if (length > static_cast<s32>(sizeof(buf)) - 3)
                length = static_cast<s32>(sizeof(buf)) - 3;
            while (length > 0 && text[length - 1] == ' ')
                length--;
            for (s32 i = 0; i < length; i++)
                buf[i] = text[i];
            buf[length] = '.';
            buf[length + 1] = '.';
            buf[length + 2] = '\0';
            addText(dl, font, size, x, y, col, shadow, buf);
        }

        void addTextRight(ImDrawList* dl, ImFont* font, float size, float rightX, float y,
            ImU32 col, ImU32 shadow, const char* text) {
            const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
            addText(dl, font, size, rightX - ts.x, y, col, shadow, text);
        }

        void addTextCentered(ImDrawList* dl, ImFont* font, float size, float cx, float y,
            ImU32 col, ImU32 shadow, const char* text) {
            const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
            addText(dl, font, size, cx - ts.x * 0.5f, y, col, shadow, text);
        }

        void drawMoonPanel(const Nuzlocke::OverlayState& state, ImDrawList* dl, ImFont* font,
            const ImGuiIO& io, float scale, const HudTheme::Palette& theme) {
            const auto S = [scale](float v) { return v * scale; };
            const float alpha = state.alpha;

            const float panelW = S(440.0f);
            const float padX = S(20.0f);
            const float padY = S(16.0f);
            const float titleSize = S(26.0f);
            const float rowSize = S(21.0f);
            const float iconSize = S(22.0f);
            const float valueSize = S(23.0f);
            const float deltaSize = S(16.0f);
            const float footSize = S(17.0f);
            const float lostHeadSize = S(17.0f);
            const float lostRowSize = S(15.0f);
            const float lostRowH = S(19.0f);
            float rowH = S(31.0f);

            // The viewport is a fixed number of rows however long the list is, so
            // the panel never resizes as entries cycle through.
            const s32 lostViewRows = state.lostItemCount < Nuzlocke::cLostVisibleRows
                ? state.lostItemCount
                : Nuzlocke::cLostVisibleRows;
            const float lostViewH = lostRowH * static_cast<float>(lostViewRows);
            const float lostH = state.lostItemCount > 0
                ? S(12.0f) + lostHeadSize + S(6.0f) + lostViewH
                : 0.0f;

            // Hard runs list every kingdom that had moons, which is a much taller
            // panel than a soft penalty's handful of rows. Tighten the rows rather
            // than run off the top and bottom of the screen.
            const float chromeH = padY * 2.0f + titleSize + S(14.0f) + footSize + S(12.0f) + lostH;
            float maxRowsH = io.DisplaySize.y * 0.86f - chromeH;
            if (maxRowsH < rowH)
                maxRowsH = rowH;
            if (state.rowCount > 0 && rowH * state.rowCount > maxRowsH)
                rowH = maxRowsH / static_cast<float>(state.rowCount);

            const float panelH = chromeH + rowH * static_cast<float>(state.rowCount);
            const float panelX = (io.DisplaySize.x - panelW) * 0.5f;
            const float panelY = (io.DisplaySize.y - panelH) * 0.5f;
            const float contentX = panelX + padX;
            const float rightX = panelX + panelW - padX;

            const ImU32 shadow = applyAlpha(IM_COL32_BLACK, alpha * 0.8f);

            // A full-screen scrim: the moon panel sits over a destroyed scene, so
            // without it the rows float on whatever the wipe left behind.
            dl->AddRectFilled(ImVec2(0.0f, 0.0f), io.DisplaySize, applyAlpha(kColShade, alpha));
            dl->AddRectFilled(ImVec2(panelX, panelY), ImVec2(panelX + panelW, panelY + panelH),
                applyAlpha(kColPanel, alpha), S(12.0f));
            dl->AddRect(ImVec2(panelX, panelY), ImVec2(panelX + panelW, panelY + panelH),
                applyAlpha(theme.border, alpha), S(12.0f), 0, S(2.0f));

            float y = panelY + padY;
            addTextCentered(dl, font, titleSize, panelX + panelW * 0.5f, y,
                applyAlpha(theme.heading, alpha), shadow,
                state.timeout ? "Time's Up" : state.soft ? "Moons Lost"
                                                         : "Run Over");
            y += titleSize + S(6.0f);
            dl->AddLine(ImVec2(contentX, y), ImVec2(rightX, y),
                applyAlpha(theme.divider, alpha), S(1.0f));
            y += S(8.0f);

            for (s32 row = 0; row < state.rowCount; row++) {
                const TrackerOverlay::WorldStyle style = TrackerOverlay::getWorldStyle(state.rowWorld[row]);
                if (!style.icon)
                    continue;

                const bool active = state.activeRow < 0 || state.activeRow == row;
                const float pulse = active ? state.activePulse : 0.0f;
                const bool done = state.rowValue[row] <= state.rowAfter[row];
                const float midY = y + rowH * 0.5f;

                if (pulse > 0.0f) {
                    dl->AddRectFilled(ImVec2(contentX - S(8.0f), y + S(1.0f)),
                        ImVec2(rightX + S(8.0f), y + rowH - S(1.0f)),
                        applyAlpha(style.color, alpha * pulse * 0.22f), S(6.0f));
                    dl->AddRectFilled(ImVec2(contentX - S(8.0f), y + S(1.0f)),
                        ImVec2(contentX - S(4.0f), y + rowH - S(1.0f)),
                        applyAlpha(style.color, alpha * pulse), S(2.0f));
                }

                // Rows that have already given up their moons recede so the eye
                // stays on the one currently being taken from.
                const float rowAlpha = alpha * (done && state.activeRow >= 0 ? 0.55f : 1.0f);
                addText(dl, font, iconSize, contentX, midY - iconSize * 0.5f,
                    applyAlpha(style.color, rowAlpha), shadow, style.icon);
                addText(dl, font, rowSize, contentX + S(30.0f), midY - rowSize * 0.5f - S(2.0f),
                    applyAlpha(kColText, rowAlpha), shadow, style.name);

                char deltaBuf[16];
                nn::util::SNPrintf(deltaBuf, sizeof(deltaBuf), "-%d",
                    state.rowBefore[row] - state.rowAfter[row]);
                addTextRight(dl, font, deltaSize, rightX, midY - deltaSize * 0.5f,
                    applyAlpha(kColLoss, rowAlpha), shadow, deltaBuf);

                char valueBuf[16];
                nn::util::SNPrintf(valueBuf, sizeof(valueBuf), "%d", state.rowValue[row]);
                addTextRight(dl, font, valueSize, rightX - S(44.0f), midY - valueSize * 0.5f - S(2.0f),
                    applyAlpha(lerpColor(kColText, kColLoss, pulse), rowAlpha), shadow, valueBuf);

                y += rowH;
            }

            if (state.lostItemCount > 0) {
                y += S(12.0f);
                char lostHeadBuf[64];
                nn::util::SNPrintf(lostHeadBuf, sizeof(lostHeadBuf),
                    "Captures & Abilities Lost  (%d)", state.lostItemCount);
                addText(dl, font, lostHeadSize, contentX, y,
                    applyAlpha(theme.heading, alpha), shadow, lostHeadBuf);
                y += lostHeadSize + S(6.0f);

                const float viewTop = y;
                const float viewBottom = y + lostViewH;
                // Entries partly outside the viewport are still drawn (that is
                // what produces the rise-in and rise-out), so the band has to be
                // clipped or they would spill over the heading and the footer.
                dl->PushClipRect(ImVec2(panelX, viewTop), ImVec2(panelX + panelW, viewBottom), true);

                const float cycle = static_cast<float>(state.lostItemCount);
                for (s32 item = 0; item < state.lostItemCount; item++) {
                    if (!state.lostItemName[item])
                        continue;

                    // age is how many rows this entry has risen since it entered:
                    // -1 is just below the viewport, 0 the bottom row, and
                    // lostViewRows one row past the top. Wrapping it by a whole
                    // cycle is what makes an overflowing list loop seamlessly.
                    float age = state.lostScroll - static_cast<float>(item);
                    if (age < -1.0f)
                        age += cycle;
                    if (age < -1.0f || age > static_cast<float>(lostViewRows))
                        continue;

                    float fade = 1.0f;
                    if (age < 0.0f)
                        fade = 1.0f + age;
                    else if (age > static_cast<float>(lostViewRows) - 1.0f)
                        fade = static_cast<float>(lostViewRows) - age;
                    if (fade <= 0.0f)
                        continue;
                    if (fade > 1.0f)
                        fade = 1.0f;

                    const float itemAlpha = alpha * fade;
                    const float itemY = viewBottom - (age + 1.0f) * lostRowH;
                    // The bullet distinguishes the two kinds without spending a
                    // column on a label; abilities are the hollow one.
                    const float dotY = itemY + lostRowSize * 0.5f;
                    if (state.lostItemKind[item] == Nuzlocke::LostItemKind::Capture) {
                        dl->AddCircleFilled(ImVec2(contentX + S(5.0f), dotY), S(3.5f),
                            applyAlpha(kColLoss, itemAlpha));
                    } else {
                        dl->AddCircle(ImVec2(contentX + S(5.0f), dotY), S(3.5f),
                            applyAlpha(kColLoss, itemAlpha), 0, S(1.5f));
                    }
                    addTextClipped(dl, font, lostRowSize, contentX + S(16.0f), itemY,
                        applyAlpha(kColText, itemAlpha),
                        applyAlpha(IM_COL32_BLACK, itemAlpha * 0.8f),
                        state.lostItemName[item], rightX - contentX - S(20.0f));
                }

                dl->PopClipRect();
                y = viewBottom;
            }

            y += S(6.0f);
            char footBuf[64];
            if (state.soft) {
                nn::util::SNPrintf(footBuf, sizeof(footBuf),
                    "%d moons lost - rerolling the rest of the seed", state.lostTotal);
            } else {
                nn::util::SNPrintf(footBuf, sizeof(footBuf), "This save file is being deleted");
            }
            addTextCentered(dl, font, footSize, panelX + panelW * 0.5f, y,
                applyAlpha(kColDim, alpha), shadow, footBuf);
        }

        void drawRerollPanel(const Nuzlocke::OverlayState& state, ImDrawList* dl, ImFont* font,
            const ImGuiIO& io, float scale, const HudTheme::Palette& theme) {
            const auto S = [scale](float v) { return v * scale; };
            const float alpha = state.alpha;
            const bool failed = state.stage == Nuzlocke::OverlayStage::RerollFailed;

            const float panelW = S(440.0f);
            const float padX = S(20.0f);
            const float padY = S(18.0f);
            const float titleSize = S(26.0f);
            const float bodySize = S(17.0f);
            const float barH = S(12.0f);
            const float panelH = padY * 2.0f + titleSize + S(14.0f) + barH + S(12.0f)
                + bodySize + S(10.0f) + bodySize;

            const float panelX = (io.DisplaySize.x - panelW) * 0.5f;
            const float panelY = (io.DisplaySize.y - panelH) * 0.5f;
            const float contentX = panelX + padX;
            const float rightX = panelX + panelW - padX;
            const ImU32 shadow = applyAlpha(IM_COL32_BLACK, alpha * 0.8f);

            dl->AddRectFilled(ImVec2(0.0f, 0.0f), io.DisplaySize, applyAlpha(kColShade, alpha));
            dl->AddRectFilled(ImVec2(panelX, panelY), ImVec2(panelX + panelW, panelY + panelH),
                applyAlpha(kColPanel, alpha), S(12.0f));
            dl->AddRect(ImVec2(panelX, panelY), ImVec2(panelX + panelW, panelY + panelH),
                applyAlpha(failed ? kColLoss : theme.border, alpha), S(12.0f), 0, S(2.0f));

            float y = panelY + padY;
            addTextCentered(dl, font, titleSize, panelX + panelW * 0.5f, y,
                applyAlpha(failed ? kColLoss : theme.heading, alpha), shadow,
                failed ? "Reroll Failed" : "Rerolling Seed");
            y += titleSize + S(14.0f);

            // The worker publishes real progress, so show it rather than an
            // indeterminate spinner: generation can take a while and a black
            // screen with no readout looks like a hang.
            s32 percent = SeedGenerator::progressPercent();
            if (percent < 0)
                percent = 0;
            if (percent > 100)
                percent = 100;
            dl->AddRectFilled(ImVec2(contentX, y), ImVec2(rightX, y + barH),
                applyAlpha(kColBarBack, alpha), barH * 0.5f);
            if (percent > 0) {
                const float fillX = contentX + (rightX - contentX) * (percent / 100.0f);
                dl->AddRectFilled(ImVec2(contentX, y), ImVec2(fillX, y + barH),
                    applyAlpha(failed ? kColLoss : theme.heading, alpha), barH * 0.5f);
            }
            y += barH + S(12.0f);

            const char* status = SeedGenerator::status();
            char statusBuf[96];
            if (failed) {
                nn::util::SNPrintf(statusBuf, sizeof(statusBuf),
                    "No valid seed yet - trying another");
            } else {
                nn::util::SNPrintf(statusBuf, sizeof(statusBuf), "%d%%  %s", percent,
                    status ? status : "Working");
            }
            addTextCentered(dl, font, bodySize, panelX + panelW * 0.5f, y,
                applyAlpha(kColText, alpha), shadow, statusBuf);
            y += bodySize + S(10.0f);

            addTextCentered(dl, font, bodySize, panelX + panelW * 0.5f, y,
                applyAlpha(kColDim, alpha), shadow,
                "Your kept moons carry over");
        }

    } // namespace

    void draw() {
        const Nuzlocke::OverlayState& state = Nuzlocke::getOverlayState();
        if (state.stage == Nuzlocke::OverlayStage::Hidden || state.alpha <= 0.001f)
            return;

        ImGuiIO& io = ImGui::GetIO();
        if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
            return;

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImFont* font = ImGui::GetFont();
        if (!dl || !font)
            return;

        const float scale = TrackerOverlay::getHudScale();
        const HudTheme::Palette theme = HudTheme::get();

        if (state.stage == Nuzlocke::OverlayStage::MoonLoss)
            drawMoonPanel(state, dl, font, io, scale, theme);
        else
            drawRerollPanel(state, dl, font, io, scale, theme);
    }

} // namespace NuzlockeOverlay
