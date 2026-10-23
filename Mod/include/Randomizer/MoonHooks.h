#pragma once

#include <basis/seadTypes.h>

class GameDataFile;
class ShineInfo;
class GameDataHolderWriter;
class Shine;

namespace al {
    class ExecuteDirector;
    class IUseSceneObjHolder;
}

namespace ShineDB {
    struct ShineEntry;
}

namespace MoonHooks {

    // Marks a synchronous live-actor moon grant as multiplayer-originated.
    // Scopes nest safely and always restore the previous source, including
    // when the grant path returns early.
    class RemoteShineGrantScope {
    public:
        RemoteShineGrantScope();
        ~RemoteShineGrantScope();
        RemoteShineGrantScope(const RemoteShineGrantScope&) = delete;
        RemoteShineGrantScope& operator=(const RemoteShineGrantScope&) = delete;

    private:
        u8 mPreviousSource;
    };

    void clearShineColorTable();
    // Drops the cached per-kingdom moon totals that getShineNum() serves while
    // kingdom redistribution is active. Call after rewriting acquisition state
    // outside the normal collect path (see Nuzlocke's Soft reroll).
    void invalidateShineCountCache();
    void sanitizeBossRematchReturnToHome(GameDataFile* file, const char* stageName);
    bool isGotShineRaw(const GameDataFile* file, const ShineInfo* shineInfo);
    void installHooks();

    // Every ActorModelDraw executor list is sized for the handful of distinct
    // moon models a vanilla stage places; randomized moons and their moon-get
    // presentation actors add far more, and registerActorModel has no bounds
    // check. Call once per scene, at the end of al::ExecuteDirector::init, to
    // re-base the (still empty) drawer buffers onto larger ones.
    void reserveMoonModelDrawerCapacity(al::ExecuteDirector* director);

    void setGotShineHook(GameDataHolderWriter writer, const ShineInfo* shineInfo);

    bool grantMoonDirect(GameDataHolderWriter writer, const ShineDB::ShineEntry* entry,
        const ShineDB::ShineEntry* displayLocation = nullptr);
    // Same real setGotShine/unlock path as a direct shop reward, but tagged so
    // multiplayer does not rebroadcast local safety-net grants and the
    // notification queue can append several grants into one startup batch.
    bool grantMoonAuto(GameDataHolderWriter writer, const ShineDB::ShineEntry* entry,
        const ShineDB::ShineEntry* displayLocation = nullptr);
    bool isMoonAcquiredDirect(GameDataFile* file, const ShineDB::ShineEntry* entry);
    // Applies a moon a peer already credited (shineInfo is already the exact
    // destination/credited slot, same as grantMoonDirect's entry) through the
    // real GameDataFunction::setGotShine path rather than the bare
    // GameDataFile::setGotShine flag write. Routing through it is what makes
    // setGotShineHook run applyUnlockFeedback, so a moon that carries a
    // capture/ability unlock actually grants it to the receiving player too,
    // not just the got-flag. Same sBypassCreditRemap guard as grantMoonDirect,
    // since shineInfo must not be re-remapped through MoonRando::getCreditMoon
    // a second time.
    void applyRemoteShine(GameDataHolderWriter writer, const ShineInfo* shineInfo);
    // Retires the live Shine actor standing in this player's stage for a moon
    // a PEER collected, without playing anything. Shine::getDirect() - the
    // call this replaces on the receive path - is the player-pickup entry
    // point and starts the moon-get cutscene, so using it for a remote grant
    // stole the camera for a moon this player never touched (once per moon in
    // the backlog when joining a room). This fires the moon's stage switches
    // so the world stays consistent, suppresses the change-stage payload that
    // would warp the player, and kills the actor. Credit itself is separate -
    // see applyRemoteShine.
    void collectStageShineSilently(Shine* shine);
    // True while applyRemoteShine's call into the real setGotShine path is
    // still on the stack. Lets setGotShineHook's multiplayer callback
    // (syncHooks.cpp's mpOnSetGotShine) tell "a peer's grant is being
    // applied locally" apart from "the local player actually picked this
    // up" - the two look identical from inside the hook otherwise, since
    // applyRemoteShine deliberately routes through the same real
    // setGotShine path a genuine pickup uses.
    bool isRemoteShineGrantActive();
    bool isAutoGrantActive();
    const char16_t* tryMakeMoonRockKeyCapMessage(const char* fileName, const char* key);

    // Subarea Keys. The refusal is decided inside the stage transition, so the
    // reason is armed there and spoken a few frames after the bounce lands.
    // returnStage is the stage the refusal bounces back into; the line is only
    // ever spoken on that arrival (see noteSubareaKeyDeniedArrival).
    void armSubareaKeyDeniedMessage(s32 kingdom, s32 subareaType, const char* returnStage);
    // Releases the armed line once the bounce has actually landed. Call from
    // StageScene::init; without it the message would count down through the
    // outgoing wipe and be cut off by the reload.
    void noteSubareaKeyDeniedArrival(const char* stageName);
    void updateSubareaKeyDeniedMessage(const al::IUseSceneObjHolder* holder);
    const char16_t* tryMakeSubareaKeyCapMessage(const char* fileName, const char* key);

    void createMoonCutsceneHooks();
    void installMoonCutsceneHooks();
    void uninstallMoonCutsceneHooks();

} // namespace MoonHooks
