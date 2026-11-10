#ifndef OPENMW_MWMP_VOICEPROCESSOR_HPP
#define OPENMW_MWMP_VOICEPROCESSOR_HPP

#include "voicelevel.hpp"
#include <array>
#include <cstdint>
#include <mutex>
#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>

namespace mwmp
{
// Shared by the SDL capture thread and OpenAL stream thread. No game objects
// are accessed here. The reference is voice playback, not the whole game mix.
class VoiceProcessor
{
public:
    static constexpr int Rate = 16000;
    static constexpr int Frame = 320;
    static constexpr int History = Rate * 2;
    VoiceProcessor();
    ~VoiceProcessor();
    VoiceProcessor(const VoiceProcessor&) = delete;
    VoiceProcessor& operator=(const VoiceProcessor&) = delete;
    static double now();
    void configure(bool enabled, bool echo, bool denoise, bool normalize, int delayMs, float preamp = 1.f);
    void resetCapture();
    void reference(const std::int16_t* samples, std::size_t count, double start, float gain);
    void process(std::int16_t* samples, double start);
private:
    std::mutex mMutex;
    SpeexEchoState* mEcho = nullptr;
    SpeexPreprocessState* mPreprocess = nullptr;
    std::array<std::int64_t, History> mTags{};
    std::array<float, History> mReference{};
    bool mEnabled = true, mEchoEnabled = true, mDenoise = true, mNormalize = true;
    int mDelay = 960;
    bool mConfigured = false;
    float mPreamp = 1.f;
    VoiceLevel mLevel;
};
}
#endif
