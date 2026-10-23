#include "AudioBackend.h"

#include "nn/fs.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "Randomizer/Randomizer.h"

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

namespace hk {
    namespace audio {

        // nn/audio.h declares nn::audio::AcquireVoiceSlot as returning Result, but the
        // real function returns a bool: it hands back non-zero on success, whereas
        // nn::Result::IsSuccess() is "value == 0". Read through that declaration, every
        // successful acquire looks like a failure, and there is no way to write the
        // call correctly -- Result has no operator bool either. Every sibling in that
        // block (AcquireFinalMix, AcquireMemoryPool, AppendWaveBuffer...) is already
        // declared bool, so it reads as a typo in the header.
        //
        // Rather than patch the OdysseyHeaders submodule, the symbol is bound here with
        // the right return type. Return types are not mangled and both bool and a
        // 4-byte Result come back in w0, so this is the same call the header would have
        // produced -- only the value is now interpreted correctly. The label is the
        // entry from syms/nn/audio.sym verbatim.
        bool acquireVoiceSlotBool(nn::audio::AudioRendererConfig* config, nn::audio::VoiceType* voice, s32 sampleRate, s32 channelCount,
            nn::audio::SampleFormat sampleFormat, s32 priority, const void* buffer,
            size_t bufferSize) __asm__("_ZN2nn5audio16AcquireVoiceSlotEPNS0_19AudioRendererConfigEPNS0_9VoiceTypeEiiNS0_12SampleFormatEiPKvm");

        namespace {
            constexpr int cRendererSampleRate = 48000;
            constexpr int cRendererChannels = 2;
            constexpr size_t cPageSize = 0x1000;

            // Do not use aligned_alloc here. The one this module links against returns
            // blocks that are neither aligned nor the requested size -- a 0x1000-aligned
            // 0x4000 request came back as 0x208d9dd470, overlapping the next
            // allocation -- so memsetting a "0x24000" buffer corrupted the heap, and
            // free()ing the result tripped CentralHeap's corruption check
            // (nnmem_nlib_debug_break). malloc/free are sound, so over-allocate and
            // align by hand, keeping the raw pointer to free with.
            void* allocPageAligned(size_t size, void** rawOut) {
                void* raw = std::malloc(size + cPageSize);
                *rawOut = raw;
                if (!raw)
                    return nullptr;
                return reinterpret_cast<void*>((reinterpret_cast<uintptr_t>(raw) + (cPageSize - 1)) & ~static_cast<uintptr_t>(cPageSize - 1));
            }
        } // namespace

        AudioBackend::AudioBackend() {
            mp3dec_init(&mDecoder);
        }

        AudioBackend::~AudioBackend() { }

        void AudioBackend::init() {
            if (mThreadStarted)
                return;

            // Page-aligned because nn::os aliases the stack into the thread region.
            mThreadStack = allocPageAligned(cThreadStackSize, &mThreadStackRaw);
            if (!mThreadStack) {
                Randomizer::logPush("[AudioBackend] thread stack alloc failed!");
                return;
            }

            mShouldStopThread = false;
            nn::Result res = nn::os::CreateThread(&mThread, ThreadEntry, this, mThreadStack, cThreadStackSize, 16, 0);
            if (res.IsFailure()) {
                Randomizer::logPush("[AudioBackend] CreateThread failed! %08x", *(u32*)&res);
                std::free(mThreadStackRaw);
                mThreadStackRaw = nullptr;
                mThreadStack = nullptr;
                return;
            }

            mThreadStarted = true;
            nn::os::StartThread(&mThread);
        }

        bool AudioBackend::initRenderer() {
            nn::audio::InitializeAudioRendererParameter(&mParameter);
            mParameter.sampleRate = cRendererSampleRate;
            mParameter.sampleCount = 240;
            mParameter.mixBufferCount = 8;
            mParameter.subMixCount = 0;
            mParameter.voiceCount = 4;
            mParameter.sinkCount = 1;
            mParameter.effectCount = 0;
            mParameter.performanceFrameCount = 0;
            mParameter.isVoiceDropEnabled = false;

            size_t workBufferSize = nn::audio::GetAudioRendererWorkBufferSize(mParameter);
            mWorkBuffer = allocPageAligned(workBufferSize, &mWorkBufferRaw);
            if (!mWorkBuffer) {
                Randomizer::logPush("[AudioBackend] work buffer alloc failed! (%d bytes)", (int)workBufferSize);
                return false;
            }

            nn::Result res = nn::audio::OpenAudioRenderer(&mRendererHandle, mParameter, mWorkBuffer, workBufferSize);
            if (res.IsFailure()) {
                Randomizer::logPush("[AudioBackend] OpenAudioRenderer failed! %08x", *(u32*)&res);
                teardownRenderer();
                return false;
            }

            size_t configBufferSize = nn::audio::GetAudioRendererConfigWorkBufferSize(mParameter);
            mConfigBuffer = allocPageAligned(configBufferSize, &mConfigBufferRaw);
            if (!mConfigBuffer) {
                Randomizer::logPush("[AudioBackend] config buffer alloc failed!");
                teardownRenderer();
                return false;
            }

            nn::audio::InitializeAudioRendererConfig(&mRendererConfig, mParameter, mConfigBuffer, configBufferSize);

            if (!nn::audio::AcquireFinalMix(&mRendererConfig, &mFinalMix, cRendererChannels)) {
                Randomizer::logPush("[AudioBackend] AcquireFinalMix failed!");
                teardownRenderer();
                return false;
            }

            s8 channelMap[] = { 0, 1 };
            res = nn::audio::AddDeviceSink(&mRendererConfig, &mDeviceSink, &mFinalMix, channelMap, cRendererChannels, "MainAudioOut");
            if (res.IsFailure()) {
                Randomizer::logPush("[AudioBackend] AddDeviceSink failed! %08x", *(u32*)&res);
                teardownRenderer();
                return false;
            }

            mPoolBuffer = allocPageAligned(cPoolSize, &mPoolBufferRaw);
            if (!mPoolBuffer) {
                Randomizer::logPush("[AudioBackend] pool alloc failed! (%d bytes)", (int)cPoolSize);
                teardownRenderer();
                return false;
            }
            std::memset(mPoolBuffer, 0, cPoolSize);
            if (!nn::audio::AcquireMemoryPool(&mRendererConfig, &mMemoryPool, mPoolBuffer, cPoolSize)) {
                Randomizer::logPush("[AudioBackend] AcquireMemoryPool failed!");
                teardownRenderer();
                return false;
            }
            nn::audio::RequestAttachMemoryPool(&mMemoryPool);

            // Page offsets rather than addresses: all three must be 0. (Printing the
            // full pointers needed several %llx in one line, and the SDK's formatter
            // mangled all but the last one -- it reported 0x1470 for a buffer the
            // renderer was demonstrably using.)
            Randomizer::logPush("[AudioBackend] buffers pageoff=%x/%x/%x sizes=%x/%x/%x", (u32)((u64)mWorkBuffer & 0xFFF), (u32)((u64)mConfigBuffer & 0xFFF),
                (u32)((u64)mPoolBuffer & 0xFFF), (u32)workBufferSize, (u32)configBufferSize, (u32)cPoolSize);

            // The pool only becomes usable once the renderer has processed the request,
            // so pump updates until it reports attached. This is why the whole bring-up
            // lives on the worker thread -- it can take a few hundred milliseconds.
            if (!pumpRenderer()) {
                teardownRenderer();
                return false;
            }
            for (int i = 0; i < 100 && !nn::audio::IsMemoryPoolAttached(&mMemoryPool); i++) {
                nn::os::SleepThread(nn::TimeSpan::FromMilliSeconds(5));
                if (!pumpRenderer()) {
                    teardownRenderer();
                    return false;
                }
            }
            if (!nn::audio::IsMemoryPoolAttached(&mMemoryPool)) {
                Randomizer::logPush("[AudioBackend] MemoryPool failed to attach!");
                teardownRenderer();
                return false;
            }
            Randomizer::logPush("[AudioBackend] MemoryPool attached successfully");

            // The voice is declared at the renderer rate; files that are not 48 kHz are
            // handled with SetVoicePitch instead of re-acquiring the slot.
            if (!acquireVoiceSlotBool(&mRendererConfig, &mVoice, cRendererSampleRate, cRendererChannels, nn::audio::SampleFormat_PcmInt16, 1, nullptr, 0)) {
                Randomizer::logPush("[AudioBackend] AcquireVoiceSlot failed!");
                teardownRenderer();
                return false;
            }
            nn::audio::SetVoiceDestination(&mRendererConfig, &mVoice, &mFinalMix);
            nn::audio::SetVoicePitch(&mVoice, 1.0f);
            nn::audio::SetVoiceMixVolume(&mVoice, &mFinalMix, 1.0f, 0, 0);
            nn::audio::SetVoiceMixVolume(&mVoice, &mFinalMix, 1.0f, 1, 1);
            mAppliedVolume = 1.0f;

            if (!pumpRenderer()) {
                teardownRenderer();
                return false;
            }
            nn::audio::StartAudioRenderer(mRendererHandle);

            Randomizer::logPush("[AudioBackend] init() completed successfully!");
            return true;
        }

        // nn::audio's update goes out over IPC. If the kernel refuses it -- the audren
        // session it lands in is shared with the game's own renderer, and hammering a
        // rejected request every 10 ms was enough to hang the whole game -- report the
        // code once and give up quickly rather than keep pushing.
        bool AudioBackend::pumpRenderer() {
            const nn::Result res = nn::audio::RequestUpdateAudioRenderer(mRendererHandle, &mRendererConfig);
            if (res.IsSuccess()) {
                mUpdateFailCount = 0;
                return true;
            }

            if (mUpdateFailCount < 3)
                Randomizer::logPush("[AudioBackend] RequestUpdate failed %08x", *(u32*)&res);
            if (++mUpdateFailCount < 20)
                return true;

            Randomizer::logPush("[AudioBackend] renderer update keeps failing, shutting down");
            mFaulted = true;
            mRendererReady = false;
            nn::audio::StopAudioRenderer(mRendererHandle);
            nn::audio::CloseAudioRenderer(mRendererHandle);
            return false;
        }

        void AudioBackend::teardownRenderer() {
            // Always the raw pointers -- free() rejects the aligned offsets inside them.
            if (mPoolBufferRaw) {
                std::free(mPoolBufferRaw);
                mPoolBufferRaw = nullptr;
            }
            if (mConfigBufferRaw) {
                std::free(mConfigBufferRaw);
                mConfigBufferRaw = nullptr;
            }
            if (mWorkBufferRaw) {
                std::free(mWorkBufferRaw);
                mWorkBufferRaw = nullptr;
            }
            mPoolBuffer = nullptr;
            mConfigBuffer = nullptr;
            mWorkBuffer = nullptr;
        }

        // ---------------------------------------------------------------------------
        // Command channel. Game threads only ever write here; every nn::audio call is
        // made by the worker. mCommandSeq is a seqlock so the worker never observes a
        // half-written path.
        // ---------------------------------------------------------------------------

        void AudioBackend::play(const char* filepath) {
            if (!filepath)
                return;
            Randomizer::logPush("[AudioBackend] play(%s)", filepath);

            const u32 seq = mCommandSeq;
            __atomic_store_n(&mCommandSeq, seq + 1, __ATOMIC_RELAXED);
            __atomic_thread_fence(__ATOMIC_RELEASE);

            std::strncpy(mCommandFile, filepath, cPathSize - 1);
            mCommandFile[cPathSize - 1] = '\0';
            mCommandPlay = true;

            __atomic_thread_fence(__ATOMIC_RELEASE);
            __atomic_store_n(&mCommandSeq, seq + 2, __ATOMIC_RELAXED);
        }

        void AudioBackend::stop() {
            const u32 seq = mCommandSeq;
            __atomic_store_n(&mCommandSeq, seq + 1, __ATOMIC_RELAXED);
            __atomic_thread_fence(__ATOMIC_RELEASE);

            mCommandFile[0] = '\0';
            mCommandPlay = false;

            __atomic_thread_fence(__ATOMIC_RELEASE);
            __atomic_store_n(&mCommandSeq, seq + 2, __ATOMIC_RELAXED);
        }

        void AudioBackend::setPaused(bool paused) {
            mCommandPaused = paused;
        }

        void AudioBackend::setVolume(float volume) {
            if (volume < 0.0f)
                volume = 0.0f;
            else if (volume > 1.0f)
                volume = 1.0f;
            mCommandVolume = volume;
        }

        void AudioBackend::ThreadEntry(void* arg) {
            reinterpret_cast<AudioBackend*>(arg)->threadLoop();
        }

        void AudioBackend::threadLoop() {
            if (!initRenderer()) {
                mFaulted = true;
                return;
            }
            mRendererReady = true;

            nn::audio::WaveBuffer waveBuffers[cWaveBufferCount] = {};
            for (int i = 0; i < cWaveBufferCount; i++)
                waveBuffers[i].buffer = (char*)mPoolBuffer + i * cWaveBufferBytes;

            int nextBuffer = 0;
            // A decoded buffer the voice would not accept yet. It is retried before
            // anything else is decoded, because mFileOffset has already moved past its
            // frames -- dropping it would skip ~200 ms of the track.
            bool pendingAppend = false;
            int queuedBuffers = 0;
            int loopsWithoutSamples = 0;

            while (!mShouldStopThread) {
                // ---- Pick up a new play/stop request (seqlock read). ----
                const u32 seq = __atomic_load_n(&mCommandSeq, __ATOMIC_ACQUIRE);
                if (seq != mAppliedSeq && (seq & 1) == 0) {
                    char wanted[cPathSize];
                    std::memcpy(wanted, mCommandFile, cPathSize);
                    const bool wantPlay = mCommandPlay;
                    __atomic_thread_fence(__ATOMIC_ACQUIRE);

                    // If the writer moved on mid-copy, leave mAppliedSeq alone and
                    // retry on the next tick rather than acting on a torn path.
                    if (__atomic_load_n(&mCommandSeq, __ATOMIC_RELAXED) == seq) {
                        mAppliedSeq = seq;
                        wanted[cPathSize - 1] = '\0';
                        std::memcpy(mPendingFile, wanted, cPathSize);
                        mPendingPlay = wantPlay;
                        mHasPendingRequest = true;
                    }
                }

                // ---- Envelope. A request is held until the current track has faded
                //      out, and whatever replaces it fades in, so tracks never cut in
                //      or out the way the game's own themes never do. ----
                bool applyRequestNow = false;
                if (mHasPendingRequest) {
                    if (mIsPlaying && mFadeGain > 0.0f) {
                        mFadeGain -= cFadeStep;
                        if (mFadeGain < 0.0f)
                            mFadeGain = 0.0f;
                    } else {
                        applyRequestNow = true;
                    }
                } else if (mIsPlaying && mFadeGain < 1.0f) {
                    mFadeGain += cFadeStep;
                    if (mFadeGain > 1.0f)
                        mFadeGain = 1.0f;
                }

                if (applyRequestNow) {
                    mHasPendingRequest = false;
                    mFadeGain = 0.0f;

                    // Stopping flushes the voice's queue, but only once the renderer
                    // has seen the request -- hence the update before the drain, and
                    // the drain before anything new is appended.
                    nn::audio::SetVoicePlayState(&mVoice, nn::audio::VoiceType::PlayState_Stop);
                    // Wait for the voice to actually give the wave buffers back before
                    // reusing them. One update is usually enough, but when it was not,
                    // the next track was decoded straight over pool memory the voice
                    // was still reading -- a burst of garbled, sped-up audio at the
                    // switch. Bounded, so a wedged renderer cannot hang the thread.
                    for (int i = 0; i < 8 && queuedBuffers > 0; i++) {
                        while (nn::audio::GetReleasedWaveBuffer(&mVoice)) {
                            if (queuedBuffers > 0)
                                queuedBuffers--;
                        }
                        if (queuedBuffers > 0) {
                            nn::os::SleepThread(nn::TimeSpan::FromMilliSeconds(2));
                            if (!pumpRenderer())
                                break;
                        }
                    }
                    queuedBuffers = 0;
                    nextBuffer = 0;
                    pendingAppend = false;
                    loopsWithoutSamples = 0;

                    if (mFileBuffer) {
                        std::free(mFileBuffer);
                        mFileBuffer = nullptr;
                    }
                    mFileSize = 0;
                    mFileOffset = 0;
                    mSourceSampleRate = 0;
                    mSourceChannels = 0;
                    mFileIsUndecodable = false;
                    mLoadedFile[0] = '\0';
                    mIsPlaying = false;
                    mPlayedSamples = 0;
                    mp3dec_init(&mDecoder);
                    // The pitch is deliberately left where it is. Resetting it to 1.0
                    // here meant that anything the voice had not finished flushing
                    // played back at renderRate/sourceRate -- 1.5x for the 32 kHz files
                    // in use -- for the rest of that flush. mSourceSampleRate is
                    // cleared above, so the first decoded frame re-applies the correct
                    // ratio regardless, and an unchanged rate never audibly shifts.

                    if (mPendingPlay && mPendingFile[0]) {
                        nn::fs::FileHandle handle;
                        if (nn::fs::OpenFile(&handle, mPendingFile, nn::fs::OpenMode_Read).IsSuccess()) {
                            s64 size = 0;
                            if (nn::fs::GetFileSize(&size, handle).IsSuccess() && size > 0 && (size_t)size <= cMaxFileSize) {
                                void* buffer = std::malloc((size_t)size);
                                size_t readSize = 0;
                                if (buffer && nn::fs::ReadFile(&readSize, handle, 0, buffer, (size_t)size).IsSuccess() && readSize > 0) {
                                    mFileBuffer = buffer;
                                    mFileSize = readSize;
                                    mIsPlaying = true;
                                    std::strcpy(mLoadedFile, mPendingFile);
                                    Randomizer::logPush("[AudioBackend] loaded %s (%d bytes)", mPendingFile, (int)mFileSize);
                                } else {
                                    if (buffer)
                                        std::free(buffer);
                                    Randomizer::logPush("[AudioBackend] ReadFile failed %s", mPendingFile);
                                }
                            } else {
                                Randomizer::logPush("[AudioBackend] bad file size %d for %s", (int)size, mPendingFile);
                            }
                            nn::fs::CloseFile(handle);
                        } else {
                            Randomizer::logPush("[AudioBackend] OpenFile FAILED %s", mPendingFile);
                        }
                    }
                }

                // ---- Volume and pause, applied on this thread only. ----
                // The requested level is ramped rather than snapped, so ducking for a
                // loading zone sounds like the game's own transitions. It multiplies
                // with the track-change envelope above.
                const float targetVolume = mCommandVolume;
                if (mVolumeGain < targetVolume) {
                    mVolumeGain += cFadeStep;
                    if (mVolumeGain > targetVolume)
                        mVolumeGain = targetVolume;
                } else if (mVolumeGain > targetVolume) {
                    mVolumeGain -= cFadeStep;
                    if (mVolumeGain < targetVolume)
                        mVolumeGain = targetVolume;
                }

                const float wantVolume = mVolumeGain * mFadeGain;
                if (wantVolume != mAppliedVolume) {
                    nn::audio::SetVoiceMixVolume(&mVoice, &mFinalMix, wantVolume, 0, 0);
                    nn::audio::SetVoiceMixVolume(&mVoice, &mFinalMix, wantVolume, 1, 1);
                    mAppliedVolume = wantVolume;
                }
                const bool wantPaused = mCommandPaused;

                if (mIsPlaying && !mFileIsUndecodable) {
                    while (nn::audio::GetReleasedWaveBuffer(&mVoice)) {
                        if (queuedBuffers > 0)
                            queuedBuffers--;
                    }

                    // Re-offer a buffer the voice refused last tick before decoding
                    // anything new. Its frames are already behind mFileOffset, so
                    // giving up on it would silently drop them.
                    if (pendingAppend && nn::audio::AppendWaveBuffer(&mVoice, &waveBuffers[nextBuffer])) {
                        pendingAppend = false;
                        queuedBuffers++;
                        nextBuffer = (nextBuffer + 1) % cWaveBufferCount;
                    }

                    while (!pendingAppend && queuedBuffers < cWaveBufferCount) {
                        nn::audio::WaveBuffer& wave = waveBuffers[nextBuffer];
                        char* out = (char*)wave.buffer;
                        size_t outBytes = 0;
                        int outSamples = 0;

                        // `skipBudget` bounds how much non-audio (ID3 tags, junk) one
                        // fill will walk past, so a pathological file can never hold
                        // this loop for an unbounded stretch.
                        int skipBudget = 64;
                        // Room for one more worst-case frame is the only precondition,
                        // so a frame is never decoded and then dropped for lack of
                        // space (which would punch a hole in the audio).
                        while (outBytes + cMaxFrameBytes <= cWaveBufferBytes) {
                            if (mFileOffset >= mFileSize) {
                                // Loop the track. minimp3 carries a bit reservoir
                                // between frames, so the decoder is reset at the seam.
                                mFileOffset = 0;
                                mp3dec_init(&mDecoder);
                                break;
                            }

                            mp3dec_frame_info_t info = {};
                            const size_t remaining = mFileSize - mFileOffset;
                            const int chunk = remaining > (size_t)0x7FFFFFFF ? 0x7FFFFFFF : (int)remaining;
                            const int samples = mp3dec_decode_frame(&mDecoder, (const uint8_t*)mFileBuffer + mFileOffset, chunk, mDecodeScratch, &info);

                            if (info.frame_bytes <= 0) {
                                // Nothing recognisable left (trailing tag or garbage):
                                // treat it as end of stream and let the wrap above run.
                                mFileOffset = mFileSize;
                                continue;
                            }

                            mFileOffset += (size_t)info.frame_bytes;

                            if (samples <= 0) {
                                // Skipped an ID3v2 tag or a junk region; frame_bytes
                                // says how far to step. The first version reset the
                                // offset to zero here instead, so a tagged file never
                                // produced a single sample and just spun forever.
                                if (--skipBudget <= 0)
                                    break;
                                continue;
                            }

                            if (info.hz != mSourceSampleRate || info.channels != mSourceChannels) {
                                mSourceSampleRate = info.hz;
                                mSourceChannels = info.channels;
                                // The voice is declared as 48 kHz stereo, so anything
                                // else is resampled by the renderer via the pitch
                                // ratio. Without this a 44.1 kHz file plays ~9% sharp.
                                nn::audio::SetVoicePitch(&mVoice, (float)info.hz / (float)cRendererSampleRate);
                                Randomizer::logPush("[AudioBackend] source format %d Hz, %d ch", info.hz, info.channels);
                            }

                            const size_t frameBytes = (size_t)samples * cRendererChannels * sizeof(mp3d_sample_t);
                            mp3d_sample_t* dst = (mp3d_sample_t*)(out + outBytes);
                            if (info.channels == 1) {
                                // Duplicate mono into both channels: fed raw to a
                                // stereo voice it would play at double speed.
                                for (int s = 0; s < samples; s++) {
                                    dst[s * 2 + 0] = mDecodeScratch[s];
                                    dst[s * 2 + 1] = mDecodeScratch[s];
                                }
                            } else {
                                std::memcpy(dst, mDecodeScratch, frameBytes);
                            }
                            outBytes += frameBytes;
                            outSamples += samples;
                        }

                        if (outSamples == 0) {
                            // A file with no decodable frames at all would otherwise
                            // spin this loop at 100% CPU forever.
                            if (++loopsWithoutSamples >= 4) {
                                mFileIsUndecodable = true;
                                Randomizer::logPush("[AudioBackend] no decodable frames in %s", mLoadedFile);
                            }
                            break;
                        }
                        loopsWithoutSamples = 0;

                        wave.bufferSize = outBytes;
                        wave.startSampleOffset = 0;
                        wave.endSampleOffset = outSamples;
                        wave.shouldLoop = false;
                        wave.isEndOfStream = false;
                        wave.context = nullptr;
                        wave.contextSize = 0;

                        if (!nn::audio::AppendWaveBuffer(&mVoice, &wave)) {
                            // Hold it rather than drop it: the voice's queue is fuller
                            // than queuedBuffers thinks, which happens when a track
                            // change flushed less than expected. Dropping it here is
                            // what made a new track sound like it was racing ahead.
                            pendingAppend = true;
                            break;
                        }
                        queuedBuffers++;
                        nextBuffer = (nextBuffer + 1) % cWaveBufferCount;
                    }

                    const nn::audio::VoiceType::PlayState target = wantPaused ? nn::audio::VoiceType::PlayState_Pause : nn::audio::VoiceType::PlayState_Start;
                    if (queuedBuffers > 0 && nn::audio::GetVoicePlayState(&mVoice) != target)
                        nn::audio::SetVoicePlayState(&mVoice, target);

                    // The played sample count is the one signal that says whether the
                    // renderer is actually consuming what we queue. It also gates
                    // isAudible(), so the game's own music is never silenced for a
                    // custom track that turns out never to play.
                    mPlayedSamples = nn::audio::GetVoicePlayedSampleCount(&mVoice);
                    if (++mHeartbeatTicks >= 300) {
                        mHeartbeatTicks = 0;
                        Randomizer::logPush("[AudioBackend] hb played=%d queued=%d off=%d/%d", (int)mPlayedSamples, queuedBuffers, (int)mFileOffset, (int)mFileSize);
                    }
                } else {
                    mHeartbeatTicks = 0;
                    mPlayedSamples = 0;
                }

                if (!pumpRenderer())
                    break;
                nn::os::SleepThread(nn::TimeSpan::FromMilliSeconds(10));
            }

            // The thread is done either way here: pumpRenderer already closed the
            // renderer when it gave up, so only a clean shutdown still has to.
            mRendererReady = false;
            if (!mFaulted) {
                nn::audio::StopAudioRenderer(mRendererHandle);
                nn::audio::CloseAudioRenderer(mRendererHandle);
            }
            if (mFileBuffer) {
                std::free(mFileBuffer);
                mFileBuffer = nullptr;
            }
            teardownRenderer();
            // The stack this thread is standing on cannot be freed from here; it stays
            // reserved for the life of the process, which is fine for a singleton.
        }

    } // namespace audio
} // namespace hk
