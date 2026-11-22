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
    Random,      // stepped sample-and-hold, one new value per cycle (slew-able)
    Custom,      // user-drawn multi-step waveform (the LFO designer)
    Exponential, // unipolar 0..1: falls exponentially from 1 to 0
    Ramp         // unipolar 0..1: rises linearly from 0 to 1
};

// How the LFO reacts to a note trigger (noteTrigger()). Free
// ignores trigs; Trig restarts its phase; Hold latches its output at each trig
// (freezes between); OneShot runs a single cycle per trig then holds the final
// value; Half runs to the middle of the cycle then holds. A restarting mode (Trig /
// OneShot / Half) also draws a fresh Random value, so random is new on every hit.
enum class LfoTrigMode
{
    Free,
    Trig,
    Hold,
    OneShot,
    Half
};

class Lfo
{
public:
    void setSampleRate(double sampleRate);
    void setShape(LfoShape shape);
    // Rate in Hz; a NEGATIVE rate plays the cycle backwards.
    void setRate(float hz);
    void setDepth(float depth);       // [0, 1]
    void setPhaseOffset(float phase); // [0, 1], applied on reset/retrigger
    void setTrigMode(LfoTrigMode mode);

    // Random slew [0, 1]: the fraction of each step spent gliding from the previous
    // value to the new one (0 = stepped S&H).
    void setSlew(float slew);

    // Fade [-1, 1] after each note trig: negative fades the modulation in, positive
    // fades it out, 0 is off. |fade| = 1 spans four LFO cycles.
    void setFade(float fade);

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

    // The output at the current phase without advancing (used to read the source
    // at a note trig, e.g. for sample-select). Honors Hold/OneShot latching.
    [[nodiscard]] float peek() const;

    // Convert a host tempo (BPM) and cycle length in beats to a rate in Hz.
    // beatsPerCycle = 1 -> one cycle per quarter note; 4 -> per whole note.
    [[nodiscard]] static double divisionToHz(double bpm, double beatsPerCycle);

    // Speed x multiplier at a tempo, in Hz: speed x multiplier = 128 is one cycle per bar
    // (16 steps in 4/4). A negative speed gives a negative (backward) rate.
    [[nodiscard]] static double tempoRateHz(double speed, double multiplier, double bpm);

private:
    [[nodiscard]] float shapeValue(double p) const;
    [[nodiscard]] float fadeGain() const; // the fade envelope's current level
    float nextRandom();
    void stepRandom(); // move to the next Random value (the old one becomes the glide start)
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
    float prevRandom = 0.0f; // the value Random glides away from (slew)
    float slew = 0.0f;
    float fade = 0.0f;
    double fadeCounter = 0.0; // samples since the last trig (drives the fade)
    std::uint32_t rngState = 0x1234567u;

    std::vector<float> customPoints;
    float latched = 0.0f; // Hold output / OneShot final value
    bool running = true;  // OneShot / Half: whether the run is still in progress
};

} // namespace cold::dsp
