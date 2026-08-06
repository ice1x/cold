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

    // Cutoff in Hz (clamped to (0, ~0.49*fs)) and resonance/Q (clamped to a
    // stable positive range). Coefficients update lazily before the next sample.
    void setCutoff(float hz);
    void setResonance(float q);

    // Clear the integrator state (silence in, silence out).
    void reset();

    // Process one sample, returning all four outputs.
    Outputs processAll(float x);

    [[nodiscard]] float cutoff() const noexcept { return cutoffHz; }
    [[nodiscard]] float resonance() const noexcept { return resonanceQ; }

private:
    void updateCoefficients();

    double sampleRate = 44100.0;
    float cutoffHz = 1000.0f;
    float resonanceQ = 0.707f;
    bool coeffsDirty = true;

    // Coefficients (Cytomic SVF).
    double g = 0.0, k = 0.0, a1 = 0.0, a2 = 0.0, a3 = 0.0;

    // Integrator states.
    double ic1eq = 0.0, ic2eq = 0.0;
};

} // namespace cold::dsp
