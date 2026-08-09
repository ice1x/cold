#pragma once

// Framework-agnostic (no JUCE) low-frequency oscillator.
//
// A modulation source for the mod matrix (M3): its bipolar output is scaled and
// routed to destinations including filter cutoff and, distinctively, the filter
// morph type. Output is in [-depth, depth] so the matrix can scale/offset per
// destination. Rate is in Hz; divisionToHz() converts a host-tempo division for
// sync (the processor reads Logic's BPM and feeds the Hz in).

#include <cstdint>
#include <vector>

namespace cold::dsp
{

enum class LfoShape
{
    Sine,
    Triangle,
    SawUp,
    SawDown,
    Square,
    Random, // stepped sample-and-hold, one new value per cycle
    Custom  // user-drawn multi-step waveform (the LFO designer)
};

// How the LFO reacts to a note trigger (noteTrigger()). Free ignores trigs; Trig
// restarts its phase; Hold latches its output at each trig (freezes between);
// OneShot runs a single cycle per trig then holds the final value.
enum class LfoTrigMode
{
    Free,
    Trig,
    Hold,
    OneShot
};

class Lfo
{
public:
    void setSampleRate(double sampleRate);
    void setShape(LfoShape shape);
    void setRate(float hz);
    void setDepth(float depth);       // [0, 1]
    void setPhaseOffset(float phase); // [0, 1], applied on reset/retrigger
    void setTrigMode(LfoTrigMode mode);

    // The LFO-designer waveform: `count` points in [-1, 1] sampled uniformly over
    // one cycle, linearly interpolated (and wrapping). Selects the Custom shape.
    void setCustomShape(const float* points, int count);

    // reset() returns to the start phase and re-seeds the S&H value; retrigger()
    // unconditionally restarts the phase. noteTrigger() applies the trig-mode
    // behavior for an incoming note (the engine calls this on note-on).
    void reset();
    void retrigger();
    void noteTrigger();

    // Advance one sample; returns the depth-scaled bipolar output for this sample.
    float getNextSample();

    // Return the output at the current phase, then advance `numSamples`. Used for
    // block-rate modulation (one LFO value per rendered chunk, phase kept correct).
    float advance(int numSamples);

    [[nodiscard]] float currentPhase() const noexcept { return static_cast<float>(phase); }

    // Convert a host tempo (BPM) and cycle length in beats to a rate in Hz.
    // beatsPerCycle = 1 -> one cycle per quarter note; 4 -> per whole note.
    [[nodiscard]] static double divisionToHz(double bpm, double beatsPerCycle);

private:
    [[nodiscard]] float shapeValue(double p) const;
    float nextRandom();
    void advancePhase();

    double sampleRate = 44100.0;
    LfoShape shape = LfoShape::Sine;
    LfoTrigMode trigMode = LfoTrigMode::Free;
    float rateHz = 1.0f;
    float depth = 1.0f;
    double phaseOffset = 0.0;

    double phase = 0.0;
    double phaseInc = 0.0;
    float heldRandom = 0.0f;
    std::uint32_t rngState = 0x1234567u;

    std::vector<float> customPoints;
    float latched = 0.0f; // Hold output / OneShot final value
    bool running = true;  // OneShot: whether a cycle is in progress
};

} // namespace cold::dsp
