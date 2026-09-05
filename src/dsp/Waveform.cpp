#include "dsp/Waveform.h"

#include "dsp/Envelope.h"

#include <algorithm>

namespace cold::dsp
{

std::vector<float> sampleEnvelopeShape(const EnvelopeShape& shape, double totalSeconds,
                                       int numColumns)
{
    std::vector<float> out;
    if (totalSeconds <= 0.0 || numColumns <= 0)
        return out;

    // Run the real envelope so the drawing matches what is heard. Sampling rate is
    // one step per column across the whole span, so column i is at time
    // i * totalSeconds / numColumns.
    Envelope env;
    env.setSampleRate(static_cast<double>(numColumns) / totalSeconds);
    env.setParameters(shape.delay, shape.attack, shape.hold, shape.decay, shape.sustain,
                      shape.release);
    env.setCurves(shape.attackCurve, shape.decayCurve, shape.releaseCurve);
    env.setVelocitySensitivity(0.0f); // full-scale contour, independent of velocity
    env.noteOn(1.0f);                 // held (no note-off): a one-shot plays to the end

    out.resize(static_cast<size_t>(numColumns));
    for (int i = 0; i < numColumns; ++i)
        out[static_cast<size_t>(i)] = std::clamp(env.getNextSample(), 0.0f, 1.0f);
    return out;
}

WaveformPeaks computeWaveformPeaks(const float* const* channels, int numChannels, int startFrame,
                                   int numFrames, int numBins)
{
    WaveformPeaks peaks;
    if (channels == nullptr || numChannels <= 0 || numFrames <= 0 || numBins <= 0)
        return peaks;

    peaks.minValues.resize(static_cast<size_t>(numBins));
    peaks.maxValues.resize(static_cast<size_t>(numBins));

    // Mono sample at frame f: average across channels.
    const float invCh = 1.0f / static_cast<float>(numChannels);
    const auto monoAt = [&](int f)
    {
        float sum = 0.0f;
        for (int c = 0; c < numChannels; ++c)
            sum += channels[c][startFrame + f];
        return sum * invCh;
    };

    for (int b = 0; b < numBins; ++b)
    {
        // Bin b covers [lo, hi) of the window, clamped so every bin gets >= 1
        // frame even when numFrames < numBins.
        int lo = static_cast<int>(static_cast<long long>(b) * numFrames / numBins);
        int hi = static_cast<int>(static_cast<long long>(b + 1) * numFrames / numBins);
        lo = std::min(lo, numFrames - 1);
        hi = std::max(hi, lo + 1);
        hi = std::min(hi, numFrames);

        float lowest = monoAt(lo);
        float highest = lowest;
        for (int f = lo + 1; f < hi; ++f)
        {
            const float v = monoAt(f);
            lowest = std::min(lowest, v);
            highest = std::max(highest, v);
        }
        peaks.minValues[static_cast<size_t>(b)] = lowest;
        peaks.maxValues[static_cast<size_t>(b)] = highest;
    }

    return peaks;
}

} // namespace cold::dsp
