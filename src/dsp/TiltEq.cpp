#include "dsp/TiltEq.h"

#include <algorithm>
#include <cmath>

namespace cold::dsp
{

namespace
{
constexpr float kPi = 3.14159265358979323846f;
} // namespace

void TiltEq::prepare(double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    updateCoeff();
    reset();
}

void TiltEq::updateCoeff()
{
    // One-pole lowpass coefficient for the pivot: a = 1 - exp(-2*pi*fc/fs).
    const float fc = std::clamp(pivotHz, 20.0f, static_cast<float>(sampleRate) * 0.45f);
    lowCoeff = 1.0f - std::exp(-2.0f * kPi * fc / static_cast<float>(sampleRate));
}

void TiltEq::setParameters(float tiltDb, float newPivotHz)
{
    pivotHz = newPivotHz;
    updateCoeff();
    lowGain = std::pow(10.0f, -tiltDb / 20.0f);
    highGain = std::pow(10.0f, tiltDb / 20.0f);
}

void TiltEq::reset()
{
    for (float& s : lpState)
        s = 0.0f;
}

void TiltEq::processBlock(float* const* channels, int numChannels, int numSamples)
{
    if (!isEnabled || channels == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    const int chans = std::min(numChannels, kMaxChannels);
    for (int c = 0; c < chans; ++c)
    {
        float lp = lpState[c];
        float* x = channels[c];
        for (int n = 0; n < numSamples; ++n)
        {
            lp += lowCoeff * (x[n] - lp); // low band (one-pole lowpass)
            const float high = x[n] - lp; // high band (complementary)
            x[n] = lp * lowGain + high * highGain;
        }
        lpState[c] = lp;
    }
}

} // namespace cold::dsp
