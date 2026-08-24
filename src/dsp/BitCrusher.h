#pragma once

// Framework-agnostic (no JUCE) bit crusher / sample-rate reducer (lo-fi / vintage).
//
// A per-track "cell" lo-fi effect for grit and vintage-sampler character: two
// classic degradations applied together.
//   - Bit depth: each sample is quantized to a coarse resolution (fewer bits =
//     bigger steps = more quantization noise).
//   - Sample-rate reduction: a sample-and-hold decimator holds every Nth input
//     sample, so the effective rate drops to sourceRate / N (aliasing + grit).
// Kept in the DSP layer so it is unit-testable with plain float buffers and
// reusable off the message thread. Bypassed by default.

#include <array>

namespace cold::dsp
{

class BitCrusher
{
public:
    // Sample rate is not needed for these integer-ratio degradations; kept for a
    // uniform DSP interface. Safe to call again on a rate change (resets state).
    void prepare(double sampleRate);

    // bits: effective bit depth (1..16; higher = cleaner). The signal snaps to
    // multiples of 1 / 2^(bits-1). downsample: sample-and-hold factor (>= 1; 1 =
    // no rate reduction). Takes effect on the next processed sample.
    void setParameters(float bits, int downsample);

    void setEnabled(bool shouldBeEnabled) { isEnabled = shouldBeEnabled; }
    [[nodiscard]] bool enabled() const noexcept { return isEnabled; }

    // Clear the sample-and-hold state so the next block captures afresh.
    void reset();

    // Degrade `channels` (an array of `numChannels` pointers, each `numSamples`
    // floats) in place. Channels share the decimation clock (they hold together)
    // but are quantized independently. A no-op while disabled.
    void processBlock(float* const* channels, int numChannels, int numSamples);

private:
    [[nodiscard]] float quantize(float x) const;

    static constexpr int kMaxChannels = 2;

    float scale = 32768.0f;                 // 2^(bits-1); the quantization grid
    int holdPeriod = 1;                     // sample-and-hold length (downsample factor)
    int holdCounter = 0;                    // position within the current hold window
    std::array<float, kMaxChannels> held{}; // last captured value per channel
    bool isEnabled = false;
};

} // namespace cold::dsp
