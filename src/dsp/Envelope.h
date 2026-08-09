#pragma once

// Framework-agnostic (no JUCE) DAHDSR envelope with per-segment curvature and
// velocity sensitivity. Higher-quality replacement/companion to the linear
// AmpEnvelope: it shapes amplitude, and — being a plain [0,1] generator — doubles
// as a modulation source (env -> cutoff / filter type / pitch / sample-select).
//
// Stages: Delay -> Attack -> Hold -> Decay -> Sustain -> Release. Segment lengths
// are driven by integer sample counts (sample-accurate, drift-free); the Attack/
// Decay/Release ramps are shaped by a curve in [-1, 1] (0 = linear, >0 convex /
// slow-start, <0 concave / fast-start) while still hitting their endpoints exactly.

namespace cold::dsp
{

class Envelope
{
public:
    enum class Stage
    {
        Idle,
        Delay,
        Attack,
        Hold,
        Decay,
        Sustain,
        Release
    };

    void setSampleRate(double sampleRate);

    // All times in seconds (>= 0); sustain a level in [0, 1].
    void setParameters(float delaySeconds, float attackSeconds, float holdSeconds,
                       float decaySeconds, float sustainLevel, float releaseSeconds);

    // Per-segment curvature in [-1, 1] for attack / decay / release.
    void setCurves(float attackCurve, float decayCurve, float releaseCurve);

    // How much note velocity scales the output: 0 = ignore velocity, 1 = output
    // scales linearly with velocity.
    void setVelocitySensitivity(float sensitivity);

    // Begin (or retrigger) a note; attack resumes from the current level. Velocity
    // in [0, 1] sets the velocity scaling for this note.
    void noteOn(float velocity = 1.0f);
    void noteOff();
    void reset();

    // Advance one sample; returns the shaped, velocity-scaled level in [0, 1].
    float getNextSample();

    [[nodiscard]] bool isActive() const noexcept { return stage != Stage::Idle; }
    [[nodiscard]] Stage currentStage() const noexcept { return stage; }
    [[nodiscard]] float level() const noexcept
    {
        return static_cast<float>(currentLevel) * velScale;
    }

private:
    void enterDelay();
    void enterAttack();
    void enterHold();
    void enterDecay();
    void enterRelease();
    void beginRamp(double target, float curve, int samples);
    [[nodiscard]] static float shape(double phase, float curve);

    double sampleRate = 44100.0;

    float delaySeconds = 0.0f;
    float attackSeconds = 0.0f;
    float holdSeconds = 0.0f;
    float decaySeconds = 0.0f;
    float sustainLevel = 1.0f;
    float releaseSeconds = 0.0f;

    float attackCurve = 0.0f;
    float decayCurve = 0.0f;
    float releaseCurve = 0.0f;
    float velocitySensitivity = 0.0f;
    float velScale = 1.0f;

    Stage stage = Stage::Idle;
    double currentLevel = 0.0;

    // Active ramp state.
    double rampStart = 0.0;
    double rampTarget = 0.0;
    float rampCurve = 0.0f;
    int rampSamples = 0;
    int rampPos = 0;
    int stageSamples = 0; // for the flat Delay/Hold stages
    int stagePos = 0;
};

} // namespace cold::dsp
