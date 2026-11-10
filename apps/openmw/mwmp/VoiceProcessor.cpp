#include "VoiceProcessor.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace mwmp
{
double VoiceProcessor::now()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
VoiceProcessor::VoiceProcessor()
{
    mTags.fill(std::numeric_limits<std::int64_t>::min());
    mEcho = speex_echo_state_init(Frame, Rate / 4);
    int rate = Rate;
    if (mEcho) speex_echo_ctl(mEcho, SPEEX_ECHO_SET_SAMPLING_RATE, &rate);
    mPreprocess = speex_preprocess_state_init(Frame, Rate);
}
VoiceProcessor::~VoiceProcessor()
{
    if (mEcho) speex_echo_state_destroy(mEcho);
    if (mPreprocess) speex_preprocess_state_destroy(mPreprocess);
}
void VoiceProcessor::configure(bool enabled, bool echo, bool denoise, bool normalize, int delayMs, float preamp)
{
    std::lock_guard<std::mutex> lock(mMutex);
    const int delay = std::clamp(delayMs, 0, 250) * Rate / 1000;
    preamp = std::isfinite(preamp) ? std::clamp(preamp, .025f, 32.f) : 1.f;
    if (mConfigured && mEnabled == enabled && mEchoEnabled == echo && mDenoise == denoise
        && mNormalize == normalize && mDelay == delay && mPreamp == preamp) return;
    mConfigured = true;
    mPreamp = preamp;
    mEnabled = enabled;
    mEchoEnabled = echo;
    mDenoise = denoise;
    mNormalize = normalize;
    mDelay = delay;
    int flag = denoise ? 1 : 0;
    if (mPreprocess) speex_preprocess_ctl(mPreprocess, SPEEX_PREPROCESS_SET_DENOISE, &flag);
    // Use a bounded, slow level controller below instead of unrestricted AGC.
    flag = 0;
    if (mPreprocess) speex_preprocess_ctl(mPreprocess, SPEEX_PREPROCESS_SET_AGC, &flag);
    flag = 1;
    if (mPreprocess) speex_preprocess_ctl(mPreprocess, SPEEX_PREPROCESS_SET_VAD, &flag);
}
void VoiceProcessor::resetCapture()
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (mEcho) speex_echo_state_reset(mEcho);
    mLevel.reset();
}
void VoiceProcessor::reference(const std::int16_t* samples, std::size_t count, double start, float gain)
{
    std::lock_guard<std::mutex> lock(mMutex);
    const auto first = static_cast<std::int64_t>(std::llround(start * Rate));
    for (std::size_t i = 0; i < std::min(count, std::size_t(History)); ++i)
    {
        const auto tick = first + static_cast<std::int64_t>(i);
        const auto slot = static_cast<std::size_t>(tick % History);
        if (mTags[slot] != tick) { mTags[slot] = tick; mReference[slot] = 0.f; }
        mReference[slot] += samples[i] * std::clamp(gain, 0.f, 8.f);
    }
}
void VoiceProcessor::process(std::int16_t* samples, double start)
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mEnabled) {
        mLevel.process(samples, Frame, mPreamp, false, false);
        return;
    }
    mLevel.highpass(samples, Frame);
    std::array<std::int16_t, Frame> reference{}, clean{};
    std::array<spx_int32_t, Frame + 1> residual{};
    const auto first = static_cast<std::int64_t>(std::llround(start * Rate)) - mDelay;
    if (mEchoEnabled && mEcho)
    {
        for (int i = 0; i < Frame; ++i)
        {
            const auto tick = first + i;
            const auto slot = static_cast<std::size_t>(tick % History);
            if (mTags[slot] == tick)
                reference[i] = static_cast<std::int16_t>(std::clamp(mReference[slot], -32768.f, 32767.f));
        }
        speex_echo_cancel(mEcho, samples, reference.data(), clean.data(), residual.data());
        std::copy(clean.begin(), clean.end(), samples);
    }
    bool speech = true;
    if (mPreprocess && (mDenoise || mNormalize))
        speech = speex_preprocess(mPreprocess, samples, mEchoEnabled && mEcho ? residual.data() : nullptr) != 0;
    mLevel.process(samples, Frame, mPreamp, mNormalize, speech);
}
}
