#pragma once

// Framework-agnostic (no JUCE) waveform peak reducer.
//
// Turns a decoded sample into a compact min/max envelope for drawing: the
// source window is split into `numBins` equal columns and each column keeps the
// minimum and maximum mono-summed sample it spans. The editor paints one
// vertical line per bin (min..max), which is how a sampler shows a waveform
// without pushing every sample to the GUI. Kept in the DSP layer so it is
// testable with plain float buffers and reusable off the message thread.

#include <vector>

namespace cold::dsp
{

// A downsampled waveform: `minValues[i]`/`maxValues[i]` bound the samples in bin
// i (both in the source's amplitude range, typically [-1, 1]). Empty when there
// is nothing to draw.
struct WaveformPeaks
{
    std::vector<float> minValues;
    std::vector<float> maxValues;

    [[nodiscard]] int numBins() const { return static_cast<int>(minValues.size()); }
    [[nodiscard]] bool empty() const { return minValues.empty(); }
};

// Reduce `channels` (an array of `numChannels` pointers, each valid for at least
// `startFrame + numFrames` floats) to `numBins` min/max columns over the window
// [startFrame, startFrame + numFrames). Channels are averaged to mono first.
// Returns an empty result if the inputs are degenerate (no channels, no frames,
// or numBins <= 0). Each bin always covers at least one frame; when there are
// fewer frames than bins, later bins repeat the last frame so the envelope still
// spans the full width.
[[nodiscard]] WaveformPeaks computeWaveformPeaks(const float* const* channels, int numChannels,
                                                 int startFrame, int numFrames, int numBins);

} // namespace cold::dsp
