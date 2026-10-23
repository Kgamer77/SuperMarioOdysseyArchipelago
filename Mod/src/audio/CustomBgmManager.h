#pragma once

#include <basis/seadTypes.h>
#include <cstddef>

class CustomBgmManager {
public:
    static CustomBgmManager& instance() {
        static CustomBgmManager s_instance;
        return s_instance;
    }

    void init();

    // Which al::BgmLine a track stands in for is MusicHooks' business; this
    // class only owns the audio side. Starting and stopping both cross-fade --
    // the backend runs the envelope on its own clock.
    void play(const char* bgmName);
    void setPaused(bool paused);
    void stop();

    void setVolume(float volume);

    // The audio backend gave up (renderer never came up, or its update IPC kept
    // being rejected). Callers must hand the BGM back to the game rather than
    // leave it muted behind a track that will never play.
    bool isFaulted();

    // The custom track is genuinely coming out of the speakers. Only then is it
    // safe to silence the game's own BGM.
    bool isAudible();

    // Pool scanning
    void rescanCustomMusic();
    // Deterministic per (seed, cue): revisiting the same area keeps the same
    // track, and a seed reproduces the same assignment. Returns nullptr when no
    // tracks are installed.
    const char* getCustomMusicForCue(const char* cueKey);
    // Max mode draws fresh on every newly requested cue rather than staying
    // stable per area, matching what MusicRando's Max does to the vanilla pool.
    // That applies to which *side* a cue lands on as well, or a Vanilla + Custom
    // seed would keep sending the same area to the same side forever.
    const char* getRuntimeRandomCustomMusic(const char* cueKey);
    bool rollPrefersCustom();
    // For the Vanilla + Custom source: which side this cue falls on, fixed per
    // seed. False when there is nothing to play.
    bool cuePrefersCustom(const char* cueKey);
    size_t getTrackCount();

    // Labels for the Music Source roll, in MusicSourceMode order, with the
    // installed track count folded into the two custom entries. The pointers
    // are stable; the text behind them is rewritten on each call.
    const char16_t** getMusicSourceLabels();

private:
    CustomBgmManager();
    ~CustomBgmManager();

    // The track list is kept in flat storage rather than a vector of strings on
    // purpose. operator new here goes to sead's *current* heap, and the first
    // scan happens from a menu that runs on a scene heap -- so every name became
    // a dangling pointer the moment that scene was torn down, and the pool
    // handed back garbage like "z_B!" for a track it had read correctly minutes
    // earlier. This lives in the singleton, which outlives every scene.
    static constexpr size_t cMaxTracks = 256;
    static constexpr size_t cMaxTrackNameLength = 128;

    bool mIsRunning = false;
    bool mShouldPlay = false;
    bool mIsPaused = false;

    char mCurrentBgmPath[192] = { 0 };

    bool mHasScanned = false;
    size_t mTrackCount = 0;
    char mTrackNames[cMaxTracks][cMaxTrackNameLength] = {};

    // We keep track of the current BGM name to avoid restarting if it's already playing
    char mCurrentBgmName[128] = { 0 };
};
