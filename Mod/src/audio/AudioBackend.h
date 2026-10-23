#pragma once

#include "nn/audio.h"
#include "nn/os.h"

#include "minimp3.h"

namespace hk {
    namespace audio {

        class AudioBackend {
        public:
            static AudioBackend& instance() {
                static AudioBackend s_instance;
                return s_instance;
            }

            // Spawns the worker thread and returns immediately. The renderer itself is
            // brought up *on* that thread: nn::audio's memory-pool attach handshake
            // polls for up to half a second, which must never block the game's main
            // thread. Commands issued before the renderer is up are simply picked up
            // once it is ready.
            void init();

            // All four of these are called from game threads. They never touch
            // nn::audio directly -- they only publish a request that the worker thread
            // picks up (see the seqlock in the .cpp). nn::audio's config API is not
            // thread safe, so every renderer call has to happen on one thread.
            void play(const char* filepath);
            void stop();
            void setPaused(bool paused);
            void setVolume(float volume);

            // True once the renderer has been given up on: either it never came up, or
            // RequestUpdateAudioRenderer kept failing. Callers must fall back to the
            // game's own music -- continuing to poke a broken renderer session is what
            // wedged the game.
            bool isFaulted() const { return mFaulted; }

            // The renderer has actually consumed samples from our voice. Nothing should
            // silence the game's own music before this is true -- otherwise a renderer
            // that never plays leaves the player with no audio at all.
            bool isAudible() const { return mRendererReady && !mFaulted && mPlayedSamples > 0; }

        private:
            AudioBackend();
            ~AudioBackend();

            static void ThreadEntry(void* arg);
            void threadLoop();
            bool initRenderer();
            void teardownRenderer();
            // Returns false once the renderer has failed too many times in a row.
            bool pumpRenderer();

            // One decoded MPEG frame is at most 1152 samples * 2 channels.
            static constexpr size_t cMaxFrameBytes = MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(mp3d_sample_t);
            // A voice accepts at most four wave buffers in flight.
            static constexpr int cWaveBufferCount = 4;
            // 8 frames is ~209 ms per buffer, so ~836 ms is queued ahead. A track
            // change flushes the queue outright, so the depth costs no responsiveness
            // and buys tolerance for SD-card and scheduler hitches.
            static constexpr int cFramesPerWaveBuffer = 8;
            static constexpr size_t cWaveBufferBytes = cFramesPerWaveBuffer * cMaxFrameBytes;
            static constexpr size_t cPoolSize = cWaveBufferCount * cWaveBufferBytes;
            // minimp3 puts a ~16 KB mp3dec_scratch_t on the stack inside
            // mp3dec_decode_frame, and its synthesis helpers nest another kilobyte or
            // so under that. The original 0x4000 stack overflowed on the very first
            // decode; keep a wide margin.
            static constexpr size_t cThreadStackSize = 0x10000;
            static constexpr size_t cMaxFileSize = 32u * 1024u * 1024u;
            // One loop tick is 10 ms, so 0.02 per tick is a half-second fade -- close to
            // the length the game's own BGM transitions use.
            static constexpr float cFadeStep = 0.02f;
            // 25 chars of "sd:/SMO-Randomizer/Music/" plus a 127-char track name plus
            // ".mp3" needs more than 128.
            static constexpr size_t cPathSize = 192;

            bool mThreadStarted = false;
            volatile bool mShouldStopThread = false;
            volatile bool mRendererReady = false;
            volatile bool mFaulted = false;
            int mUpdateFailCount = 0;
            volatile u64 mPlayedSamples = 0;

            nn::os::ThreadType mThread;
            // Each of these page-aligned buffers is carved out of a larger malloc; the
            // *Raw pointer is the one free() will accept. See allocPageAligned.
            void* mThreadStack = nullptr;
            void* mThreadStackRaw = nullptr;

            // Audio Renderer
            nn::audio::AudioRendererParameter mParameter;
            nn::audio::AudioRendererHandle mRendererHandle;
            nn::audio::AudioRendererConfig mRendererConfig;

            void* mWorkBuffer = nullptr;
            void* mWorkBufferRaw = nullptr;
            void* mConfigBuffer = nullptr;
            void* mConfigBufferRaw = nullptr;

            nn::audio::FinalMixType mFinalMix;
            nn::audio::DeviceSinkType mDeviceSink;
            nn::audio::VoiceType mVoice;

            // Memory Pool
            nn::audio::MemoryPoolType mMemoryPool;
            void* mPoolBuffer = nullptr;
            void* mPoolBufferRaw = nullptr;

            // ---- Command channel: written by game threads, read by the worker. ----
            // mCommandSeq is a seqlock: odd while mCommandFile/mCommandPlay are being
            // written, even when they are consistent.
            volatile u32 mCommandSeq = 0;
            char mCommandFile[cPathSize] = { 0 };
            volatile bool mCommandPlay = false;
            volatile bool mCommandPaused = false;
            volatile float mCommandVolume = 1.0f;

            // ---- Worker-thread-only state. ----
            u32 mAppliedSeq = 0;
            // A request waits here while the track it replaces fades out.
            char mPendingFile[cPathSize] = { 0 };
            bool mPendingPlay = false;
            bool mHasPendingRequest = false;
            float mFadeGain = 0.0f;
            // Ramps toward mCommandVolume, so ducking fades instead of snapping.
            float mVolumeGain = 1.0f;
            float mAppliedVolume = -1.0f;
            bool mIsPlaying = false;
            char mLoadedFile[cPathSize] = { 0 };

            void* mFileBuffer = nullptr;
            size_t mFileSize = 0;
            size_t mFileOffset = 0;
            int mSourceSampleRate = 0;
            int mSourceChannels = 0;
            bool mFileIsUndecodable = false;
            int mHeartbeatTicks = 0;

            mp3dec_t mDecoder;
            mp3d_sample_t mDecodeScratch[MINIMP3_MAX_SAMPLES_PER_FRAME];
        };

    } // namespace audio
} // namespace hk
