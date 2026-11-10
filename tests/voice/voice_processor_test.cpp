#include "VoiceProcessor.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <vector>
using mwmp::VoiceProcessor;
int main()
{
    VoiceProcessor dsp;
    dsp.configure(true, true, false, false, 60);
    std::mt19937 rng(41);
    std::uniform_int_distribution<int> noise(-10000, 10000);
    const int frames = 600, size = frames * VoiceProcessor::Frame;
    std::vector<std::int16_t> far(size), near(size), output(size);
    for (auto& s : far) s = static_cast<std::int16_t>(noise(rng));
    double inputEnergy = 0, outputEnergy = 0;
    for (int f = 0; f < frames; ++f)
    {
        const int offset = f * VoiceProcessor::Frame;
        const double now = 10.0 + double(offset) / VoiceProcessor::Rate;
        dsp.reference(far.data() + offset, VoiceProcessor::Frame, now, 1.f);
        for (int i = 0; i < VoiceProcessor::Frame; ++i)
        {
            int n = offset + i;
            // Acoustic echo: configured 60 ms delay plus 20 ms filter tail.
            float echo = n >= 1280 ? far[n - 1280] * 0.6f : 0.f;
            if (n >= 1311) echo += far[n - 1311] * 0.2f;
            near[n] = output[n] = static_cast<std::int16_t>(echo);
        }
        dsp.process(output.data() + offset, now);
        if (f > frames - 100)
            for (int i = 0; i < VoiceProcessor::Frame; ++i)
            {
                inputEnergy += double(near[offset+i]) * near[offset+i];
                outputEnergy += double(output[offset+i]) * output[offset+i];
            }
    }
    const double erle = 10 * std::log10(inputEnergy / std::max(1., outputEnergy));
    std::cout << "Synthetic echo reduction: " << erle << " dB\n";
    assert(erle > 12.);
    dsp.configure(false, true, true, true, 60);
    std::vector<std::int16_t> frame(VoiceProcessor::Frame, 1234);
    dsp.process(frame.data(), 30.);
    for (auto s : frame) assert(s == 1234);
    dsp.configure(true, false, true, true, 60);
    for (int n = 0; n < 200; ++n)
    {
        for (int i = 0; i < VoiceProcessor::Frame; ++i)
            frame[i] = n < 100 ? 0 : static_cast<std::int16_t>(15000 * std::sin(i * .17));
        dsp.process(frame.data(), 31. + n * .02);
        for (auto s : frame) assert(s >= -32767);
    }
    // Stationary noise should get quieter, not be raised by normalization.
    dsp.configure(true, false, true, false, 60);
    double noiseIn = 0.0, noiseOut = 0.0;
    for (int n = 0; n < 400; ++n)
    {
        for (auto& v : frame) v = static_cast<std::int16_t>(noise(rng) / 10);
        if (n > 300) for (auto v : frame) noiseIn += double(v) * v;
        dsp.process(frame.data(), 40. + n * .02);
        if (n > 300) for (auto v : frame) noiseOut += double(v) * v;
    }
    const double noiseReduction = 10 * std::log10(noiseIn / std::max(1., noiseOut));
    std::cout << "Stationary noise reduction: " << noiseReduction << " dB\n";
    assert(noiseReduction > 3.0);
    // With echo reference absent, foreground speech must survive the canceller.
    dsp.configure(true, true, false, false, 60);
    double cleanIn = 0.0, cleanOut = 0.0;
    for (int n = 0; n < 200; ++n)
    {
        for (int i = 0; i < VoiceProcessor::Frame; ++i)
            frame[i] = static_cast<std::int16_t>(5000 * std::sin((n * VoiceProcessor::Frame + i) * .13));
        if (n > 100) for (auto v : frame) cleanIn += double(v) * v;
        dsp.process(frame.data(), 60. + n * .02);
        if (n > 100) for (auto v : frame) cleanOut += double(v) * v;
    }
    assert(cleanOut / cleanIn > 0.8 && cleanOut / cleanIn < 1.2);
    dsp.resetCapture();
    std::cout << "Bypass, reset, silence and loud input passed\n";
}
