#include "dsp/Delay.h"

#include <algorithm>
#include <cmath>

namespace cold::dsp
{

void Delay::prepare(double newSampleRate, double maxDelaySeconds)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    const double maxSec = std::max(0.001, maxDelaySeconds);
    size = std::max(1, static_cast<int>(std::ceil(maxSec * sampleRate)) + 1);
    for (auto& b : buffer)
        b.assign(static_cast<size_t>(size), 0.0f);
    reset();
}

void Delay::setParameters(float delaySeconds, float newFeedback, float tone)
{
    const int wanted = static_cast<int>(std::lround(std::max(0.0f, delaySeconds) * sampleRate));
    delaySamples = std::clamp(wanted, 1, std::max(1, size - 1));
    feedback = std::clamp(newFeedback, 0.0f, 0.99f);
    // tone 0 = bright (pass the fed-back sample straight through, coeff 1); tone 1 =
    // very dark (coeff -> 0 freezes the low-pass state).
    toneCoeff = 1.0f - std::clamp(tone, 0.0f, 1.0f);
}

void Delay::reset()
{
    for (auto& b : buffer)
        std::fill(b.begin(), b.end(), 0.0f);
    writePos.fill(0);
    lpState.fill(0.0f);
}

void Delay::processBlock(float* const* channels, int numChannels, int numSamples)
{
    const int chans = std::min(numChannels, kMaxChannels);
    for (int c = 0; c < chans; ++c)
    {
        auto& line = buffer[static_cast<size_t>(c)];
        int wp = writePos[static_cast<size_t>(c)];
        float lp = lpState[static_cast<size_t>(c)];

        for (int n = 0; n < numSamples; ++n)
        {
            int rp = wp - delaySamples;
            if (rp < 0)
                rp += size;
            const float delayed = line[static_cast<size_t>(rp)];

            // Low-pass the fed-back sample for darker successive repeats.
            lp += toneCoeff * (delayed - lp);
            line[static_cast<size_t>(wp)] = channels[c][n] + feedback * lp;

            channels[c][n] = delayed; // wet output (100% wet; caller adds at return level)

            if (++wp >= size)
                wp = 0;
        }

        writePos[static_cast<size_t>(c)] = wp;
        lpState[static_cast<size_t>(c)] = lp;
    }
}

} // namespace cold::dsp
