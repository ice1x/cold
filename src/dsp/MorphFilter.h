#pragma once

// Framework-agnostic (no JUCE) morphing multimode filter.
//
// A TPT / Zavalishin state-variable filter (Cytomic "SVF" topology). One filter
// produces low-pass, band-pass, high-pass and notch outputs simultaneously from
// the same internal state, which makes continuous type-morphing (000020/000021)
// cheap and stable even while the cutoff is modulated. See docs/DSP_NOTES.md.

namespace cold::dsp
{

class MorphFilter
{
public:
    // The four simultaneous SVF outputs for one input sample.
    struct Outputs
    {
        float lp = 0.0f;
        float bp = 0.0f;
        float hp = 0.0f;
        float notch = 0.0f;
    };

    // Sample rate; also clamps/refreshes the coefficients. Clears state.
    void prepare(double sampleRate);

    // Targets for cutoff in Hz (clamped to (1, ~0.49*fs)), resonance/Q (clamped to
    // a stable positive range), and the morph type in [0, 1]. These are one-pole
    // smoothed per sample so modulating them (block-rate or from an LFO) does not
    // produce zipper noise.
    void setCutoff(float hz);
    void setResonance(float q);
    void setFilterType(float type);

    // Clear the integrator state (silence in, silence out).
    void reset();

    // Process one sample. processAll returns the four raw outputs; process returns
    // the single morphed output selected by the (smoothed) filter type.
    Outputs processAll(float x);
    float process(float x);

    // Blend the four outputs along LP -> BP -> HP -> Notch for type in [0, 1].
    // Static + branchless-ish so it can be unit-tested and reused directly.
    [[nodiscard]] static float morph(const Outputs& o, float type);

    [[nodiscard]] float cutoff() const noexcept { return targetCutoff; }
    [[nodiscard]] float resonance() const noexcept { return targetResonance; }
    [[nodiscard]] float filterType() const noexcept { return targetType; }

    [[nodiscard]] float smoothedCutoff() const noexcept { return static_cast<float>(smCutoff); }
    [[nodiscard]] float smoothedResonance() const noexcept
    {
        return static_cast<float>(smResonance);
    }
    [[nodiscard]] float smoothedFilterType() const noexcept { return static_cast<float>(smType); }

private:
    Outputs computeOutputs(float x);

    double sampleRate = 44100.0;

    // Smoothing targets and their smoothed followers.
    float targetCutoff = 1000.0f;
    float targetResonance = 0.707f;
    float targetType = 0.0f;
    double smCutoff = 1000.0;
    double smResonance = 0.707;
    double smType = 0.0;
    double smoothCoeff = 1.0; // per-sample one-pole coefficient

    // Integrator states.
    double ic1eq = 0.0, ic2eq = 0.0;
};

} // namespace cold::dsp
