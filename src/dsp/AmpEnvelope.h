#pragma once

// Framework-agnostic (no JUCE) linear ADSR amplitude envelope.
//
// One instance per SampleVoice shapes that voice's output. Stages are linear
// ramps advanced one output sample at a time (sample-accurate). Retriggering
// (noteOn while still sounding) restarts the attack from the current level, so a
// re-struck voice glides up rather than clicking to zero.

namespace cold::dsp
{

class AmpEnvelope
{
public:
    enum class Stage
    {
        Idle,
        Attack,
        Decay,
        Sustain,
        Release
    };

    // Output sample rate; stage times are interpreted against it.
    void setSampleRate(double sampleRate);

    // attack/decay/release in seconds (>= 0); sustain is a level in [0, 1].
    void setParameters(float attackSeconds, float decaySeconds, float sustainLevel,
                       float releaseSeconds);

    // Begin (or retrigger) the note. Attack starts from the current level.
    void noteOn();

    // Enter the release stage from the current level.
    void noteOff();

    // Return to idle silence immediately.
    void reset();

    // Advance one output sample and return the new envelope level in [0, 1].
    float getNextSample();

    [[nodiscard]] bool isActive() const noexcept { return stage != Stage::Idle; }
    [[nodiscard]] Stage currentStage() const noexcept { return stage; }
    [[nodiscard]] float level() const noexcept { return static_cast<float>(currentLevel); }

private:
    void startDecay();

    double sampleRate = 44100.0;
    float attackSeconds = 0.0f;
    float decaySeconds = 0.0f;
    float sustainLevel = 1.0f;
    float releaseSeconds = 0.0f;

    Stage stage = Stage::Idle;
    double currentLevel = 0.0;
    double increment = 0.0;   // per-sample delta for the active ramp stage
    int samplesRemaining = 0; // samples left in the active ramp stage
};

} // namespace cold::dsp
