#pragma once

// Framework-agnostic (no JUCE) master brickwall peak limiter.
//
// Sits at the very end of the master chain to catch overs when many cells (and
// the delay/reverb returns) sum past 0 dBFS. Feed-forward and lookahead-free:
// each sample's gain is computed from that same sample's peak and applied to it,
// with an INSTANT attack (the gain drops immediately so the output never exceeds
// the ceiling) and an exponential release (the gain rises back gently to avoid
// pumping). Stereo-linked — one gain scales every channel so the image is kept.
// Bypassed by default; a fresh limiter passes audio through unchanged.

namespace cold::dsp
{

class Limiter
{
public:
    // Host sample rate; sets the release smoothing. Call before processing and
    // again on a sample-rate change (recomputes the coefficient).
    void prepare(double sampleRate);

    // ceiling in dBFS (the output is held at or below this level) and release in
    // seconds (how fast the gain recovers after a peak). Takes effect on the next
    // processed sample.
    void setParameters(float ceilingDb, float releaseSeconds);

    void setEnabled(bool shouldBeEnabled) { isEnabled = shouldBeEnabled; }
    [[nodiscard]] bool enabled() const noexcept { return isEnabled; }

    // Reset the gain to unity so the next block starts unattenuated.
    void reset();

    // Limit `channels` (an array of `numChannels` pointers, each `numSamples`
    // floats) in place. Stereo-linked: the detector follows the loudest channel
    // per sample and one gain scales them all. A no-op while disabled.
    void processBlock(float* const* channels, int numChannels, int numSamples);

    // The current gain reduction in dB (<= 0), for metering / tests.
    [[nodiscard]] float gainReductionDb() const noexcept;

private:
    double sampleRate = 44100.0;

    float ceilingLinear = 1.0f; // ceiling as a linear peak (10^(dB/20))
    float releaseCoeff = 1.0f;  // one-pole coefficient (rising gain)

    float gain = 1.0f; // current applied gain (<= 1)
    bool isEnabled = false;
};

} // namespace cold::dsp
