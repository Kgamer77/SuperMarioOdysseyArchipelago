#include "Randomizer/SpoilerLog.hpp"

#include "Randomizer/AbilityRando.h"
#include "Randomizer/CaptureRando.h"
#include "Randomizer/EnemySwapRando.h"
#include "Randomizer/EntranceRando.h"
#include "Randomizer/FeatureFlags.h"
#include "Randomizer/FoolPunishment.h"
#include "Randomizer/KingdomRando.h"
#include "Randomizer/LogicData.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/PaintingRando.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/RequirementRando.h"
#include "Randomizer/StartPositionRando.h"
#include "Randomizer/SubareaKeyDB.h"
#include "Randomizer/WorldMeta.h"
#include "ShineDB.h"

#include "nn/fs.h"
#include "nn/util.h"

#include "SDRedirect/SDPaths.h"
#include <cstdarg>
#include <cstring>

namespace SpoilerLog {

#if SMOR_FEATURE_CHAOS_ANY
    namespace EnemyDB = Randomizer::EnemySwapDB;
#endif

    struct TextBuf {
        static constexpr size_t kCapacity = 1024 * 1024;

        static char data[kCapacity];
        size_t used = 0;

        void reset() { used = 0; }

        void append(const char* str) {
            size_t len = strlen(str);
            if (used + len >= kCapacity)
                return;
            memcpy(data + used, str, len);
            used += len;
        }

        void appendf(const char* fmt, ...) {
            if (used >= kCapacity)
                return;
            char tmp[512];
            va_list args;
            va_start(args, fmt);
            s32 n = nn::util::VSNPrintf(tmp, sizeof(tmp), fmt, args);
            va_end(args);
            if (n > 0)
                append(tmp);
        }
    };

    char TextBuf::data[TextBuf::kCapacity];

    const char* hintPriceOptionName(Randomizer::HintCurrencyMode currency, u8 option) {
        if (currency == Randomizer::HintCurrencyMode::HintTokens) {
            switch (static_cast<Randomizer::HintTokenPrice>(option)) {
            case Randomizer::HintTokenPrice::Tokens1:
                return "1 Hint Token";
            case Randomizer::HintTokenPrice::Tokens2:
                return "2 Hint Tokens";
            case Randomizer::HintTokenPrice::Tokens3:
                return "3 Hint Tokens";
            case Randomizer::HintTokenPrice::Tokens5:
                return "5 Hint Tokens";
            case Randomizer::HintTokenPrice::Disabled:
            default:
                return "Disabled";
            }
        }

        switch (static_cast<Randomizer::HintCoinPrice>(option)) {
        case Randomizer::HintCoinPrice::Coins100:
            return "100 coins";
        case Randomizer::HintCoinPrice::Coins200:
            return "200 coins";
        case Randomizer::HintCoinPrice::Coins250:
            return "250 coins";
        case Randomizer::HintCoinPrice::Coins500:
            return "500 coins";
        case Randomizer::HintCoinPrice::Coins1000:
            return "1000 coins";
        case Randomizer::HintCoinPrice::Coins2000:
            return "2000 coins";
        case Randomizer::HintCoinPrice::Coins2500:
            return "2500 coins";
        case Randomizer::HintCoinPrice::Disabled:
        default:
            return "Disabled";
        }
    }

    const char* hintProgressiveIncrementName(Randomizer::HintCurrencyMode currency, u8 option) {
        if (currency == Randomizer::HintCurrencyMode::HintTokens) {
            switch (static_cast<Randomizer::HintTokenProgressiveIncrement>(option)) {
            case Randomizer::HintTokenProgressiveIncrement::Tokens1:
                return "+1 Hint Token";
            case Randomizer::HintTokenProgressiveIncrement::Tokens2:
                return "+2 Hint Tokens";
            case Randomizer::HintTokenProgressiveIncrement::Tokens3:
                return "+3 Hint Tokens";
            case Randomizer::HintTokenProgressiveIncrement::Tokens5:
                return "+5 Hint Tokens";
            case Randomizer::HintTokenProgressiveIncrement::Disabled:
            default:
                return "Disabled";
            }
        }

        switch (static_cast<Randomizer::HintCoinProgressiveIncrement>(option)) {
        case Randomizer::HintCoinProgressiveIncrement::Coins100:
            return "+100 coins";
        case Randomizer::HintCoinProgressiveIncrement::Coins200:
            return "+200 coins";
        case Randomizer::HintCoinProgressiveIncrement::Coins250:
            return "+250 coins";
        case Randomizer::HintCoinProgressiveIncrement::Coins500:
            return "+500 coins";
        case Randomizer::HintCoinProgressiveIncrement::Coins1000:
            return "+1000 coins";
        case Randomizer::HintCoinProgressiveIncrement::Coins2000:
            return "+2000 coins";
        case Randomizer::HintCoinProgressiveIncrement::Coins2500:
            return "+2500 coins";
        case Randomizer::HintCoinProgressiveIncrement::Disabled:
        default:
            return "Disabled";
        }
    }

    static bool writeFile(const char* path, const char* data, size_t size) {
        nn::fs::DeleteFile(path);
        if (nn::fs::CreateFile(path, (s64)size).IsFailure())
            return false;

        nn::fs::FileHandle handle;
        if (nn::fs::OpenFile(&handle, path, nn::fs::OpenMode_Write).IsFailure())
            return false;

        nn::fs::WriteFile(handle, 0, data, size, nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush));
        nn::fs::CloseFile(handle);
        return true;
    }

    static const char* stageDisplayName(const char* stage) {
        if (!stage || !stage[0])
            return "???";
        if (const WorldMeta::Entry* world = WorldMeta::findByMainStage(stage))
            return world->displayName;
        for (s32 i = 0; i < Randomizer::LogicData::sStageCount; i++) {
            const Randomizer::LogicData::Stage& info = Randomizer::LogicData::sStages[i];
            if (info.stage && std::strcmp(info.stage, stage) == 0
                && info.displayName && info.displayName[0])
                return info.displayName;
        }
        return stage;
    }

    static const char* moonRockKeyKingdomShortName(s32 kingdom) {
        switch (kingdom) {
        case WorldMeta::WorldId_Cap:
            return "Cap";
        case WorldMeta::WorldId_Cascade:
            return "Cascade";
        case WorldMeta::WorldId_Sand:
            return "Sand";
        case WorldMeta::WorldId_Wooded:
            return "Wooded";
        case WorldMeta::WorldId_Lake:
            return "Lake";
        case WorldMeta::WorldId_Cloud:
            return "Cloud";
        case WorldMeta::WorldId_Lost:
            return "Lost";
        case WorldMeta::WorldId_Metro:
            return "Metro";
        case WorldMeta::WorldId_Seaside:
            return "Seaside";
        case WorldMeta::WorldId_Snow:
            return "Snow";
        case WorldMeta::WorldId_Luncheon:
            return "Luncheon";
        case WorldMeta::WorldId_Ruined:
            return "Ruined";
        case WorldMeta::WorldId_Bowsers:
            return "Bowser's";
        case WorldMeta::WorldId_Moon:
            return "Moon";
        default:
            return nullptr;
        }
    }

    static bool appendMoonRockKeyName(TextBuf& buf, s32 kingdom) {
        const char* shortName = moonRockKeyKingdomShortName(kingdom);
        if (!shortName)
            return false;
        buf.appendf("%s Kingdom's Moon Rock Key", shortName);
        return true;
    }

    static bool appendMoonRockKeyNameForItem(TextBuf& buf, const ShineDB::ShineEntry& item) {
        return appendMoonRockKeyName(
            buf, MoonRando::getMoonRockKeyKingdomForItem(item.uid, item.stage));
    }

    static void appendItemDisplayName(TextBuf& buf, const ShineDB::ShineEntry& item) {
        if (appendMoonRockKeyNameForItem(buf, item))
            return;
        buf.append(item.name ? item.name : "???");
    }

    static void appendMoonUnlocks(TextBuf& buf, const ShineDB::ShineEntry& moon, bool includeMoonRockKey = true) {
        const s32 captureCount = CaptureRando::getCaptureCountForMoon(moon);
        const s32 abilityCount = AbilityRando::getAbilityCountForMoon(moon);
        const bool captureFool = CaptureRando::isFoolMoon(moon);
        const bool abilityFool = AbilityRando::isFoolMoon(moon);
        const s32 paintingCount = Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled()
            ? PaintingRando::getCloudMoonPaintingUnlockCount(moon)
            : 0;
        const s32 moonRockKeyKingdom = includeMoonRockKey
            ? MoonRando::getMoonRockKeyKingdomForItem(moon.uid, moon.stage)
            : -1;
        if (captureCount <= 0 && abilityCount <= 0 && paintingCount <= 0 && moonRockKeyKingdom < 0
            && !captureFool && !abilityFool)
            return;

        buf.append(" (");
        bool wrote = false;

        for (s32 i = 0; i < captureCount; i++) {
            const CaptureRando::CaptureEntry* capture = CaptureRando::getCaptureForMoon(moon, i);
            if (!capture)
                continue;
            if (wrote)
                buf.append(", ");
            buf.append(capture->displayName ? capture->displayName : capture->hackName);
            if (!CaptureRando::isProgressionCaptureSlotForMoon(moon, i))
                buf.append(" [duplicate]");
            wrote = true;
        }

        for (s32 i = 0; i < abilityCount; i++) {
            const AbilityRando::AbilityEntry* ability = AbilityRando::getAbilityForMoon(moon, i);
            if (!ability)
                continue;
            if (wrote)
                buf.append(", ");
            buf.append(ability->displayName ? ability->displayName : ability->key);
            if (!AbilityRando::isProgressionAbilitySlotForMoon(moon, i))
                buf.append(" [duplicate]");
            wrote = true;
        }

        for (s32 i = 0; i < paintingCount; i++) {
            const s32 paintingIndex = PaintingRando::getCloudMoonPaintingUnlockIndex(moon, i);
            const char* sourceKingdom = nullptr;
            const char* destinationKingdom = nullptr;
            if (!PaintingRando::getPaintingUnlockKingdomNames(
                    paintingIndex, &sourceKingdom, &destinationKingdom))
                continue;
            if (wrote)
                buf.append(", ");
            buf.appendf("Painting: %s <-> %s",
                sourceKingdom ? sourceKingdom : "???",
                destinationKingdom ? destinationKingdom : "???");
            wrote = true;
        }

        if (moonRockKeyKingdom >= 0) {
            if (wrote)
                buf.append(", ");
            appendMoonRockKeyName(buf, moonRockKeyKingdom);
            wrote = true;
        }

        if (captureFool || abilityFool) {
            if (wrote)
                buf.append(", ");
            const Randomizer::FoolPunishment punishment = captureFool
                ? CaptureRando::getFoolPunishment(moon)
                : AbilityRando::getFoolPunishment(moon);
            buf.append("FOOL: ");
            buf.append(Randomizer::getFoolPunishmentText(punishment));
            wrote = true;
        }

        buf.append(")");
    }

    static void appendKingdomOrder(TextBuf& buf) {
        buf.append("=== Kingdom Order ===\n");
        if (!Randomizer::isKingdomRandoEnabled())
            buf.append("\nKingdom order randomization is disabled (vanilla order).\n");

        buf.appendf("\nStarting Location: %s\n\n",
            WorldMeta::getDisplayNameForWorldId(KingdomRando::getStartingWorldId()));

        s32 step = 1;
        for (s32 t = 0; t < KingdomRando::getTierCount(); t++) {
            const KingdomRando::RouteTier* tier = KingdomRando::getTier(t);
            if (!tier || tier->count <= 0)
                continue;
            if (tier->count == 2) {
                buf.appendf("%2d. %s OR %s (player choice)\n", step++,
                    WorldMeta::getDisplayNameForWorldId(tier->worlds[0]),
                    WorldMeta::getDisplayNameForWorldId(tier->worlds[1]));
            } else {
                buf.appendf("%2d. %s\n", step++,
                    WorldMeta::getDisplayNameForWorldId(tier->worlds[0]));
            }
        }
        buf.append("\n");
    }

    static void appendMoonPlacements(TextBuf& buf) {
        buf.append("=== Moon Placements by Final Location ===\n");

        s32 currentWorldId = -1;
        s32 validSrc = 0;
        s32 validDest = 0;

        for (s32 i = 0; i < ShineDB::sShineTableSize; i++) {
            const ShineDB::ShineEntry* src = ShineDB::entryAt(i);
            if (!src || src->objId < 0)
                continue;
            validSrc++;

            char objIdStr[32];
            nn::util::SNPrintf(objIdStr, sizeof(objIdStr), "obj%d", src->objId);

            const ShineDB::ShineEntry* dest = MoonRando::getCreditMoon(objIdStr, src->stage);
            if (!dest)
                continue;
            validDest++;

            if (src->worldId != currentWorldId) {
                currentWorldId = src->worldId;
                buf.appendf("\n%s:\n", WorldMeta::getDisplayNameForWorldId(currentWorldId));
            }

            buf.append("  ");
            appendItemDisplayName(buf, *dest);
            buf.appendf(" - %s", WorldMeta::getDisplayNameForWorldId(MoonRando::getAssignedKingdomForEntry(dest)));
            appendMoonUnlocks(buf, *dest, false);
            if (src->name && dest->name && std::strcmp(src->name, dest->name) != 0)
                buf.appendf(" @ %s", src->name);
            buf.append("\n");
        }

        buf.appendf("\nMoon slots written: %d/%d\n\n", validDest, validSrc);
    }

    static void formatStageDisplayName(char* out, s32 outSize, const char* raw);
    static void stageFriendlyName(char* out, s32 outSize, const char* stage);

    static bool sameString(const char* a, const char* b) {
        if (!a)
            a = "";
        if (!b)
            b = "";
        return std::strcmp(a, b) == 0;
    }

    static bool loadingZoneNeedsId(const EntranceDB::EntranceLink& link) {
        s32 matchCount = 0;
        for (s32 i = 0; i < EntranceDB::sLinkCount; i++) {
            if (EntranceDB::isExcludedLink(i))
                continue;
            const EntranceDB::EntranceLink& other = EntranceDB::sLinks[i];
            if (sameString(other.homeStage, link.homeStage)
                && sameString(other.subStage, link.subStage)
                && ++matchCount > 1)
                return true;
        }
        return false;
    }

    static const char* homeEntranceIdForSide(const EntranceDB::EntranceLink& link, bool farSide) {
        if (farSide && EntranceDB::hasHomeOutDoor(link) && link.homeEntranceOut && link.homeEntranceOut[0])
            return link.homeEntranceOut;
        return link.homeEntranceIn;
    }

    static const char* subEntranceIdForSide(const EntranceDB::EntranceLink& link, bool farSide) {
        if (farSide && EntranceDB::hasSubOutDoor(link) && link.subEntranceOut && link.subEntranceOut[0])
            return link.subEntranceOut;
        return link.subEntranceIn;
    }

    // A loading zone is named by the (vanilla) sub-area it connects to, tagged with
    // the side the player uses: the near/entry side reads "Entrance", the far/exit
    // side reads "Exit". If two links collapse to the same friendly name (Sand has
    // two Ice Cave rows), include the raw door id so the spoiler points at the exact
    // physical loading zone.
    static void appendLoadingZoneName(
        TextBuf& buf,
        const char* subStage,
        bool farSide,
        const char* disambiguator = nullptr,
        bool showDisambiguator = false) {
        char displayName[128];
        stageFriendlyName(displayName, sizeof(displayName), subStage);
        const char* name = displayName[0] ? displayName : (subStage && subStage[0] ? subStage : "???");
        buf.appendf("%s %s", name, farSide ? "Exit" : "Entrance");
        if (showDisambiguator && disambiguator && disambiguator[0])
            buf.appendf(" (%s)", disambiguator);
    }

    static void appendHomeLoadingZoneName(TextBuf& buf, const EntranceDB::EntranceLink& link, bool farSide) {
        appendLoadingZoneName(
            buf,
            link.subStage,
            farSide,
            homeEntranceIdForSide(link, farSide),
            loadingZoneNeedsId(link));
    }

    static void appendSubLoadingZoneName(TextBuf& buf, const EntranceDB::EntranceLink& link, bool farSide) {
        appendLoadingZoneName(
            buf,
            link.subStage,
            farSide,
            subEntranceIdForSide(link, farSide),
            loadingZoneNeedsId(link));
    }

    // Deep Woods is presented as its own region in the spoiler even though its
    // entrances live in Wooded Kingdom's overworld.
    static const char* regionDisplayNameForSub(const EntranceDB::EntranceLink& link) {
        if (link.subStage && std::strcmp(link.subStage, "ForestWorldWoodsStage") == 0)
            return "Deep Woods";
        return stageDisplayName(link.homeStage);
    }

    static void appendEntranceDestination(TextBuf& buf, const EntranceDB::EntranceLink& dest, bool enteredFar) {
        buf.appendf("%s: ", regionDisplayNameForSub(dest));
        appendSubLoadingZoneName(buf, dest, enteredFar);
    }

    static void appendHomeArrival(TextBuf& buf, const EntranceDB::EntranceLink& dest, bool outSide) {
        if (!outSide
            && dest.homeStage
            && std::strcmp(dest.homeStage, "Special2WorldHomeStage") == 0
            && dest.subStage
            && std::strcmp(dest.subStage, "Special2WorldLavaStage") == 0) {
            buf.appendf("%s (Odyssey)", stageDisplayName(dest.homeStage));
            return;
        }

        // Surfacing in an overworld: name the loading zone by the sub-area on the
        // far side (this pipe's vanilla destination), tagged by which side we exit.
        buf.appendf("%s ", stageDisplayName(dest.homeStage));
        appendHomeLoadingZoneName(buf, dest, outSide);
    }

    // Follow a chain: each sub door's outward connection leads into the next
    // sub-area or surfaces at an overworld pipe (possibly in another kingdom).
    // Doors are paired symmetrically, so this is exactly the forward walk the
    // player experiences.
    static void appendEntranceChain(TextBuf& buf, EntranceRando::Conn conn) {
        bool visited[EntranceDB::sLinkCount][2] = {};

        for (;;) {
            if (conn.kind == EntranceRando::ConnKind_HomeIn || conn.kind == EntranceRando::ConnKind_HomeOut) {
                if (conn.link >= 0 && conn.link < EntranceDB::sLinkCount) {
                    const EntranceDB::EntranceLink& dest = EntranceDB::sLinks[conn.link];
                    appendHomeArrival(buf, dest, conn.kind == EntranceRando::ConnKind_HomeOut);
                } else {
                    buf.append("???");
                }
                return;
            }

            const bool enteredFar = conn.kind == EntranceRando::ConnKind_SubFar;
            if ((conn.kind != EntranceRando::ConnKind_SubNear && !enteredFar)
                || conn.link < 0 || conn.link >= EntranceDB::sLinkCount) {
                buf.append("[sealed]");
                return;
            }

            appendEntranceDestination(buf, EntranceDB::sLinks[conn.link], enteredFar);

            if (!MoonRando::canEntranceSubBeTraversed(conn.link, enteredFar)) {
                buf.append(" [one-way pocket]");
                return;
            }
            const bool otherIsFar = !enteredFar;
            if (otherIsFar && !EntranceRando::linkHasFarDoor(conn.link)) {
                buf.append(" [dead end]");
                return;
            }
            const s32 visitSlot = otherIsFar ? 1 : 0;
            if (visited[conn.link][visitSlot]) {
                buf.append(" [loops]");
                return;
            }
            visited[conn.link][visitSlot] = true;
            conn = EntranceRando::getSubDoorConn(conn.link, otherIsFar);
            buf.append(" -> ");
        }
    }

    static const char* doorSlotName(u8 slot) {
        switch (slot) {
        case EntranceRando::DoorSlot_HomeIn:
            return "overworld entry";
        case EntranceRando::DoorSlot_HomeOut:
            return "overworld exit";
        case EntranceRando::DoorSlot_SubNear:
            return "near side";
        default:
            return "far side";
        }
    }

    static const char* doorClassName(u8 doorClass) {
        switch (doorClass) {
        case EntranceRando::DoorClass_DeadEnd:
            return "dead end";
        case EntranceRando::DoorClass_Pocket:
            return "pocket";
        case EntranceRando::DoorClass_PassThrough:
            return "pass-through";
        case EntranceRando::DoorClass_CrossKingdom:
            return "cross-kingdom";
        default:
            return "?";
        }
    }

    static const char* wireIntentName(u8 intent) {
        switch (intent) {
        case EntranceRando::WireIntent_Expand:
            return "expand";
        case EntranceRando::WireIntent_Compact:
            return "compact";
        default:
            return "fill";
        }
    }

    // The doors the fill chose, in the order it chose them. Deliberately its own
    // block rather than folded into the itinerary: these carry the CONSTRUCTION
    // sphere counter, which is a different numbering from the playthrough
    // spheres appendItinerary prints (that is a second, independent replay).
    static void appendEntranceWiringOrder(TextBuf& buf) {
        const s32 count = MoonRando::getEntranceWireStepCount();
        if (count <= 0)
            return;

        buf.append("--- Doors chosen during placement ---\n");
        buf.append(
            "  Listed in the order the fill wired them. Any door not listed here was\n"
            "  paired by the leftover pass once placement finished.\n");
        s32 lastSphere = -1;
        for (s32 i = 0; i < count; i++) {
            MoonRando::EntranceWireStep step;
            if (!MoonRando::getEntranceWireStep(i, &step))
                continue;
            if (step.sphere != lastSphere) {
                buf.appendf("  Sphere %d:\n", (s32)step.sphere);
                lastSphere = step.sphere;
            }
            const EntranceDB::EntranceLink* source = EntranceDB::entryAt(step.sourceLink);
            const EntranceDB::EntranceLink* target = EntranceDB::entryAt(step.targetLink);
            if (!source || !target)
                continue;
            const bool sourceIsSub = step.sourceSlot == EntranceRando::DoorSlot_SubNear
                || step.sourceSlot == EntranceRando::DoorSlot_SubFar;
            const bool targetIsSub = step.targetSlot == EntranceRando::DoorSlot_SubNear
                || step.targetSlot == EntranceRando::DoorSlot_SubFar;
            buf.appendf("    %s (%s) -> %s (%s)  [%s, %s]\n",
                sourceIsSub ? source->subStage : source->homeStage,
                doorSlotName(step.sourceSlot),
                targetIsSub ? target->subStage : target->homeStage,
                doorSlotName(step.targetSlot),
                doorClassName(step.doorClass),
                wireIntentName(step.intent));
        }
        buf.append("\n");
    }

    static void appendEntrancePlacements(TextBuf& buf) {
        if (!Randomizer::isEntranceRandoEnabled() || !EntranceRando::isInitialized()) {
            buf.append("=== Entrance Randomizer ===\n\nEntrance randomizer is disabled or not initialized.\n\n");
            return;
        }

        buf.append("=== Entrance Randomizer ===\n");
        appendEntranceWiringOrder(buf);

        // Deep Woods lives inside Wooded Kingdom's overworld, but its doors read
        // more clearly under their own heading. Group by a stable key (the literal
        // sub-stage name for Deep Woods, otherwise the home-stage pointer).
        const char* currentKey = nullptr;
        for (s32 i = 0; i < EntranceDB::sLinkCount; i++) {
            if (EntranceDB::isExcludedLink(i))
                continue;
            const EntranceDB::EntranceLink& src = EntranceDB::sLinks[i];
            // Only list a door this row actually owns. Two rows can name the same
            // overworld loading zone (Sand's two Ice Cave rows both call their entry
            // "arijigoku"); the runtime routes that id to the first one, so listing
            // it twice printed a phantom forward door under a duplicate label.
            const bool hasHomeIn = EntranceDB::isHomeInEnterable(src)
                && EntranceDB::ownsHomeEntranceSide(i, false);
            const bool hasHomeOut = EntranceDB::isHomeOutEnterable(src)
                && EntranceDB::ownsHomeEntranceSide(i, true);
            if (!hasHomeIn && !hasHomeOut)
                continue;
            const char* vanillaTag = EntranceDB::isVanillaLink(i) ? " [vanilla]" : "";

            const bool isDeepWoods = src.subStage && std::strcmp(src.subStage, "ForestWorldWoodsStage") == 0;
            const char* groupKey = isDeepWoods ? "ForestWorldWoodsStage" : src.homeStage;
            if (!currentKey || std::strcmp(currentKey, groupKey) != 0) {
                currentKey = groupKey;
                buf.appendf("\n%s:\n", isDeepWoods ? "Deep Woods" : stageDisplayName(src.homeStage));
            }

            if (hasHomeIn) {
                buf.appendf("  %s ", stageDisplayName(src.homeStage));
                appendHomeLoadingZoneName(buf, src, false);
                buf.appendf("%s -> ", vanillaTag);
                appendEntranceChain(buf, EntranceRando::getHomeDoorConn(i, false));
                buf.append("\n");
            }
            // Only a real, enterable home-out door gets its own line. Return-spawn
            // surfaces like doukutu2 stay implicit to avoid phantom forward doors.
            if (hasHomeOut) {
                buf.appendf("  %s ", stageDisplayName(src.homeStage));
                appendHomeLoadingZoneName(buf, src, true);
                buf.appendf("%s -> ", vanillaTag);
                appendEntranceChain(buf, EntranceRando::getHomeDoorConn(i, true));
                buf.append("\n");
            }
        }

        buf.append("\n");
    }

    static void appendPaintingLinks(TextBuf& buf) {
        buf.append("=== Painting Links ===\n");
        const s32 linkCount = MoonRando::getPaintingLinkCount();
        if (linkCount <= 0) {
            buf.append("\nNo painting links are defined in logic data.\n\n");
            return;
        }

        for (s32 i = 0; i < linkCount; i++) {
            const MoonRando::PaintingLinkInfo link = MoonRando::getPaintingLinkInfo(i);
            const bool isFree = Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled()
                && PaintingRando::isPaintingLinkFree(i);
            buf.appendf("  %s (%s) %s %s (%s)%s\n",
                link.sourceKingdom,
                link.sourceNode,
                link.bidirectional ? "<->" : "->",
                link.destinationKingdom,
                link.destinationNode,
                isFree ? " [Free - unlocked from the start]" : "");
        }

        buf.append("\n");
    }

#if SMOR_FEATURE_CHAOS_ANY
    static const char* chaosSwapModeName(Randomizer::ChaosSwapMode mode) {
        switch (mode) {
        case Randomizer::ChaosSwapMode::SameCategory:
            return "Same Category";
        case Randomizer::ChaosSwapMode::WithCombat:
            return "With Combat";
        case Randomizer::ChaosSwapMode::WithDeletion:
            return "With Combat + Deletion";
        case Randomizer::ChaosSwapMode::Off:
            return "Off";
        }
        return "Off";
    }

    // A placement inside an embedded zone carries the zone in `stage` and the
    // physical stage the game actually loads only in `logicStage`. A crash
    // report names the physical stage, so print both whenever they differ.
    static void appendPlacementLocation(
        TextBuf& buf, const EnemyDB::Placement& placement) {
        const char* stage = placement.stage ? placement.stage : "???";
        buf.append(stage);
        if (placement.logicStage && placement.logicStage[0]
            && std::strcmp(placement.logicStage, stage) != 0)
            buf.appendf(" in %s", placement.logicStage);
    }

    // Scenario masks are 0-based (bit 0 is ScenarioNo 1) while everything a
    // player sees is 1-based, so convert here. A placement present in every
    // scenario prints nothing, which keeps the common row short.
    static void appendPlacementScenarios(
        TextBuf& buf, const EnemyDB::Placement& placement) {
        s32 present = 0;
        for (s32 bit = 0; bit < 32; bit++)
            if (placement.scenarioMask & (1u << bit))
                present++;
        if (present == 0 || present == 32)
            return;
        buf.append(present > 1 ? " @ scenarios" : " @ scenario");
        for (s32 bit = 0; bit < 32; bit++)
            if (placement.scenarioMask & (1u << bit))
                buf.appendf(" %d", bit + 1);
    }
#endif

    static void appendEnemySwapAssignment(TextBuf& buf) {
        buf.append("=== Enemy/Capture Assignment ===\n");
#if SMOR_FEATURE_CHAOS_ANY
        if (!EnemySwapRando::isInitialized()) {
            buf.append("Enemy/Capture assignment is disabled or was not initialized.\n\n");
            return;
        }

        buf.appendf("Assignment data version: %u\n", EnemyDB::kAssignmentDataVersion);
        // Which audited compatibility rules were in force. Version 0 means the
        // build carries no reviewed restrictions, so a placement here was
        // filtered only by the authored pools and gates -- worth knowing when
        // a seed is being investigated after a crash.
        buf.appendf(
            "Compatibility data version: %u (%d restriction(s) enforced)\n",
            EnemyDB::kCompatibilityDataVersion,
            EnemyDB::sEnemySpawnRestrictionCount);
        // The categories the rows below were produced under. Without them a
        // short list is ambiguous: "few swaps" and "one category on" look the
        // same in the placement list.
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
        buf.appendf("Enemies/Captures: %s\n",
            Randomizer::isEnemySwapRandoEnabled() ? "On" : "Off");
        buf.appendf("NPCs: %s\n",
            chaosSwapModeName(Randomizer::getNpcSwapMode()));
#endif
#if SMOR_FEATURE_PROP_RANDOMIZATION
        buf.appendf("Props: %s (%u%% of eligible props)\n",
            chaosSwapModeName(Randomizer::getPropSwapMode()),
            static_cast<unsigned>(Randomizer::getPropSwapChancePercent()));
#endif
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
        buf.appendf("2D Enemies: %s\n",
            Randomizer::isTwoDSwapRandoEnabled() ? "On" : "Off");
#endif
        buf.append("Route witnesses:\n");
        const s32 witnessCount = EnemySwapRando::getRouteWitnessCount();
        if (witnessCount == 0) {
            buf.append("  (none)\n");
        } else {
            for (s32 i = 0; i < witnessCount; i++) {
                const char* edgeId = nullptr;
                const char* gateId = nullptr;
                s16 scenario = 0;
                if (EnemySwapRando::getRouteWitness(
                        i, &edgeId, &scenario, &gateId)) {
                    buf.appendf(
                        "  %s @ scenario %d: %s\n",
                        edgeId ? edgeId : "???", scenario,
                        gateId ? gateId : "???");
                }
            }
        }

        // One row per changed object id: the placement's own stage (which for
        // an object inside an embedded zone is the zone, with the physical
        // stage alongside it), the scenarios the object exists in, the object
        // id, and both sides of the swap with their categories. This is the
        // crash-hunting view -- enough to take a report naming a stage and a
        // scenario and find the handful of objects that could have caused it,
        // which is why it carries the ids and not just the friendly names.
        buf.append("Changed placements (omitted placements remain vanilla):\n");
        buf.append(
            "  <stage>[ in <physical stage>]:<object id>"
            "[ @ scenarios ...] source [category] -> target [category]\n");
        s32 changed = 0;
        const s32 placementCount = EnemySwapRando::getResolvedPlacementCount();
        for (s32 i = 0; i < placementCount; i++) {
            const EnemyDB::Placement& placement = EnemyDB::sPlacements[i];
            const s16 entityIndex = EnemySwapRando::getResolvedEntityIndex(i);
            if (placement.entity < 0
                || placement.entity >= EnemyDB::sEntityCount
                || entityIndex == placement.entity)
                continue;
            const bool deleted = entityIndex == EnemySwapRando::kDeletedPlacementEntity;
            if (!deleted
                && (entityIndex < 0 || entityIndex >= EnemyDB::sEntityCount))
                continue;
            const EnemyDB::Entity& source = EnemyDB::sEntities[placement.entity];

            buf.append("  ");
            appendPlacementLocation(buf, placement);
            buf.appendf(":%s", placement.objId ? placement.objId : "???");
            appendPlacementScenarios(buf, placement);
            buf.appendf(" %s [%s] -> ",
                source.friendlyName ? source.friendlyName : source.name,
                source.category ? source.category : "?");
            if (deleted) {
                buf.append("[deleted]\n");
            } else {
                const EnemyDB::Entity& target = EnemyDB::sEntities[entityIndex];
                buf.appendf("%s (%s) [%s]\n",
                    target.friendlyName ? target.friendlyName : target.name,
                    target.name,
                    target.category ? target.category : "?");
            }
            changed++;
        }
        if (changed == 0)
            buf.append("  (none)\n");
        buf.appendf(
            "\nResolved placements: %d; changed: %d; witnesses: %d\n\n",
            placementCount, changed, witnessCount);
#else
        buf.append("Chaos randomization is not compiled into this build.\n\n");
#endif
    }

    static void appendMoonRockKeys(TextBuf& buf) {
        if (!Randomizer::isMoonRockKeyEnabled())
            return;

        buf.append("=== Moon Rock Keys ===\n");

        s32 count = 0;
        for (s32 kingdom = 0; kingdom < WorldMeta::sWorldCount; kingdom++) {
            const ShineDB::ShineEntry* key = MoonRando::getMoonRockKeyForKingdom(kingdom);
            if (!key)
                continue;

            buf.append("  ");
            appendMoonRockKeyName(buf, kingdom);
            buf.append(": ");
            buf.append(key->name ? key->name : "???");

            if (const MoonRando::Placement* placement = MoonRando::getPlacementForItem(key->uid, key->stage)) {
                if (placement->location) {
                    buf.appendf(" -> %s (%s)",
                        placement->location->name ? placement->location->name : "???",
                        WorldMeta::getDisplayNameForWorldId(placement->location->worldId));
                }
            }
            buf.append("\n");
            count++;
        }

        if (count == 0)
            buf.append("  (none)\n");
        buf.append("\n");
    }

    static void appendSubareaKeys(TextBuf& buf) {
        if (!Randomizer::isSubareaUnlockEnabled())
            return;

        buf.append("=== Subarea Keys ===\n");

        const s32 slotCount = MoonRando::getSubareaKeySlotCount();
        s32 count = 0;
        for (s32 slot = 0; slot < slotCount; slot++) {
            const ShineDB::ShineEntry* key = MoonRando::getSubareaKeyForSlot(slot);
            if (!key)
                continue;

            s32 kingdom = -1;
            SubareaKeyDB::SubareaType type = EntranceDB2::SubareaType_None;
            SubareaKeyDB::keySlotAt(slot, &kingdom, &type);

            buf.append("  ");
            // A coarser mode leaves one half of the pair unnamed: a per-type key
            // covers every kingdom, a per-kingdom key every door kind.
            if (kingdom >= 0)
                buf.appendf("%s ", WorldMeta::getDisplayNameForWorldId(kingdom));
            else
                buf.append("All Kingdoms ");
            if (type != EntranceDB2::SubareaType_Count)
                buf.appendf("%s", SubareaKeyDB::typeNameAscii(type));
            else
                buf.append("Subareas");

            buf.append(": ");
            buf.append(key->name ? key->name : "???");

            if (const MoonRando::Placement* placement = MoonRando::getPlacementForItem(key->uid, key->stage)) {
                if (placement->location) {
                    buf.appendf(" -> %s (%s)",
                        placement->location->name ? placement->location->name : "???",
                        WorldMeta::getDisplayNameForWorldId(placement->location->worldId));
                }
            }
            buf.append("\n");
            count++;
        }

        if (count == 0)
            buf.append("  (none)\n");
        buf.append("\n");
    }

    static void appendToadetteMedals(TextBuf& buf) {
        if (!Randomizer::isToadetteMedalModeEnabled())
            return;

        buf.append("=== Toadette Medal Hunt ===\n");

        // Entries and medals differ: a triple medal is one entry worth three
        // medals, so the header states both.
        const s32 total = MoonRando::getToadetteMedalTotalCount();
        buf.appendf("  %d medals across %d moons\n", MoonRando::getToadetteMedalTotalValue(), total);
        for (s32 i = 0; i < total; i++) {
            const ShineDB::ShineEntry* medal = MoonRando::getToadetteMedalAt(i);
            if (!medal)
                continue;

            buf.append("  ");
            buf.append(medal->name ? medal->name : "???");
            buf.appendf(" (%s)", WorldMeta::getDisplayNameForWorldId(medal->worldId));

            if (const MoonRando::Placement* placement = MoonRando::getPlacementForItem(medal->uid, medal->stage)) {
                if (placement->location) {
                    buf.appendf(" -> %s (%s)",
                        placement->location->name ? placement->location->name : "???",
                        WorldMeta::getDisplayNameForWorldId(placement->location->worldId));
                }
            }
            if (const s32 value = MoonRando::getToadetteMedalValueAt(i); value > 1)
                buf.appendf(" [x%d medals]", value);
            buf.append("\n");
        }

        if (total == 0)
            buf.append("  (none)\n");
        buf.append("\n");
    }

    namespace LD = Randomizer::LogicData;

    static void formatStageDisplayName(char* out, s32 outSize, const char* raw) {
        if (!raw || !raw[0]) {
            if (outSize > 0)
                out[0] = '\0';
            return;
        }
        s32 w = 0;
        for (const char* p = raw; *p && w < outSize - 1; p++) {
            if (*p >= 'A' && *p <= 'Z' && p > raw)
                out[w++] = ' ';
            if (w >= outSize - 1)
                break;
            out[w++] = *p;
        }
        out[w] = '\0';
        s32 len = (s32)std::strlen(out);
        if (len > 5 && std::strcmp(out + len - 5, "Stage") == 0)
            out[len - 5] = '\0';
        else if (len > 4 && std::strcmp(out + len - 4, "Zone") == 0)
            out[len - 4] = '\0';
    }

    // Player-facing name for a stage id. Prefers the authored display name from the
    // logic graph editor (e.g. "Ice Cave" for SandWorldPressExStage); falls back to
    // a spaced/trimmed form of the raw id when no friendly name was authored.
    static void stageFriendlyName(char* out, s32 outSize, const char* stage) {
        if (stage && stage[0]) {
            for (s32 i = 0; i < LD::sStageCount; i++) {
                if (std::strcmp(LD::sStages[i].stage, stage) != 0)
                    continue;
                const char* name = LD::sStages[i].displayName;
                if (name && name[0] && std::strcmp(name, stage) != 0) {
                    s32 w = 0;
                    for (const char* p = name; *p && w < outSize - 1; p++)
                        out[w++] = *p;
                    out[w] = '\0';
                    return;
                }
                break;
            }
        }
        formatStageDisplayName(out, outSize, stage);
    }

    // The active per-kingdom requirements (RequirementRando-scaled when built),
    // matching what the engine's fill and itinerary actually used.
    static s32 requirementFor(s32 worldId) {
        if (RequirementRando::isInitialized())
            return RequirementRando::getRequirement(worldId);
        if (worldId >= 0 && worldId < LD::sKingdomCount)
            return LD::sMoonRequirementsByKingdom[worldId];
        return 0;
    }

    static void appendMoonRequirements(TextBuf& buf) {
        buf.append("=== Moon Requirements by Kingdom ===\n");
        if (RequirementRando::isInitialized()) {
            for (s32 i = 0; i < RequirementRando::getRequirementKingdomCount(); i++) {
                const s32 worldId = RequirementRando::getRequirementKingdomWorldId(i);
                const s32 req = RequirementRando::getRequirement(worldId);
                MoonRando::KingdomAllocationSummary summary = {};
                if (Randomizer::isMoonAllocationActive()
                    && MoonRando::getKingdomAllocationSummary(worldId, &summary)) {
                    buf.appendf("  %s: requirement=%d max-moon-value=%d list-rows=%d\n",
                        WorldMeta::getDisplayNameForWorldId(worldId), req,
                        summary.assignedValue, summary.assignedEntries);
                } else {
                    buf.appendf("  %s: %d moons\n",
                        WorldMeta::getDisplayNameForWorldId(worldId), req);
                }
            }
        } else {
            for (s32 k = 0; k < LD::sKingdomCount; k++) {
                s32 req = requirementFor(k);
                if (req <= 0)
                    continue;
                buf.appendf("  %s: %d moons\n", WorldMeta::getDisplayNameForWorldId(k), req);
            }
        }
        buf.append("\n");
    }

    static const char* tripleMoonsLabel() {
        const bool anywhere = Randomizer::isTripleMoonsAnywhereEnabled();
        const bool randomized = Randomizer::isRandomizeTripleMoonsEnabled();
        if (anywhere && randomized)
            return "Anywhere + Randomized";
        if (anywhere)
            return "Anywhere";
        if (randomized)
            return "Randomized";
        return "Vanilla";
    }

    static void appendMoonAllocation(TextBuf& buf) {
        buf.append("=== Moon Allocation ===\n");

        if (!Randomizer::isMoonAllocationActive()) {
            buf.append("Disabled\n");
            // Keep the legacy Triple redistribution signal even when the full
            // allocator is inactive. Individual placement lines below continue
            // to show their credited kingdom as well.
            buf.appendf("Triple redistribution: %s\n\n",
                MoonRando::isKingdomRedistributionActive() ? "Active" : "Inactive");
            return;
        }

        buf.appendf("Difficulty: %s\n",
            Randomizer::getMoonAllocationDifficultyName(
                Randomizer::getMoonAllocationDifficulty()));
        buf.appendf("Moon Coins: %s\n",
            Randomizer::getMoonCoinPolicyName(Randomizer::getMoonCoinPolicy()));

        for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++) {
            MoonRando::KingdomAllocationSummary summary = {};
            if (!MoonRando::getKingdomAllocationSummary(kingdom, &summary)) {
                buf.appendf("  %s: allocation summary unavailable\n",
                    WorldMeta::getDisplayNameForWorldId(kingdom));
                continue;
            }

            buf.appendf("  %s: list-rows=%d moon-value=%d base=%d excess=%d target-value=%d minimum=%d",
                WorldMeta::getDisplayNameForWorldId(kingdom),
                summary.assignedEntries, summary.assignedValue,
                summary.baseRequirement, summary.excessValue,
                summary.targetValue, summary.minimumValue);

            if (summary.pinned)
                buf.append(" profile=Pinned-vanilla");
            else if (summary.targetTight)
                buf.append(" profile=Target-tight");
            else if (summary.concentration)
                buf.append(" profile=Concentration");

            switch (summary.specialKind) {
            case MoonRando::AllocationConstraintKind::CaptureSlots:
                buf.appendf(" capture-capacity=%d/%d",
                    summary.specialCapacity, summary.specialMinimum);
                break;
            case MoonRando::AllocationConstraintKind::PaintingEntries:
                buf.appendf(" painting-credits=%d/%d",
                    summary.specialCapacity, summary.specialMinimum);
                break;
            case MoonRando::AllocationConstraintKind::AbilitySlotsCombined:
                buf.appendf(" ability-capacity-combined=%d/%d",
                    summary.specialCapacity, summary.specialMinimum);
                break;
            case MoonRando::AllocationConstraintKind::None:
            default:
                break;
            }
            buf.append("\n");
        }
        buf.append("\n");
    }

    // Does this moon's item grant a capture, ability, cloud painting, moon rock
    // key, or subarea key? Such moons are listed under the sphere's unlock
    // section, everything else under "Moons to Collect".
    static bool itemGrantsUnlock(const ShineDB::ShineEntry& item) {
        return CaptureRando::getCaptureCountForMoon(item) > 0
            || AbilityRando::getAbilityCountForMoon(item) > 0
            || MoonRando::isMoonRockKeyItem(item.uid, item.stage)
            || MoonRando::isSubareaKeyItem(item.uid, item.stage)
            || (Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled()
                && PaintingRando::getCloudMoonPaintingUnlockCount(item) > 0);
    }

    static const char* accessSuffixWord(MoonRando::KingdomAccessKind k) {
        switch (k) {
        case MoonRando::KingdomAccessKind::Painting:
            return "painting";
        case MoonRando::KingdomAccessKind::Entrance:
            return "entrance";
        default:
            return nullptr;
        }
    }

    // Append the " through <Kingdom> painting/entrance" suffix for entrance/painting
    // access, if applicable. For entrance access we also name the specific overworld
    // loading zone by the original (vanilla, non-randomized) sub-area it leads to, so
    // the reader knows which entrance to take.
    static void appendAccessSuffix(
        TextBuf& buf,
        MoonRando::KingdomAccessKind access,
        s16 viaStageIdx,
        s16 viaEntranceIdx,
        s16 entranceChainFirstLink,
        u8 entranceChainFirstKind,
        bool includeEntranceChain) {
        const char* word = accessSuffixWord(access);
        if (!word)
            return;
        s32 viaKingdom = -1;
        if (viaStageIdx >= 0 && viaStageIdx < LD::sStageCount)
            viaKingdom = LD::sStages[viaStageIdx].kingdom;

        if (access == MoonRando::KingdomAccessKind::Entrance
            && viaEntranceIdx >= 0 && viaEntranceIdx < EntranceDB::sLinkCount) {
            const EntranceDB::EntranceLink& via = EntranceDB::sLinks[viaEntranceIdx];
            if (via.subStage && via.subStage[0]) {
                if (viaKingdom >= 0 && viaKingdom < LD::sKingdomCount)
                    buf.appendf(" through %s ",
                        WorldMeta::getDisplayNameForWorldId(viaKingdom));
                else
                    buf.append(" through the ");
                // Name the door the player actually walks through. Naming the entry
                // side unconditionally mislabelled any link reached through its
                // home-out door, and rendered a shadowed row (whose entry id belongs
                // to an earlier link) as if it owned that entry.
                const bool viaFarSide = !EntranceDB::ownsHomeEntranceSide(viaEntranceIdx, false)
                    && EntranceDB::ownsHomeEntranceSide(viaEntranceIdx, true);
                appendHomeLoadingZoneName(buf, via, viaFarSide);

                const EntranceRando::ConnKind chainKind = (EntranceRando::ConnKind)entranceChainFirstKind;
                if (includeEntranceChain
                    && entranceChainFirstLink >= 0
                    && entranceChainFirstLink < EntranceDB::sLinkCount
                    && chainKind != EntranceRando::ConnKind_None) {
                    buf.append(" -> ");
                    appendEntranceChain(
                        buf,
                        EntranceRando::Conn {
                            entranceChainFirstLink,
                            chainKind,
                        });
                }
                return;
            }
        }

        if (viaKingdom >= 0 && viaKingdom < LD::sKingdomCount)
            buf.appendf(" through %s %s", WorldMeta::getDisplayNameForWorldId(viaKingdom), word);
        else
            buf.appendf(" through a %s", word);
    }

    static void appendSphereAreaLine(TextBuf& buf, const MoonRando::SphereArea& area) {
        buf.append("  ");
        switch (area.kind) {
        case MoonRando::SphereAreaKind::Kingdom:
            buf.append(WorldMeta::getDisplayNameForWorldId(area.kingdom));
            buf.appendf(" - Scenario %d", (s32)area.scenario);
            appendAccessSuffix(
                buf, area.access, area.viaStageIdx, area.viaEntranceIdx,
                area.entranceChainFirstLink, area.entranceChainFirstKind, false);
            break;
        case MoonRando::SphereAreaKind::SubArea: {
            char displayName[128];
            const char* raw = (area.stageIdx >= 0 && area.stageIdx < LD::sStageCount)
                ? LD::sStages[area.stageIdx].stage
                : "";
            stageFriendlyName(displayName, sizeof(displayName), raw);
            buf.append(displayName);
            if (area.kingdom >= 0 && area.kingdom < LD::sKingdomCount)
                buf.appendf(" (%s)", WorldMeta::getDisplayNameForWorldId(area.kingdom));
            appendAccessSuffix(
                buf, area.access, area.viaStageIdx, area.viaEntranceIdx,
                area.entranceChainFirstLink, area.entranceChainFirstKind, false);
            break;
        }
        case MoonRando::SphereAreaKind::ScenarioBump:
            buf.append(WorldMeta::getDisplayNameForWorldId(area.kingdom));
            buf.appendf(" -> Scenario %d", (s32)area.scenario);
            break;
        }
        buf.append("\n");
    }

    static void appendSphereMoonLine(TextBuf& buf, const MoonRando::PathStep* step) {
        const ShineDB::ShineEntry& loc = *step->location;
        const ShineDB::ShineEntry& item = *step->item;
        const bool itemIsMulti = MoonRando::isRewardMultiEntry(&item);

        buf.append("  ");
        buf.append(loc.name ? loc.name : "???");
        buf.appendf(" (%s)", WorldMeta::getDisplayNameForWorldId(MoonRando::getAssignedKingdomForEntry(&item)));
        if (itemIsMulti)
            buf.append(" [3 moons]");
        appendMoonUnlocks(buf, item);

        // Sub-area moons: name the stage they live in.
        const s32 stageIdx = step->locationStageIdx;
        const s32 kingdom = step->locationKingdomIdx;
        const bool inSubArea = stageIdx >= 0 && stageIdx < LD::sStageCount
            && kingdom >= 0 && kingdom < LD::sKingdomCount
            && stageIdx != LD::sMainStageByKingdom[kingdom];
        if (inSubArea) {
            char displayName[128];
            stageFriendlyName(displayName, sizeof(displayName), LD::sStages[stageIdx].stage);
            buf.appendf("  [in %s]", displayName);
        }
        buf.append("\n");

        // For sub-area moons, print how to get there (entrance/painting provenance and
        // the in-stage route). Skip the "treated as free / inferred" filler notes.
        if (inSubArea) {
            for (s32 t = 0; t < step->travelStepCount; t++) {
                const char* text = step->travelSteps[t].text;
                if (!text || !text[0])
                    continue;
                if (std::strstr(text, "treated as free") || std::strstr(text, "no authored routes")
                    || std::strstr(text, "no route provenance") || std::strstr(text, "inferred from")
                    || std::strstr(text, "no node route") || std::strstr(text, "no authored graph"))
                    continue;
                buf.appendf("      - %s\n", text);
            }
        }
    }

    static void appendItinerary(TextBuf& buf) {
        buf.append("STARTING KINGDOM: ");
        buf.append(WorldMeta::getDisplayNameForWorldId(KingdomRando::getStartingWorldId()));
        buf.append("\n\n");

        const s32 sphereCount = MoonRando::getSphereCount();
        if (sphereCount <= 0)
            buf.append("No required progress placements were needed.\n\n");

        for (s32 si = 0; si < sphereCount; si++) {
            const MoonRando::SphereInfo* sphere = MoonRando::getSphere(si);
            if (!sphere)
                continue;

            buf.appendf("=== Sphere %d ===\n", sphere->index);

            if (sphere->areaCount > 0) {
                buf.append("New Areas:\n");
                for (s32 i = 0; i < sphere->areaCount; i++)
                    appendSphereAreaLine(buf, sphere->areas[i]);
            }

            bool wroteMoonHeader = false;
            for (s32 i = 0; i < sphere->stepCount; i++) {
                const MoonRando::PathStep* step = MoonRando::getOptimizedPathStep(sphere->firstStepIndex + i);
                if (!step || !step->location || !step->item)
                    continue;
                if (itemGrantsUnlock(*step->item))
                    continue;
                if (!wroteMoonHeader) {
                    buf.append("\nMoons to Collect (progress):\n");
                    wroteMoonHeader = true;
                }
                appendSphereMoonLine(buf, step);
            }

            bool wroteUnlockHeader = false;
            for (s32 i = 0; i < sphere->stepCount; i++) {
                const MoonRando::PathStep* step = MoonRando::getOptimizedPathStep(sphere->firstStepIndex + i);
                if (!step || !step->location || !step->item)
                    continue;
                if (!itemGrantsUnlock(*step->item))
                    continue;
                if (!wroteUnlockHeader) {
                    buf.append("\nCaptures, Abilities & Keys Available:\n");
                    wroteUnlockHeader = true;
                }
                appendSphereMoonLine(buf, step);
            }

            // Progression milestone: depositing this sphere's moons opened the
            // next kingdom(s). Printed at the foot because that is when it
            // happens -- the unlock sweep runs after the sphere's collection, so
            // heading the sphere with it read as "these were already open".
            // Painting/entrance arrivals are NOT route unlocks.
            if (sphere->routeUnlockCount > 0)
                buf.append("\n");
            for (s32 i = 0; i < sphere->routeUnlockCount; i++) {
                buf.append("Kingdom Unlocked: ");
                buf.append(WorldMeta::getDisplayNameForWorldId(sphere->routeUnlocked[i]));
                buf.append("\n");
            }

            buf.append("\n");
        }

        buf.append("=== Final Objective ===\n");
        buf.append("Open Moon Kingdom Chapel\n");
        buf.append(MoonRando::finalObjectiveReached() ? "Proven" : "Not proven");
        buf.append("\n");
        const char* note = MoonRando::getFinalObjectiveNote();
        if (note && note[0]) {
            buf.append(note);
            buf.append("\n");
        }
    }

    // Where the progression placer put the moons it cared about (requirements,
    // captures and abilities) before the leftover fill. Listed ahead of the path so an
    // unbeatable seed can be diagnosed by spotting a critical moon placed behind itself.
    static void appendCriticalPathMoons(TextBuf& buf) {
        buf.append("=== Critical Path Moon Placements ===\n");
        buf.append("(requirements + captures + abilities + keys placed before the remaining moons)\n\n");

        if (!MoonRando::usedLogicPlacement()) {
            buf.append("Graph-aware placement was not used for this seed.\n\n");
            return;
        }

        s32 count = 0;
        for (s32 i = 0; i < MoonRando::getPlacementCount(); i++) {
            if (!MoonRando::isCriticalPathPlacement(i))
                continue;
            const MoonRando::Placement* placement = MoonRando::getPlacement(i);
            if (!placement || !placement->location || !placement->item)
                continue;

            const ShineDB::ShineEntry& item = *placement->item;
            const ShineDB::ShineEntry& location = *placement->location;
            buf.append("  ");
            appendItemDisplayName(buf, item);
            buf.appendf(" (%s)", WorldMeta::getDisplayNameForWorldId(MoonRando::getAssignedKingdomForEntry(&item)));
            appendMoonUnlocks(buf, item, false);
            buf.appendf(" -> %s (%s)\n",
                location.name ? location.name : "???",
                WorldMeta::getDisplayNameForWorldId(location.worldId));
            count++;
        }

        if (count == 0)
            buf.append("  (none)\n");
        buf.appendf("\nCritical path moons: %d\n\n", count);
    }

    // Shared by the spoiler and by generateFailureReport, which is the only
    // record a rejected seed leaves. Everything here reads generation
    // diagnostics rather than the placement, so it is safe on a failed run.
    static void appendGenerationDiagnostics(TextBuf& buf) {
        buf.appendf("Logic proof: %s\n", MoonRando::logicSucceeded() ? "Succeeded" : "Rejected");
        buf.appendf("Dependency attempt: %u\n", MoonRando::getLastEntranceAttempt());
        buf.appendf("Search nodes: %d\n", MoonRando::getSearchNodeCount());
        buf.appendf("Unlock bundles tested: %d\n", MoonRando::getSearchBundleCount());

        const s32 generationAttemptCount = MoonRando::getGenerationAttemptCount();
        if (generationAttemptCount > 0) {
            const u32 durationMillis = MoonRando::getGenerationDurationMillis();
            buf.appendf("Moon logic time: %u.%03u seconds\n",
                durationMillis / 1000, durationMillis % 1000);
            buf.append("Generation attempts:\n");
            for (s32 i = 0; i < generationAttemptCount; i++) {
                const MoonRando::GenerationAttemptInfo* info = MoonRando::getGenerationAttempt(i);
                if (!info)
                    continue;
                buf.appendf(
                    "  %u: %s; setup=%u ms, placement=%u ms, nodes=%d, bundles=%d\n",
                    info->attempt,
                    info->succeeded ? "Succeeded"
                                    : MoonRando::getFailureCodeName(info->result),
                    info->dependencyMillis, info->placementMillis,
                    info->searchNodes, info->searchBundles);
                if (!info->succeeded && info->message[0])
                    buf.appendf("      %s\n", info->message);
            }
        }

        if (MoonRando::getWarningCount() > 0) {
            buf.append("\nLogic warnings:\n");
            for (s32 i = 0; i < MoonRando::getWarningCount(); i++) {
                const char* w = MoonRando::getWarning(i);
                if (w)
                    buf.appendf("  - %s\n", w);
            }
            buf.append("\n");
        }

        if (!MoonRando::logicSucceeded())
            buf.appendf("Failure: %s\n\n", MoonRando::getFailureMessage());
    }

    static void appendSuggestedPath(TextBuf& buf) {
        buf.append("=== Suggested Progress Path ===\n\n");

        if (!MoonRando::usedLogicPlacement()) {
            buf.append("Graph-aware placement was not used for this seed.\n");
            return;
        }

        appendGenerationDiagnostics(buf);

        appendMoonRequirements(buf);

        if (MoonRando::getOptimizedPathStepCount() <= 0) {
            buf.append("No required progress placements were needed.\n");
            buf.append("\n=== Final Objective ===\n");
            buf.append("Open Moon Kingdom Chapel\n");
            buf.appendf("%s\n", MoonRando::finalObjectiveReached() ? "Proven" : "Not proven");
            const char* note = MoonRando::getFinalObjectiveNote();
            if (note && note[0])
                buf.appendf("%s\n", note);
            return;
        }

        appendItinerary(buf);
    }

    void ensureDirectory() {
        SDPaths::ensureRandomizerRoot();
        nn::fs::CreateDirectory("sd:/SMO-Randomizer/Spoiler-Logs");
    }

    static void appendSettings(TextBuf& buf) {
        buf.append("=== Settings ===\n");
        buf.appendf("Moon Requirement: %u moons\n", Randomizer::getMoonRequirement());
        buf.appendf("Moon Difficulty: %s\n", Randomizer::getMoonDifficultyName(Randomizer::getMoonDifficulty()));
        buf.appendf("Logic Difficulty: %s\n", Randomizer::getLogicDifficultyName(Randomizer::getLogicDifficulty()));
        // Each axis prints as chosen -> resolved, so a Random pick stays
        // visible here (and only here) the way Mystery used to.
        buf.appendf("Abilities & Captures: %s (%s)\n",
            Randomizer::getUnlockPacingName(Randomizer::getUnlockPacing()),
            Randomizer::getUnlockPacingName(Randomizer::getResolvedUnlockPacing()));
        buf.appendf("Seed Openness: %s (%s)\n",
            Randomizer::getSeedOpennessName(Randomizer::getSeedOpenness()),
            Randomizer::getSeedOpennessName(Randomizer::getResolvedSeedOpenness()));
        buf.appendf("Seed Difficulty: %s (%s)\n",
            Randomizer::getSeedDifficultyName(Randomizer::getSeedDifficulty()),
            Randomizer::getSeedDifficultyName(Randomizer::getResolvedSeedDifficulty()));
        buf.appendf("Cap Kingdom Moons Use: %s\n",
            Randomizer::getKingdomMoonUseName(WorldMeta::WorldId_Cap));
        buf.appendf("Cloud Kingdom Moons Use: %s\n",
            Randomizer::getKingdomMoonUseName(WorldMeta::WorldId_Cloud));
        buf.appendf("Moon Kingdom Moons Use: %s\n",
            Randomizer::getKingdomMoonUseName(WorldMeta::WorldId_Moon));
        buf.appendf("World Peace Requirement: %s\n", Randomizer::getCurrentWorldPeaceRequirementModeName());
        buf.appendf("Kingdoms on Map: %s\n",
            Randomizer::getVisitedKingdomsMapModeName(Randomizer::getVisitedKingdomsMapMode()));
        buf.appendf("Moon Rock Key: %s\n", Randomizer::isMoonRockKeyEnabled() ? "Enabled" : "Disabled");
        if (Randomizer::isSubareaUnlockEnabled()) {
            buf.appendf("Subarea Unlocks: %s (on %s moons)\n",
                Randomizer::getSubareaUnlockModeName(Randomizer::getSubareaUnlockMode()),
                Randomizer::getSubareaUnlockHostName(Randomizer::getSubareaUnlockHost()));
        } else {
            buf.append("Subarea Unlocks: Unlocked\n");
        }
        buf.appendf("Start Kingdom State: %s\n",
            Randomizer::isStartKingdomStateRandoEnabled() ? "Randomized" : "Vanilla");
        buf.appendf("Chest Order: %s\n",
            Randomizer::isChestOrderRandoEnabled() ? "Randomized" : "Vanilla");
        buf.appendf("Starting Position: %s\n",
            Randomizer::getStartPositionModeName(Randomizer::getStartPositionMode()));
        if (StartPositionRando::isActive()) {
            const s32 startKingdom = StartPositionRando::getStartKingdom();
            const WorldMeta::Entry* startEntry = WorldMeta::getByWorldId(startKingdom);
            StartPositionRando::Spawn spawn = {};
            StartPositionRando::getSpawn(&spawn);
            buf.appendf("  Opens at: %s (%s%s)\n",
                StartPositionRando::getStartNodeId(),
                startEntry && startEntry->displayName ? startEntry->displayName : "?",
                StartPositionRando::isStartInSubArea() ? ", sub-area" : "");
            if (spawn.stage)
                buf.appendf("  Spawn: %s%s%s\n", spawn.stage,
                    spawn.preferEntranceId ? " / " : "",
                    spawn.preferEntranceId ? spawn.entranceId : "");
        }
        buf.appendf("Win Condition: %s\n", Randomizer::getBowserWinConditionName(Randomizer::getBowserWinCondition()));
        if (Randomizer::isToadetteMedalModeEnabled()) {
            const Randomizer::ToadetteMedalRatio ratio = Randomizer::getToadetteMedalRatio();
            buf.appendf("Toadette Medals: %d required / %d total\n", ratio.required, ratio.total);
        }
        buf.appendf("Moon Rando: %s\n", Randomizer::isMoonRandoEnabled() ? "Enabled" : "Disabled");
        buf.appendf("Moon Allocation Master: %s\n",
            Randomizer::isMoonReallocationEnabled() ? "Enabled" : "Disabled");
        buf.appendf("Moon Allocation Randomize: %s\n",
            Randomizer::isRandomizeMoonAllocationEnabled() ? "Enabled" : "Disabled");
        buf.appendf("Moon Allocation Active: %s\n",
            Randomizer::isMoonAllocationActive() ? "Enabled" : "Disabled");
        buf.appendf("Moon Allocation Difficulty: %s\n",
            Randomizer::getMoonAllocationDifficultyName(
                Randomizer::getMoonAllocationDifficulty()));
        buf.appendf("Moon Coin Policy: %s\n",
            Randomizer::getMoonCoinPolicyName(Randomizer::getMoonCoinPolicy()));
        if (Randomizer::FeatureFlags::TripleMoonRandomization)
            buf.appendf("Triple Moons: %s\n", tripleMoonsLabel());
        buf.appendf("Entrance Rando: %s\n", Randomizer::getCurrentEntranceRandoModeName());
        buf.appendf("Shop Locations: %s\n", Randomizer::getCurrentShopLocationModeName());
        buf.appendf("Painting Rando: %s\n", Randomizer::isPaintingRandoEnabled() ? "Enabled" : "Disabled");
        buf.appendf("Kingdom Rando: %s\n", Randomizer::isKingdomRandoEnabled() ? "Enabled" : "Disabled");
        buf.appendf("Logic Rando: %s\n", Randomizer::isLogicRandoEnabled() ? "Enabled" : "Disabled");
        buf.appendf("Skybox Rando: %s\n", Randomizer::getSkyboxRandoModeName(Randomizer::getSkyboxRandoMode()));
        buf.appendf("Capture Rando: %s\n", Randomizer::isCaptureRandoEnabled() ? "Enabled" : "Disabled");
        buf.appendf("Starting Equipment: %s\n", Randomizer::getStartingEquipmentName(Randomizer::getStartingEquipment()));
        buf.appendf("Item Scarcity: %s\n", Randomizer::getCaptureAbilityDifficultyName(Randomizer::getCaptureAbilityDifficulty()));
        buf.appendf("Music Rando: %s\n", Randomizer::getMusicRandoModeName(Randomizer::getMusicRandoMode()));
        buf.appendf("Music Source: %s\n", Randomizer::getMusicSourceModeName(Randomizer::getMusicSourceMode()));
        buf.appendf("Streamer Mode: %s\n", Randomizer::isStreamerModeEnabled() ? "Enabled" : "Disabled");
        buf.appendf("Purple Shop Abilities: %d\n", Randomizer::getPurpleShopAbilityCount());
        buf.appendf("Purple Shop Captures: %d\n", Randomizer::getPurpleShopCaptureCount());
        buf.appendf("Purple Shop Moons: %s\n",
            Randomizer::arePurpleShopMoonsEnabled() ? "Enabled" : "Disabled");
        buf.appendf("Purple Shop Price: %s\n",
            Randomizer::getPurpleShopPriceName(Randomizer::getPurpleShopPrice()));
        buf.appendf("Proportional Kingdom Pricing: %s\n",
            Randomizer::isProportionalKingdomPricingEnabled() ? "Enabled" : "Disabled");
        buf.appendf("Shop Price Rando: %s\n",
            Randomizer::isShopPriceRandoEnabled() ? "Enabled" : "Disabled");
        buf.appendf("Music Overlay: %s\n", Randomizer::isMusicOverlayEnabled() ? "Enabled" : "Disabled");
        const Randomizer::HintCurrencyMode hintCurrency = Randomizer::getHintCurrencyMode();
        const Randomizer::HintPriceProfile coinProfile = Randomizer::getHintCoinPriceProfile();
        const Randomizer::HintPriceProfile tokenProfile = Randomizer::getHintTokenPriceProfile();
        buf.appendf("Hint Currency: %s\n", Randomizer::getHintCurrencyModeName(hintCurrency));
        buf.appendf("Hint Coin Price Moons: %s\n", hintPriceOptionName(Randomizer::HintCurrencyMode::Coins, coinProfile.values[0]));
        buf.appendf("Hint Coin Price Captures: %s\n", hintPriceOptionName(Randomizer::HintCurrencyMode::Coins, coinProfile.values[1]));
        buf.appendf("Hint Coin Price Abilities: %s\n", hintPriceOptionName(Randomizer::HintCurrencyMode::Coins, coinProfile.values[2]));
        buf.appendf("Hint Coin Price Path: %s\n", hintPriceOptionName(Randomizer::HintCurrencyMode::Coins, coinProfile.values[3]));
        buf.appendf("Hint Coin Price Purchases: %s\n", hintPriceOptionName(Randomizer::HintCurrencyMode::Coins, coinProfile.values[4]));
        buf.appendf("Hint Coin Progressive Price: %s\n", hintProgressiveIncrementName(Randomizer::HintCurrencyMode::Coins, coinProfile.progressiveIncrement));
        buf.appendf("Hint Coin Progressive Cost Scope: %s\n", coinProfile.progressiveGlobal ? "Global" : "Per Category");
        buf.appendf("Hint Token Price Moons: %s\n", hintPriceOptionName(Randomizer::HintCurrencyMode::HintTokens, tokenProfile.values[0]));
        buf.appendf("Hint Token Price Captures: %s\n", hintPriceOptionName(Randomizer::HintCurrencyMode::HintTokens, tokenProfile.values[1]));
        buf.appendf("Hint Token Price Abilities: %s\n", hintPriceOptionName(Randomizer::HintCurrencyMode::HintTokens, tokenProfile.values[2]));
        buf.appendf("Hint Token Price Path: %s\n", hintPriceOptionName(Randomizer::HintCurrencyMode::HintTokens, tokenProfile.values[3]));
        buf.appendf("Hint Token Price Purchases: %s\n", hintPriceOptionName(Randomizer::HintCurrencyMode::HintTokens, tokenProfile.values[4]));
        buf.appendf("Hint Token Progressive Price: %s\n", hintProgressiveIncrementName(Randomizer::HintCurrencyMode::HintTokens, tokenProfile.progressiveIncrement));
        buf.appendf("Hint Token Progressive Cost Scope: %s\n", tokenProfile.progressiveGlobal ? "Global" : "Per Category");
        buf.appendf("Sphinx Rando: %s\n", Randomizer::isSphinxRandoEnabled() ? "Enabled" : "Disabled");
        buf.appendf("Locked Abilities: %s\n", Randomizer::isLockedAbilitiesEnabled() ? "Enabled" : "Disabled");
        buf.append("\n");
    }

    void generateFailureReport() {
        SDPaths::ensureRandomizerRoot();
        nn::fs::CreateDirectory("sd:/SMO-Randomizer/Spoiler-Logs");

        TextBuf buf;
        buf.reset();
        buf.appendf("=== SMO Randomizer Generation Failure ===\n"
                    "Seed: %u\n\n",
            Randomizer::getSeed());
        appendSettings(buf);
        buf.append("\n=== Generation Diagnostics ===\n");
        appendGenerationDiagnostics(buf);
        appendKingdomOrder(buf);
        appendMoonRequirements(buf);

        char path[80];
        nn::util::SNPrintf(path, sizeof(path),
            "sd:/SMO-Randomizer/Spoiler-Logs/GenerationFailure - %u.txt",
            Randomizer::getSeed());
        const bool written = writeFile(path, buf.data, buf.used);
        Randomizer::logPush("failure report %s: seed=%u bytes=%u",
            written ? "written" : "WRITE FAILED", Randomizer::getSeed(),
            (u32)buf.used);
    }

    void generate() {
        SDPaths::ensureRandomizerRoot();
        nn::fs::CreateDirectory("sd:/SMO-Randomizer/Spoiler-Logs");

        // Every skip and every failed write is reported. A spoiler that never
        // appears on the SD card is otherwise indistinguishable between the
        // option being off, the placement not being resident yet, and the write
        // itself failing - none of which left a trace before.
        if (!Randomizer::isMoonRandoEnabled()) {
            Randomizer::logPush("spoiler skipped: moon rando off seed=%u",
                Randomizer::getSeed());
            return;
        }

        if (!MoonRando::isInitialized()) {
            Randomizer::logPush("spoiler skipped: placement not initialized seed=%u",
                Randomizer::getSeed());
            return;
        }

        TextBuf buf;
        buf.reset();

        buf.appendf("=== SMO Randomizer Spoiler Log ===\n"
                    "Seed: %u\n\n",
            Randomizer::getSeed());
        appendSettings(buf);
        appendKingdomOrder(buf);
        appendMoonAllocation(buf);
        appendMoonPlacements(buf);
        appendEntrancePlacements(buf);
        appendPaintingLinks(buf);
        appendEnemySwapAssignment(buf);
        appendMoonRockKeys(buf);
        appendSubareaKeys(buf);
        appendToadetteMedals(buf);
        appendCriticalPathMoons(buf);
        appendSuggestedPath(buf);

        char path[64];
        nn::util::SNPrintf(path, sizeof(path), "sd:/SMO-Randomizer/Spoiler-Logs/SpoilerLog - %u.txt", Randomizer::getSeed());
        const bool written = writeFile(path, buf.data, buf.used);
        // A full buffer means the tail sections were silently dropped, so say
        // so rather than leaving a truncated log looking complete.
        Randomizer::logPush("spoiler %s: seed=%u bytes=%u%s",
            written ? "written" : "WRITE FAILED", Randomizer::getSeed(),
            (u32)buf.used,
            buf.used >= TextBuf::kCapacity - 512 ? " (truncated)" : "");
    }

} // namespace SpoilerLog
