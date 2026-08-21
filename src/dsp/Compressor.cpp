#include "dsp/Compressor.h"

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

void Compressor::prepare(double newSampleRate)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    reset();
}

void Compressor::setParameters(float newThresholdDb, float newRatio, float attackSeconds,
                               float releaseSeconds, float newMakeupDb)
{
    thresholdDb = newThresholdDb;
    ratio = std::max(1.0f, newRatio);
    makeupDb = newMakeupDb;
    attackCoeff = onePoleCoeff(attackSeconds, sampleRate);
    releaseCoeff = onePoleCoeff(releaseSeconds, sampleRate);
}

void Compressor::reset()
{
    envelope = 0.0f;
    currentReductionDb = 0.0f;
}

void Compressor::processBlock(float* const* channels, int numChannels, int numSamples)
{
    if (!isEnabled || channels == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    const float slope = 1.0f - 1.0f / ratio; // dB of reduction per dB over threshold

    for (int n = 0; n < numSamples; ++n)
    {
        // Stereo-linked detector: the loudest channel this sample.
        float level = 0.0f;
        for (int c = 0; c < numChannels; ++c)
            level = std::max(level, std::abs(channels[c][n]));

        // Peak envelope with separate attack (rising) / release (falling) smoothing.
        const float coeff = (level > envelope) ? attackCoeff : releaseCoeff;
        envelope += coeff * (level - envelope);

        // Hard-knee gain computer in dB.
        const float envDb = 20.0f * std::log10(std::max(envelope, 1.0e-9f));
        const float over = envDb - thresholdDb;
        currentReductionDb = over > 0.0f ? -over * slope : 0.0f;

        const float gain = std::pow(10.0f, (currentReductionDb + makeupDb) / 20.0f);
        for (int c = 0; c < numChannels; ++c)
            channels[c][n] *= gain;
    }
}

} // namespace cold::dsp
