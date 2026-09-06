#pragma once

// Framework-agnostic (no JUCE) Schroeder/Freeverb-style reverb for a send/return.
//
// Eight damped comb filters in parallel into four all-pass diffusers per channel
// (the classic Freeverb topology), which gives a smooth, cheap tail. Like the
// delay it is a pure WET processor: processBlock replaces the send signal with the
// reverberated signal, so the caller mixes the result back at a return level.

#include <array>
#include <vector>

namespace cold::dsp
{

class Reverb
{
public:
    void prepare(double sampleRate);

    // roomSize: tail length 0..1 (bigger = longer). damping: high-frequency decay
    // 0..1 (bigger = darker tail). Takes effect immediately.
    void setParameters(float roomSize, float damping);

    void reset();

    // Replace `channels` (an array of `numChannels` pointers, each `numSamples`
    // floats) with the wet reverb in place. Up to two channels.
    void processBlock(float* const* channels, int numChannels, int numSamples);

private:
    static constexpr int kNumCombs = 8;
    static constexpr int kNumAllpass = 4;
    static constexpr int kMaxChannels = 2;

    struct Comb
    {
        std::vector<float> buffer;
        int pos = 0;
        float store = 0.0f; // damping low-pass state
        float process(float input, float feedback, float damp);
        void reset();
    };
    struct Allpass
    {
        std::vector<float> buffer;
        int pos = 0;
        float process(float input);
        void reset();
    };

    std::array<std::array<Comb, kNumCombs>, kMaxChannels> combs;
    std::array<std::array<Allpass, kNumAllpass>, kMaxChannels> allpass;

    float feedback = 0.84f; // from roomSize
    float damp = 0.2f;      // from damping
};

} // namespace cold::dsp
