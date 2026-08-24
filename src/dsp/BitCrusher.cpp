#include "dsp/BitCrusher.h"

#include <algorithm>
#include <cmath>

namespace cold::dsp
{

void BitCrusher::prepare(double)
{
    reset();
}

void BitCrusher::setParameters(float bits, int downsample)
{
    const float clampedBits = std::clamp(bits, 1.0f, 16.0f);
    scale = std::pow(2.0f, clampedBits - 1.0f);
    holdPeriod = std::max(1, downsample);
}

void BitCrusher::reset()
{
    holdCounter = 0;
    held.fill(0.0f);
}

float BitCrusher::quantize(float x) const
{
    // Snap to the nearest step of 1 / scale, then bound to the valid signal range.
    return std::clamp(std::round(x * scale) / scale, -1.0f, 1.0f);
}

void BitCrusher::processBlock(float* const* channels, int numChannels, int numSamples)
{
    if (!isEnabled)
        return;

    const int chans = std::min(numChannels, kMaxChannels);
    for (int n = 0; n < numSamples; ++n)
    {
        // Sample-and-hold: capture a fresh input at the start of each hold window,
        // otherwise repeat the held value (the decimator's zero-order hold).
        if (holdCounter == 0)
            for (int c = 0; c < chans; ++c)
                held[static_cast<size_t>(c)] = channels[c][n];

        for (int c = 0; c < chans; ++c)
            channels[c][n] = quantize(held[static_cast<size_t>(c)]);

        if (++holdCounter >= holdPeriod)
            holdCounter = 0;
    }
}

} // namespace cold::dsp
