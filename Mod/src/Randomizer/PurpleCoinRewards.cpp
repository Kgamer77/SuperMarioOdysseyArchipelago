#include "Randomizer/PurpleCoinRewards.h"

#include "Randomizer/AbilityOverlay.h"
#include "Randomizer/AbilityRando.h"
#include "Randomizer/BingoBoard.h"
#include "Randomizer/CaptureOverlay.h"
#include "Randomizer/CaptureRando.h"
#include "Randomizer/CoinRando.h"
#include "Randomizer/KingdomProgress.h"
#include "Randomizer/LogicData.h"
#include "Randomizer/MoonHooks.h"
#include "Randomizer/MoonPurchaseOverlay.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/RequirementRando.h"
#include "Randomizer/ShopPriceRando.h"
#include "Randomizer/WorldMeta.h"
#include "ShineDB.h"

#include "Multiplayer/MpDebugLog.h"
#include "Multiplayer/layouts/PlayerEventLog.h"
#include "Multiplayer/main.hpp"
#include "Multiplayer/server/Client.hpp"
#include "al/Library/Base/StringUtil.h"
#include "al/Library/Event/EventFlowNode.h"
#include "al/Library/Layout/LayoutActor.h"
#include "al/Library/Layout/LayoutActorUtil.h"
#include "al/Library/LiveActor/ActorPoseUtil.h"
#include "al/Library/LiveActor/LiveActor.h"
#include "al/Library/Nerve/NerveStateBase.h"
#include "al/Library/Nerve/NerveUtil.h"
#include "al/Library/Placement/PlacementFunction.h"
#include "al/Library/Placement/PlacementId.h"
#include "al/Library/Placement/PlacementInfo.h"
#include "game/Item/ShineInfo.h"
#include "game/Layout/ShopLayoutInfo.h"
#include "game/Layout/ShopUtil.h"
#include "game/MapObj/CapMessageShowInfo.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"
#include "game/System/GameDataHolderWriter.h"
#include "game/Util/ClothUtil.h"
#include "game/Util/ItemUtil.h"
#include "sead/container/seadPtrArray.h"

#include "hk/hook/Trampoline.h"
#include "hk/ro/RoModule.h"
#include "nn/util.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace al {
    class LayoutActor;
}

namespace PurpleCoinRewards {

    namespace {

        bool sBlockNextMultiplayerPurchase = false;

        constexpr uintptr_t kCoinCollectArchiveNameOffset = 0x00530c10;
        constexpr uintptr_t kCoinCollectEmptyArchiveNameOffset = 0x00530c68;
        constexpr uintptr_t kCoinCollect2DArchiveNameOffset = 0x00530cc0;
        constexpr uintptr_t kCoinCollect2DEmptyArchiveNameOffset = 0x00530d18;
        constexpr uintptr_t kWorldCoinCollectPictureFontOffset = 0x00578698;
        constexpr uintptr_t kGetShopItemInfoListOffset = 0x0052bbc4;
        constexpr uintptr_t kIsEnableBuyItemOffset = 0x00209488;
        constexpr uintptr_t kGetGiftListOffset = 0x0054cbb8;
        constexpr uintptr_t kCreateTextureInfoOffset = 0x008b48cc;
        constexpr s32 kMaxShopInventories = 64;
        constexpr s32 kMaxShopOffers = 3 + 3 + WorldMeta::sWorldCount;
        constexpr s32 kSyntheticItemBase = 0x6F000000;
        constexpr char kOfferMessageKeyPrefix[] = "PurpleShop_";
        constexpr s32 kOfferMessageKeyHexDigits = 8;
        // NOTE on ability tile art: the shop/list tiles render textures at
        // native texel scale. Vanilla TextureSouvenir and HackListTexture
        // entries are all exactly 256x256; the mod's ability images (495-910px)
        // therefore overflow and crop. No pane-side scale/size write fixes
        // that (it only moves the crop window) - the images themselves must be
        // exported at 256x256.
        // Purple-clerk labels rerouted by startNpcMessageHook and resolved by
        // tryGetShopMessageOverride: the vanilla buy question and thanks lines
        // both promise Odyssey delivery, which no longer happens. Neither may
        // contain kOfferMessageKeyPrefix ("PurpleShop_").
        constexpr char kPurpleShopBuyThanksLabel[] = "PurpleShopThanks";
        constexpr char kPurpleShopBuyQuestionLabel[] = "PurpleShopBuyQuestion";
        // Cappy's once-per-kingdom purple-coin explainer, replacing the vanilla
        // "collect them all!" tutorial with the active shop settings.
        constexpr char kFirstCoinCapMessageKey[] = "PurpleShopCoinHint";
        constexpr char kFirstCoinHintFlagPrefix[] = "PurpleCoinHint_";
        constexpr s32 kMaxNpcActorPlacements = 2048;

        struct NpcActorPlacement {
            const al::LiveActor* actor;
            u32 key;
        };

        NpcActorPlacement sNpcActorPlacements[kMaxNpcActorPlacements] = {};
        s32 sNpcActorPlacementCount = 0;

        // Purchase-ledger records (see the header for why they exist).  The
        // hack dictionary holds 160 shared slots of 128 chars, so abilities and
        // captures collapse into one hex mask each - both id spaces are well
        // under 64 - and the moons ride in a single comma-separated list.
        constexpr char kPurchasedAbilityPrefix[] = "PurpleBuyAbility:";
        constexpr char kPurchasedCapturePrefix[] = "PurpleBuyCapture:";
        constexpr char kPurchasedMoonPrefix[] = "PurpleBuyMoon:";
        // Ledger records share the 128-char record width with everything else.
        constexpr s32 kPurchaseRecordSize = 128;

        u64 readPurchaseMask(const GameDataFile* file, const char* prefix) {
            if (!file)
                return 0;
            const char* record = file->customFindHackDictionaryPrefix(prefix);
            return record ? std::strtoull(record + std::strlen(prefix), nullptr, 16) : 0;
        }

        void writePurchaseMask(GameDataFile* file, const char* prefix, u64 mask) {
            if (!file)
                return;
            file->customRemoveHackDictionaryPrefix(prefix);
            if (!mask)
                return;
            char record[kPurchaseRecordSize];
            nn::util::SNPrintf(record, sizeof(record), "%s%llx", prefix,
                static_cast<unsigned long long>(mask));
            file->customAddHackDictionary(record);
        }

        void recordPurchasedBit(GameDataFile* file, const char* prefix, s32 index) {
            if (!file || index < 0 || index >= 64)
                return;
            writePurchaseMask(file, prefix, readPurchaseMask(file, prefix) | (1ull << index));
        }

        void recordPurchasedMoon(GameDataFile* file, const ShineDB::ShineEntry* item) {
            const s32 index = item ? ShineDB::findIndex(item) : -1;
            if (!file || index < 0)
                return;

            const char* existing = file->customFindHackDictionaryPrefix(kPurchasedMoonPrefix);
            char record[kPurchaseRecordSize];
            if (existing) {
                // A run that somehow fills the record keeps the entries it has;
                // the alternative is truncating one mid-number into a different
                // moon's index.
                const s32 written = nn::util::SNPrintf(record, sizeof(record), "%s,%d",
                    existing, index);
                if (written < 0 || written >= static_cast<s32>(sizeof(record))) {
                    Randomizer::logPush("purple shop purchase ledger full; moon %d unrecorded",
                        index);
                    return;
                }
                file->customRemoveHackDictionaryPrefix(kPurchasedMoonPrefix);
            } else {
                nn::util::SNPrintf(record, sizeof(record), "%s%d", kPurchasedMoonPrefix, index);
            }
            file->customAddHackDictionary(record);
        }

        enum class CoinArchiveKind
        {
            Normal,
            Empty,
            Normal2D,
            Empty2D,
        };

        template <typename Fn>
        Fn mainOffsetFunction(uintptr_t offset) {
            return reinterpret_cast<Fn>(hk::ro::getMainModule()->range().start() + offset);
        }

        // ShopInfo+0x20 is the constructor's isCoinShop flag: 1 drives the gold
        // "Coin" side, 0 the purple "CollectCoin" side (updateItemPartsData
        // picks the coin icon from this exact byte). This is the structural
        // purple-side signal; clerk model names vary per kingdom (the interior
        // Sand clerk never matched the CapWoman/SeaWoman/CityWomanShop list,
        // leaving that shop on its vanilla inventory).
        bool isCollectCoinShopInfo(const void* shopInfo) {
            return shopInfo && *(reinterpret_cast<const u8*>(shopInfo) + 0x20) == 0;
        }

        // Clerk actors whose shop list we replaced, so actor-keyed hooks (the
        // post-purchase message reroute) can recognize a generated shop without
        // guessing at model names. Refilled every scene via createShopItemList.
        constexpr s32 kMaxGeneratedShopActors = 8;
        const al::LiveActor* sGeneratedShopActors[kMaxGeneratedShopActors] = {};
        s32 sGeneratedShopActorNext = 0;

        void recordGeneratedShopActor(const al::LiveActor* actor) {
            if (!actor)
                return;
            for (s32 i = 0; i < kMaxGeneratedShopActors; i++) {
                if (sGeneratedShopActors[i] == actor)
                    return;
            }
            sGeneratedShopActors[sGeneratedShopActorNext] = actor;
            sGeneratedShopActorNext = (sGeneratedShopActorNext + 1) % kMaxGeneratedShopActors;
        }

        bool isGeneratedShopActor(const al::LiveActor* actor) {
            if (!actor)
                return false;
            for (s32 i = 0; i < kMaxGeneratedShopActors; i++) {
                if (sGeneratedShopActors[i] == actor)
                    return true;
            }
            return false;
        }

        u32 makeNpcActionKey(const al::LiveActor* actor, u32 domain) {
            u32 hash = 2166136261u ^ domain;
            auto append = [&hash](const char* text) {
                while (text && *text) {
                    hash ^= static_cast<u8>(*text++);
                    hash *= 16777619u;
                }
            };
            append(actor ? actor->getName() : nullptr);
            if (actor) {
                GameDataHolderAccessor accessor(actor);
                GameDataFile* file = accessor.mData ? accessor.mData->getGameDataFile() : nullptr;
                append(file ? file->getStageNameCurrent() : nullptr);
                const sead::Vector3f& trans = al::getTrans(actor);
                u32 bits[3];
                std::memcpy(bits, &trans, sizeof(bits));
                for (s32 i = 0; i < 3; i++) {
                    hash ^= bits[i];
                    hash *= 16777619u;
                }
            }
            return hash ? hash : domain;
        }

        u32 findNpcActorPlacementKey(const al::LiveActor* actor) {
            if (!actor)
                return 0;
            for (s32 i = 0; i < sNpcActorPlacementCount; i++)
                if (sNpcActorPlacements[i].actor == actor)
                    return sNpcActorPlacements[i].key;
            return 0;
        }

        u32 makeNpcPlacementKey(
            const al::LiveActor* actor, const al::PlacementInfo& placementInfo) {
            u32 hash = 2166136261u ^ 0x58000000u;
            auto append = [&hash](const char* text) {
                while (text && *text) {
                    hash ^= static_cast<u8>(*text++);
                    hash *= 16777619u;
                }
                // Keep adjacent fields unambiguous even when one is empty.
                hash ^= 0xFFu;
                hash *= 16777619u;
            };
            append(actor ? actor->getName() : nullptr);
            // Placement Ids are only unique inside their owning map. Include
            // the active stage so the same authored Id/transform in two maps
            // cannot satisfy the unique-NPC goal as one shared conversation.
            if (actor) {
                GameDataHolderAccessor accessor(actor);
                GameDataFile* file = accessor.mData ? accessor.mData->getGameDataFile() : nullptr;
                append(file ? file->getStageNameCurrent() : nullptr);
            } else {
                append(nullptr);
            }
            // PlacementInfo owns the placement and zone iterators separately.
            // Reading guessed zone/file keys from the placement iterator loses
            // the zone identity and can merge two same-position NPCs from
            // different embedded maps. PlacementId is the engine's canonical
            // compound identity and includes all four stable authored fields.
            al::PlacementId placementId;
            al::getPlacementId(&placementId, placementInfo);
            append(placementId.getId());
            append(placementId.getUnitConfigName());
            append(placementId.getZoneId());
            append(placementId.getCommonId());

            // Id is normally sufficient, but generated/copied placements can
            // reuse it. Their authored transform remains stable across stage
            // reloads, unlike the actor's live transform used by the old key.
            sead::Vector3f trans = sead::Vector3f::zero;
            if (al::tryGetTrans(&trans, placementInfo)) {
                u32 bits[3];
                std::memcpy(bits, &trans, sizeof(bits));
                for (s32 i = 0; i < 3; i++) {
                    hash ^= bits[i];
                    hash *= 16777619u;
                }
            }
            return hash ? hash : 0x58000000u;
        }

        void recordNpcConversationImpl(const al::LiveActor* actor) {
            if (!actor)
                return;
            const u32 placementKey = findNpcActorPlacementKey(actor);
            Bingo::recordEvent({ Bingo::GoalType::NpcConversations, 1, -1,
                placementKey ? placementKey : makeNpcActionKey(actor, 0x58000000u),
                Bingo::EventOrigin::Local });
        }

        bool isSouvenirShopItem(const ShopItem::ShopItemInfo* itemInfo) {
            if (!itemInfo)
                return false;
            return itemInfo->coinType == ShopItem::CoinType::Collect
                && (itemInfo->info.type == ShopItem::ItemType::Gift
                    || itemInfo->info.type == ShopItem::ItemType::Sticker);
        }

        bool isPurpleShopFeatureEnabled() {
            return Randomizer::getPurpleShopAbilityCount() > 0
                || Randomizer::getPurpleShopCaptureCount() > 0
                || Randomizer::arePurpleShopMoonsEnabled();
        }

        s32 getCoinCollectGotNum(GameDataFile* file, s32 worldId) {
            if (!file || worldId < 0 || worldId >= sNumWorlds)
                return 0;
            return file->mCoinCollectGotNum[worldId];
        }

        s32 getCoinCollectNumMax(GameDataHolder* holder, s32 worldId) {
            if (!holder || !holder->mCoinCollectNumMax || worldId < 0 || worldId >= sNumWorlds)
                return 0;
            return holder->mCoinCollectNumMax[worldId];
        }

        const char* getCoinCollectArchiveName(GameDataHolder* holder, s32 worldId, CoinArchiveKind kind) {
            if (!holder || worldId < 0 || worldId >= sNumWorlds)
                return nullptr;
            // The native GameDataHolder::getCoinCollect*ArchiveName functions index a
            // table that's only populated for worlds with regional coins (e.g. Cap,
            // Cloud, Moon, Mushroom, Dark, Darker have none) and dereference the entry
            // through a vtable call with no null check, so an unregistered worldId
            // crashes instead of returning a name. rs::getWorldCoinCollectPictureFont
            // guards the same lookup with this exact check; mirror it here.
            if (getCoinCollectNumMax(holder, worldId) <= 0)
                return nullptr;

            using Fn = const char* (*)(const GameDataHolder*, s32);
            static const Fn sFunctions[] = {
                mainOffsetFunction<Fn>(kCoinCollectArchiveNameOffset),
                mainOffsetFunction<Fn>(kCoinCollectEmptyArchiveNameOffset),
                mainOffsetFunction<Fn>(kCoinCollect2DArchiveNameOffset),
                mainOffsetFunction<Fn>(kCoinCollect2DEmptyArchiveNameOffset),
            };

            s32 index = 0;
            switch (kind) {
            case CoinArchiveKind::Empty:
                index = 1;
                break;
            case CoinArchiveKind::Normal2D:
                index = 2;
                break;
            case CoinArchiveKind::Empty2D:
                index = 3;
                break;
            case CoinArchiveKind::Normal:
            default:
                break;
            }

            return sFunctions[index](holder, worldId);
        }

        enum class OfferKind : u8
        {
            Ability,
            Capture,
            Moon,
        };

        struct ShopOffer {
            OfferKind kind;
            s32 rewardId;
            s32 itemIndex;
            // What this offer costs, settled by buildRuntimeItems. Under
            // Random every offer on the shelf carries its own number, so the
            // clerk's quote and the purchase log have to read the offer rather
            // than the shop. Zero until a shop screen has been built.
            s32 price;
            char16_t displayName[96];
        };

        struct ShopInventory {
            bool inUse;
            u32 key;
            s32 offerCount;
            ShopOffer offers[kMaxShopOffers];
            ShopItem::ShopItemInfo items[kMaxShopOffers];
        };

        // ShopLayoutItemInfo is a private 16-byte native record: item pointer,
        // sort key, and original display position. The native ShopInfo owns these
        // records after createShopItemList returns.
        struct ShopLayoutItemInfo {
            ShopItem::ShopItemInfo* item;
            s32 sortKey;
            s32 originalIndex;
        };

        static_assert(sizeof(ShopLayoutItemInfo) == 0x10, "ShopLayoutItemInfo layout");

        ShopInventory sShopInventories[kMaxShopInventories] = {};

        u32 scramble(u32 value) {
            value ^= value >> 16;
            value *= 0x7FEB352Du;
            value ^= value >> 15;
            value *= 0x846CA68Bu;
            value ^= value >> 16;
            return value;
        }

        u32 nextRandom(u32& state) {
            state = scramble(state + 0x9E3779B9u);
            return state;
        }

        void copyAscii(char16_t* out, size_t capacity, const char* text) {
            if (!out || capacity == 0)
                return;
            size_t pos = 0;
            while (text && *text && pos + 1 < capacity)
                out[pos++] = static_cast<char16_t>(*text++);
            out[pos] = u'\0';
        }

        void appendAscii(char16_t* out, size_t capacity, const char* text) {
            if (!out || capacity == 0)
                return;
            size_t pos = 0;
            while (pos + 1 < capacity && out[pos])
                pos++;
            while (text && *text && pos + 1 < capacity)
                out[pos++] = static_cast<char16_t>(*text++);
            out[pos] = u'\0';
        }

        void makeOfferMessageKey(char* out, size_t capacity, s32 itemIndex) {
            if (!out || capacity == 0)
                return;
            nn::util::SNPrintf(out, capacity, "%s%08X", kOfferMessageKeyPrefix,
                static_cast<u32>(itemIndex));
        }

        bool isEligibleMoonKingdom(s32 worldId) {
            auto isEligibleSpecialUse = [](u8 mask) {
                // A synthetic shop moon is a direct purchase. It must never
                // consume a Hint Token reward moon, because direct purchases
                // intentionally do not award the token back.
                constexpr u8 cPurchaseUseMask = (1u << Randomizer::SpecialMoonUse_AbilitiesOrCaptures)
                    | (1u << Randomizer::SpecialMoonUse_KingdomUnlock)
                    | (1u << Randomizer::SpecialMoonUse_PaintingUnlock)
                    | (1u << Randomizer::SpecialMoonUse_Abilities)
                    | (1u << Randomizer::SpecialMoonUse_Captures);
                return (mask & cPurchaseUseMask) != 0;
            };
            switch (worldId) {
            case WorldMeta::WorldId_Cap:
                return isEligibleSpecialUse(Randomizer::getCapKingdomMoonUseMask());
            case WorldMeta::WorldId_Cloud:
                return isEligibleSpecialUse(Randomizer::getCloudKingdomMoonUseMask());
            case WorldMeta::WorldId_Moon:
                return isEligibleSpecialUse(Randomizer::getMoonKingdomMoonUseMask());
            case WorldMeta::WorldId_Mushroom:
            case WorldMeta::WorldId_Dark:
            case WorldMeta::WorldId_Darker:
                // Post-game kingdoms: their moon totals never gate progression,
                // so a purchased moon there would be a dead offer slot.
                return false;
            default:
                return WorldMeta::getByWorldId(worldId) != nullptr;
            }
        }

        static bool isHintTokenRewardMoon(const ShineDB::ShineEntry& entry) {
            switch (MoonRando::getAssignedKingdomForEntry(&entry)) {
            case WorldMeta::WorldId_Cap:
                return Randomizer::isCapKingdomMoonUseSelected(
                    Randomizer::SpecialMoonUse_HintTokens);
            case WorldMeta::WorldId_Cloud:
                return Randomizer::isCloudKingdomMoonUseSelected(
                    Randomizer::SpecialMoonUse_HintTokens);
            case WorldMeta::WorldId_Moon:
                return Randomizer::isMoonKingdomMoonUseSelected(
                    Randomizer::SpecialMoonUse_HintTokens);
            default:
                return false;
            }
        }

        // TextureShine uses the game's internal world names, which differ from
        // several WorldMeta display/development names (Waterfall, Forest, etc.).
        const char* getMoonTextureName(s32 worldId) {
            static const char* sTextureNames[WorldMeta::sWorldCount] = {
                "MoonCap",
                "MoonWaterfall",
                "MoonSand",
                "MoonForest",
                "MoonLake",
                "MoonCloud",
                "MoonClash",
                "MoonCity",
                "MoonSea",
                "MoonSnow",
                "MoonLava",
                "MoonAttack",
                "MoonSky",
                "MoonMoon",
                "MoonPeach",
                "MoonSpecial1",
                "MoonSpecial2",
            };
            return worldId >= 0 && worldId < WorldMeta::sWorldCount
                ? sTextureNames[worldId]
                : nullptr;
        }

        void addOffer(ShopInventory* inventory, OfferKind kind, s32 rewardId, const char* displayName) {
            if (!inventory || inventory->offerCount >= kMaxShopOffers)
                return;
            ShopOffer& offer = inventory->offers[inventory->offerCount];
            offer.kind = kind;
            offer.rewardId = rewardId;
            offer.price = 0;
            offer.itemIndex = kSyntheticItemBase
                + static_cast<s32>((inventory - sShopInventories) * kMaxShopOffers)
                + inventory->offerCount;
            copyAscii(offer.displayName, sizeof(offer.displayName) / sizeof(offer.displayName[0]), displayName);
            inventory->offerCount++;
        }

        void sampleDistinct(s32* values, s32 count, s32 wanted, u32& random,
            ShopInventory* inventory, OfferKind kind) {
            if (!values || !inventory || count <= 0 || wanted <= 0)
                return;
            const s32 selected = wanted < count ? wanted : count;
            for (s32 i = 0; i < selected; i++) {
                const s32 swapIndex = i + static_cast<s32>(nextRandom(random) % static_cast<u32>(count - i));
                const s32 value = values[swapIndex];
                values[swapIndex] = values[i];
                values[i] = value;

                if (kind == OfferKind::Ability) {
                    const AbilityRando::AbilityEntry* ability = AbilityRando::getAbility(value);
                    if (ability)
                        addOffer(inventory, kind, value,
                            AbilityRando::getProgressiveDisplayName(*ability));
                } else {
                    const CaptureRando::CaptureEntry* capture = CaptureRando::getCapture(value);
                    if (capture)
                        addOffer(inventory, kind, value,
                            capture->displayName ? capture->displayName : capture->hackName);
                }
            }
        }

        ShopInventory* findInventory(u32 key) {
            for (s32 i = 0; i < kMaxShopInventories; i++) {
                if (sShopInventories[i].inUse && sShopInventories[i].key == key)
                    return &sShopInventories[i];
            }
            return nullptr;
        }

        ShopInventory* allocateInventory(u32 key) {
            if (ShopInventory* existing = findInventory(key))
                return existing;
            for (s32 i = 0; i < kMaxShopInventories; i++) {
                if (!sShopInventories[i].inUse) {
                    sShopInventories[i] = {};
                    sShopInventories[i].inUse = true;
                    sShopInventories[i].key = key;
                    return &sShopInventories[i];
                }
            }
            return nullptr;
        }

        const ShopItem::ShopItemInfo* findDisplayTemplate(const al::LiveActor* actor,
            s32 giftIndex = 0) {
            GameDataHolderAccessor accessor(actor);
            if (!accessor.mData)
                return nullptr;
            using GetShopItemInfoListFn = const sead::PtrArray<ShopItem::ShopItemInfo>& (*)(GameDataHolderAccessor);
            const sead::PtrArray<ShopItem::ShopItemInfo>& items = mainOffsetFunction<GetShopItemInfoListFn>(kGetShopItemInfoListOffset)(accessor);
            s32 giftCount = 0;
            for (s32 i = 0; i < items.size(); i++) {
                const ShopItem::ShopItemInfo* item = items.at(i);
                if (!item || item->info.type != ShopItem::ItemType::Gift)
                    continue;
                if (giftCount++ == giftIndex)
                    return item;
            }
            return nullptr;
        }

        const ShopItem::ShopItemInfo* findDisplayTemplateByGiftName(
            const al::LiveActor* actor, const char* giftName) {
            if (!giftName || !giftName[0])
                return nullptr;
            GameDataHolderAccessor accessor(actor);
            if (!accessor.mData)
                return nullptr;
            using GetShopItemInfoListFn = const sead::PtrArray<ShopItem::ShopItemInfo>& (*)(GameDataHolderAccessor);
            const sead::PtrArray<ShopItem::ShopItemInfo>& items = mainOffsetFunction<GetShopItemInfoListFn>(kGetShopItemInfoListOffset)(accessor);
            for (s32 i = 0; i < items.size(); i++) {
                const ShopItem::ShopItemInfo* item = items.at(i);
                if (item && item->info.type == ShopItem::ItemType::Gift
                    && std::strcmp(item->info.name, giftName) == 0)
                    return item;
            }
            return nullptr;
        }

        const ShopItem::ItemInfo* getGiftDisplayItem(const al::LiveActor* actor, s32 giftIndex) {
            if (giftIndex < 0)
                return nullptr;
            GameDataHolderAccessor accessor(actor);
            if (!accessor.mData)
                return nullptr;
            using GetGiftListFn = const sead::PtrArray<ShopItem::ItemInfo>& (*)(GameDataHolderAccessor);
            const sead::PtrArray<ShopItem::ItemInfo>& gifts = mainOffsetFunction<GetGiftListFn>(kGetGiftListOffset)(accessor);
            return giftIndex < gifts.size() ? gifts.at(giftIndex) : nullptr;
        }

        s32 getAbilityDisplayIndex(s32 abilityId) {
            for (s32 i = 0;; i++) {
                const AbilityRando::AbilityEntry* ability = AbilityRando::getDisplayAbility(i);
                if (!ability)
                    return -1;
                if (ability->id == abilityId)
                    return i;
            }
        }

        // The Ability List loads TextureSouvenir from the native mItemGift entries.
        // Resolve the same entry by name rather than assuming it shares an index
        // with the separate ShopItemInfo list.
        const ShopItem::ShopItemInfo* findOfferDisplayTemplate(const al::LiveActor* actor,
            const ShopOffer& offer) {
            if (offer.kind == OfferKind::Ability) {
                const s32 displayIndex = getAbilityDisplayIndex(offer.rewardId);
                if (displayIndex >= 0) {
                    if (const ShopItem::ItemInfo* gift = getGiftDisplayItem(actor, displayIndex)) {
                        if (const ShopItem::ShopItemInfo* item = findDisplayTemplateByGiftName(actor, gift->name))
                            return item;
                    }
                }
            }
            return findDisplayTemplate(actor);
        }

        s32 getShopKingdomId(const al::LiveActor* actor);

        // What this kingdom's purple shop charges. Flat selections are the same
        // number everywhere; Random and Proportional Kingdom Pricing both read
        // the kingdom's own purple-coin total, which is the number the HUD
        // counter shows (custom table first, native table second - the same
        // order getCoinCollectNumMaxHook uses, so the price agrees with the
        // "/100" the player is looking at).
        s32 getKingdomPurpleTotal(GameDataHolderAccessor accessor, s32 worldId) {
            if (!accessor.mData || worldId < 0)
                return 0;
            GameDataFile* file = accessor->getGameDataFile();
            const s32 customMax = file ? file->customGetCoinCollectMax(worldId) : 0;
            if (customMax > 0)
                return customMax;
            return getCoinCollectNumMax(accessor.mData, worldId);
        }

        // Identifies one offer to the price roll. The kingdom half of the key
        // is added by resolvePurpleShopPrice, so this only has to separate the
        // items sharing a single shelf: an offer is exactly its kind and the
        // reward it hands over, and buildInventoryForShop never puts the same
        // pair on a shelf twice.
        u32 makeOfferPriceKey(const ShopOffer& offer) {
            return (static_cast<u32>(offer.kind) + 1) * 0x85ebca6bu
                ^ (static_cast<u32>(offer.rewardId) + 1) * 0xc2b2ae35u;
        }

        // The clerk's "that'll be N purple coins" line has no actor to resolve
        // a kingdom from and no item in hand: the label it is built from names
        // the shop, not the row. Prices are per offer, so quoting the shop is
        // not enough - ShopStateInShop::exeSelect runs
        // ShopInfo::isEnableBuyItem for the row the player just pressed A on
        // (and is its only caller), which is where the selection is latched.
        // A null inventory means the screen on display is a vanilla one.
        static ShopInventory* sCurrentShopInventory = nullptr;
        static s32 sSelectedOfferIndex = -1;

        const ShopOffer* currentShopOffer() {
            if (!sCurrentShopInventory || sSelectedOfferIndex < 0
                || sSelectedOfferIndex >= sCurrentShopInventory->offerCount)
                return nullptr;
            return &sCurrentShopInventory->offers[sSelectedOfferIndex];
        }

        s32 currentShopPurplePrice() {
            const ShopOffer* offer = currentShopOffer();
            return offer && offer->price > 0 ? offer->price
                                             : Randomizer::getPurpleShopPriceValue();
        }

        void buildRuntimeItems(ShopInventory* inventory, const al::LiveActor* actor) {
            if (!inventory)
                return;
            const s32 worldId = getShopKingdomId(actor);
            // One lookup for the whole shelf: every offer prices against the
            // same kingdom total, and only the per-offer key changes.
            const s32 purpleTotal
                = actor ? getKingdomPurpleTotal(GameDataHolderAccessor(actor), worldId) : 0;
            for (s32 i = 0; i < inventory->offerCount; i++) {
                ShopOffer& offer = inventory->offers[i];
                offer.price = actor ? Randomizer::resolvePurpleShopPrice(
                                          worldId, purpleTotal, makeOfferPriceKey(offer))
                                    : Randomizer::getPurpleShopPriceValue();
                ShopItem::ShopItemInfo& item = inventory->items[i];
                const ShopItem::ShopItemInfo* displayTemplate = findOfferDisplayTemplate(actor, offer);
                item = displayTemplate ? *displayTemplate : ShopItem::ShopItemInfo();
                item.info.index = offer.itemIndex;
                item.info.type = ShopItem::ItemType::Gift;
                // The price the row draws, the amount ShopInfo::isEnableBuyItem
                // checks the wallet against, and the amount
                // ShopStateInShop::exeBuy charges are all this one field, so
                // per-offer prices need nothing further to be honoured.
                item.price = offer.price;
                item.coinType = ShopItem::CoinType::Collect;
                // ShopStateInShop builds its selected-item description from
                // storeName. Give every generated offer a unique dynamic message
                // key instead of inheriting the copied souvenir's dialogue.
                makeOfferMessageKey(item.storeName, sizeof(item.storeName), item.info.index);
            }
            sCurrentShopInventory = inventory;
        }

        s32 getShopKingdomId(const al::LiveActor* actor) {
            if (!actor)
                return -1;
            GameDataHolderAccessor accessor(actor);
            if (!accessor.mData)
                return -1;
            const s32 worldId = findOriginalWorldIdForStage(
                GameDataFunction::getCurrentStageName(accessor));
            return worldId >= 0 ? worldId : GameDataFunction::getCurrentWorldId(accessor);
        }

        ShopInventory* buildInventoryForShop(const al::LiveActor* actor) {
            if (!actor)
                return nullptr;
            // One deterministic selection per kingdom. The old key mixed in the
            // closest shop-table index and the clerk's position, both of which
            // are unstable while the scene is still initializing, so revisiting
            // a kingdom could rebuild a different inventory.
            const s32 kingdomId = getShopKingdomId(actor);
            if (kingdomId < 0)
                return nullptr;
            const u32 key = scramble(0x4B440000u ^ static_cast<u32>(kingdomId));
            ShopInventory* inventory = allocateInventory(key);
            if (!inventory)
                return nullptr;
            if (inventory->offerCount > 0) {
                buildRuntimeItems(inventory, actor);
                return inventory;
            }

            u32 random = scramble(Randomizer::getSeed() ^ key ^ 0x50534850u);
            // The selector always determines the number of offers. When the
            // corresponding lock feature is disabled its offers simply render
            // Sold Out because the player already owns that whole collection.
            if (Randomizer::getPurpleShopAbilityCount() > 0) {
                s32 candidates[AbilityRando::AbilityId_Count] = {};
                s32 candidateCount = 0;
                const s32 count = AbilityRando::getAbilityCount();
                for (s32 i = 0; i < count && candidateCount < AbilityRando::AbilityId_Count; i++) {
                    const AbilityRando::AbilityEntry* ability = AbilityRando::getAbility(i);
                    if (ability && !ability->alwaysUnlocked)
                        candidates[candidateCount++] = i;
                }
                sampleDistinct(candidates, candidateCount, Randomizer::getPurpleShopAbilityCount(),
                    random, inventory, OfferKind::Ability);
            }

            if (Randomizer::getPurpleShopCaptureCount() > 0) {
                s32 candidates[128] = {};
                s32 candidateCount = 0;
                const s32 count = CaptureRando::getCaptureCount();
                for (s32 i = 0; i < count && candidateCount < 128; i++) {
                    const CaptureRando::CaptureEntry* capture = CaptureRando::getCapture(i);
                    if (capture && CaptureRando::canGrantCaptureDirectly(*capture))
                        candidates[candidateCount++] = i;
                }
                sampleDistinct(candidates, candidateCount, Randomizer::getPurpleShopCaptureCount(),
                    random, inventory, OfferKind::Capture);
            }

            if (Randomizer::arePurpleShopMoonsEnabled()) {
                for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++) {
                    if (!isEligibleMoonKingdom(worldId))
                        continue;
                    char name[96];
                    nn::util::SNPrintf(name, sizeof(name), "%s Power Moon",
                        WorldMeta::getDisplayNameForWorldId(worldId));
                    addOffer(inventory, OfferKind::Moon, worldId, name);
                }
            }

            buildRuntimeItems(inventory, actor);
            Randomizer::logPush("purple shop inventory key=%08x offers=%d abilities=%d captures=%d moons=%d",
                key, inventory->offerCount, Randomizer::getPurpleShopAbilityCount(),
                Randomizer::getPurpleShopCaptureCount(), Randomizer::arePurpleShopMoonsEnabled());
            return inventory;
        }

        ShopOffer* findOfferByItemIndex(s32 itemIndex) {
            if (itemIndex < kSyntheticItemBase || itemIndex >= kSyntheticItemBase + kMaxShopInventories * kMaxShopOffers)
                return nullptr;
            for (s32 inventoryIndex = 0; inventoryIndex < kMaxShopInventories; inventoryIndex++) {
                ShopInventory& inventory = sShopInventories[inventoryIndex];
                if (!inventory.inUse)
                    continue;
                for (s32 offerIndex = 0; offerIndex < inventory.offerCount; offerIndex++) {
                    if (inventory.offers[offerIndex].itemIndex == itemIndex)
                        return &inventory.offers[offerIndex];
                }
            }
            return nullptr;
        }

        ShopOffer* findOfferByItemInfo(const ShopItem::ItemInfo& itemInfo) {
            if (ShopOffer* offer = findOfferByItemIndex(itemInfo.index))
                return offer;
            for (s32 inventoryIndex = 0; inventoryIndex < kMaxShopInventories; inventoryIndex++) {
                ShopInventory& inventory = sShopInventories[inventoryIndex];
                if (!inventory.inUse)
                    continue;
                for (s32 offerIndex = 0; offerIndex < inventory.offerCount; offerIndex++) {
                    if (&inventory.items[offerIndex].info == &itemInfo)
                        return &inventory.offers[offerIndex];
                }
            }
            return nullptr;
        }

        bool isAbilityOfferSoldOut(GameDataHolderAccessor accessor, const ShopOffer& offer) {
            const AbilityRando::AbilityEntry* ability = AbilityRando::getAbility(offer.rewardId);
            for (s32 depth = 0; ability && depth < AbilityRando::AbilityId_Count; depth++) {
                if (!AbilityRando::isAbilityUnlocked(accessor, *ability))
                    return false;
                ability = ability->prerequisite >= 0
                    ? AbilityRando::getAbility(ability->prerequisite)
                    : nullptr;
            }
            return true;
        }

        // Power Moon list rows a shop purchase may credit: ordinary rows of the
        // target kingdom. Story/multi rows drive scripted progression, Toadette
        // rows are achievements, and rows holding Toadette Medals or a key of
        // either kind (or placed at story/multi locations) follow the hint
        // shop's purchase-block rule.
        constexpr s32 kMaxShopMoonCandidates = 128;

        s32 collectShopMoonCandidates(GameDataFile* file, s32 worldId,
            const ShineDB::ShineEntry** outCandidates, s32 maxCandidates) {
            if (!file || maxCandidates <= 0)
                return 0;
            s32 count = 0;
            for (s32 i = 0; i < ShineDB::sShineTableSize && count < maxCandidates; i++) {
                const ShineDB::ShineEntry& entry = ShineDB::sShineTable[i];
                if (entry.worldId != worldId || entry.objId < 0)
                    continue;
                if (entry.flags & (ShineDB::ShineFlag_Story | ShineDB::ShineFlag_Toadette))
                    continue;
                // The direct grant intentionally suppresses currency rewards,
                // so a synthetic Purple Shop offer must not consume a moon
                // that a world pickup would have paid as a Hint Token.
                if (isHintTokenRewardMoon(entry))
                    continue;
                // Never offer a triple/grand for sale. Honors Randomize Triple
                // Moons so the seed's actual triples are excluded (and a demoted
                // vanilla grand, now a single, may be sold like any moon).
                if (MoonRando::isRewardMultiEntry(&entry))
                    continue;
                if (MoonRando::isToadetteMedalItem(entry.uid, entry.stage)
                    || MoonRando::isMoonRockKeyItem(entry.uid, entry.stage)
                    || MoonRando::isSubareaKeyItem(entry.uid, entry.stage))
                    continue;
                if (MoonRando::isInitialized()) {
                    if (const MoonRando::Placement* placement = MoonRando::getPlacementForItem(entry.uid, entry.stage)) {
                        const ShineDB::ShineEntry* location = placement->location;
                        if (location && (location->objId < 0 || (location->flags & (ShineDB::ShineFlag_Story | ShineDB::ShineFlag_Multi))))
                            continue;
                    }
                }
                if (MoonHooks::isMoonAcquiredDirect(file, &entry))
                    continue;
                if (outCandidates)
                    outCandidates[count] = &entry;
                count++;
            }
            return count;
        }

        const ShineDB::ShineEntry* pickRandomShopMoon(GameDataFile* file, s32 worldId) {
            const ShineDB::ShineEntry* candidates[kMaxShopMoonCandidates];
            const s32 count = collectShopMoonCandidates(file, worldId, candidates,
                kMaxShopMoonCandidates);
            if (count <= 0)
                return nullptr;
            static u32 sPickState = 0;
            if (sPickState == 0)
                sPickState = scramble(Randomizer::getSeed() ^ 0x4D4F4F4Eu);
            return candidates[nextRandom(sPickState) % static_cast<u32>(count)];
        }

        bool isMoonOfferSoldOut(GameDataFile* file, s32 targetWorldId) {
            if (!file || targetWorldId < 0 || targetWorldId >= WorldMeta::sWorldCount)
                return true;
            if ((Randomizer::getVisitedKingdoms() & (1u << targetWorldId)) == 0)
                return false;
            if (collectShopMoonCandidates(file, targetWorldId, nullptr,
                    kMaxShopMoonCandidates)
                <= 0)
                return true;
            if (const KingdomProgress::KingdomProgressEntry* entry = KingdomProgress::findEntry(file, targetWorldId)) {
                return KingdomProgress::requirementsSatisfied(file, *entry);
            }
            // Pay-shines are indexed by the globe where they were deposited;
            // reward assignment and kingdom requirements use the credited total.
            return file->getShineNum(targetWorldId)
                >= RequirementRando::getRequirement(targetWorldId);
        }

        bool isOfferSoldOut(const al::IUseSceneObjHolder* holder, const ShopOffer& offer) {
            GameDataHolderAccessor accessor(holder);
            GameDataFile* file = accessor.mData ? accessor->getGameDataFile() : nullptr;
            if (!file)
                return true;
            switch (offer.kind) {
            case OfferKind::Ability:
                return isAbilityOfferSoldOut(accessor, offer);
            case OfferKind::Capture: {
                const CaptureRando::CaptureEntry* capture = CaptureRando::getCapture(offer.rewardId);
                return !capture || CaptureRando::isCaptureUnlocked(accessor, *capture);
            }
            case OfferKind::Moon:
                return isMoonOfferSoldOut(file, offer.rewardId);
            }
            return true;
        }

        const AbilityRando::AbilityEntry* getNextAbilityGrant(GameDataHolderAccessor accessor,
            const AbilityRando::AbilityEntry* ability) {
            for (s32 depth = 0; ability && depth < AbilityRando::AbilityId_Count; depth++) {
                if (ability->prerequisite < 0)
                    return ability;
                const AbilityRando::AbilityEntry* prerequisite = AbilityRando::getAbility(ability->prerequisite);
                if (!prerequisite || AbilityRando::isAbilityUnlocked(accessor, *prerequisite))
                    return ability;
                ability = prerequisite;
            }
            return nullptr;
        }

        bool tryPurchaseOffer(const al::IUseSceneObjHolder* holder, ShopOffer& offer) {
            GameDataHolderAccessor accessor(holder);
            GameDataFile* file = accessor.mData ? accessor->getGameDataFile() : nullptr;
            if (!accessor.mData || !file || isOfferSoldOut(holder, offer))
                return false;

            // The vanilla buy flow already charged the player: ShopStateInShop::exeBuy
            // runs GameDataFunction::useCoinCollect (remap-aware via useCoinCollectHook)
            // before ShopInfo::buyItem lands here, and exeCheckBuy blocked the purchase
            // when coins were short. Charging again here double-billed every purchase.
            GameDataHolderWriter writer(accessor.mData);
            bool granted = false;
            switch (offer.kind) {
            case OfferKind::Ability: {
                const AbilityRando::AbilityEntry* endpoint = AbilityRando::getAbility(offer.rewardId);
                const AbilityRando::AbilityEntry* next = getNextAbilityGrant(accessor, endpoint);
                if (next) {
                    AbilityRando::setAbilityUnlocked(writer, *next, true);
                    AbilityOverlay::queueUnlock(next->displayName ? next->displayName : next->key);
                    // The ledger records what was actually granted, not the
                    // offer's endpoint: a chained ability sells one prerequisite
                    // step at a time, and only the step paid for is bought.
                    recordPurchasedBit(file, kPurchasedAbilityPrefix, static_cast<s32>(next->id));
                    // Unlike a shop Moon (which syncs automatically through
                    // MoonHooks::grantMoonDirect -> setGotShine ->
                    // mpOnSetGotShine), this only wrote the local save flag -
                    // the other player never found out an ability was bought.
                    // Confirmed via user report: partner didn't receive an
                    // ability purchased from the purple-coin shop.
                    if (Client::isSocketActive())
                        Client::sendAbilityUnlockPacket(static_cast<int>(next->id));
                    PlayerEventLog::addSelfEvent(PlayerEventLog::PURPLE_BUY,
                        next->displayName ? next->displayName : next->key);
                    granted = true;
                }
                break;
            }
            case OfferKind::Capture: {
                const CaptureRando::CaptureEntry* capture = CaptureRando::getCapture(offer.rewardId);
                if (capture && CaptureRando::canGrantCaptureDirectly(*capture)) {
                    CaptureRando::setCaptureUnlocked(writer, *capture, true);
                    CaptureOverlay::queueUnlock(
                        capture->displayName ? capture->displayName : capture->hackName);
                    recordPurchasedBit(file, kPurchasedCapturePrefix, offer.rewardId);
                    // Same sync gap as the Ability branch above.
                    if (Client::isSocketActive())
                        Client::sendCaptureUnlockPacket(offer.rewardId);
                    PlayerEventLog::addSelfEvent(PlayerEventLog::PURPLE_BUY,
                        capture->displayName ? capture->displayName : capture->hackName);
                    granted = true;
                }
                break;
            }
            case OfferKind::Moon: {
                // Pick a random not-yet-acquired row of the target kingdom's
                // Power Moon list and grant it exactly the way the hint shop's
                // Buy action does: MoonHooks::grantMoonDirect on the row, with
                // the overlay showing the randomized location the same way
                // (so its name matches what flips in the Power Moon list).
                const ShineDB::ShineEntry* item = pickRandomShopMoon(file, offer.rewardId);
                const MoonRando::Placement* placement = item && MoonRando::isInitialized()
                    ? MoonRando::getPlacementForItem(item->uid, item->stage)
                    : nullptr;
                const ShineDB::ShineEntry* location = placement ? placement->location : nullptr;
                if (item && MoonHooks::grantMoonDirect(writer, item, location)) {
                    MoonPurchaseOverlay::queue(*item, location, false);
                    recordPurchasedMoon(file, item);
                    Randomizer::logPush("purple shop moon item=%s uid=%d location=%s uid=%d",
                        item->name ? item->name : "<none>", item->uid,
                        location && location->name ? location->name : "<none>",
                        location ? location->uid : -1);
                    granted = true;
                    // Synthetic purple-shop rewards do not have a native
                    // ShineInfo shop record, so they cannot update the native
                    // shop counter. Count the purchased item directly for Bingo.
                    Bingo::recordEvent({ Bingo::GoalType::ShopMoons, 1, -1,
                        0x5A000000u
                            | (static_cast<u32>(ShineDB::findIndex(item))
                                & 0x00FFFFFFu),
                        Bingo::EventOrigin::Local });
                }
                break;
            }
            }

            if (granted) {
                GameDataFunction::setRequireSave(writer);
                Randomizer::logPush("purple shop purchase kind=%d reward=%d price=%d",
                    static_cast<s32>(offer.kind), offer.rewardId,
                    offer.price > 0 ? offer.price : currentShopPurplePrice());
            }
            return granted;
        }

        void applyGeneratedOfferTextures(void* layoutInfo, const sead::PtrArrayImpl* list,
            s32 itemCount) {
            if (!layoutInfo || !list || itemCount <= 0)
                return;

            nn::ui2d::TextureInfo** textures = *reinterpret_cast<nn::ui2d::TextureInfo***>(
                reinterpret_cast<u8*>(layoutInfo) + 0x48);

            for (s32 i = 0; i < itemCount; i++) {
                const ShopLayoutItemInfo* layoutItem = static_cast<const ShopLayoutItemInfo*>(list->at(i));
                if (!layoutItem || !layoutItem->item)
                    continue;
                ShopOffer* offer = findOfferByItemInfo(layoutItem->item->info);
                if (!offer)
                    continue;

                const s32 textureIndex = layoutItem->originalIndex;
                if (textures && textureIndex >= 0 && offer->kind == OfferKind::Capture) {
                    const CaptureRando::CaptureEntry* capture = CaptureRando::getCapture(offer->rewardId);
                    if (capture && capture->hackName) {
                        using CreateTextureInfoFn = nn::ui2d::TextureInfo* (*)(const char*,
                            const char*, const char*);
                        textures[textureIndex] = mainOffsetFunction<CreateTextureInfoFn>(
                            kCreateTextureInfoOffset)(
                            "ObjectData/HackListTexture", "HackListTexture", capture->hackName);
                    }
                } else if (textures && textureIndex >= 0
                    && offer->kind == OfferKind::Ability) {
                    // Movement Abilities only. Their art was added to
                    // TextureSouvenir for this mod, so no vanilla gift record names
                    // it and the row would otherwise inherit whatever souvenir sits
                    // at its display index. Safe to build here, unlike in the
                    // Ability List: this runs inside the shop's own texture load,
                    // where a sead heap is current.
                    if (const char* textureName = AbilityRando::getSouvenirTextureName(
                            static_cast<AbilityRando::AbilityId>(offer->rewardId))) {
                        using CreateTextureInfoFn = nn::ui2d::TextureInfo* (*)(const char*,
                            const char*, const char*);
                        textures[textureIndex] = mainOffsetFunction<CreateTextureInfoFn>(
                            kCreateTextureInfoOffset)(
                            "ObjectData/TextureSouvenir", "TextureSouvenir", textureName);
                    }
                } else if (textures && textureIndex >= 0 && offer->kind == OfferKind::Moon) {
                    if (const char* textureName = getMoonTextureName(offer->rewardId)) {
                        using CreateTextureInfoFn = nn::ui2d::TextureInfo* (*)(const char*,
                            const char*, const char*);
                        textures[textureIndex] = mainOffsetFunction<CreateTextureInfoFn>(
                            kCreateTextureInfoOffset)(
                            "ObjectData/TextureShine", "TextureShine", textureName);
                    }
                }

                // loadTexture above has already consumed the original Gift-list
                // name to load the correct Ability List icon. Replace it only
                // afterwards so every remaining UI lookup resolves our dynamic
                // title and description rather than a copied souvenir record.
                makeOfferMessageKey(layoutItem->item->info.name,
                    sizeof(layoutItem->item->info.name), layoutItem->item->info.index);
            }
        }

        // ShopLayoutInfo creates the Ability List/Gift texture itself. Replace only
        // generated capture and moon rows with their source collection-list assets.
        static HkTrampoline<void*, void*, const sead::PtrArrayImpl*, s32, const char*>
            loadShopLayoutTexturesHook = hk::hook::trampoline(
                [](void* layoutInfo, const sead::PtrArrayImpl* list, s32 itemCount,
                    const char* worldName) -> void* {
                    void* result = loadShopLayoutTexturesHook.orig(
                        layoutInfo, list, itemCount, worldName);
                    applyGeneratedOfferTextures(layoutInfo, list, itemCount);
                    return result;
                });

        static HkTrampoline<s32, void*, const al::LayoutActor*>
            createShopItemListHook = hk::hook::trampoline(
                [](void* shopInfo, const al::LayoutActor* layout) -> s32 {
                    al::LiveActor* actor = shopInfo
                        ? *reinterpret_cast<al::LiveActor**>(reinterpret_cast<u8*>(shopInfo) + 0x28)
                        : nullptr;
                    // Every shop screen is built here, so this is where the
                    // previous screen's selection stops being current.
                    // buildRuntimeItems re-adopts the inventory below when the
                    // generated path is taken; a vanilla shop leaves it null.
                    sCurrentShopInventory = nullptr;
                    sSelectedOfferIndex = -1;
                    // Before the purple branch: a gold-coin shop never reaches
                    // the generated-inventory path below, and both kinds read
                    // the same catalogue.
                    if (actor) {
                        ShopPriceRando::applyToShopCatalogue(
                            GameDataHolderAccessor(actor), getShopKingdomId(actor));
                    }
                    if (!isPurpleShopFeatureEnabled() || !isCollectCoinShopInfo(shopInfo))
                        return createShopItemListHook.orig(shopInfo, layout);

                    ShopInventory* inventory = buildInventoryForShop(actor);
                    // sead::PtrArrayImpl::allocBuffer rejects a zero-size buffer
                    // (vanilla createShopItemList only allocates for 1+ items), so
                    // an offer-less shop falls through to the vanilla empty list.
                    if (!inventory || inventory->offerCount <= 0)
                        return createShopItemListHook.orig(shopInfo, layout);

                    recordGeneratedShopActor(actor);

                    sead::PtrArrayImpl* list = reinterpret_cast<sead::PtrArrayImpl*>(
                        reinterpret_cast<u8*>(shopInfo) + 0x40);
                    list->allocBuffer(inventory->offerCount, nullptr, alignof(void*));
                    for (s32 i = 0; i < inventory->offerCount; i++) {
                        ShopLayoutItemInfo* item = new ShopLayoutItemInfo {
                            &inventory->items[i], i, i
                        };
                        list->pushBack(item);
                    }
                    *reinterpret_cast<s32*>(reinterpret_cast<u8*>(shopInfo) + 0x50) = inventory->offerCount;
                    return inventory->offerCount > 0;
                });

        // ShopStateInShop::exeSelect is the only caller, and it asks once for
        // the highlighted row the moment the player decides on it - which is
        // the last thing to happen before exeCheckBuyExplain puts the buy
        // question on screen. That makes it the one place the clerk's quote
        // can learn which offer it is being asked about.
        static HkTrampoline<bool, const void*, s32, const void*>
            isEnableBuyItemHook = hk::hook::trampoline(
                [](const void* shopInfo, s32 itemIndex, const void* itemState) -> bool {
                    if (sCurrentShopInventory && itemIndex >= 0
                        && itemIndex < sCurrentShopInventory->offerCount)
                        sSelectedOfferIndex = itemIndex;
                    return isEnableBuyItemHook.orig(shopInfo, itemIndex, itemState);
                });

        static HkTrampoline<bool, const al::IUseSceneObjHolder*, s32>
            isBuyItemInShopItemListHook = hk::hook::trampoline(
                [](const al::IUseSceneObjHolder* holder, s32 itemIndex) -> bool {
                    if (ShopOffer* offer = findOfferByItemIndex(itemIndex))
                        return isOfferSoldOut(holder, *offer);
                    return isBuyItemInShopItemListHook.orig(holder, itemIndex);
                });

        static HkTrampoline<void, const al::IUseSceneObjHolder*, s32>
            buyItemInShopItemListHook = hk::hook::trampoline(
                [](const al::IUseSceneObjHolder* holder, s32 itemIndex) -> void {
                    if (sBlockNextMultiplayerPurchase) {
                        sBlockNextMultiplayerPurchase = false;
                        Randomizer::logPush(
                            "Purple purchase cancelled: shared balance changed");
                        return;
                    }
                    if (ShopOffer* offer = findOfferByItemIndex(itemIndex)) {
                        tryPurchaseOffer(holder, *offer);
                        return;
                    }
                    buyItemInShopItemListHook.orig(holder, itemIndex);
                });

        static HkTrampoline<const char16_t*, const al::IUseMessageSystem*, const ShopItem::ItemInfo&>
            getDisplayNameHook = hk::hook::trampoline(
                [](const al::IUseMessageSystem* messageSystem,
                    const ShopItem::ItemInfo& itemInfo) -> const char16_t* {
                    if (ShopOffer* offer = findOfferByItemInfo(itemInfo))
                        return offer->displayName;
                    return getDisplayNameHook.orig(messageSystem, itemInfo);
                });

        ShopOffer* findOfferByMessageKey(const char* key) {
            if (!key)
                return nullptr;
            const char* encodedItemIndex = std::strstr(key, kOfferMessageKeyPrefix);
            if (!encodedItemIndex)
                return nullptr;
            encodedItemIndex += sizeof(kOfferMessageKeyPrefix) - 1;

            u32 itemIndex = 0;
            for (s32 i = 0; i < kOfferMessageKeyHexDigits; i++) {
                const char c = encodedItemIndex[i];
                u32 nibble = 0;
                if (c >= '0' && c <= '9')
                    nibble = static_cast<u32>(c - '0');
                else if (c >= 'A' && c <= 'F')
                    nibble = static_cast<u32>(c - 'A' + 10);
                else if (c >= 'a' && c <= 'f')
                    nibble = static_cast<u32>(c - 'a' + 10);
                else
                    return nullptr;
                itemIndex = (itemIndex << 4) | nibble;
            }
            return findOfferByItemIndex(static_cast<s32>(itemIndex));
        }

        const char16_t* makeOfferDescription(const ShopOffer& offer) {
            static char16_t sDescription[256] = {};
            switch (offer.kind) {
            case OfferKind::Ability: {
                const AbilityRando::AbilityEntry* ability = AbilityRando::getAbility(offer.rewardId);
                if (ability && ability->prerequisite < 0) {
                    copyAscii(sDescription, sizeof(sDescription) / sizeof(sDescription[0]),
                        "Unlocks Mario's ");
                    appendAscii(sDescription, sizeof(sDescription) / sizeof(sDescription[0]),
                        ability->displayName ? ability->displayName : ability->key);
                    appendAscii(sDescription, sizeof(sDescription) / sizeof(sDescription[0]),
                        " ability.");
                } else {
                    copyAscii(sDescription, sizeof(sDescription) / sizeof(sDescription[0]),
                        "Unlocks the next technique in this progressive ability chain.");
                }
                break;
            }
            case OfferKind::Capture: {
                const CaptureRando::CaptureEntry* capture = CaptureRando::getCapture(offer.rewardId);
                copyAscii(sDescription, sizeof(sDescription) / sizeof(sDescription[0]),
                    "Unlocks Mario's ");
                appendAscii(sDescription, sizeof(sDescription) / sizeof(sDescription[0]),
                    capture && capture->displayName ? capture->displayName
                                                    : (capture && capture->hackName ? capture->hackName : "capture"));
                appendAscii(sDescription, sizeof(sDescription) / sizeof(sDescription[0]), " capture.");
                break;
            }
            case OfferKind::Moon:
                copyAscii(sDescription, sizeof(sDescription) / sizeof(sDescription[0]),
                    "Grants a random Power Moon from ");
                appendAscii(sDescription, sizeof(sDescription) / sizeof(sDescription[0]),
                    WorldMeta::getDisplayNameForWorldId(offer.rewardId));
                appendAscii(sDescription, sizeof(sDescription) / sizeof(sDescription[0]),
                    " Kingdom's moon list.");
                break;
            }
            return sDescription;
        }

        const char16_t* makeFirstCoinHintMessage() {
            static char16_t sMessage[192];
            copyAscii(sMessage, sizeof(sMessage) / sizeof(sMessage[0]),
                "Use these to buy goodies\nin the purple Crazy Cap shop");
            return sMessage;
        }

        // Shown in place of the vanilla "collect 100 of these!" Cappy tutorial
        // on the first purple coin of a kingdom (once per kingdom, persisted in
        // the hack dictionary), directing the player to the Purple Crazy Cap shop.
        bool tryShowFirstCoinHint(const al::IUseSceneObjHolder* holder) {
            if (!holder || !isPurpleShopFeatureEnabled())
                return false;

            GameDataHolderAccessor accessor(holder);
            if (!accessor.mData)
                return false;
            GameDataFile* file = accessor->getGameDataFile();
            if (!file)
                return false;

            const s32 worldId = findOriginalWorldIdForStage(
                GameDataFunction::getCurrentStageName(accessor));
            if (worldId < 0 || getCoinCollectGotNum(file, worldId) > 1)
                return false;
            if (!makeFirstCoinHintMessage())
                return false;

            char flag[64];
            nn::util::SNPrintf(flag, sizeof(flag), "%s%02d", kFirstCoinHintFlagPrefix, worldId);
            if (file->customIsExistInHackDictionary(flag))
                return false;
            if (!rs::tryShowCapMessage(holder, kFirstCoinCapMessageKey, 90, 0))
                return false;

            file->customAddHackDictionary(flag);
            GameDataFunction::setRequireSave(GameDataHolderWriter(accessor.mData));
            return true;
        }

        const char* getNativeCoinArchiveName(const al::LiveActor* actor, CoinArchiveKind kind,
            const char* fallback) {
            if (!actor)
                return fallback;

            GameDataHolderAccessor accessor(actor);
            if (!accessor.mData)
                return fallback;

            const char* stageName = GameDataFunction::getCurrentStageName(accessor);
            const s32 worldId = findOriginalWorldIdForStage(stageName);
            if (worldId < 0)
                return fallback;
            if (worldId == GameDataFunction::getCurrentWorldId(accessor))
                return fallback;

            const char* archiveName = getCoinCollectArchiveName(accessor.mData, worldId, kind);
            return archiveName ? archiveName : fallback;
        }

        const char16_t* getNativeCoinPictureFont(const al::LayoutActor* layout,
            const char16_t* fallback) {
            if (!layout)
                return fallback;

            GameDataHolderAccessor accessor(layout);
            if (!accessor.mData)
                return fallback;

            const s32 worldId = findOriginalWorldIdForStage(
                GameDataFunction::getCurrentStageName(accessor));
            if (worldId < 0)
                return fallback;
            if (worldId == GameDataFunction::getCurrentWorldId(accessor))
                return fallback;

            using Fn = const char16_t* (*)(const al::LayoutActor*, s32);
            const char16_t* pictureFont = mainOffsetFunction<Fn>(kWorldCoinCollectPictureFontOffset)(layout, worldId);
            return pictureFont ? pictureFont : fallback;
        }

        s32 getRemappedWorldId(GameDataHolderAccessor accessor) {
            if (!accessor.mData)
                return -1;
            const s32 worldId = findOriginalWorldIdForStage(GameDataFunction::getCurrentStageName(accessor));
            if (worldId < 0)
                return -1;

            // Same-kingdom pickups must stay on the game's native provider
            // path. In particular, MoonWorldHome2DZone is an embedded zone of
            // MoonWorldHomeStage: CoinCollectList registers its coins under the
            // main stage with composite zone/object placement IDs. Sending
            // those IDs through the cross-kingdom lookup loses the record and
            // leaves every pickup displayed as 1/50.
            if (worldId == GameDataFunction::getCurrentWorldId(accessor))
                return -1;
            return worldId;
        }

        static HkTrampoline<const char*, const al::LiveActor*>
            getStageCoinCollectArchiveNameHook = hk::hook::trampoline([](const al::LiveActor* actor) -> const char* {
                const char* fallback = getStageCoinCollectArchiveNameHook.orig(actor);
                return getNativeCoinArchiveName(actor, CoinArchiveKind::Normal, fallback);
            });

        static HkTrampoline<const char*, const al::LiveActor*>
            getStageCoinCollectEmptyArchiveNameHook = hk::hook::trampoline([](const al::LiveActor* actor) -> const char* {
                const char* fallback = getStageCoinCollectEmptyArchiveNameHook.orig(actor);
                return getNativeCoinArchiveName(actor, CoinArchiveKind::Empty, fallback);
            });

        // ── Kingdoms that ship no 2D purple coin ─────────────────────────────
        //
        // Both 2D coin archives are per-kingdom the same way the 3D ones are:
        // GameDataHolder maps the world through WorldItemTypeList's CoinCollect
        // letter and yields "CoinCollect2D_<letter>". Only C, F, G, H, K and N
        // ship one (the empty husk adds E), because vanilla only ever authors a
        // 2D purple coin in the kingdoms that have one.
        //
        // Two paths here can ask for one of the others. Coin Shuffle permutes
        // coin identities inside a single map, so a 3D purple coin can land on
        // a CoinDot placement wherever a map holds both kinds - Cascade's
        // WaterfallWorldHomeStage and Lift2DExStage do, and Cascade's letter is
        // B. getNativeCoinArchiveName can also hand back a foreign kingdom's 2D
        // archive for a cross-kingdom purple-coin reward. Either way
        // al::findOrCreateActorResource builds an al::Resource over a null
        // archive and al::Resource::tryGetByml walks it, killing
        // StageScene::init while the map is still being placed.
        //
        // Substituting a shipped design keeps the coin on the shelf. Refusing
        // the placement instead would quietly take purple coins out of that
        // kingdom's shop economy.
        constexpr const char* kFallbackCoinCollect2DArchive = "CoinCollect2D_C";
        constexpr const char* kFallbackCoinCollectEmpty2DArchive = "CoinCollectEmpty2D_C";
        constexpr uintptr_t kIsExistArchiveOffset = 0x00897ee0;

        bool coinArchiveExists(const char* archiveName) {
            using Fn = bool (*)(const sead::SafeString&);
            al::StringTmp<128> resourcePath("ObjectData/%s", archiveName);
            return mainOffsetFunction<Fn>(kIsExistArchiveOffset)(resourcePath);
        }

        // One romfs probe per distinct name covers a whole map's worth of
        // coins; there is one name per world per variant.
        struct CoinArchiveProbe {
            char name[32];
            bool exists;
        };
        CoinArchiveProbe sCoinArchiveProbes[24] = {};
        s32 sCoinArchiveProbeCount = 0;

        bool coinArchiveExistsCached(const char* archiveName) {
            if (!archiveName || !archiveName[0])
                return false;
            for (s32 i = 0; i < sCoinArchiveProbeCount; i++)
                if (std::strcmp(sCoinArchiveProbes[i].name, archiveName) == 0)
                    return sCoinArchiveProbes[i].exists;
            const bool exists = coinArchiveExists(archiveName);
            if (!exists)
                Randomizer::logPush(
                    "purple coins: %s is not shipped, substituting", archiveName);
            if (sCoinArchiveProbeCount
                    < static_cast<s32>(
                        sizeof(sCoinArchiveProbes) / sizeof(sCoinArchiveProbes[0]))
                && std::strlen(archiveName) < sizeof(sCoinArchiveProbes[0].name)) {
                CoinArchiveProbe& probe = sCoinArchiveProbes[sCoinArchiveProbeCount++];
                std::strncpy(probe.name, archiveName, sizeof(probe.name) - 1);
                probe.name[sizeof(probe.name) - 1] = '\0';
                probe.exists = exists;
            }
            return exists;
        }

        const char* shippedCoinArchiveName(const char* archiveName, const char* fallback) {
            return coinArchiveExistsCached(archiveName) ? archiveName : fallback;
        }

        static HkTrampoline<const char*, const al::LiveActor*>
            getStageCoinCollect2DArchiveNameHook = hk::hook::trampoline([](const al::LiveActor* actor) -> const char* {
                const char* fallback = getStageCoinCollect2DArchiveNameHook.orig(actor);
                return shippedCoinArchiveName(
                    getNativeCoinArchiveName(actor, CoinArchiveKind::Normal2D, fallback),
                    kFallbackCoinCollect2DArchive);
            });

        static HkTrampoline<const char*, const al::LiveActor*>
            getStageCoinCollect2DEmptyArchiveNameHook = hk::hook::trampoline([](const al::LiveActor* actor) -> const char* {
                const char* fallback = getStageCoinCollect2DEmptyArchiveNameHook.orig(actor);
                return shippedCoinArchiveName(
                    getNativeCoinArchiveName(actor, CoinArchiveKind::Empty2D, fallback),
                    kFallbackCoinCollectEmpty2DArchive);
            });

        static HkTrampoline<const char16_t*, const al::LayoutActor*>
            getWorldCoinCollectPictureFontHook = hk::hook::trampoline(
                [](const al::LayoutActor* layout) -> const char16_t* {
                    const char16_t* fallback = getWorldCoinCollectPictureFontHook.orig(layout);
                    return getNativeCoinPictureFont(layout, fallback);
                });

        static HkTrampoline<s32, GameDataHolderAccessor>
            getCoinCollectNumHook = hk::hook::trampoline(
                [](GameDataHolderAccessor accessor) -> s32 {
                    GameDataFile* file = accessor.mData ? accessor->getGameDataFile() : nullptr;
                    const s32 worldId = getRemappedWorldId(accessor);
                    if (file && worldId >= 0)
                        return file->customGetCoinCollectNum(worldId);
                    return getCoinCollectNumHook.orig(accessor);
                });

        static HkTrampoline<s32, GameDataHolderAccessor>
            getCoinCollectGotNumHook = hk::hook::trampoline(
                [](GameDataHolderAccessor accessor) -> s32 {
                    GameDataFile* file = accessor.mData ? accessor->getGameDataFile() : nullptr;
                    const s32 worldId = getRemappedWorldId(accessor);
                    if (file && worldId >= 0)
                        return file->customGetCoinCollectGotNum(worldId);
                    return getCoinCollectGotNumHook.orig(accessor);
                });

        // Keep the Figure Walking achievement counter aligned with randomized
        // destination flags. The Koopa's actual prize selection is handled by
        // linked-shine event-flow checks in MoonHooks, not by this counter.
        static HkTrampoline<s32, const GameDataFile*, const char*, GameDataFile::CountType>
            calcGetShineNumByObjectNameHook = hk::hook::trampoline(
                [](const GameDataFile* file, const char* objectNameOrOptionalId,
                    GameDataFile::CountType countType) -> s32 {
                    if (file && countType == GameDataFile::CountType::Value_1) {
                        const s32 figureWalkingCount = MoonRando::getFigureWalkingCollectedCount(
                            file, objectNameOrOptionalId);
                        if (figureWalkingCount >= 0)
                            return figureWalkingCount;
                    }
                    return calcGetShineNumByObjectNameHook.orig(file, objectNameOrOptionalId, countType);
                });

        // Picture Match asks isGotShine directly for each linked ShineActor,
        // rather than using the object-name counter above. Resolve those four
        // physical source IDs to their randomized destination records here.
        static HkTrampoline<bool, GameDataHolderAccessor, const al::PlacementId*>
            isGotShineByPlacementIdHook = hk::hook::trampoline(
                [](GameDataHolderAccessor accessor, const al::PlacementId* placeID) -> bool {
                    GameDataFile* file = accessor.mData ? accessor->getGameDataFile() : nullptr;
                    if (file && placeID) {
                        al::StringTmp<128> objectId;
                        placeID->makeString(&objectId);
                        const s32 pictureMatchState = MoonRando::getPictureMatchCollectedState(
                            file, objectId.cstr(), GameDataFunction::getCurrentStageName(accessor));
                        if (pictureMatchState >= 0)
                            return pictureMatchState != 0;
                    }
                    return isGotShineByPlacementIdHook.orig(accessor, placeID);
                });

        static HkTrampoline<bool, const GameDataFile*, const al::PlacementId*>
            isGotCoinCollectHook = hk::hook::trampoline(
                [](const GameDataFile* file, const al::PlacementId* placeID) -> bool {
                    placeID = CoinRando::translatePurplePlacement(placeID);
                    if (file) {
                        GameDataHolder* holder = const_cast<GameDataFile*>(file)->getGameDataHolder();
                        const s32 worldId = getRemappedWorldId(GameDataHolderAccessor(holder));
                        if (worldId >= 0) {
                            const char* stageName = GameDataFunction::getCurrentStageName(
                                GameDataHolderAccessor(holder));
                            if (file->customHasCoinCollectInfo(placeID, worldId, stageName))
                                return file->customIsGotCoinCollect(placeID, stageName, worldId);
                            if (worldId == file->getCurrentWorldId())
                                return isGotCoinCollectHook.orig(file, placeID);
                            return false;
                        }
                    }
                    return isGotCoinCollectHook.orig(file, placeID);
                });

        // GameDataFunction::getCoinCollectNumMax(GameDataHolderAccessor) - the "/100"-style
        // max shown by the HUD counter and CoinCollectWatcher's popup - reads the native
        // current world's max, not the remapped one. Without this hook a coin correctly
        // credited to a remapped kingdom (via addCoinCollectHook) still shows the wrong
        // kingdom's total (e.g. Cascade's 50 displayed as some other kingdom's 100).
        static HkTrampoline<s32, GameDataHolderAccessor>
            getCoinCollectNumMaxHook = hk::hook::trampoline(
                [](GameDataHolderAccessor accessor) -> s32 {
                    GameDataFile* file = accessor.mData ? accessor->getGameDataFile() : nullptr;
                    const s32 worldId = getRemappedWorldId(accessor);
                    if (file && worldId >= 0) {
                        const s32 customMax = file->customGetCoinCollectMax(worldId);
                        if (customMax > 0)
                            return customMax;
                        const s32 nativeMax = getCoinCollectNumMax(accessor.mData, worldId);
                        if (nativeMax > 0)
                            return nativeMax;
                    }
                    return getCoinCollectNumMaxHook.orig(accessor);
                });

        static HkTrampoline<void, GameDataHolderWriter, s32>
            useCoinCollectHook = hk::hook::trampoline(
                [](GameDataHolderWriter writer, s32 count) -> void {
                    GameDataFile* file = writer.mData ? writer->getGameDataFile() : nullptr;
                    GameDataHolderAccessor accessor(writer.mData);
                    // getRemappedWorldId() deliberately returns -1 for the
                    // common same-kingdom case (buying with that kingdom's
                    // own currency) - it's a "stay on the native path"
                    // signal for the collect side's actor lookup, not an
                    // error. mpOnAddCoinCollect (syncHooks.cpp) already
                    // falls back to the real current world id when that
                    // happens; mirror that here so a same-kingdom spend -
                    // by far the common case - doesn't get silently routed
                    // to the vanilla local-only path with count never sent.
                    s32 worldId = getRemappedWorldId(accessor);
                    if (worldId < 0 && accessor.mData)
                        worldId = GameDataFunction::getCurrentWorldId(accessor);
                    MpDebugLog::write("useCoinCollectHook: worldId=%d count=%d fileValid=%d", worldId, count, file != nullptr);
                    u64 reservationId = 0;
                    const bool needsReservation = Client::isSocketActive() && Client::isRoomReady();
                    if (needsReservation
                        && !Client::reserveCoinSpend(worldId, count, &reservationId)) {
                        sBlockNextMultiplayerPurchase = true;
                        Randomizer::logPush(
                            "Not enough shared purple coins; purchase cancelled");
                        return;
                    }
                    if (file && worldId >= 0 && file->customUseCoinCollect(worldId, count)) {
                        GameDataFunction::setRequireSave(writer);
                        if (reservationId)
                            Client::commitCoinSpend(reservationId);
                        return;
                    }
                    useCoinCollectHook.orig(writer, count);
                    if (reservationId)
                        Client::commitCoinSpend(reservationId);
                });

        // The native addCoinCollect always increments the coin count for the game's own
        // notion of "current world" (GameDataFile::mCurrentWorldId), which is not remapped
        // for sub-areas whose logical kingdom differs from that (e.g. via entrance rando).
        // Without this hook the coin visibly shows the right kingdom's type/count (via the
        // hooks above) but picking it up silently increments the wrong kingdom's total.
        // Hooked by symbol rather than a hardcoded main offset, since
        // GameDataFile::addCoinCollect is a real exported symbol in syms/main.sym.
        // This is the single trampoline on that function; Multiplayer's coin sync
        // rides along through mpOnAddCoinCollect below.
        static HkTrampoline<void, GameDataFile*, const al::PlacementId*>
            addCoinCollectHook = hk::hook::trampoline(
                [](GameDataFile* file, const al::PlacementId* placeID) -> void {
                    placeID = CoinRando::translatePurplePlacement(placeID);
                    // First statement on purpose: several paths below return
                    // without reaching orig, and Multiplayer has to see every
                    // collect. It used to own a second trampoline here (one
                    // trampoline per function -- see Multiplayer/main.hpp).
                    mpOnAddCoinCollect(file, placeID);

                    GameDataHolder* holder = file ? file->getGameDataHolder() : nullptr;
                    const s32 worldId = getRemappedWorldId(GameDataHolderAccessor(holder));
                    if (file && worldId >= 0) {
                        const char* stageName = GameDataFunction::getCurrentStageName(GameDataHolderAccessor(holder));
                        const bool hasCustomRecord = file->customHasCoinCollectInfo(placeID, worldId, stageName);
                        if (file->customAddCoinCollect(placeID, worldId, stageName) && holder)
                            GameDataFunction::setRequireSave(GameDataHolderWriter(holder));
                        if (hasCustomRecord)
                            return;

                        // When logical and native ownership agree, preserve the
                        // game's own stage-provider lookup as a final fallback.
                        // This prevents a newly encountered embedded-map alias
                        // from turning a collected coin into a no-op.
                        if (worldId == file->getCurrentWorldId()) {
                            addCoinCollectHook.orig(file, placeID);
                            return;
                        }
                        return;
                    }
                    addCoinCollectHook.orig(file, placeID);
                });

        static HkTrampoline<bool, const char*, bool, const ShopItem::ShopItemInfo*, const al::LayoutActor*, bool>
            isShopItemAvailableHook = hk::hook::trampoline(
                [](const char* storeName, bool isCoinShop, const ShopItem::ShopItemInfo* itemInfo,
                    const al::LayoutActor* layout, bool hasShine) -> bool {
                    // With every Purple Shop offer type disabled the shops revert to
                    // their vanilla souvenir inventory instead of hiding it.
                    if (isPurpleShopFeatureEnabled() && isSouvenirShopItem(itemInfo))
                        return false;
                    return isShopItemAvailableHook.orig(storeName, isCoinShop, itemInfo, layout, hasShine);
                });

        static HkTrampoline<bool, const al::IUseSceneObjHolder*>
            tryShowCapMsgCollectCoinGetFirstHook = hk::hook::trampoline(
                [](const al::IUseSceneObjHolder* holder) -> bool {
                    if (tryShowFirstCoinHint(holder))
                        return true;
                    return tryShowCapMsgCollectCoinGetFirstHook.orig(holder);
                });

        // MessageTalk::start runs only after the event flow has selected an actual
        // dialogue node. The older TalkNpc Reaction hook also ran for accepted
        // interactions that had no message, such as pressing X near an NPC that
        // cannot currently be spoken to.
        static HkTrampoline<void, al::EventFlowNode*>
            messageTalkStartHook = hk::hook::trampoline(
                [](al::EventFlowNode* node) -> void {
                    if (node)
                        recordNpcConversationImpl(node->getActor());
                    messageTalkStartHook.orig(node);
                });

        static HkTrampoline<void, al::SimpleLayoutAppearWaitEnd*, const char*, al::LiveActor*, const char*, bool*, al::TalkMessageVoicePlayer*>
            startNpcMessageHook = hk::hook::trampoline(
                [](al::SimpleLayoutAppearWaitEnd* layout, const char* messageName,
                    al::LiveActor* actor, const char* actorName, bool* isSkipTextAnim,
                    al::TalkMessageVoicePlayer* voicePlayer) -> void {
                    // Shop-moon goals poll the game's purchase counter. Keep shop
                    // interactions in the general NPC-conversation goal only.
                    recordNpcConversationImpl(actor);
                    // Both purchase lines promise Odyssey delivery in vanilla:
                    // the confirm question ("Select_BuyQuestion_<shop>...", "I'll
                    // have this sent to the Odyssey") and the post-purchase
                    // thanks ("ShopText_Buy_<shop>..."). Generated offers take
                    // effect on the spot, so reroute both labels.
                    if (messageName && isPurpleShopFeatureEnabled()
                        && isGeneratedShopActor(actor)) {
                        if (std::strncmp(messageName, "ShopText_Buy_",
                                sizeof("ShopText_Buy_") - 1)
                            == 0)
                            messageName = kPurpleShopBuyThanksLabel;
                        else if (std::strncmp(messageName, "Select_BuyQuestion_",
                                     sizeof("Select_BuyQuestion_") - 1)
                            == 0)
                            messageName = kPurpleShopBuyQuestionLabel;
                        // exeSendHome's post-purchase line for Gift/Sticker items
                        // ("I'll have this sent to the Odyssey!").
                        else if (std::strncmp(messageName, "ShopText_SendHome",
                                     sizeof("ShopText_SendHome") - 1)
                            == 0)
                            messageName = kPurpleShopBuyThanksLabel;
                    }
                    startNpcMessageHook.orig(layout, messageName, actor, actorName, isSkipTextAnim, voicePlayer);
                });

    } // namespace

    void clearNpcActorPlacements() {
        sNpcActorPlacementCount = 0;
    }

    void registerNpcActorPlacement(
        const al::LiveActor* actor, const al::PlacementInfo* placementInfo) {
        if (!actor || !placementInfo || sNpcActorPlacementCount >= kMaxNpcActorPlacements)
            return;
        sNpcActorPlacements[sNpcActorPlacementCount++] = {
            actor, makeNpcPlacementKey(actor, *placementInfo)
        };
    }

    void recordNpcConversation(const al::LiveActor* actor) {
        recordNpcConversationImpl(actor);
    }

    const char16_t* tryGetShopMessageOverride(const char* fileName, const char* key) {
        (void)fileName;
        // Purchase clerk lines: startNpcMessageHook reroutes the vanilla
        // Odyssey-delivery labels to these markers for purple-coin clerks.
        if (key && std::strcmp(key, kPurpleShopBuyThanksLabel) == 0)
            return u"Thank you! Your purchase takes effect right away.";
        if (key && std::strcmp(key, kPurpleShopBuyQuestionLabel) == 0) {
            char text[128];
            nn::util::SNPrintf(text, sizeof(text),
                "That'll be %d purple coins, and it takes\neffect right away. OK?",
                currentShopPurplePrice());
            static char16_t sBuyQuestion[128];
            copyAscii(sBuyQuestion, sizeof(sBuyQuestion) / sizeof(sBuyQuestion[0]), text);
            return sBuyQuestion;
        }
        // Cappy's first-purple-coin explainer (tryShowFirstCoinHint).
        if (key && std::strcmp(key, kFirstCoinCapMessageKey) == 0)
            return makeFirstCoinHintMessage();

        ShopOffer* offer = findOfferByMessageKey(key);
        if (!offer)
            return nullptr;

        // Item-name lookups use the dynamic ItemInfo name directly. The
        // selected-item pane instead prefixes storeName with ShopText_Wear_.
        // Both explanation paths need a description specific to this offer.
        if (std::strstr(key, "_Explain") || std::strstr(key, "ShopText_Wear_"))
            return makeOfferDescription(*offer);
        return offer->displayName;
    }

    s32 findOriginalWorldIdForStage(const char* stageName) {
        if (!stageName || !stageName[0])
            return -1;

        static char sCachedStageName[128] = {};
        static s32 sCachedWorldId = -1;
        if (std::strcmp(sCachedStageName, stageName) == 0)
            return sCachedWorldId;

        sCachedWorldId = -1;
        for (s32 i = 0; i < Randomizer::LogicData::sStageCount; i++) {
            const Randomizer::LogicData::Stage& stage = Randomizer::LogicData::sStages[i];
            if (stage.stage && std::strcmp(stage.stage, stageName) == 0) {
                sCachedWorldId = stage.kingdom;
                break;
            }
        }

        if (sCachedWorldId < 0) {
            if (const WorldMeta::Entry* entry = WorldMeta::findByMainStage(stageName))
                sCachedWorldId = entry->worldId;
        }

        // Embedded maps (such as MoonWorldHome2DZone) are not independent
        // LogicData stages, but their moon entries still identify the kingdom
        // that owns their regional coins. This also covers other stage-only
        // subareas without adding a one-off mapping for each one.
        if (sCachedWorldId < 0) {
            for (s32 i = 0; i < ShineDB::sShineTableSize; i++) {
                const ShineDB::ShineEntry& shine = ShineDB::sShineTable[i];
                if (std::strcmp(shine.stage, stageName) == 0) {
                    sCachedWorldId = shine.worldId;
                    break;
                }
            }
        }

        nn::util::SNPrintf(sCachedStageName, sizeof(sCachedStageName), "%s", stageName);
        return sCachedWorldId;
    }

    void rebuildShopInventories() {
        for (s32 i = 0; i < kMaxShopInventories; i++)
            sShopInventories[i] = {};
    }

    u64 getPurchasedAbilityMask(const GameDataFile* file) {
        return readPurchaseMask(file, kPurchasedAbilityPrefix);
    }

    u64 getPurchasedCaptureMask(const GameDataFile* file) {
        return readPurchaseMask(file, kPurchasedCapturePrefix);
    }

    s32 getPurchasedMoonIndices(const GameDataFile* file, s32* out, s32 maxOut) {
        if (!file || !out || maxOut <= 0)
            return 0;
        const char* record = file->customFindHackDictionaryPrefix(kPurchasedMoonPrefix);
        if (!record)
            return 0;

        s32 count = 0;
        const char* cursor = record + std::strlen(kPurchasedMoonPrefix);
        while (*cursor && count < maxOut) {
            char* next = nullptr;
            const long value = std::strtol(cursor, &next, 10);
            if (next == cursor)
                break;
            if (value >= 0 && value < ShineDB::sShineTableSize)
                out[count++] = static_cast<s32>(value);
            cursor = *next == ',' ? next + 1 : next;
        }
        return count;
    }

    void clearPurchaseLedger(GameDataFile* file) {
        if (!file)
            return;
        file->customRemoveHackDictionaryPrefix(kPurchasedAbilityPrefix);
        file->customRemoveHackDictionaryPrefix(kPurchasedCapturePrefix);
        file->customRemoveHackDictionaryPrefix(kPurchasedMoonPrefix);
    }

    void installHooks() {
        getStageCoinCollectArchiveNameHook.installAtMainOffset(0x0055654c);
        getStageCoinCollectEmptyArchiveNameHook.installAtMainOffset(0x005565a8);
        getStageCoinCollect2DArchiveNameHook.installAtMainOffset(0x00556604);
        getStageCoinCollect2DEmptyArchiveNameHook.installAtMainOffset(0x00556660);
        getWorldCoinCollectPictureFontHook.installAtMainOffset(0x00578798);
        getCoinCollectNumHook.installAtMainOffset(0x00529b50);
        getCoinCollectGotNumHook.installAtMainOffset(0x00529b58);
        isGotCoinCollectHook.installAtSym<"_ZNK12GameDataFile16isGotCoinCollectEPKN2al11PlacementIdE">();
        calcGetShineNumByObjectNameHook.installAtMainOffset(0x00525d50);
        isGotShineByPlacementIdHook.installAtMainOffset(0x0052886c);
        getCoinCollectNumMaxHook.installAtMainOffset(0x00529b68);
        useCoinCollectHook.installAtMainOffset(0x00529afc);
        addCoinCollectHook.installAtSym<"_ZN12GameDataFile14addCoinCollectEPKN2al11PlacementIdE">();
        isShopItemAvailableHook.installAtMainOffset(0x002097b0);
        loadShopLayoutTexturesHook.installAtMainOffset(0x00207e00);
        createShopItemListHook.installAtMainOffset(0x00209038);
        isEnableBuyItemHook.installAtMainOffset(kIsEnableBuyItemOffset);
        buyItemInShopItemListHook.installAtMainOffset(0x0054c2bc);
        isBuyItemInShopItemListHook.installAtMainOffset(0x0054c33c);
        getDisplayNameHook.installAtMainOffset(0x0054cd14);
        // al::isExistLabelInSystemMessage is already trampolined by MoonHooks'
        // isExistLabelInSystemMessageHook (one trampoline per function!); the
        // Purple Shop label check is merged there via tryGetShopMessageOverride.
        messageTalkStartHook.installAtMainOffset(0x001b7fc4);
        startNpcMessageHook.installAtMainOffset(0x0020a28c);
        tryShowCapMsgCollectCoinGetFirstHook.installAtMainOffset(0x0023b534);
    }

} // namespace PurpleCoinRewards
