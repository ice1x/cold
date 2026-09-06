#include "dsp/Reverb.h"

#include <algorithm>
#include <cmath>

namespace cold::dsp
{
namespace
{
// Freeverb's tuned delay lengths (in samples at 44.1 kHz); scaled to the host rate
// in prepare(). The right channel is offset by kStereoSpread for a wider image.
constexpr std::array<int, 8> kCombTuning{1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr std::array<int, 4> kAllpassTuning{556, 441, 341, 225};
constexpr int kStereoSpread = 23;
constexpr float kFixedGain = 0.015f; // input scaling into the comb bank
constexpr float kAllpassFeedback = 0.5f;
} // namespace

float Reverb::Comb::process(float input, float fb, float damping)
{
    const float output = buffer[static_cast<size_t>(pos)];
    store = output * (1.0f - damping) + store * damping;
    buffer[static_cast<size_t>(pos)] = input + store * fb;
    if (++pos >= static_cast<int>(buffer.size()))
        pos = 0;
    return output;
}

void Reverb::Comb::reset()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    pos = 0;
    store = 0.0f;
}

float Reverb::Allpass::process(float input)
{
    const float bufout = buffer[static_cast<size_t>(pos)];
    const float output = -input + bufout;
    buffer[static_cast<size_t>(pos)] = input + bufout * kAllpassFeedback;
    if (++pos >= static_cast<int>(buffer.size()))
        pos = 0;
    return output;
}

void Reverb::Allpass::reset()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    pos = 0;
}

void Reverb::prepare(double sampleRate)
{
    const double scale = (sampleRate > 0.0 ? sampleRate : 44100.0) / 44100.0;
    for (int c = 0; c < kMaxChannels; ++c)
    {
        const int spread = (c == 1) ? kStereoSpread : 0;
        for (int i = 0; i < kNumCombs; ++i)
        {
            const int len =
                std::max(1, static_cast<int>(std::lround(
                                (kCombTuning[static_cast<size_t>(i)] + spread) * scale)));
            combs[static_cast<size_t>(c)][static_cast<size_t>(i)].buffer.assign(
                static_cast<size_t>(len), 0.0f);
        }
        for (int i = 0; i < kNumAllpass; ++i)
        {
            const int len =
                std::max(1, static_cast<int>(std::lround(
                                (kAllpassTuning[static_cast<size_t>(i)] + spread) * scale)));
            allpass[static_cast<size_t>(c)][static_cast<size_t>(i)].buffer.assign(
                static_cast<size_t>(len), 0.0f);
        }
    }
    reset();
}

void Reverb::setParameters(float roomSize, float damping)
{
    // Freeverb scaling: feedback in ~[0.7, 0.98], damping in [0, 0.4].
    feedback = 0.7f + std::clamp(roomSize, 0.0f, 1.0f) * 0.28f;
    damp = std::clamp(damping, 0.0f, 1.0f) * 0.4f;
}

void Reverb::reset()
{
    for (auto& chanCombs : combs)
        for (auto& c : chanCombs)
            c.reset();
    for (auto& chanAp : allpass)
        for (auto& a : chanAp)
            a.reset();
}

void Reverb::processBlock(float* const* channels, int numChannels, int numSamples)
{
    const int chans = std::min(numChannels, kMaxChannels);
    for (int c = 0; c < chans; ++c)
    {
        auto& cc = combs[static_cast<size_t>(c)];
        auto& ap = allpass[static_cast<size_t>(c)];
        for (int n = 0; n < numSamples; ++n)
        {
            const float input = channels[c][n] * kFixedGain;
            float out = 0.0f;
            for (auto& comb : cc)
                out += comb.process(input, feedback, damp);
            for (auto& a : ap)
                out = a.process(out);
            channels[c][n] = out; // pure wet
        }
    }
}

} // namespace cold::dsp
