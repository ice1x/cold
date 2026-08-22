#include "dsp/Saturator.h"

#include <algorithm>
#include <cmath>

namespace cold::dsp
{

void Saturator::prepare(double /*sampleRate*/)
{
    // Memoryless: nothing to precompute yet.
}

void Saturator::setParameters(float newDrive, float newMix)
{
    drive = std::max(1.0f, newDrive);
    mix = std::clamp(newMix, 0.0f, 1.0f);
}

void Saturator::processBlock(float* const* channels, int numChannels, int numSamples)
{
    if (!isEnabled || mix <= 0.0f || channels == nullptr || numChannels <= 0 || numSamples <= 0)
        return;

    const float dryMix = 1.0f - mix;
    for (int c = 0; c < numChannels; ++c)
    {
        float* data = channels[c];
        for (int n = 0; n < numSamples; ++n)
        {
            const float x = data[n];
            const float wet = std::tanh(drive * x); // soft-clip, bounded by +/-1
            data[n] = dryMix * x + mix * wet;
        }
    }
}

} // namespace cold::dsp
