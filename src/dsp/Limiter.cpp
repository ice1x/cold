#include "dsp/Limiter.h"

#include <algorithm>
#include <cmath>

namespace cold::dsp
{

namespace
{
// One-pole smoothing coefficient for a given time constant (seconds). A zero/tiny
// time gives an instantaneous coefficient (1 = no smoothing).
float onePoleCoeff(float seconds, double sampleRate)
{
    if (seconds <= 0.0f || sampleRate <= 0.0)
        return 1.0f;
    return 1.0f - std::exp(-1.0f / (seconds * static_cast<float>(sampleRate)));
}
} // namespace

void Limiter::prepare(double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    releaseCoeff = onePoleCoeff(0.1f, sampleRate);
    reset();
}

void Limiter::setParameters(float ceilingDb, float releaseSeconds)
{
    ceilingLinear = std::pow(10.0f, ceilingDb / 20.0f);
    releaseCoeff = onePoleCoeff(releaseSeconds, sampleRate);
}

void Limiter::reset()
{
    gain = 1.0f;
}

void Limiter::processBlock(float* const* channels, int numChannels, int numSamples)
{
    if (!isEnabled || channels == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    for (int n = 0; n < numSamples; ++n)
    {
        // Stereo-linked detector: the loudest channel this sample.
        float level = 0.0f;
        for (int c = 0; c < numChannels; ++c)
            level = std::max(level, std::abs(channels[c][n]));

        // The gain needed to hold this sample at the ceiling (1 when already under).
        const float target = (level > ceilingLinear) ? ceilingLinear / level : 1.0f;

        // Instant attack (clamp immediately so nothing exceeds the ceiling),
        // exponential release (recover gently once the peak passes).
        if (target < gain)
            gain = target;
        else
            gain += releaseCoeff * (target - gain);

        for (int c = 0; c < numChannels; ++c)
            channels[c][n] *= gain;
    }
}

float Limiter::gainReductionDb() const noexcept
{
    return 20.0f * std::log10(std::max(gain, 1.0e-9f));
}

} // namespace cold::dsp
