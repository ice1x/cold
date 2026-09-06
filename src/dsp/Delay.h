#pragma once

// Framework-agnostic (no JUCE) stereo delay line for a send/return effect.
//
// Fed by a per-cell send bus and mixed back into the master: it is a pure WET
// processor — processBlock replaces the input (the send signal) with the delayed
// signal, so the caller adds the result back at a return level. A one-pole low-pass
// in the feedback path ("tone") darkens successive repeats for an analogue feel.

#include <array>
#include <vector>

namespace cold::dsp
{

class Delay
{
public:
    // Allocate the delay line for up to `maxDelaySeconds`. Clears state.
    void prepare(double sampleRate, double maxDelaySeconds);

    // delaySeconds: echo time (clamped to the allocated max). feedback: 0..~0.99 (how
    // much of each echo feeds the next). tone: feedback damping in [0, 1] (0 = bright
    // repeats, 1 = very dark). Takes effect immediately.
    void setParameters(float delaySeconds, float feedback, float tone);

    void reset();

    // Replace `channels` (an array of `numChannels` pointers, each `numSamples`
    // floats) with the delayed (wet) signal in place. Up to two channels.
    void processBlock(float* const* channels, int numChannels, int numSamples);

private:
    static constexpr int kMaxChannels = 2;

    double sampleRate = 44100.0;
    int size = 0;         // delay-line length in samples (>= 1)
    int delaySamples = 1; // current read distance
    float feedback = 0.0f;
    float toneCoeff = 1.0f; // one-pole coefficient applied to the fed-back sample

    std::array<std::vector<float>, kMaxChannels> buffer;
    std::array<int, kMaxChannels> writePos{};
    std::array<float, kMaxChannels> lpState{};
};

} // namespace cold::dsp
