#pragma once

// Framework-agnostic (no JUCE) master tilt EQ (spectral tilt tone control).
//
// A single "tilt" knob shapes the overall balance: positive tilt lifts the highs
// and drops the lows (brighter), negative does the reverse (darker/warmer), and
// zero is exactly flat. Implemented as a first-order tilt — a one-pole crossover
// at a pivot frequency splits the signal into a low band (the lowpass) and a high
// band (input minus the lowpass); the two are recombined with complementary gains
// (low = 10^(-tilt/20), high = 10^(+tilt/20)), so at tilt 0 both are unity and the
// output equals the input. Each channel keeps its own filter state, so the stereo
// image is untouched. Bypassed by default — a fresh tilt EQ passes audio through.

namespace cold::dsp
{

class TiltEq
{
public:
    static constexpr int kMaxChannels = 2;

    // Host sample rate; sets the crossover coefficient. Call before processing and
    // again on a sample-rate change (recomputes from the current pivot).
    void prepare(double sampleRate);

    // tilt in dB (the amount of lift/cut at the extremes; +bright / -dark, 0 flat)
    // and the crossover pivot in Hz (the balance point the tilt rotates around).
    // Takes effect on the next processed sample.
    void setParameters(float tiltDb, float pivotHz);

    void setEnabled(bool shouldBeEnabled) { isEnabled = shouldBeEnabled; }
    [[nodiscard]] bool enabled() const noexcept { return isEnabled; }

    // Clear the crossover state so the next block starts clean.
    void reset();

    // Tilt `channels` (an array of `numChannels` pointers, each `numSamples`
    // floats) in place. Channels are filtered independently. A no-op while disabled.
    void processBlock(float* const* channels, int numChannels, int numSamples);

private:
    void updateCoeff();

    double sampleRate = 44100.0;
    float pivotHz = 700.0f;
    float lowCoeff = 0.0f; // one-pole lowpass coefficient at the pivot
    float lowGain = 1.0f;  // gain applied to the low band  (10^(-tilt/20))
    float highGain = 1.0f; // gain applied to the high band (10^(+tilt/20))

    float lpState[kMaxChannels] = {0.0f, 0.0f}; // per-channel one-pole state
    bool isEnabled = false;
};

} // namespace cold::dsp
