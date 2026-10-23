#pragma once

#include <basis/seadTypes.h>

class GameDataFile;

namespace KingdomProgress {

    static constexpr s32 cKingdomCount = 14;
    static constexpr s32 cBranchCount = 2;

    struct KingdomRequirement {
        u16 moonCount;
        bool requiresBoss;
    };

    struct KingdomProgressEntry {
        s32 kingdomId;
        bool completeStatus;
        KingdomRequirement requirement;
    };

    struct KingdomProgressList {
        KingdomProgressEntry entries[cKingdomCount];
        u8 resolvedBranches;
    };

    // The whole of this save's progression, in the smallest form that can be
    // merged with another player's without either side needing to know how the
    // other got there.
    //
    // Multiplayer syncs THIS, not the individual mutations that produce it.
    // Modelling the mutations was tried first and is partial by construction:
    // completeStatus advances only inside tryCompleteFrontier, which has a
    // single caller gated on the player personally depositing moons at the
    // globe, so a team-mate holding identical moons could sit on a stale
    // frontier forever. Enumerating "the ways progression can change" means
    // chasing every future one; describing the state means never missing one.
    //
    // Both fields are monotone, which is what makes a merge order-independent
    // and repeatable:
    //
    //   completedCount - a PREFIX LENGTH, not a per-kingdom mask. validate()
    //                    requires completion to be a contiguous prefix of the
    //                    ordered list (a completed kingdom after an incomplete
    //                    one is a rejected record), so the only mergeable
    //                    representation is "how far along", merged with max().
    //   branchChoice   - which sibling was taken at each branch point, or -1
    //                    while unresolved. First resolution wins team-wide;
    //                    once set it never changes.
    struct SyncState {
        s8 branchChoice[cBranchCount];
        u8 completedCount;
    };

    // Reads the merge state out of `file`. Returns false when there is no
    // decodable progression record (an uninitialised save).
    bool captureSyncState(GameDataFile* file, SyncState* out);

    // Merges `remote` into this save and persists if anything moved. Monotone:
    // it only ever resolves an unresolved branch and only ever raises the
    // completed prefix, so applying the same state twice - or applying two
    // players' states in either order - lands on the same result.
    //
    // Deliberately NOT expressed through commitBranchChoice: that only acts on
    // the CURRENT frontier, so a player who was behind could not adopt a choice
    // their team made further ahead, and would then be stuck unable to adopt it
    // later either. This writes the ordering positionally instead.
    //
    // Returns whether anything changed.
    bool applySyncState(GameDataFile* file, const SyncState& remote);

    void reset();
    bool initializeNew(GameDataFile* file);
    bool prepareAuditComplete(GameDataFile* file);
    bool load(GameDataFile* file);
    bool save(GameDataFile* file);
    bool isValidSave(GameDataFile* file);
    bool isValidRecordInBuffer(const u8* buffer, u32 size);
    void debugLogState(GameDataFile* file, const char* context);

    const KingdomProgressList* get(GameDataFile* file);
    s32 getFirstIncompleteIndex(GameDataFile* file);
    s32 getVisibleCount(GameDataFile* file);
    bool isVisible(GameDataFile* file, s32 kingdomId);
    const KingdomProgressEntry* findEntry(GameDataFile* file, s32 kingdomId);

    bool requirementsSatisfied(GameDataFile* file, const KingdomProgressEntry& entry);
    bool tryCompleteFrontier(GameDataFile* file, s32 currentKingdomId);
    s32 getFrontierUnlockCandidates(GameDataFile* file, s32* outKingdomIds, s32 maxCount);
    bool commitBranchChoice(GameDataFile* file, s32 selectedKingdomId);

    bool areAllEntriesExceptComplete(GameDataFile* file, s32 excludedKingdomId);

} // namespace KingdomProgress
