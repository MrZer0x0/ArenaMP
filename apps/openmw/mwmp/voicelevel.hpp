#ifndef OPENMW_MWMP_VOICELEVEL_HPP
#define OPENMW_MWMP_VOICELEVEL_HPP
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace mwmp {
// 16 kHz capture: DC/rumble filter (~80 Hz), bounded AGC and final limiter.
// Used before encoding on both SDL/Windows and Android capture paths.
class VoiceLevel {
    float mInput = 0.f, mHighpass = 0.f, mGain = 1.f;
public:
    void reset() { mInput = mHighpass = 0.f; mGain = 1.f; }
    void highpass(std::int16_t* data, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            const float input = data[i];
            mHighpass = .969f * (mHighpass + input - mInput);
            mInput = input;
            data[i] = static_cast<std::int16_t>(std::clamp(mHighpass, -32768.f, 32767.f));
        }
    }
    void process(std::int16_t* data, std::size_t count, float preamp, bool normalize, bool speech) {
        if (!count) return;
        preamp = std::isfinite(preamp) ? std::clamp(preamp, .025f, 32.f) : 1.f;
        double energy = 0.;
        for (std::size_t i = 0; i < count; ++i) energy += double(data[i]) * data[i];
        const float rawRms = static_cast<float>(std::sqrt(energy / count));
        const float rms = rawRms * preamp;
        // Never amplify silence/noise solely because it is quiet. Loud frames
        // attenuate even when VAD mistakes them for non-speech.
        float target = 1.f;
        if (normalize && rms > 4000.f) target = std::max(.01f, 4000.f / rms);
        else if (normalize && speech && rawRms > 180.f)
            target = std::clamp(4000.f / std::max(1.f, rms), .01f, 4.f);
        mGain += (target - mGain) * (target < mGain ? .65f : .02f);
        if (!normalize) mGain = 1.f;
        for (std::size_t i = 0; i < count; ++i) {
            const float value = data[i] * preamp * mGain;
            const float a = std::abs(value);
            const float limited = a <= 22000.f ? a : 22000.f + 10700.f * std::tanh((a - 22000.f) / 10700.f);
            data[i] = static_cast<std::int16_t>(std::copysign(std::min(limited, 32700.f), value));
        }
    }
};
}
#endif
