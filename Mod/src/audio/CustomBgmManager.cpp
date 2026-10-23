#include "CustomBgmManager.h"

#include "nn/fs.h"
#include "nn/os.h"
#include "nn/util.h"

#include <algorithm>
#include <cstring>

#include "AudioBackend.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/Rng.h"

namespace {
    // FNV-1a plus an avalanche. Hashing the cue into the seed keeps every decision
    // reproducible and, unlike a rolling counter, uncorrelated between
    // neighbouring cues.
    u32 hashCue(const char* cueKey, u32 salt) {
        u32 hash = 2166136261u;
        for (const char* c = cueKey ? cueKey : ""; *c; c++) {
            hash ^= (u32)(u8)*c;
            hash *= 16777619u;
        }
        hash ^= salt;

        // FNV-1a's low end barely moves: it multiplies by an odd constant, so
        // bit 0 survives untouched and ends up as nothing more than a parity of
        // the input bytes. Taking a decision from the low bits therefore is not
        // a hash at all -- every resource name shares the "StmRsBgm" prefix,
        // whose parity cancels, leaving the short suffix to decide. Finish with
        // an avalanche so every output bit depends on the whole key.
        hash ^= hash >> 16;
        hash *= 0x7feb352du;
        hash ^= hash >> 15;
        hash *= 0x846ca68bu;
        hash ^= hash >> 16;
        return hash;
    }

    char16_t sCustomSourceLabel[40];
    char16_t sBothSourceLabel[40];
    const char16_t* sMusicSourceLabels[4] = { u"Off", u"Vanilla", sCustomSourceLabel, sBothSourceLabel };

    size_t appendUtf16(char16_t* dst, size_t at, const char16_t* text) {
        for (const char16_t* c = text; *c; c++)
            dst[at++] = *c;
        return at;
    }

    size_t appendUtf16Number(char16_t* dst, size_t at, int value) {
        if (value < 0)
            value = 0;
        else if (value > 9999)
            value = 9999;

        char digits[8];
        int n = 0;
        do {
            digits[n++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value > 0);
        while (n > 0)
            dst[at++] = static_cast<char16_t>(digits[--n]);
        return at;
    }
} // namespace

CustomBgmManager::CustomBgmManager() { }

CustomBgmManager::~CustomBgmManager() {
    stop();
}

void CustomBgmManager::init() {
    if (mIsRunning)
        return;
    hk::audio::AudioBackend::instance().init();
    mIsRunning = true;
}

void CustomBgmManager::play(const char* bgmName) {
    if (!bgmName || !bgmName[0])
        return;

    if (std::strcmp(mCurrentBgmName, bgmName) == 0 && mShouldPlay) {
        // Already playing this song
        return;
    }

    std::strncpy(mCurrentBgmName, bgmName, sizeof(mCurrentBgmName) - 1);
    mCurrentBgmName[sizeof(mCurrentBgmName) - 1] = '\0';

    nn::util::SNPrintf(mCurrentBgmPath, sizeof(mCurrentBgmPath), "sd:/SMO-Randomizer/Music/%s.mp3", mCurrentBgmName);

    mShouldPlay = true;
    mIsPaused = false;

    // A new track always starts at full level, whatever the last transition
    // ducked the previous one to.
    hk::audio::AudioBackend::instance().setVolume(1.0f);
    hk::audio::AudioBackend::instance().setPaused(false);
    hk::audio::AudioBackend::instance().play(mCurrentBgmPath);
}

void CustomBgmManager::setPaused(bool paused) {
    if (mIsPaused == paused)
        return;
    mIsPaused = paused;
    hk::audio::AudioBackend::instance().setPaused(paused);
}

void CustomBgmManager::stop() {
    if (!mShouldPlay && mCurrentBgmName[0] == '\0')
        return;
    // Logged because a track going quiet with nothing in the log to explain it
    // is indistinguishable from the audio pipeline having broken.
    Randomizer::logPush("[CustomBgm] stop (%s)", mCurrentBgmName);
    mShouldPlay = false;
    mIsPaused = false;
    mCurrentBgmName[0] = '\0';
    mCurrentBgmPath[0] = '\0';
    hk::audio::AudioBackend::instance().stop();
}

void CustomBgmManager::setVolume(float volume) {
    hk::audio::AudioBackend::instance().setVolume(volume);
}

void CustomBgmManager::rescanCustomMusic() {
    Randomizer::logPush("[CustomBgm] rescanCustomMusic begin");
    mTrackCount = 0;
    mHasScanned = true;

    nn::fs::DirectoryHandle handle;
    if (nn::fs::OpenDirectory(&handle, "sd:/SMO-Randomizer/Music", nn::fs::OpenDirectoryMode_File).IsFailure()) {
        Randomizer::logPush("[CustomBgm] OpenDirectory failed");
        return;
    }

    while (mTrackCount < cMaxTracks) {
        s64 entryCount = 0;
        nn::fs::DirectoryEntry entry;
        if (nn::fs::ReadDirectory(&entryCount, &entry, handle, 1).IsFailure() || entryCount == 0) {
            break;
        }

        const char* name = entry.mName;
        const size_t length = std::strlen(name);
        if (length > 4 && std::strcmp(name + length - 4, ".mp3") == 0) {
            char* slot = mTrackNames[mTrackCount];
            const size_t copy = std::min(cMaxTrackNameLength - 1, length - 4);
            std::memcpy(slot, name, copy);
            slot[copy] = '\0';
            mTrackCount++;
        }
    }
    nn::fs::CloseDirectory(handle);
    Randomizer::logPush("[CustomBgm] rescan done, total %d", (int)mTrackCount);
}

bool CustomBgmManager::cuePrefersCustom(const char* cueKey) {
    if (getTrackCount() == 0)
        return false;
    if (Randomizer::getMusicSourceMode() == Randomizer::MusicSourceMode::Custom)
        return true;
    if (Randomizer::getMusicSourceMode() != Randomizer::MusicSourceMode::Both)
        return false;

    // A separate salt from the track pick, so the coin flip and the track it
    // selects are not derived from the same value. The seed goes in before the
    // avalanche, and the answer comes off the top bit: Rng is an LCG with a
    // power-of-two modulus and an odd multiplier, so its bit 0 simply flips on
    // every step and carries no information about the seed at all.
    return (hashCue(cueKey, 0x5350414Cu ^ Randomizer::getSeed()) >> 31) != 0;
}

namespace {
    // Mirrors MusicRando::pickRuntimeRandomResource: Max is meant to draw fresh
    // for every newly requested cue, so this deliberately does not derive from
    // the seed the way the stable modes do.
    u32 runtimeRandom() {
        static u32 sRuntimeState = 0x9E3779B9u;
        const u64 tick = nn::os::GetSystemTick().value;
        sRuntimeState = sRuntimeState * 1664525u + 1013904223u;
        sRuntimeState ^= static_cast<u32>(tick) + static_cast<u32>(tick >> 32) * 2654435761u;
        return sRuntimeState;
    }
} // namespace

bool CustomBgmManager::rollPrefersCustom() {
    if (getTrackCount() == 0)
        return false;
    if (Randomizer::getMusicSourceMode() == Randomizer::MusicSourceMode::Custom)
        return true;
    if (Randomizer::getMusicSourceMode() != Randomizer::MusicSourceMode::Both)
        return false;

    // Top bit, for the same reason cuePrefersCustom uses one: this is an LCG
    // with a power-of-two modulus, so its low bits carry almost nothing.
    return (runtimeRandom() >> 31) != 0;
}

const char* CustomBgmManager::getRuntimeRandomCustomMusic(const char* cueKey) {
    if (!mHasScanned)
        rescanCustomMusic();
    if (mTrackCount == 0)
        return nullptr;

    const u32 idx = static_cast<u32>((static_cast<u64>(runtimeRandom()) * mTrackCount) >> 32);
    Randomizer::logPush("[CustomBgm] max roll %s -> %s", cueKey ? cueKey : "(none)", mTrackNames[idx]);
    return mTrackNames[idx];
}

const char* CustomBgmManager::getCustomMusicForCue(const char* cueKey) {
    if (!mHasScanned)
        rescanCustomMusic();

    if (mTrackCount == 0) {
        Randomizer::logPush("[CustomBgm] no tracks installed!");
        return nullptr;
    }

    const u32 hash = hashCue(cueKey, Randomizer::getSeed() ^ Randomizer::getRngStreamSalt(Randomizer::RngStream::Music));
    // Multiply-shift off the top bits rather than a modulo. Rng::nextModulo is
    // next() % n, which draws from the low end of an LCG -- the same weakness
    // that made the Vanilla + Custom coin flip a parity of the resource name.
    const u32 idx = (u32)(((u64)hash * (u64)mTrackCount) >> 32);
    Randomizer::logPush("[CustomBgm] cue %s -> %s", cueKey ? cueKey : "(none)", mTrackNames[idx]);
    return mTrackNames[idx];
}

bool CustomBgmManager::isFaulted() {
    return mIsRunning && hk::audio::AudioBackend::instance().isFaulted();
}

bool CustomBgmManager::isAudible() {
    return mIsRunning && mShouldPlay && hk::audio::AudioBackend::instance().isAudible();
}

size_t CustomBgmManager::getTrackCount() {
    if (!mHasScanned) {
        rescanCustomMusic();
    }
    return mTrackCount;
}

const char16_t** CustomBgmManager::getMusicSourceLabels() {
    const int count = static_cast<int>(getTrackCount());

    size_t at = appendUtf16Number(sCustomSourceLabel, 0, count);
    at = appendUtf16(sCustomSourceLabel, at, u" Custom Songs");
    sCustomSourceLabel[at] = u'\0';

    at = appendUtf16(sBothSourceLabel, 0, u"Vanilla + ");
    at = appendUtf16Number(sBothSourceLabel, at, count);
    at = appendUtf16(sBothSourceLabel, at, u" Custom");
    sBothSourceLabel[at] = u'\0';

    return sMusicSourceLabels;
}
