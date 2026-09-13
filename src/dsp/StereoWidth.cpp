#include "dsp/StereoWidth.h"

#include <algorithm>

namespace cold::dsp
{

void StereoWidth::setWidth(float newWidth)
{
    width = std::max(0.0f, newWidth);
}

void StereoWidth::processBlock(float* const* channels, int numChannels, int numSamples)
{
    if (!isEnabled || channels == nullptr || numChannels < 2 || numSamples <= 0)
        return;

    float* left = channels[0];
    float* right = channels[1];
    for (int n = 0; n < numSamples; ++n)
    {
        const float mid = 0.5f * (left[n] + right[n]);
        const float side = 0.5f * (left[n] - right[n]) * width;
        left[n] = mid + side;
        right[n] = mid - side;
    }
}

} // namespace cold::dsp
