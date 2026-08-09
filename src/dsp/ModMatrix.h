#pragma once

// Framework-agnostic (no JUCE) modulation matrix.
//
// Routes a modulation source (an Lfo's bipolar output, or later an envelope) to
// filter destinations. The distinctive requirement (see TASKS.md / ARCHITECTURE):
// the filter **morph type** is a first-class destination, so an LFO can sweep the
// filter's character, not just its cutoff.
//
// Cutoff modulation is exponential (per-octave) so it tracks pitch/perception;
// type modulation is a linear offset in [0,1]. apply() is a pure transform of the
// base parameters given the current source value, which keeps it unit-testable.

#include <vector>

namespace cold::dsp
{

enum class ModDestination
{
    FilterCutoff, // amount is in octaves at full-scale source
    FilterType,   // amount is a linear offset added to the [0,1] morph type
    Pitch,        // amount is in semitones at full-scale source
    SampleSelect  // amount is a (fractional) slot/layer offset at full-scale source
};

class ModMatrix
{
public:
    struct Route
    {
        ModDestination destination;
        float amount; // signed depth for this route
    };

    // Modulated values for one source reading. cutoffHz/type feed the filter;
    // pitchSemitones offsets playback pitch; sampleOffset shifts the selected
    // slot/layer (rounded by the engine at trig time — LFO->sample-select).
    struct ModValues
    {
        float cutoffHz;
        float type;
        float pitchSemitones;
        float sampleOffset;
    };

    void addRoute(ModDestination destination, float amount);
    void clearRoutes();
    [[nodiscard]] bool hasRoutes() const noexcept { return !routes.empty(); }

    // Transform the base parameters by every route, given `sourceValue` (typically
    // an LFO or envelope output). cutoff is exponential (octaves) and kept
    // positive; type is a linear offset clamped to [0,1]; pitch is additive
    // semitones; sampleOffset is an additive slot/layer offset.
    [[nodiscard]] ModValues apply(float baseCutoffHz, float baseType, float sourceValue) const;

private:
    std::vector<Route> routes;
};

} // namespace cold::dsp
