#pragma once

// Framework-agnostic (no JUCE) low-frequency oscillator.
//
// A modulation source for the mod matrix (M3): its bipolar output is scaled and
// routed to destinations including filter cutoff and, distinctively, the filter
// morph type. Output is in [-depth, depth] so the matrix can scale/offset per
// destination. Rate is in Hz; divisionToHz() converts a host-tempo division for
// sync (the processor reads Logic's BPM and feeds the Hz in).

#include <cstdint>

namespace cold::dsp
{

enum class LfoShape
{
    Sine,
    Triangle,
    SawUp,
    SawDown,
    Square,
    Random // stepped sample-and-hold, one new value per cycle
};

class Lfo
{
public:
    void setSampleRate(double sampleRate);
    void setShape(LfoShape shape);
    void setRate(float hz);
    void setDepth(float depth);       // [0, 1]
    void setPhaseOffset(float phase); // [0, 1], applied on reset/retrigger

    // reset() returns to the start phase and re-seeds the S&H value; retrigger()
    // restarts the phase for trig-mode use without disturbing the RNG stream more
    // than a fresh cycle would.
    void reset();
    void retrigger();

    // Advance one sample; returns the depth-scaled bipolar output for this sample.
    float getNextSample();

    [[nodiscard]] float currentPhase() const noexcept { return static_cast<float>(phase); }

    // Convert a host tempo (BPM) and cycle length in beats to a rate in Hz.
    // beatsPerCycle = 1 -> one cycle per quarter note; 4 -> per whole note.
    [[nodiscard]] static double divisionToHz(double bpm, double beatsPerCycle);

private:
    [[nodiscard]] float shapeValue(double p) const;
    float nextRandom();

    double sampleRate = 44100.0;
    LfoShape shape = LfoShape::Sine;
    float rateHz = 1.0f;
    float depth = 1.0f;
    double phaseOffset = 0.0;

    double phase = 0.0;
    double phaseInc = 0.0;
    float heldRandom = 0.0f;
    std::uint32_t rngState = 0x1234567u;
};

} // namespace cold::dsp
