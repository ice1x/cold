#pragma once

// Framework-agnostic (no JUCE) waveshaping saturator / drive.
//
// A per-track "cell" saturator for warmth and grit (Battery-style FX). Memoryless
// tanh soft-clip: the signal is pre-gained by `drive` and shaped by tanh (so small
// signals pass ~linearly at unity drive and larger ones round off toward +/-1,
// adding harmonics), then blended with the dry signal by `mix`. Kept in the DSP
// layer so it is unit-testable with plain float buffers and reusable off the
// message thread. Bypassed by default — a fresh saturator passes audio unchanged.

namespace cold::dsp
{

class Saturator
{
public:
    // Sample rate is not needed for a memoryless shaper; kept for a uniform DSP
    // interface (and future oversampling). Safe to call again on a rate change.
    void prepare(double sampleRate);

    // drive: linear pre-gain into the shaper (>= 1; 1 = gentlest). mix: dry/wet in
    // [0, 1] (0 = fully dry passthrough, 1 = fully shaped). Takes effect on the
    // next processed sample.
    void setParameters(float drive, float mix);

    void setEnabled(bool shouldBeEnabled) { isEnabled = shouldBeEnabled; }
    [[nodiscard]] bool enabled() const noexcept { return isEnabled; }

    void reset() {}

    // Shape `channels` (an array of `numChannels` pointers, each `numSamples`
    // floats) in place. Channels are independent (waveshaping is memoryless). A
    // no-op while disabled or at mix 0.
    void processBlock(float* const* channels, int numChannels, int numSamples);

private:
    float drive = 1.0f;
    float mix = 1.0f;
    bool isEnabled = false;
};

} // namespace cold::dsp
