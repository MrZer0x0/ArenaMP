#include "VoiceChat.hpp"
#include "VoiceProcessor.hpp"

#ifdef __ANDROID__
#include "VoiceUiState.hpp"
#include <jni.h>
namespace { mwmp::VoiceUiState sAndroidVoiceUi; }
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <exception>
#include <mutex>
#include <vector>

#include <SDL.h>

#include <components/misc/constants.hpp>
#include <components/settings/settings.hpp>
#include <components/openmw-mp/Base/BasePlayer.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerVoice.hpp>
#include <components/openmw-mp/TimedLog.hpp>

#include "Main.hpp"
#include "GUIController.hpp"
#include "LocalPlayer.hpp"
#include "Networking.hpp"
#include "PlayerList.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwsound/sound.hpp"
#include "../mwsound/sound_decoder.hpp"

namespace
{
#ifdef __ANDROID__
    // Android UI must never touch Settings::Manager directly. The Java UI thread
    // can exist before the OpenMW settings/rendering subsystems are fully ready,
    // which made the old JNI state()/toggle() path a startup race. JNI only talks
    // to these atomics; the actual setting change is applied from VoiceChat::update
    // on the game thread.
    std::atomic<int> sGyroUiState{-1};        // -1 unknown, 0 off, 1 on
    std::atomic<int> sGyroRequestedState{-1}; // -1 no request, 0 off, 1 on
    bool sGyroImmersiveManaged = false;
    bool sGyroPreviousImmersive = false;

    void setAndroidGyroOnGameThread(bool enabled)
    {
        const bool current = Settings::Manager::getBool("enable gyroscope", "Input");
        bool inputChanged = false;
        bool cameraChanged = false;

        if (enabled)
        {
            if (!sGyroImmersiveManaged)
            {
                sGyroPreviousImmersive = Settings::Manager::getBool("immersive first person", "Camera");
                sGyroImmersiveManaged = true;
            }
            if (!current)
            {
                Settings::Manager::setBool("enable gyroscope", "Input", true);
                inputChanged = true;
            }
            if (!Settings::Manager::getBool("immersive first person", "Camera"))
            {
                Settings::Manager::setBool("immersive first person", "Camera", true);
                cameraChanged = true;
            }
        }
        else
        {
            if (current)
            {
                Settings::Manager::setBool("enable gyroscope", "Input", false);
                inputChanged = true;
            }
            if (sGyroImmersiveManaged)
            {
                const bool immersiveNow = Settings::Manager::getBool("immersive first person", "Camera");
                if (immersiveNow != sGyroPreviousImmersive)
                {
                    Settings::Manager::setBool("immersive first person", "Camera", sGyroPreviousImmersive);
                    cameraChanged = true;
                }
                sGyroImmersiveManaged = false;
            }
        }

        // setBool() only marks settings as pending. Apply the exact changed keys
        // immediately so SensorManager reopens/closes the SDL gyroscope and the
        // camera rebuilds its immersive first-person mesh without requiring the
        // Settings window's Apply button. Do not clear unrelated pending changes.
        Settings::CategorySettingVector changed;
        if (inputChanged)
            changed.emplace("Input", "enable gyroscope");
        if (cameraChanged)
            changed.emplace("Camera", "immersive first person");

        if (!changed.empty())
        {
            const MWBase::Environment& env = MWBase::Environment::get();
            if (inputChanged && env.getInputManager() != nullptr)
                env.getInputManager()->processChangedSettings(changed);
            if (cameraChanged && env.getWorld() != nullptr)
                env.getWorld()->processChangedSettings(changed);

            if (inputChanged)
                Settings::Manager::resetPendingChange("enable gyroscope", "Input");
            if (cameraChanged)
                Settings::Manager::resetPendingChange("immersive first person", "Camera");
            Settings::Manager::saveUser();
        }

        sGyroUiState.store(enabled ? 1 : 0, std::memory_order_release);
    }

    void updateAndroidGyroOnGameThread()
    {
        // An explicit OSC button press has priority over the previous settings
        // snapshot. This avoids briefly applying the old state before the new one.
        const int requested = sGyroRequestedState.exchange(-1, std::memory_order_acq_rel);
        if (requested >= 0)
        {
            setAndroidGyroOnGameThread(requested != 0);
            return;
        }

        const bool settingEnabled = Settings::Manager::getBool("enable gyroscope", "Input");
        if (sGyroUiState.load(std::memory_order_acquire) < 0)
        {
            // Honour a launcher/settings value that was already enabled before
            // entering the game, including the immersive first-person body rule.
            setAndroidGyroOnGameThread(settingEnabled);
        }
        else if (settingEnabled != (sGyroUiState.load(std::memory_order_relaxed) == 1))
        {
            // Settings window/launcher changed the value outside the OSC button.
            setAndroidGyroOnGameThread(settingEnabled);
        }
    }
#endif
}

namespace
{
    constexpr int sSampleRate = mwmp::VoiceFrame::SampleRate;
    constexpr std::size_t sFrameSamples = mwmp::VoiceFrame::FrameSamples; // 20 ms
    constexpr std::size_t sMaxCaptureSamples = sSampleRate;
    constexpr std::size_t sMaxPlaybackSamples = sSampleRate / 2;
    constexpr std::size_t sAdpcmPayloadBytes = 4 + (sFrameSamples / 2);
    constexpr float sSpeakerTimeout = 0.75f;

    constexpr std::array<int, 89> sStepTable = {
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
        34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
        157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544,
        598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878,
        2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
        6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
        18500, 20350, 22385, 24623, 27086, 29794, 32767
    };
    constexpr std::array<int, 16> sIndexTable = {
        -1, -1, -1, -1, 2, 4, 6, 8,
        -1, -1, -1, -1, 2, 4, 6, 8
    };

    int clamp16(int value)
    {
        return std::max(-32768, std::min(32767, value));
    }

    std::uint8_t encodeNibble(std::int16_t sample, int& predictor, int& index)
    {
        const int step = sStepTable[index];
        int diff = static_cast<int>(sample) - predictor;
        std::uint8_t code = 0;
        if (diff < 0)
        {
            code = 8;
            diff = -diff;
        }

        int delta = step >> 3;
        if (diff >= step) { code |= 4; diff -= step; delta += step; }
        if (diff >= (step >> 1)) { code |= 2; diff -= step >> 1; delta += step >> 1; }
        if (diff >= (step >> 2)) { code |= 1; delta += step >> 2; }

        predictor += (code & 8) ? -delta : delta;
        predictor = clamp16(predictor);
        index = std::clamp(index + sIndexTable[code & 0x0f], 0, 88);
        return code & 0x0f;
    }

    std::vector<std::uint8_t> encodeAdpcm(const std::array<std::int16_t, sFrameSamples>& samples, int& rollingIndex)
    {
        std::vector<std::uint8_t> out;
        out.reserve(4 + (sFrameSamples / 2));

        int predictor = samples[0];
        int index = std::clamp(rollingIndex, 0, 88);
        out.push_back(static_cast<std::uint8_t>(predictor & 0xff));
        out.push_back(static_cast<std::uint8_t>((predictor >> 8) & 0xff));
        out.push_back(static_cast<std::uint8_t>(index));
        out.push_back(0);

        bool low = true;
        std::uint8_t packed = 0;
        for (std::size_t i = 1; i < samples.size(); ++i)
        {
            const std::uint8_t nibble = encodeNibble(samples[i], predictor, index);
            if (low)
            {
                packed = nibble;
                low = false;
            }
            else
            {
                packed |= static_cast<std::uint8_t>(nibble << 4);
                out.push_back(packed);
                low = true;
            }
        }
        if (!low)
            out.push_back(packed);

        rollingIndex = index;
        return out;
    }

    bool decodeAdpcm(const std::vector<std::uint8_t>& payload, std::vector<std::int16_t>& out)
    {
        // A voice frame is fixed at 20 ms / 320 mono samples. Reject both
        // truncated and oversized datagrams before they can create playback state.
        if (payload.size() != sAdpcmPayloadBytes)
            return false;

        int predictor = static_cast<std::int16_t>(
            static_cast<std::uint16_t>(payload[0]) | (static_cast<std::uint16_t>(payload[1]) << 8));
        int index = payload[2];
        if (index < 0 || index > 88)
            return false;

        out.clear();
        out.reserve(sFrameSamples);
        out.push_back(static_cast<std::int16_t>(predictor));

        for (std::size_t byteIndex = 4; byteIndex < payload.size() && out.size() < sFrameSamples; ++byteIndex)
        {
            for (int shift : {0, 4})
            {
                if (out.size() >= sFrameSamples)
                    break;
                const std::uint8_t code = static_cast<std::uint8_t>((payload[byteIndex] >> shift) & 0x0f);
                const int step = sStepTable[index];
                int delta = step >> 3;
                if (code & 4) delta += step;
                if (code & 2) delta += step >> 1;
                if (code & 1) delta += step >> 2;
                predictor += (code & 8) ? -delta : delta;
                predictor = clamp16(predictor);
                index = std::clamp(index + sIndexTable[code], 0, 88);
                out.push_back(static_cast<std::int16_t>(predictor));
            }
        }
        return out.size() == sFrameSamples;
    }

    bool sequenceNewer(std::uint16_t sequence, std::uint16_t previous)
    {
        return static_cast<std::int16_t>(sequence - previous) > 0;
    }
}

namespace mwmp
{
    class VoiceStreamDecoder final : public MWSound::Sound_Decoder
    {
    public:
        explicit VoiceStreamDecoder(std::shared_ptr<VoiceProcessor> processor)
            : Sound_Decoder(nullptr), mProcessor(std::move(processor)) {}
        void onPlaybackQueued(const char* data, size_t bytes, double delay, float gain) override
        {
            const double estimatedStart = VoiceProcessor::now() + delay;
            const double start = std::abs(estimatedStart - mReferenceEnd) < 0.015
                ? mReferenceEnd : estimatedStart;
            mProcessor->reference(reinterpret_cast<const std::int16_t*>(data), bytes / sizeof(std::int16_t), start, gain);
            mReferenceEnd = start + double(bytes / sizeof(std::int16_t)) / sSampleRate;
        }
        void open(const std::string&) override {}
        void close() override {}
        std::string getName() override { return "ArenaMP proximity voice"; }
        void getInfo(int* samplerate, MWSound::ChannelConfig* chans, MWSound::SampleType* type) override
        {
            *samplerate = sSampleRate;
            *chans = MWSound::ChannelConfig_Mono;
            *type = MWSound::SampleType_Int16;
        }
        size_t read(char* buffer, size_t bytes) override
        {
            const std::size_t requested = bytes / sizeof(std::int16_t);
            auto* output = reinterpret_cast<std::int16_t*>(buffer);
            std::size_t copied = 0;
            bool drainedAndFinished = false;
            {
                std::lock_guard<std::mutex> lock(mMutex);
                while (copied < requested && !mSamples.empty())
                {
                    output[copied++] = mSamples.front();
                    mSamples.pop_front();
                }
                drainedAndFinished = mFinished && mSamples.empty();
            }

            // Let the SoundManager/OpenAL owner retire the Stream naturally.
            // VoiceChat deliberately keeps no raw Stream* after start, because
            // OpenAL can retire an underrun stream before the speaker timeout.
            if (copied == 0 && drainedAndFinished)
                return 0;

            std::fill(output + copied, output + requested, 0);
            mOffset += requested;
            return requested * sizeof(std::int16_t);
        }
        size_t getSampleOffset() override { return mOffset; }

        void push(const std::vector<std::int16_t>& samples)
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (mFinished)
                return;
            if (mSamples.size() + samples.size() > sMaxPlaybackSamples)
            {
                const std::size_t overflow = mSamples.size() + samples.size() - sMaxPlaybackSamples;
                for (std::size_t i = 0; i < overflow && !mSamples.empty(); ++i)
                    mSamples.pop_front();
            }
            mSamples.insert(mSamples.end(), samples.begin(), samples.end());
        }
        void pushSilence(std::size_t samples)
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (mFinished)
                return;
            samples = std::min(samples, sMaxPlaybackSamples);
            while (mSamples.size() + samples > sMaxPlaybackSamples && !mSamples.empty())
                mSamples.pop_front();
            mSamples.insert(mSamples.end(), samples, 0);
        }
        std::size_t queuedSamples() const
        {
            std::lock_guard<std::mutex> lock(mMutex);
            return mSamples.size();
        }
        void finish()
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mFinished = true;
        }
    private:
        std::shared_ptr<VoiceProcessor> mProcessor;
        double mReferenceEnd = 0.0;
        mutable std::mutex mMutex;
        std::deque<std::int16_t> mSamples;
        std::size_t mOffset = 0;
        bool mFinished = false;
    };

    // U035 soft limiter: linear up to sLimiterKnee, then a tanh knee up to full
    // scale. Digital gain on 16-bit voice has to be limited, not clipped - hard
    // clipping turns a boosted voice into buzz, and IMA ADPCM quantises a
    // rounded peak far better than a square one.
    constexpr float sLimiterKnee = 22000.f;
    constexpr float sLimiterCeil = 32767.f;

    std::int16_t softLimit(float value)
    {
        const float magnitude = std::fabs(value);
        if (magnitude <= sLimiterKnee)
            return static_cast<std::int16_t>(std::lround(value));
        const float sign = value < 0.f ? -1.f : 1.f;
        const float over = (magnitude - sLimiterKnee) / (sLimiterCeil - sLimiterKnee);
        const float shaped = sLimiterKnee + (sLimiterCeil - sLimiterKnee) * std::tanh(over);
        return static_cast<std::int16_t>(std::lround(sign * std::min(shaped, sLimiterCeil)));
    }

    void applyGain(std::int16_t* samples, std::size_t count, float gain)
    {
        if (gain >= 0.999f && gain <= 1.001f)
            return;
        for (std::size_t i = 0; i < count; ++i)
            samples[i] = softLimit(static_cast<float>(samples[i]) * gain);
    }

    float voiceLipLevel(const std::int16_t* samples, std::size_t count)
    {
        if (samples == nullptr || count == 0)
            return 0.f;

        double sumSquares = 0.0;
        for (std::size_t i = 0; i < count; ++i)
        {
            const double normalized = static_cast<double>(samples[i]) / 32768.0;
            sumSquares += normalized * normalized;
        }

        // Native head talk morphs expect the same approximate 0..1 loudness range
        // as voiced dialogue. A small gain keeps ordinary microphones expressive
        // without making background noise hold the mouth open.
        const float rms = static_cast<float>(std::sqrt(sumSquares / static_cast<double>(count)));
        const float gated = std::max(0.f, rms - 0.012f);
        return std::clamp(gated * 5.f, 0.f, 1.f);
    }

    struct VoiceChat::Impl
    {
        struct Speaker
        {
            std::shared_ptr<VoiceStreamDecoder> decoder;
            bool streamStarted = false;
            float age = 0.f;
            std::uint16_t lastSequence = 0;
            bool haveSequence = false;
            float lipLevel = 0.f;
            // Z005: a group frame is played flat, at the listener, because the
            // speaker may be in another cell entirely - or not even loaded.
            bool group = false;
            bool streamIsGroup = false;
        };

        SDL_AudioDeviceID captureDevice = 0;
        SDL_AudioStream* captureConverter = nullptr;
        SDL_Scancode pushToTalk = SDL_SCANCODE_V;
        bool ownsAudioSubsystem = false;
        std::mutex captureMutex;
        std::deque<std::int16_t> captureSamples;
        std::shared_ptr<VoiceProcessor> processor = std::make_shared<VoiceProcessor>();
        std::array<std::int16_t, sFrameSamples> processingFrame{};
        std::size_t processingCount = 0;
        double processingStart = 0.0;
        double nextCaptureStart = 0.0;
        std::unordered_map<std::uint64_t, Speaker> speakers;
        float localLipLevel = 0.f;
        float localLipAge = 1.f;

        // U035 radio mode: latch state and key edge detection. SDL reports the
        // physical key state every frame, so the latch may only flip on the
        // frame the key goes down.
        bool toggleLatched = false;
        bool pttBlockedUntilRelease = false;
        bool pttKeyWasDown = false;
        bool touchWasPressed = false;
        // Android screen button is always a tap-to-toggle control, even if a
        // legacy profile still has keyboard push-to-talk mode selected.
        bool androidTapLatched = false;

        // Y041: diagnostics for a push-to-talk key that appears to do nothing.
        float initRetryTimer = 0.f;
        int initAttempts = 0;
        bool notifiedUnavailable = false;
        bool loggedDisabled = false;
    };

    namespace
    {
        void appendCaptureSamples(VoiceChat::Impl* impl, const std::int16_t* samples, std::size_t count)
        {
            if (impl == nullptr || samples == nullptr || count == 0)
                return;
            std::lock_guard<std::mutex> lock(impl->captureMutex);
            if (impl->captureSamples.size() + count > sMaxCaptureSamples)
            {
                const std::size_t overflow = impl->captureSamples.size() + count - sMaxCaptureSamples;
                for (std::size_t i = 0; i < overflow && !impl->captureSamples.empty(); ++i)
                    impl->captureSamples.pop_front();
            }
            const double estimatedStart = VoiceProcessor::now() - double(count) / sSampleRate;
            const double callbackStart = std::abs(estimatedStart - impl->nextCaptureStart) < 0.030
                ? impl->nextCaptureStart : estimatedStart;
            impl->nextCaptureStart = callbackStart + double(count) / sSampleRate;
            for (std::size_t i = 0; i < count; ++i)
            {
                if (impl->processingCount == 0)
                    impl->processingStart = callbackStart + double(i) / sSampleRate;
                impl->processingFrame[impl->processingCount++] = samples[i];
                if (impl->processingCount == sFrameSamples)
                {
                    impl->processor->process(impl->processingFrame.data(), impl->processingStart);
                    impl->captureSamples.insert(impl->captureSamples.end(),
                        impl->processingFrame.begin(), impl->processingFrame.end());
                    impl->processingCount = 0;
                }
            }
        }

        void captureCallback(void* userdata, Uint8* stream, int len)
        {
#ifdef __ANDROID__
            // Android can pause the engine loop before the next update() stops
            // capture. Do not queue background audio for playback on resume.
            if (!sAndroidVoiceUi.foreground()) return;
#endif
            auto* impl = static_cast<VoiceChat::Impl*>(userdata);
            if (impl == nullptr || stream == nullptr || len <= 0)
                return;

            if (impl->captureConverter == nullptr)
            {
                appendCaptureSamples(impl, reinterpret_cast<const std::int16_t*>(stream),
                    static_cast<std::size_t>(len) / sizeof(std::int16_t));
                return;
            }

            if (SDL_AudioStreamPut(impl->captureConverter, stream, len) != 0)
                return;

            std::array<std::int16_t, 2048> converted{};
            for (;;)
            {
                const int available = SDL_AudioStreamAvailable(impl->captureConverter);
                if (available < static_cast<int>(sizeof(std::int16_t)))
                    break;
                const int wanted = std::min<int>(available, static_cast<int>(sizeof(converted)));
                const int got = SDL_AudioStreamGet(impl->captureConverter, converted.data(), wanted);
                if (got <= 0)
                    break;
                appendCaptureSamples(impl, converted.data(), static_cast<std::size_t>(got) / sizeof(std::int16_t));
            }
        }
    }

    VoiceChat::VoiceChat() : mImpl(new Impl) {}
    VoiceChat::~VoiceChat() { shutdown(); }

    void VoiceChat::configure(bool enabled, const std::string& pushToTalkKey, const std::string& captureDevice,
        float rangeMeters, float fullVolumeMeters, float sourceVolume, float micGain, float playbackGain,
        bool toggleMode)
    {
        mToggleMode = toggleMode;
        mEnabled = enabled;
        mPushToTalkKey = pushToTalkKey.empty() ? "V" : pushToTalkKey;
        mCaptureDevice = captureDevice;
        mRangeMeters = std::clamp(rangeMeters, 3.f, 100.f);
        // Full volume out to fullVolumeMeters, then the usual falloff towards
        // rangeMeters. The reference distance must never exceed the audible
        // range, or OpenAL would hold full gain right up to the cutoff.
        mFullVolumeMeters = std::clamp(fullVolumeMeters, 1.f, mRangeMeters);
        mSourceVolume = std::clamp(sourceVolume, 0.1f, 8.f);
        mMicGain = std::clamp(micGain, 0.1f, 8.f);
        mPlaybackGain = std::clamp(playbackGain, 0.1f, 8.f);
        if (!mToggleMode)
            mImpl->toggleLatched = false;
        const SDL_Scancode code = SDL_GetScancodeFromName(mPushToTalkKey.c_str());
        mImpl->pushToTalk = code == SDL_SCANCODE_UNKNOWN ? SDL_SCANCODE_V : code;

        // Z007: restore the remembered group-voice latch from settings.cfg
        // ([Chat] "group voice"). The key is deliberately absent from
        // settings-default.cfg - it is written by the button, not edited by
        // hand - so a first run throws here and simply means "off", exactly
        // like the guarded [Voice] reads in Main.cpp.
        try
        {
            mGroupTalk = Settings::Manager::getBool("group voice", "Chat");
        }
        catch (const std::exception&)
        {
            mGroupTalk = false;
        }
        mGroupActive = false;
    }

    void VoiceChat::init()
    {
        if (!mEnabled || mAvailable)
            return;

        if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0)
        {
            if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Voice: SDL audio init failed: %s", SDL_GetError());
                return;
            }
            mImpl->ownsAudioSubsystem = true;
        }

        mImpl->processor->configure(
            Settings::Manager::getBool("voice processing", "Chat"),
            Settings::Manager::getBool("voice echo cancellation", "Chat"),
            Settings::Manager::getBool("voice noise suppression", "Chat"),
            Settings::Manager::getBool("voice normalization", "Chat"),
            Settings::Manager::getInt("voice echo delay ms", "Chat"),
            mMicGain * Settings::Manager::getFloat("microphone gain", "Chat"));

        SDL_AudioSpec desired{};
        desired.freq = sSampleRate;
        desired.format = AUDIO_S16SYS;
        desired.channels = 1;
        desired.samples = static_cast<Uint16>(sFrameSamples);
        desired.callback = captureCallback;
        desired.userdata = mImpl.get();

        SDL_AudioSpec obtained{};
        const int allowedChanges = SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_FORMAT_CHANGE
            | SDL_AUDIO_ALLOW_CHANNELS_CHANGE;
        const char* requestedDevice = mCaptureDevice.empty() ? nullptr : mCaptureDevice.c_str();
        mImpl->captureDevice = SDL_OpenAudioDevice(requestedDevice, SDL_TRUE, &desired, &obtained, allowedChanges);
        if (mImpl->captureDevice == 0 && requestedDevice != nullptr)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "Voice: selected microphone '%s' is unavailable (%s); falling back to the system default",
                requestedDevice, SDL_GetError());
            mImpl->captureDevice = SDL_OpenAudioDevice(nullptr, SDL_TRUE, &desired, &obtained, allowedChanges);
        }
        if (mImpl->captureDevice == 0)
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Voice: microphone open failed: %s", SDL_GetError());
            return;
        }
        if (obtained.freq != sSampleRate || obtained.format != AUDIO_S16SYS || obtained.channels != 1)
        {
            mImpl->captureConverter = SDL_NewAudioStream(obtained.format, obtained.channels, obtained.freq,
                AUDIO_S16SYS, 1, sSampleRate);
            if (mImpl->captureConverter == nullptr)
            {
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Voice: microphone conversion setup failed: %s", SDL_GetError());
                SDL_CloseAudioDevice(mImpl->captureDevice);
                mImpl->captureDevice = 0;
                return;
            }
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
                "Voice: converting microphone capture from %d Hz/format %u/%u ch to 16 kHz mono S16",
                obtained.freq, static_cast<unsigned int>(obtained.format), static_cast<unsigned int>(obtained.channels));
        }

        SDL_PauseAudioDevice(mImpl->captureDevice, 1);
        mAvailable = true;
        mImpl->notifiedUnavailable = false;
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
            "Voice: proximity voice ready (%s, %s, full volume %.1f m, range %.1f m,"
            " source x%.2f, mic x%.2f, playback x%.2f, capture=%s)",
            SDL_GetScancodeName(mImpl->pushToTalk), mToggleMode ? "radio/toggle" : "push-to-talk",
            mFullVolumeMeters, mRangeMeters, mSourceVolume, mMicGain, mPlaybackGain,
            mCaptureDevice.empty() ? "system-default" : mCaptureDevice.c_str());
    }

    void VoiceChat::notifyUnavailable(const std::string& reason)
    {
        if (mImpl->notifiedUnavailable)
            return;
        mImpl->notifiedUnavailable = true;
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN, "Voice: push-to-talk ignored - %s", reason.c_str());
        MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
        if (windowManager != nullptr)
            windowManager->messageBox(reason);
    }

    void VoiceChat::update(float dt)
    {
        mImpl->processor->configure(
            Settings::Manager::getBool("voice processing", "Chat"),
            Settings::Manager::getBool("voice echo cancellation", "Chat"),
            Settings::Manager::getBool("voice noise suppression", "Chat"),
            Settings::Manager::getBool("voice normalization", "Chat"),
            Settings::Manager::getInt("voice echo delay ms", "Chat"),
            mMicGain * Settings::Manager::getFloat("microphone gain", "Chat"));
#ifdef __ANDROID__
        updateAndroidGyroOnGameThread();
#endif
        // Y041: the key is sampled before the availability checks so that a player
        // pressing push-to-talk on a client with voice disabled or without a usable
        // microphone gets told why nothing happens, instead of silence.
        LocalPlayer* local = Main::get().getLocalPlayer();
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        GUIController* gui = Main::get().getGUIController();
        const bool chatEditing = gui != nullptr && gui->getChatEditState();

        // Z005: the microphone used to open when V was typed as a letter.
        //
        // This loop samples the physical key with SDL_GetKeyboardState, which
        // knows nothing about who owns the keyboard. The existing guard only
        // covered the Player Menu's own chat box, so every OTHER text field -
        // the console, a server InputDialog or PasswordDialog, the account
        // card, a save name - still passed the keystroke to both the text
        // field and push-to-talk.
        //
        // SDL text input is started and stopped by this codebase exactly when
        // a field wants characters (WindowManager::updateVisible and
        // KeyboardManager), and the input manager already uses it as this same
        // guard for Activate and other actions. Console mode is checked too,
        // because the console takes keys without always running text input.
        const bool textEntryActive = SDL_IsTextInputActive() == SDL_TRUE;
        MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
        const bool consoleMode = windowManager != nullptr && windowManager->isConsoleMode();
        const bool keyboardTaken = chatEditing || textEntryActive || consoleMode;

        // Text entry/settings must not tear down an already-open radio mic. They
        // only suppress NEW keyboard toggle edges, so typing V remains a letter.
        bool talkAllowed = local != nullptr && local->isLoggedIn();
        bool toggleAllowed = talkAllowed && !keyboardTaken;
        bool touchPressed = false;
        bool androidToggleRequested = false;
#ifdef __ANDROID__
        talkAllowed = talkAllowed && sAndroidVoiceUi.foreground();
        toggleAllowed = toggleAllowed && talkAllowed;
        if (!talkAllowed) sAndroidVoiceUi.press(false);
        touchPressed = sAndroidVoiceUi.pressed();
        androidToggleRequested = sAndroidVoiceUi.takeToggleRequest();
#endif
        // U035: hold mode is the classic push-to-talk; radio mode latches the
        // microphone on a key-down edge and leaves it open until the next one.
        // Both are driven from the physical key state, so a held key still
        // produces exactly one transition; typing anywhere never toggles
        // anything, because toggleAllowed is false while a text field owns
        // the keyboard; an already-latched microphone remains open (Z005).
        const bool keyDown = keys != nullptr && keys[mImpl->pushToTalk];
        if (!talkAllowed && (keyDown || touchPressed)) mImpl->pttBlockedUntilRelease = true;
        if (!keyDown && !touchPressed) mImpl->pttBlockedUntilRelease = false;
        const bool keyPressed = keyDown && !mImpl->pttKeyWasDown && !mImpl->pttBlockedUntilRelease;
        const bool touchJustPressed = touchPressed && !mImpl->touchWasPressed;
        mImpl->pttKeyWasDown = keyDown;
        mImpl->touchWasPressed = touchPressed;

        // The remembered group preference opens the mic by default. V owns a
        // proximity override: toggle mode switches on/off; hold mode lasts only
        // until release. Only one channel can receive the captured samples.
        bool localTalking = false;
        if (mToggleMode)
        {
            if (toggleAllowed && !mImpl->pttBlockedUntilRelease
                && (keyPressed || touchJustPressed || androidToggleRequested))
                mImpl->toggleLatched = !mImpl->toggleLatched;
            if (!talkAllowed) mImpl->toggleLatched = false;
            mImpl->androidTapLatched = false;
            localTalking = talkAllowed && mImpl->toggleLatched;
        }
        else
        {
            // The physical keyboard may still use classic push-to-talk, but the
            // Android on-screen microphone is intentionally a latch: tap once
            // to open, tap again to close. This also fixes old profiles that
            // retained hold mode from earlier launcher versions.
            mImpl->toggleLatched = false;
            if (!talkAllowed)
                mImpl->androidTapLatched = false;
            else if (androidToggleRequested)
                mImpl->androidTapLatched = !mImpl->androidTapLatched;
            localTalking = talkAllowed && (mImpl->androidTapLatched
                || (!mImpl->pttBlockedUntilRelease && (touchPressed || (keyDown && toggleAllowed))));
        }
#ifdef __ANDROID__
        // A game-screen mute must stop both proximity AND group transmissions.
        // Clear the latched local mode so unmuting cannot silently reopen it.
        if (sAndroidVoiceUi.muted())
        {
            mImpl->toggleLatched = false;
            mImpl->androidTapLatched = false;
            localTalking = false;
        }
#endif
        mGroupActive = mGroupTalk && mInGroup && talkAllowed && !localTalking;
#ifdef __ANDROID__
        if (sAndroidVoiceUi.muted()) mGroupActive = false;
#endif
        bool wantsToTalk = localTalking || mGroupActive;

        if (!mEnabled)
        {
#ifdef __ANDROID__
            sAndroidVoiceUi.reset();
#endif
            if (!mImpl->loggedDisabled)
            {
                mImpl->loggedDisabled = true;
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "%s",
                    "Voice: microphone transmit disabled by [Voice] enabled=false; incoming voice remains active");
            }
            if (wantsToTalk)
                notifyUnavailable("Microphone transmit is disabled. Incoming player voice is still enabled.");
            mImpl->toggleLatched = false;
            wantsToTalk = false;
        }
        else
            mImpl->loggedDisabled = false;

        if (mEnabled && !mAvailable)
        {
            // Retrying SDL_OpenAudioDevice every single frame hammered the audio
            // backend and filled the log. Once every five seconds is enough to pick
            // up a microphone that is plugged in while the game is running.
            mImpl->initRetryTimer -= std::max(0.f, dt);
            if (mImpl->initRetryTimer <= 0.f)
            {
                mImpl->initRetryTimer = 5.f;
                ++mImpl->initAttempts;
                init();
            }
        }

        if (mEnabled && !mAvailable)
        {
            if (wantsToTalk)
                notifyUnavailable("Voice chat has no usable microphone. Check the system default recording device and microphone permissions.");
            // Capture availability only controls transmission. A player who has
            // no microphone (or denied Android microphone permission) must still
            // be able to hear nearby players.
        }

        // wantsToTalk is derived from physical key state, not SDL key-down
        // repeats. A held V therefore causes one transition only; when chat has
        // focus wantsToTalk is false and V is left to normal text input.
        const bool pressed = wantsToTalk && mEnabled && mAvailable;
        if (pressed != mTransmitting || (pressed && mTransmitGroup != mGroupActive))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Voice: push-to-talk %s (%s)",
                pressed ? "pressed" : "released", SDL_GetScancodeName(mImpl->pushToTalk));
            // Pause first so the capture callback cannot race the frame/converter
            // reset. This also guarantees that a new PTT press never replays the
            // tail of the previous transmission.
            SDL_PauseAudioDevice(mImpl->captureDevice, 1);
            {
                std::lock_guard<std::mutex> lock(mImpl->captureMutex);
                mImpl->captureSamples.clear();
                mImpl->processingCount = 0;
                mImpl->nextCaptureStart = 0.0;
                mImpl->processor->resetCapture();
            }
            if (mImpl->captureConverter != nullptr)
                SDL_AudioStreamClear(mImpl->captureConverter);
            mTransmitting = pressed;
            mTransmitGroup = mGroupActive;
            if (pressed)
                SDL_PauseAudioDevice(mImpl->captureDevice, 0);
            else
            {
                mImpl->localLipLevel = 0.f;
                mImpl->localLipAge = 1.f;
                if (local != nullptr)
                {
                    MWBase::SoundManager* soundManager = MWBase::Environment::get().getSoundManager();
                    if (soundManager != nullptr)
                        soundManager->clearVoiceLipSync(local->getPlayerPtr());
                }
            }
        }

        mImpl->localLipAge += std::max(0.f, dt);
        bool capturedVoiceFrame = false;
        if (mTransmitting && local != nullptr)
        {
            for (;;)
            {
                std::array<std::int16_t, sFrameSamples> frame{};
                {
                    std::lock_guard<std::mutex> lock(mImpl->captureMutex);
                    if (mImpl->captureSamples.size() < sFrameSamples)
                        break;
                    for (std::size_t i = 0; i < sFrameSamples; ++i)
                    {
                        frame[i] = mImpl->captureSamples.front();
                        mImpl->captureSamples.pop_front();
                    }
                }

                // Boost before encoding, so the listener hears the same level
                // the local indicator shows and ADPCM gets a healthier signal.
                // Gain, normalization and final limiting already ran on capture.
                mImpl->localLipLevel = voiceLipLevel(frame.data(), frame.size());
                mImpl->localLipAge = 0.f;
                capturedVoiceFrame = true;
                MWBase::SoundManager* soundManager = MWBase::Environment::get().getSoundManager();
                if (soundManager != nullptr)
                    soundManager->setVoiceLipSyncLevel(local->getPlayerPtr(), mImpl->localLipLevel);

                local->voiceFrame.sequence = ++mSequence;
                local->voiceFrame.codec = VoiceFrame::CodecImaAdpcm16k;
                local->voiceFrame.mode = mTransmitGroup ? VoiceFrame::ModeGroup : VoiceFrame::ModeProximity;
                local->voiceFrame.payload = encodeAdpcm(frame, mAdpcmIndex);
                PlayerPacket* packet = Main::get().getNetworking()->getPlayerPacket(ID_PLAYER_VOICE);
                packet->setPlayer(local);
                packet->Send();
            }
        }

        MWBase::SoundManager* soundManager = MWBase::Environment::get().getSoundManager();
        if (mTransmitting && local != nullptr && soundManager != nullptr
            && !capturedVoiceFrame && mImpl->localLipAge > 0.10f)
        {
            mImpl->localLipLevel = 0.f;
            soundManager->clearVoiceLipSync(local->getPlayerPtr());
        }

        for (auto it = mImpl->speakers.begin(); it != mImpl->speakers.end();)
        {
            Impl::Speaker& speaker = it->second;
            speaker.age += dt;
            DedicatedPlayer* player = PlayerList::getPlayer(RakNet::RakNetGUID(it->first));
            if (speaker.age > sSpeakerTimeout || player == nullptr || player->getPtr().isEmpty())
            {
                if (player != nullptr && !player->getPtr().isEmpty() && soundManager != nullptr)
                    soundManager->clearVoiceLipSync(player->getPtr());
                if (speaker.decoder)
                    speaker.decoder->finish();
                it = mImpl->speakers.erase(it);
                continue;
            }

            // Hold each 20 ms voice level briefly and then close the mouth smoothly
            // if packets stop. This follows actual speech energy instead of playing a
            // body animation, so movement/combat animation layers stay untouched.
            float lipLevel = speaker.lipLevel;
            if (speaker.age > 0.06f)
                lipLevel *= std::clamp(1.f - (speaker.age - 0.06f) / 0.12f, 0.f, 1.f);
            if (soundManager != nullptr)
                soundManager->setVoiceLipSyncLevel(player->getPtr(), lipLevel);

            // Do not retain or dereference the raw SoundManager Stream handle here.
            // The stream is positioned when an utterance starts; after silence a new
            // stream is created from the speaker's current head position.
            ++it;
        }

        // Conversation focus: when actual voice is present, reduce the game mix
        // to the configured floor (default ducking 0.95 => 5%). Voice chat itself
        // stays outside the ducked bus. Distance still controls whether remote
        // proximity speech counts as audible; group voice is always audible.
        if (soundManager != nullptr)
        {
            bool audibleSpeech = mTransmitting && mImpl->localLipAge < 0.18f
                && mImpl->localLipLevel > 0.025f;

            osg::Vec3f listenerPos;
            const bool haveListener = local != nullptr && !local->getPlayerPtr().isEmpty();
            if (haveListener)
                listenerPos = MWBase::Environment::get().getWorld()->getActorHeadTransform(local->getPlayerPtr()).getTrans();

            for (const auto& entry : mImpl->speakers)
            {
                if (entry.second.age >= 0.18f || entry.second.lipLevel <= 0.025f)
                    continue;
                if (entry.second.group)
                {
                    audibleSpeech = true;
                    break;
                }
                DedicatedPlayer* player = PlayerList::getPlayer(RakNet::RakNetGUID(entry.first));
                if (player == nullptr || player->getPtr().isEmpty())
                    continue;
                if (!haveListener)
                    continue;
                const osg::Vec3f speakerPos = MWBase::Environment::get().getWorld()
                    ->getActorHeadTransform(player->getPtr()).getTrans();
                const float distanceMeters = (speakerPos - listenerPos).length() / Constants::UnitsPerMeter;
                if (distanceMeters < mRangeMeters)
                {
                    audibleSpeech = true;
                    break;
                }
            }
            soundManager->setVoiceChatActivity(audibleSpeech ? 1.f : 0.f);
        }

        publishSpeakerHud();

#ifdef __ANDROID__
        int flags = mEnabled ? VoiceUiState::Enabled : 0;
        if (mEnabled && mAvailable) flags |= VoiceUiState::Microphone;
        if (local != nullptr && local->isLoggedIn()) flags |= VoiceUiState::LoggedIn;
        if (mEnabled && talkAllowed && mAvailable) flags |= VoiceUiState::Ready;
        if (mTransmitting) flags |= VoiceUiState::Transmitting;
        const float level = mTransmitting && mImpl->localLipAge < 0.15f ? mImpl->localLipLevel : 0.f;
        if (level > 0.025f) flags |= VoiceUiState::Speaking;
        std::vector<std::string> speaking;
        if (local != nullptr && local->isLoggedIn())
        {
            for (const auto& entry : mImpl->speakers)
            {
                if (entry.second.age > 0.18f || entry.second.lipLevel <= 0.025f) continue;
                DedicatedPlayer* player = PlayerList::getPlayer(RakNet::RakNetGUID(entry.first));
                if (player == nullptr || player->getPtr().isEmpty()) continue;
                std::string name = player->npc.mName;
                std::replace(name.begin(), name.end(), '\n', ' ');
                std::replace(name.begin(), name.end(), '\r', ' ');
                std::replace(name.begin(), name.end(), '\t', ' ');
                const int percent = static_cast<int>(std::lround(std::clamp(entry.second.lipLevel, 0.f, 1.f) * 100.f));
                speaking.push_back("0\t" + std::to_string(percent) + "\t" + name.substr(0, 120));
            }
        }
        std::sort(speaking.begin(), speaking.end());
        std::string names;
        if (mTransmitting && local != nullptr && local->isLoggedIn())
        {
            std::string name = local->npc.mName;
            std::replace(name.begin(), name.end(), '\n', ' ');
            std::replace(name.begin(), name.end(), '\r', ' ');
            std::replace(name.begin(), name.end(), '\t', ' ');
            const int percent = static_cast<int>(std::lround(std::clamp(level, 0.f, 1.f) * 100.f));
            names = "1\t" + std::to_string(percent) + "\t" + name.substr(0, 120);
        }
        for (std::size_t i = 0; i < std::min<std::size_t>(speaking.size(), 4); ++i)
        {
            if (!names.empty()) names += "\n";
            names += speaking[i];
        }
        sAndroidVoiceUi.publish(flags, level, names);
#endif
    }

    bool VoiceChat::isMicOpen() const
    {
        return mGroupActive || (mToggleMode ? mImpl->toggleLatched : mTransmitting);
    }

    void VoiceChat::toggleMicrophone()
    {
        if (!mToggleMode || SDL_IsTextInputActive() == SDL_TRUE
            || MWBase::Environment::get().getWindowManager()->isConsoleMode()
            || (Main::get().getGUIController() && Main::get().getGUIController()->getChatEditState())) return;
        mImpl->toggleLatched = !mImpl->toggleLatched;
    }

    void VoiceChat::setGroupTalk(bool state)
    {
        if (mGroupTalk == state)
            return;
        mGroupTalk = state;
        if (state) mImpl->toggleLatched = false;
        if (!mGroupTalk)
            mGroupActive = false;
        // Z007: remembered across sessions, like every other player-menu
        // preference. Saved on the flip rather than at shutdown, because a
        // crash must not be able to lose it.
        Settings::Manager::setBool("group voice", "Chat", mGroupTalk);
        Settings::Manager::saveUser();
    }

    void VoiceChat::setInGroup(bool state)
    {
        mInGroup = state;
        if (!mInGroup)
            mGroupActive = false;
    }

    // U035: feed the "who is talking" overlay. Called once per frame from
    // update(), after the speaker map has been aged and pruned, so a speaker
    // that timed out this frame is already gone from the list.
    bool VoiceChat::isSpeakerActive(const RakNet::RakNetGUID& guid) const
    {
        if (!mEnabled || !mImpl) return false;
        const auto it = mImpl->speakers.find(guid.g);
        return it != mImpl->speakers.end() && it->second.age < 0.18f
            && it->second.lipLevel > 0.025f;
    }

    void VoiceChat::publishSpeakerHud()
    {
        GUIController* gui = Main::isInitialized() ? Main::get().getGUIController() : nullptr;
        if (gui == nullptr)
            return;

        std::vector<GUIController::VoiceHudSpeaker> visible;
        LocalPlayer* local = Main::get().getLocalPlayer();
        const bool loggedIn = local != nullptr && local->isLoggedIn();
        const bool transmitting = mEnabled && loggedIn && mTransmitting;
        const float level = transmitting && mImpl->localLipAge < 0.15f ? mImpl->localLipLevel : 0.f;

        if (loggedIn)
        {
            for (const auto& entry : mImpl->speakers)
            {
                // Same hold-and-fade window the lip sync uses, so the overlay
                // and the mouth movement start and stop together.
                if (entry.second.age > 0.35f)
                    continue;
                DedicatedPlayer* player = PlayerList::getPlayer(RakNet::RakNetGUID(entry.first));
                if (player == nullptr || player->getPtr().isEmpty())
                    continue;

                GUIController::VoiceHudSpeaker speaker;
                speaker.mName = player->npc.mName;
                speaker.mLevel = std::clamp(entry.second.lipLevel, 0.f, 1.f);
                visible.push_back(speaker);
                if (visible.size() >= 5)
                    break;
            }

            std::sort(visible.begin(), visible.end(),
                [](const GUIController::VoiceHudSpeaker& a, const GUIController::VoiceHudSpeaker& b)
                { return a.mName < b.mName; });

            // The local player is useful feedback too: show the actual captured
            // speech energy instead of the old textual "You are speaking" state.
            if (transmitting)
            {
                GUIController::VoiceHudSpeaker self;
                self.mName = local->npc.mName;
                self.mLevel = std::clamp(level, 0.f, 1.f);
                self.mLocal = true;
                visible.insert(visible.begin(), self);
            }
        }

        gui->updateVoiceHud(visible, mEnabled && loggedIn && isMicOpen(), transmitting, level, mToggleMode);
    }

    void VoiceChat::receive(const RakNet::RakNetGUID& speakerGuid, const VoiceFrame& frame)
    {
        if (frame.codec != VoiceFrame::CodecImaAdpcm16k || frame.payload.empty())
            return;

        // Z005: a group frame is heard at any distance, so the speaker need not
        // be loaded on this client at all. Only world voice requires a Ptr,
        // because only world voice is positioned in the world.
        const bool groupFrame = frame.mode == VoiceFrame::ModeGroup;
        DedicatedPlayer* player = PlayerList::getPlayer(speakerGuid);
        MWBase::SoundManager* soundManager = MWBase::Environment::get().getSoundManager();
        if (soundManager == nullptr)
            return;
        if (!groupFrame && (player == nullptr || player->getPtr().isEmpty()))
            return;

        std::vector<std::int16_t> decoded;
        if (!decodeAdpcm(frame.payload, decoded))
            return;

        const std::uint64_t key = speakerGuid.g;
        Impl::Speaker& speaker = mImpl->speakers[key];
        if (speaker.haveSequence && !sequenceNewer(frame.sequence, speaker.lastSequence))
            return;
        // Switching between world and group voice changes how the stream is
        // built, so the old one is retired instead of being reused flat.
        if (speaker.streamStarted && speaker.streamIsGroup != groupFrame)
        {
            if (speaker.decoder)
                speaker.decoder->finish();
            speaker.decoder.reset();
            speaker.streamStarted = false;
        }
        speaker.group = groupFrame;

        if (!speaker.decoder)
            speaker.decoder = std::make_shared<VoiceStreamDecoder>(mImpl->processor);

        if (speaker.haveSequence)
        {
            const std::uint16_t gap = static_cast<std::uint16_t>(frame.sequence - speaker.lastSequence);
            if (gap > 1 && gap <= 4)
                speaker.decoder->pushSilence(static_cast<std::size_t>(gap - 1) * sFrameSamples);
        }

        applyGain(decoded.data(), decoded.size(), mPlaybackGain);
        speaker.decoder->push(decoded);
        speaker.lipLevel = voiceLipLevel(decoded.data(), decoded.size());
        // The receive path only stores speech energy. The native head morph is
        // updated from update(), after revalidating the remote player Ptr on the
        // main game thread. This avoids touching animation state from networking.
        speaker.lastSequence = frame.sequence;
        speaker.haveSequence = true;
        speaker.age = 0.f;

        // Prebuffer three 20 ms frames. The low-latency OpenAL stream also uses
        // exactly three 20 ms buffers, preventing the normal music-stream
        // prequeue (6 x 125 ms) from adding ~750 ms of voice delay.
        if (!speaker.streamStarted && speaker.decoder->queuedSamples() >= sFrameSamples * 3)
        {
            // The SoundManager owns the returned stream. Keep only the fact that
            // playback started; retaining the raw pointer caused a use-after-free
            // when OpenAL retired a short/underrun stream before our timeout.
            if (groupFrame)
            {
                // Flat, unattenuated, like a radio in the listener's ear.
                speaker.streamStarted =
                    soundManager->playTrack(speaker.decoder, MWSound::Type::VoiceChat) != nullptr;
            }
            else
            {
                const osg::Vec3f pos = MWBase::Environment::get().getWorld()
                    ->getActorHeadTransform(player->getPtr()).getTrans();
                speaker.streamStarted = soundManager->playTrack3D(speaker.decoder, pos,
                    mFullVolumeMeters * Constants::UnitsPerMeter, mRangeMeters * Constants::UnitsPerMeter,
                    mSourceVolume, MWSound::Type::VoiceChat) != nullptr;
            }
            speaker.streamIsGroup = groupFrame;
        }
    }

    void VoiceChat::removeSpeaker(const RakNet::RakNetGUID& speakerGuid)
    {
        const auto it = mImpl->speakers.find(speakerGuid.g);
        if (it == mImpl->speakers.end())
            return;
        MWBase::SoundManager* soundManager = MWBase::Environment::get().getSoundManager();
        DedicatedPlayer* player = PlayerList::getPlayer(speakerGuid);
        if (player != nullptr && !player->getPtr().isEmpty() && soundManager != nullptr)
            soundManager->clearVoiceLipSync(player->getPtr());
        if (it->second.decoder)
            it->second.decoder->finish();
        mImpl->speakers.erase(it);
    }

    void VoiceChat::shutdown()
    {
#ifdef __ANDROID__
        sAndroidVoiceUi.reset();
#endif
        if (!mImpl)
            return;
        MWBase::SoundManager* soundManager = nullptr;
        if (MWBase::Environment::get().getSoundManager() != nullptr)
            soundManager = MWBase::Environment::get().getSoundManager();
        for (auto& entry : mImpl->speakers)
        {
            if (entry.second.decoder)
                entry.second.decoder->finish();
            if (soundManager != nullptr)
            {
                DedicatedPlayer* player = PlayerList::getPlayer(RakNet::RakNetGUID(entry.first));
                if (player != nullptr && !player->getPtr().isEmpty())
                    soundManager->clearVoiceLipSync(player->getPtr());
            }
        }
        if (soundManager != nullptr)
        {
            LocalPlayer* local = Main::isInitialized() ? Main::get().getLocalPlayer() : nullptr;
            if (local != nullptr)
                soundManager->clearVoiceLipSync(local->getPlayerPtr());
        }
        mImpl->speakers.clear();
        if (mImpl->captureDevice != 0)
        {
            SDL_CloseAudioDevice(mImpl->captureDevice);
            mImpl->captureDevice = 0;
        }
        if (mImpl->captureConverter != nullptr)
        {
            SDL_FreeAudioStream(mImpl->captureConverter);
            mImpl->captureConverter = nullptr;
        }
        if (mImpl->ownsAudioSubsystem)
        {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            mImpl->ownsAudioSubsystem = false;
        }
        mAvailable = false;
        mTransmitting = false;
    }
}


#ifdef __ANDROID__
extern "C" JNIEXPORT void JNICALL Java_voice_NativeVoice_nativeSetForeground(JNIEnv*, jobject, jboolean active)
{
    sAndroidVoiceUi.foreground(active == JNI_TRUE);
}
extern "C" JNIEXPORT void JNICALL Java_voice_NativeVoice_nativeSetPressed(JNIEnv*, jobject, jboolean held)
{
    sAndroidVoiceUi.press(held == JNI_TRUE);
}
extern "C" JNIEXPORT void JNICALL Java_voice_NativeVoice_nativeSetMuted(JNIEnv*, jobject, jboolean muted)
{
    sAndroidVoiceUi.setMuted(muted == JNI_TRUE);
}
extern "C" JNIEXPORT void JNICALL Java_voice_NativeVoice_nativeRequestToggle(JNIEnv*, jobject)
{
    sAndroidVoiceUi.requestToggle();
}
extern "C" JNIEXPORT jint JNICALL Java_voice_NativeVoice_nativeState(JNIEnv*, jobject)
{
    return sAndroidVoiceUi.state();
}
extern "C" JNIEXPORT jboolean JNICALL Java_ui_controls_NativeGyro_nativeState(JNIEnv*, jobject)
{
    return sGyroUiState.load(std::memory_order_acquire) == 1 ? JNI_TRUE : JNI_FALSE;
}
extern "C" JNIEXPORT jboolean JNICALL Java_ui_controls_NativeGyro_nativeSetEnabled(
    JNIEnv*, jobject, jboolean enabled)
{
    const int requested = enabled == JNI_TRUE ? 1 : 0;
    sGyroRequestedState.store(requested, std::memory_order_release);
    // Optimistically reflect the requested state in the Android button. The
    // game thread applies the actual Settings key and republishes it next frame.
    sGyroUiState.store(requested, std::memory_order_release);
    return requested != 0 ? JNI_TRUE : JNI_FALSE;
}
extern "C" JNIEXPORT jbyteArray JNICALL Java_voice_NativeVoice_nativeSpeakers(JNIEnv* env, jobject)
{
    // Return UTF-8 bytes, not JNI modified UTF-8: non-BMP names stay valid.
    const std::string text = sAndroidVoiceUi.speakers();
    jbyteArray result = env->NewByteArray(static_cast<jsize>(text.size()));
    if (result && !text.empty())
        env->SetByteArrayRegion(result, 0, static_cast<jsize>(text.size()),
            reinterpret_cast<const jbyte*>(text.data()));
    return result;
}
#endif
