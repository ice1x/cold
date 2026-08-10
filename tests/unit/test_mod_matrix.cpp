#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/ModMatrix.h"

using Catch::Matchers::WithinAbs;
using cold::dsp::ModDestination;
using cold::dsp::ModMatrix;

TEST_CASE("ModMatrix with no routes passes the base values through", "[modmatrix][dsp][unit]")
{
    ModMatrix m;
    const auto out = m.apply(1000.0f, 0.3f, 0.9f); // source ignored: no routes
    REQUIRE_THAT(out.cutoffHz, WithinAbs(1000.0f, 1e-3));
    REQUIRE_THAT(out.type, WithinAbs(0.3f, 1e-6));
}

TEST_CASE("ModMatrix routes an LFO to filter cutoff exponentially", "[modmatrix][dsp][unit]")
{
    ModMatrix m;
    m.addRoute(ModDestination::FilterCutoff, 1.0f); // +/- 1 octave at full source

    // +1 source -> up an octave; -1 -> down an octave; 0 -> unchanged.
    REQUIRE_THAT(m.apply(1000.0f, 0.0f, 1.0f).cutoffHz, WithinAbs(2000.0f, 1e-2));
    REQUIRE_THAT(m.apply(1000.0f, 0.0f, -1.0f).cutoffHz, WithinAbs(500.0f, 1e-2));
    REQUIRE_THAT(m.apply(1000.0f, 0.0f, 0.0f).cutoffHz, WithinAbs(1000.0f, 1e-3));
}

TEST_CASE("ModMatrix filterMod returns raw octave/offset deltas", "[modmatrix][dsp][unit]")
{
    ModMatrix m;
    m.addRoute(ModDestination::FilterCutoff, 3.0f); // octaves at full source
    m.addRoute(ModDestination::FilterType, 0.4f);   // type offset at full source

    const auto fm = m.filterMod(0.5f);
    REQUIRE_THAT(fm.cutoffOctaves, WithinAbs(1.5f, 1e-6)); // 3.0 * 0.5
    REQUIRE_THAT(fm.typeOffset, WithinAbs(0.2f, 1e-6));    // 0.4 * 0.5

    // Deltas are raw (unclamped): a negative source flips the sign.
    const auto neg = m.filterMod(-1.0f);
    REQUIRE_THAT(neg.cutoffOctaves, WithinAbs(-3.0f, 1e-6));
    REQUIRE_THAT(neg.typeOffset, WithinAbs(-0.4f, 1e-6));
}

TEST_CASE("ModMatrix routes an LFO to the filter morph type", "[modmatrix][dsp][unit][morph]")
{
    ModMatrix m;
    m.addRoute(ModDestination::FilterType, 0.5f);

    REQUIRE_THAT(m.apply(1000.0f, 0.2f, 1.0f).type, WithinAbs(0.7f, 1e-5));  // 0.2 + 0.5
    REQUIRE_THAT(m.apply(1000.0f, 0.2f, -1.0f).type, WithinAbs(0.0f, 1e-5)); // 0.2 - 0.5 clamped
}

TEST_CASE("ModMatrix clamps the morph type to [0,1]", "[modmatrix][dsp][unit]")
{
    ModMatrix m;
    m.addRoute(ModDestination::FilterType, 1.0f);
    REQUIRE_THAT(m.apply(1000.0f, 0.8f, 1.0f).type, WithinAbs(1.0f, 1e-6));  // clamped high
    REQUIRE_THAT(m.apply(1000.0f, 0.1f, -1.0f).type, WithinAbs(0.0f, 1e-6)); // clamped low
}

TEST_CASE("ModMatrix applies cutoff and type routes independently", "[modmatrix][dsp][unit]")
{
    ModMatrix m;
    m.addRoute(ModDestination::FilterCutoff, 1.0f);
    m.addRoute(ModDestination::FilterType, 0.25f);

    const auto out = m.apply(1000.0f, 0.5f, 1.0f);
    REQUIRE_THAT(out.cutoffHz, WithinAbs(2000.0f, 1e-2)); // up an octave
    REQUIRE_THAT(out.type, WithinAbs(0.75f, 1e-5));       // 0.5 + 0.25
}

TEST_CASE("ModMatrix routes to pitch in semitones", "[modmatrix][dsp][unit][pitch]")
{
    ModMatrix m;
    m.addRoute(ModDestination::Pitch, 12.0f); // +/- an octave at full source

    REQUIRE_THAT(m.apply(1000.0f, 0.0f, 1.0f).pitchSemitones, WithinAbs(12.0f, 1e-5));
    REQUIRE_THAT(m.apply(1000.0f, 0.0f, -0.5f).pitchSemitones, WithinAbs(-6.0f, 1e-5));
    REQUIRE_THAT(m.apply(1000.0f, 0.0f, 0.0f).pitchSemitones, WithinAbs(0.0f, 1e-6));
}

TEST_CASE("ModMatrix routes to sample select as an offset", "[modmatrix][dsp][unit][sampleselect]")
{
    ModMatrix m;
    m.addRoute(ModDestination::SampleSelect, 4.0f); // up to +/-4 slots at full source

    REQUIRE_THAT(m.apply(1000.0f, 0.0f, 1.0f).sampleOffset, WithinAbs(4.0f, 1e-5));
    REQUIRE_THAT(m.apply(1000.0f, 0.0f, 0.5f).sampleOffset, WithinAbs(2.0f, 1e-5));
    // Other destinations stay at their base when only sample-select is routed.
    REQUIRE_THAT(m.apply(1000.0f, 0.3f, 1.0f).cutoffHz, WithinAbs(1000.0f, 1e-3));
    REQUIRE_THAT(m.apply(1000.0f, 0.3f, 1.0f).type, WithinAbs(0.3f, 1e-6));
}

TEST_CASE("ModMatrix clearRoutes removes all modulation", "[modmatrix][dsp][unit]")
{
    ModMatrix m;
    m.addRoute(ModDestination::FilterCutoff, 1.0f);
    REQUIRE(m.hasRoutes());
    m.clearRoutes();
    REQUIRE_FALSE(m.hasRoutes());
    REQUIRE_THAT(m.apply(1000.0f, 0.3f, 1.0f).cutoffHz, WithinAbs(1000.0f, 1e-3));
}
