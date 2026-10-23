#include "Randomizer/CoinRando.h"

#include "Randomizer/CoinRandoDB.h"
#include "Randomizer/FeatureFlags.h"
#include "Randomizer/Randomizer.h"

#include "al/Library/LiveActor/ActorInitInfo.h"
#include "al/Library/Placement/PlacementFunction.h"
#include "al/Library/Placement/PlacementId.h"
#include "al/Library/Placement/PlacementInfo.h"

#if !SMOR_FEATURE_CHAOS_ANY
#include "hk/hook/Trampoline.h"
#endif

#include <cstring>

namespace CoinRando {
    namespace {

        namespace DB = Randomizer::CoinRandoDB;

        constexpr s32 kCanonicalIdLength = 9;
        constexpr s32 kMaxLoadedPurpleCoins = 1024;

        static s16 sSourceForDestination[DB::sPlacementCount] = {};
        static s16 sActiveIndices[DB::sPlacementCount] = {};
        static char sCanonicalObjectIds[DB::sPlacementCount][kCanonicalIdLength] = {};
        static u32 sAssignmentSeed = 0;
        static s16 sAssignmentScenario = -1;
        static bool sAssignmentEnabled = false;
        static bool sAssignmentBuilt = false;
        static bool sCanonicalIdsBuilt = false;

        struct PurpleTranslation {
            al::PlacementId destination;
            al::PlacementId source;
        };
        static PurpleTranslation sPurpleTranslations[kMaxLoadedPurpleCoins] = {};
        static s32 sPurpleTranslationCount = 0;

        struct IdentityScope {
            const al::PlacementInfo* placement = nullptr;
            const char* sourceObjectId = nullptr;
            const char* sourceUnitConfigName = nullptr;
            Swap swap = {};
        };
        static IdentityScope sIdentityScopes[8] = {};
        static s32 sIdentityScopeDepth = 0;

        static bool isSameDestination(const al::PlacementId& left,
            const al::PlacementId& right) {
            return left.isEqual(right);
        }

        static bool isPurple(DB::Kind kind) {
            return kind == DB::Kind_CoinCollect || kind == DB::Kind_CoinCollect2D;
        }

        static const char* unitForKind(DB::Kind kind) {
            switch (kind) {
            case DB::Kind_CoinCollect:
                return "CoinCollect";
            case DB::Kind_CoinCollect2D:
                return "CoinCollect2D";
            case DB::Kind_CoinDot:
                return "CoinDot";
            case DB::Kind_Coin:
            default:
                return "Coin";
            }
        }

        static bool unitMatchesKind(DB::Kind kind, const char* unit) {
            if (!unit)
                return false;
            if (kind == DB::Kind_Coin)
                return std::strcmp(unit, "Coin") == 0
                    || std::strcmp(unit, "CoinChameleon") == 0;
            return std::strcmp(unitForKind(kind), unit) == 0;
        }

        static void makeCanonicalObjectId(char* out, u16 number) {
            out[0] = 'o';
            out[1] = 'b';
            out[2] = 'j';
            char reverse[5] = {};
            s32 count = 0;
            do {
                reverse[count++] = static_cast<char>('0' + number % 10);
                number /= 10;
            } while (number != 0 && count < 5);
            for (s32 i = 0; i < count; i++)
                out[3 + i] = reverse[count - i - 1];
            out[3 + count] = '\0';
        }

        static bool parseObjectNo(const char* objectId, u16* out) {
            if (!objectId || objectId[0] != 'o' || objectId[1] != 'b'
                || objectId[2] != 'j' || objectId[3] < '0'
                || objectId[3] > '9')
                return false;
            u32 value = 0;
            for (const char* cursor = objectId + 3; *cursor; cursor++) {
                if (*cursor < '0' || *cursor > '9')
                    return false;
                value = value * 10 + static_cast<u32>(*cursor - '0');
                if (value >= 0x4000)
                    return false;
            }
            *out = static_cast<u16>(value);
            return true;
        }

        static bool stageEquals(const char* runtime, const char* database) {
            if (!runtime || !database)
                return false;
            if (std::strcmp(runtime, database) == 0)
                return true;
            const size_t runtimeLength = std::strlen(runtime);
            const size_t databaseLength = std::strlen(database);
            return runtimeLength == databaseLength + 3
                && std::strcmp(runtime + databaseLength, "Map") == 0
                && std::strncmp(runtime, database, databaseLength) == 0;
        }

        static s32 findMap(const char* stage) {
            for (s32 i = 0; i < DB::sMapCount; i++)
                if (stageEquals(stage, DB::sMaps[i].stage))
                    return i;
            return -1;
        }

        static const char* placementMapName(const al::PlacementInfo& placementInfo,
            const char* currentStage) {
            const char* name = nullptr;
            if (placementInfo.getPlacementIter().tryGetStringByKey(
                    &name, "PlacementFileName")
                && name && name[0] && findMap(name) >= 0)
                return name;
            if (placementInfo.getZoneIter().tryGetStringByKey(
                    &name, "PlacementFileName")
                && name && name[0] && findMap(name) >= 0)
                return name;
            if (placementInfo.getZoneIter().tryGetStringByKey(
                    &name, "UnitConfigName")
                && name && name[0] && findMap(name) >= 0)
                return name;
            return currentStage && findMap(currentStage) >= 0 ? currentStage : nullptr;
        }

        static s32 findDestination(const DB::MapRange& map, u16 objectNo,
            const char* unitConfigName) {
            for (s32 i = map.offset; i < map.offset + map.count; i++) {
                const u16 packed = DB::sPlacements[i];
                if (DB::objectNo(packed) == objectNo
                    && unitMatchesKind(DB::kind(packed), unitConfigName))
                    return i;
            }
            return -1;
        }

        static u32 hashStage(const char* stage) {
            u32 hash = 2166136261u;
            for (const u8* cursor = reinterpret_cast<const u8*>(stage);
                 cursor && *cursor; cursor++) {
                hash ^= *cursor;
                hash *= 16777619u;
            }
            return hash;
        }

        static u32 nextRandom(u32* state) {
            u32 value = *state;
            if (value == 0)
                value = 0x6d2b79f5u;
            value ^= value << 13;
            value ^= value >> 17;
            value ^= value << 5;
            *state = value;
            return value;
        }

        static void buildCanonicalIds() {
            if (sCanonicalIdsBuilt)
                return;
            for (s32 i = 0; i < DB::sPlacementCount; i++)
                makeCanonicalObjectId(
                    sCanonicalObjectIds[i], DB::objectNo(DB::sPlacements[i]));
            sCanonicalIdsBuilt = true;
        }

        static s32 effectiveScenarioIndex(
            const DB::MapRange& map, s16 scenarioNoPlacement) {
            const s32 index = scenarioNoPlacement - 1;
            // StageResourceList indexes every physical map's BYML array with the exact
            // one-based placement scenario. An out-of-range array lookup yields no
            // object iterator; it does not clamp to that map's last scenario.
            return index >= 0 && index < map.scenarioCount && index < 16 ? index : -1;
        }

        static void buildAssignment(s16 scenarioNoPlacement) {
            buildCanonicalIds();
            const u32 seed = Randomizer::getSeed();
            const bool enabled = Randomizer::isCoinRandoEnabled();
            if (sAssignmentBuilt && seed == sAssignmentSeed
                && enabled == sAssignmentEnabled
                && scenarioNoPlacement == sAssignmentScenario)
                return;

            for (s32 i = 0; i < DB::sPlacementCount; i++)
                sSourceForDestination[i] = static_cast<s16>(i);

            if (enabled) {
                for (s32 mapIndex = 0; mapIndex < DB::sMapCount; mapIndex++) {
                    const DB::MapRange& map = DB::sMaps[mapIndex];
                    const s32 scenarioIndex
                        = effectiveScenarioIndex(map, scenarioNoPlacement);
                    if (scenarioIndex < 0)
                        continue;
                    const u16 scenarioBit = static_cast<u16>(1u << scenarioIndex);
                    s32 activeCount = 0;
                    for (s32 localGroup = 0; localGroup < map.groupCount;
                         localGroup++) {
                        const u64 group
                            = DB::sScenarioGroups[map.groupOffset + localGroup];
                        if ((DB::groupMask(group) & scenarioBit) == 0)
                            continue;
                        const s32 end = DB::groupOffset(group) + DB::groupCount(group);
                        for (s32 i = DB::groupOffset(group); i < end; i++)
                            sActiveIndices[activeCount++] = static_cast<s16>(i);
                    }

                    u32 state = seed ^ hashStage(map.stage) ^ 0x434f494eu
                        ^ (static_cast<u32>(scenarioIndex + 1) * 0x9e3779b9u);
                    for (s32 i = activeCount - 1; i > 0; i--) {
                        const s32 other = static_cast<s32>(
                            nextRandom(&state) % static_cast<u32>(i + 1));
                        const s32 destination = sActiveIndices[i];
                        const s32 otherDestination = sActiveIndices[other];
                        const s16 temp = sSourceForDestination[destination];
                        sSourceForDestination[destination]
                            = sSourceForDestination[otherDestination];
                        sSourceForDestination[otherDestination] = temp;
                    }

                    // Ensure a mixed-color loaded map actually exchanges at least one
                    // regular and purple identity, even for the rare color-preserving
                    // random permutation.
                    s32 regularDestination = -1;
                    s32 purpleDestination = -1;
                    bool hasCrossColor = false;
                    for (s32 i = 0; i < activeCount; i++) {
                        const s32 destination = sActiveIndices[i];
                        const DB::Kind destinationKind
                            = DB::kind(DB::sPlacements[destination]);
                        const DB::Kind sourceKind = DB::kind(
                            DB::sPlacements[sSourceForDestination[destination]]);
                        if (isPurple(destinationKind) != isPurple(sourceKind))
                            hasCrossColor = true;
                        if (isPurple(destinationKind))
                            purpleDestination = destination;
                        else
                            regularDestination = destination;
                    }
                    if (!hasCrossColor && regularDestination >= 0
                        && purpleDestination >= 0) {
                        const s16 temp = sSourceForDestination[regularDestination];
                        sSourceForDestination[regularDestination]
                            = sSourceForDestination[purpleDestination];
                        sSourceForDestination[purpleDestination] = temp;
                    }
                }
            }

            sAssignmentSeed = seed;
            sAssignmentScenario = scenarioNoPlacement;
            sAssignmentEnabled = enabled;
            sAssignmentBuilt = true;
            // A new seed/setting invalidates every live destination-to-source mapping.
            sPurpleTranslationCount = 0;
        }

        static void registerPurpleTranslation(const al::PlacementInfo& placementInfo,
            s32 sourceIndex) {
            al::PlacementId destination;
            al::getPlacementId(&destination, placementInfo);
            if (!destination.isValid())
                return;
            for (s32 i = 0; i < sPurpleTranslationCount; i++) {
                if (!isSameDestination(
                        sPurpleTranslations[i].destination, destination))
                    continue;
                sPurpleTranslations[i].source = destination;
                sPurpleTranslations[i].source.mId = sCanonicalObjectIds[sourceIndex];
                return;
            }
            if (sPurpleTranslationCount >= kMaxLoadedPurpleCoins) {
                Randomizer::logPush(
                    "coin shuffle: purple identity table full at %d entries",
                    kMaxLoadedPurpleCoins);
                return;
            }
            PurpleTranslation& translation
                = sPurpleTranslations[sPurpleTranslationCount++];
            translation.destination = destination;
            translation.source = destination;
            translation.source.mId = sCanonicalObjectIds[sourceIndex];
        }

        static const IdentityScope* currentScope() {
            return sIdentityScopeDepth > 0
                ? &sIdentityScopes[sIdentityScopeDepth - 1]
                : nullptr;
        }

        static bool isScoped(const al::PlacementInfo& placementInfo) {
            const IdentityScope* scope = currentScope();
            if (!scope || !scope->placement)
                return false;
            if (scope->placement == &placementInfo)
                return true;
            const char* objectId = nullptr;
            const char* unitConfigName = nullptr;
            return scope->sourceObjectId && scope->sourceUnitConfigName
                && placementInfo.getPlacementIter().tryGetStringByKey(&objectId, "Id")
                && placementInfo.getPlacementIter().tryGetStringByKey(
                    &unitConfigName, "UnitConfigName")
                && objectId && unitConfigName
                && std::strcmp(objectId, scope->sourceObjectId) == 0
                && std::strcmp(unitConfigName, scope->sourceUnitConfigName) == 0;
        }

        static bool isScoped(const al::ActorInitInfo& initInfo) {
            return initInfo.placementInfo && isScoped(*initInfo.placementInfo);
        }

    } // namespace

    bool resolveSwap(const al::PlacementInfo& placementInfo,
        const char* currentStage, s16 scenarioNoPlacement, Swap* outSwap) {
        if (!outSwap || !Randomizer::isCoinRandoEnabled())
            return false;
        buildAssignment(scenarioNoPlacement);

        const char* objectId = nullptr;
        const char* unitConfigName = nullptr;
        if (!placementInfo.getPlacementIter().tryGetStringByKey(&objectId, "Id")
            || !placementInfo.getPlacementIter().tryGetStringByKey(
                &unitConfigName, "UnitConfigName")
            || !objectId || !unitConfigName)
            return false;
        u16 objectNo = 0;
        if (!parseObjectNo(objectId, &objectNo))
            return false;

        const char* mapName = placementMapName(placementInfo, currentStage);
        const s32 mapIndex = findMap(mapName);
        if (mapIndex < 0)
            return false;
        const s32 destinationIndex = findDestination(
            DB::sMaps[mapIndex], objectNo, unitConfigName);
        if (destinationIndex < 0)
            return false;
        const s32 sourceIndex = sSourceForDestination[destinationIndex];
        if (sourceIndex < 0 || sourceIndex >= DB::sPlacementCount
            || sourceIndex == destinationIndex)
            return false;

        const DB::Kind destinationKind = DB::kind(
            DB::sPlacements[destinationIndex]);
        const DB::Kind sourceKind = DB::kind(DB::sPlacements[sourceIndex]);
        if (isPurple(sourceKind))
            registerPurpleTranslation(placementInfo, sourceIndex);

        // Regular coins do not have persistent source identity. Leaving a
        // regular-to-regular destination native preserves its authored target
        // behavior (including CoinDot and hidden CoinChameleon locations).
        if (!isPurple(sourceKind) && !isPurple(destinationKind))
            return false;

        const bool destinationIs2D = destinationKind == DB::Kind_CoinCollect2D
            || destinationKind == DB::Kind_CoinDot;
        if (isPurple(sourceKind)) {
            const char* identity = destinationIs2D ? "CoinCollect2D" : "CoinCollect";
            outSwap->objectName = identity;
            outSwap->displayName = identity;
            outSwap->className = identity;
            outSwap->modelName = identity;
        } else if (destinationIs2D) {
            outSwap->objectName = "CoinDot";
            outSwap->displayName = "Coin2D";
            outSwap->className = "Coin2D";
            outSwap->modelName = "CoinDot";
        } else {
            outSwap->objectName = "Coin";
            outSwap->displayName = "Coin";
            outSwap->className = "Coin";
            outSwap->modelName = "Coin";
        }
        return true;
    }

    const al::PlacementId* translatePurplePlacement(
        const al::PlacementId* placementId) {
        if (!placementId || !Randomizer::isCoinRandoEnabled())
            return placementId;
        for (s32 i = 0; i < sPurpleTranslationCount; i++)
            if (isSameDestination(
                    sPurpleTranslations[i].destination, *placementId))
                return &sPurpleTranslations[i].source;
        return placementId;
    }

    void resetStageState() {
        sPurpleTranslationCount = 0;
        sIdentityScopeDepth = 0;
    }

    void beginIdentityScope(const al::PlacementInfo* placementInfo,
        const char* sourceObjectId, const char* sourceUnitConfigName,
        const Swap& swap) {
        if (sIdentityScopeDepth >= static_cast<s32>(
                sizeof(sIdentityScopes) / sizeof(sIdentityScopes[0])))
            return;
        IdentityScope& scope = sIdentityScopes[sIdentityScopeDepth++];
        scope.placement = placementInfo;
        scope.sourceObjectId = sourceObjectId;
        scope.sourceUnitConfigName = sourceUnitConfigName;
        scope.swap = swap;
    }

    void endIdentityScope() {
        if (sIdentityScopeDepth > 0)
            sIdentityScopes[--sIdentityScopeDepth] = {};
    }

#if !SMOR_FEATURE_CHAOS_ANY
    namespace {

        static const Swap* scopedSwap(const al::ActorInitInfo& info) {
            const IdentityScope* scope = currentScope();
            return scope && isScoped(info) ? &scope->swap : nullptr;
        }

        static const Swap* scopedSwap(const al::PlacementInfo& info) {
            const IdentityScope* scope = currentScope();
            return scope && isScoped(info) ? &scope->swap : nullptr;
        }

#define COIN_IDENTITY_GET_HOOK(name, type, member)                              \
    static HkTrampoline<void, const char**, const type&> name                   \
        = hk::hook::trampoline([](const char** out, const type& info) -> void { \
              const Swap* swap = scopedSwap(info);                              \
              if (swap) {                                                       \
                  *out = swap->member;                                          \
                  return;                                                       \
              }                                                                 \
              name.orig(out, info);                                             \
          })

#define COIN_IDENTITY_TRY_HOOK(name, type, member)                              \
    static HkTrampoline<bool, const char**, const type&> name                   \
        = hk::hook::trampoline([](const char** out, const type& info) -> bool { \
              const Swap* swap = scopedSwap(info);                              \
              if (swap) {                                                       \
                  *out = swap->member;                                          \
                  return swap->member != nullptr;                               \
              }                                                                 \
              return name.orig(out, info);                                      \
          })

        COIN_IDENTITY_GET_HOOK(getObjectNameHook, al::ActorInitInfo, objectName);
        COIN_IDENTITY_TRY_HOOK(tryGetObjectNameHook, al::ActorInitInfo, objectName);
        COIN_IDENTITY_GET_HOOK(getPlacementObjectNameHook, al::PlacementInfo, objectName);
        COIN_IDENTITY_TRY_HOOK(tryGetPlacementObjectNameHook, al::PlacementInfo, objectName);
        COIN_IDENTITY_GET_HOOK(getDisplayNameHook, al::ActorInitInfo, displayName);
        COIN_IDENTITY_TRY_HOOK(tryGetDisplayNameHook, al::ActorInitInfo, displayName);
        COIN_IDENTITY_GET_HOOK(getPlacementDisplayNameHook, al::PlacementInfo, displayName);
        COIN_IDENTITY_TRY_HOOK(tryGetPlacementDisplayNameHook, al::PlacementInfo, displayName);
        COIN_IDENTITY_GET_HOOK(getClassNameHook, al::ActorInitInfo, className);
        COIN_IDENTITY_TRY_HOOK(tryGetClassNameHook, al::ActorInitInfo, className);
        COIN_IDENTITY_GET_HOOK(getPlacementClassNameHook, al::PlacementInfo, className);
        COIN_IDENTITY_TRY_HOOK(tryGetPlacementClassNameHook, al::PlacementInfo, className);
        COIN_IDENTITY_GET_HOOK(getModelNameHook, al::ActorInitInfo, modelName);
        COIN_IDENTITY_TRY_HOOK(tryGetModelNameHook, al::ActorInitInfo, modelName);
        COIN_IDENTITY_GET_HOOK(getPlacementModelNameHook, al::PlacementInfo, modelName);
        COIN_IDENTITY_TRY_HOOK(tryGetPlacementModelNameHook, al::PlacementInfo, modelName);

#undef COIN_IDENTITY_GET_HOOK
#undef COIN_IDENTITY_TRY_HOOK

        static HkTrampoline<bool, const al::ActorInitInfo&, const char*>
            isClassNameHook = hk::hook::trampoline(
                [](const al::ActorInitInfo& info, const char* name) -> bool {
                    const Swap* swap = scopedSwap(info);
                    return swap ? name && std::strcmp(name, swap->className) == 0
                                : isClassNameHook.orig(info, name);
                });
        static HkTrampoline<bool, const al::PlacementInfo&, const char*>
            isPlacementClassNameHook = hk::hook::trampoline(
                [](const al::PlacementInfo& info, const char* name) -> bool {
                    const Swap* swap = scopedSwap(info);
                    return swap ? name && std::strcmp(name, swap->className) == 0
                                : isPlacementClassNameHook.orig(info, name);
                });
        static HkTrampoline<bool, const al::ActorInitInfo&, const char*>
            isObjectNameHook = hk::hook::trampoline(
                [](const al::ActorInitInfo& info, const char* name) -> bool {
                    const Swap* swap = scopedSwap(info);
                    return swap ? name && std::strcmp(name, swap->objectName) == 0
                                : isObjectNameHook.orig(info, name);
                });
        static HkTrampoline<bool, const al::PlacementInfo&, const char*>
            isPlacementObjectNameHook = hk::hook::trampoline(
                [](const al::PlacementInfo& info, const char* name) -> bool {
                    const Swap* swap = scopedSwap(info);
                    return swap ? name && std::strcmp(name, swap->objectName) == 0
                                : isPlacementObjectNameHook.orig(info, name);
                });
        static HkTrampoline<bool, const al::ActorInitInfo&, const char*>
            isObjectNameSubStrHook = hk::hook::trampoline(
                [](const al::ActorInitInfo& info, const char* name) -> bool {
                    const Swap* swap = scopedSwap(info);
                    return swap ? name && std::strstr(swap->objectName, name) != nullptr
                                : isObjectNameSubStrHook.orig(info, name);
                });
        static HkTrampoline<bool, const al::PlacementInfo&, const char*>
            isPlacementObjectNameSubStrHook = hk::hook::trampoline(
                [](const al::PlacementInfo& info, const char* name) -> bool {
                    const Swap* swap = scopedSwap(info);
                    return swap ? name && std::strstr(swap->objectName, name) != nullptr
                                : isPlacementObjectNameSubStrHook.orig(info, name);
                });

    } // namespace
#endif

    void installHooks() {
#if !SMOR_FEATURE_CHAOS_ANY
        tryGetClassNameHook.installAtSym<
            "_ZN2al15tryGetClassNameEPPKcRKNS_13ActorInitInfoE">();
        getObjectNameHook.installAtMainOffset(0x0099e5cc);
        getPlacementObjectNameHook.installAtMainOffset(0x0099e628);
        tryGetPlacementObjectNameHook.installAtMainOffset(0x0099e688);
        tryGetObjectNameHook.installAtMainOffset(0x0099e6f4);
        getDisplayNameHook.installAtMainOffset(0x0099eb28);
        tryGetDisplayNameHook.installAtMainOffset(0x0099eb30);
        getPlacementDisplayNameHook.installAtMainOffset(0x0099eb38);
        tryGetPlacementDisplayNameHook.installAtMainOffset(0x0099eb3c);
        getClassNameHook.installAtMainOffset(0x0099ea88);
        tryGetPlacementClassNameHook.installAtMainOffset(0x0099e970);
        getPlacementClassNameHook.installAtMainOffset(0x0099ea90);
        isClassNameHook.installAtMainOffset(0x0099ea94);
        isPlacementClassNameHook.installAtMainOffset(0x0099eadc);
        isObjectNameHook.installAtMainOffset(0x0099e7c0);
        isPlacementObjectNameHook.installAtMainOffset(0x0099e82c);
        isObjectNameSubStrHook.installAtMainOffset(0x0099e894);
        isPlacementObjectNameSubStrHook.installAtMainOffset(0x0099e900);
        getModelNameHook.installAtMainOffset(0x009a31d8);
        getPlacementModelNameHook.installAtMainOffset(0x009a3270);
        tryGetPlacementModelNameHook.installAtMainOffset(0x009a3308);
        tryGetModelNameHook.installAtMainOffset(0x009a33ac);
        Randomizer::logPush("coin shuffle identity hooks installed");
#endif
    }

} // namespace CoinRando
