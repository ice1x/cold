#pragma once

// Framework-agnostic (no JUCE) feed-forward dynamics compressor.
//
// A per-track "cell" compressor for punch/glue on drums. Simple,
// robust design: a peak envelope follower with separate attack/release one-pole
// smoothing drives a hard-knee gain computer; the same gain is applied to every
// channel (stereo-linked) so the stereo image is preserved. Kept in the DSP layer
// so it is unit-testable with plain float buffers and reusable off the message
// thread. Bypassed by default — a fresh compressor passes audio through unchanged.

namespace cold::dsp
{

class Compressor
{
public:
    // Host sample rate; sets the attack/release smoothing. Call before processing
    // and again on a sample-rate change (recomputes the coefficients).
    void prepare(double sampleRate);

    // threshold in dBFS (level above which gain reduction begins), ratio >= 1
    // (input:output above the threshold), attack/release in seconds, makeup gain in
    // dB (applied after the reduction). Takes effect on the next processed sample.
    void setParameters(float thresholdDb, float ratio, float attackSeconds, float releaseSeconds,
                       float makeupDb);

    void setEnabled(bool shouldBeEnabled) { isEnabled = shouldBeEnabled; }
    [[nodiscard]] bool enabled() const noexcept { return isEnabled; }

    // Clear the envelope so the next block starts from silence.
    void reset();

    // Compress `channels` (an array of `numChannels` pointers, each `numSamples`
    // floats) in place. Stereo-linked: the detector follows the loudest channel per
    // sample and one gain scales them all. A no-op while disabled.
    void processBlock(float* const* channels, int numChannels, int numSamples);

    // The current gain reduction in dB (<= 0), for metering / tests.
    [[nodiscard]] float gainReductionDb() const noexcept { return currentReductionDb; }

private:
    double sampleRate = 44100.0;

    float thresholdDb = 0.0f;
    float ratio = 1.0f;
    float makeupDb = 0.0f;
    float attackCoeff = 1.0f;  // one-pole coefficient (rising level)
    float releaseCoeff = 1.0f; // one-pole coefficient (falling level)

    float envelope = 0.0f;           // linear peak envelope
    float currentReductionDb = 0.0f; // last computed gain reduction (<= 0)
    bool isEnabled = false;
};

} // namespace cold::dsp
