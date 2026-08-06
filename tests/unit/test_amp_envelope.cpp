#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/AmpEnvelope.h"

using Catch::Matchers::WithinAbs;
using cold::dsp::AmpEnvelope;

namespace
{
// 1 kHz sample rate makes seconds-to-samples arithmetic exact and readable.
constexpr double kSr = 1000.0;
} // namespace

TEST_CASE("AmpEnvelope is idle and silent before a note", "[ampenv][dsp][unit]")
{
    AmpEnvelope env;
    env.setSampleRate(kSr);
    REQUIRE_FALSE(env.isActive());
    REQUIRE_THAT(env.getNextSample(), WithinAbs(0.0f, 1e-9));
}

TEST_CASE("AmpEnvelope attack ramps 0->1 sample-accurately", "[ampenv][dsp][unit]")
{
    AmpEnvelope env;
    env.setSampleRate(kSr);
    env.setParameters(0.004f, 0.0f, 1.0f, 0.0f); // 4-sample attack, hold at 1
    env.noteOn();
    REQUIRE(env.isActive());

    const float expected[] = {0.25f, 0.5f, 0.75f, 1.0f};
    for (float e : expected)
        REQUIRE_THAT(env.getNextSample(), WithinAbs(e, 1e-6));

    // Then it sustains at 1.0.
    REQUIRE_THAT(env.getNextSample(), WithinAbs(1.0f, 1e-6));
}

TEST_CASE("AmpEnvelope decays to the sustain level and holds", "[ampenv][dsp][unit]")
{
    AmpEnvelope env;
    env.setSampleRate(kSr);
    env.setParameters(0.004f, 0.004f, 0.5f, 0.0f); // A=4, D=4, S=0.5
    env.noteOn();

    // Attack: 0.25 .. 1.0
    for (float e : {0.25f, 0.5f, 0.75f, 1.0f})
        REQUIRE_THAT(env.getNextSample(), WithinAbs(e, 1e-6));
    // Decay 1.0 -> 0.5 over 4 samples.
    for (float e : {0.875f, 0.75f, 0.625f, 0.5f})
        REQUIRE_THAT(env.getNextSample(), WithinAbs(e, 1e-6));
    // Sustain holds.
    REQUIRE_THAT(env.getNextSample(), WithinAbs(0.5f, 1e-6));
    REQUIRE(env.isActive());
}

TEST_CASE("AmpEnvelope release ramps to zero and frees", "[ampenv][dsp][unit]")
{
    AmpEnvelope env;
    env.setSampleRate(kSr);
    env.setParameters(0.0f, 0.0f, 1.0f, 0.004f); // instant to sustain=1, 4-sample release
    env.noteOn();
    REQUIRE_THAT(env.getNextSample(), WithinAbs(1.0f, 1e-6)); // sustaining at 1

    env.noteOff();
    const float expected[] = {0.75f, 0.5f, 0.25f, 0.0f};
    for (float e : expected)
        REQUIRE_THAT(env.getNextSample(), WithinAbs(e, 1e-6));

    REQUIRE_FALSE(env.isActive()); // release complete
    REQUIRE_THAT(env.getNextSample(), WithinAbs(0.0f, 1e-9));
}

TEST_CASE("AmpEnvelope with zero-length stages is transparent", "[ampenv][dsp][unit]")
{
    AmpEnvelope env;
    env.setSampleRate(kSr);
    env.setParameters(0.0f, 0.0f, 1.0f, 0.0f); // A=D=R=0, S=1
    env.noteOn();

    REQUIRE(env.isActive());
    REQUIRE_THAT(env.getNextSample(), WithinAbs(1.0f, 1e-9));

    env.noteOff();
    REQUIRE_FALSE(env.isActive()); // instant release
}

TEST_CASE("AmpEnvelope retriggers from the current level", "[ampenv][dsp][unit]")
{
    AmpEnvelope env;
    env.setSampleRate(kSr);
    env.setParameters(0.004f, 0.0f, 1.0f, 0.008f); // A=4, S=1, R=8
    env.noteOn();
    for (int i = 0; i < 4; ++i)
        env.getNextSample(); // climb to 1.0 (sustain)

    env.noteOff();
    env.getNextSample();                               // 0.875
    const float afterTwoRelease = env.getNextSample(); // 0.75
    REQUIRE_THAT(afterTwoRelease, WithinAbs(0.75f, 1e-6));

    // Retrigger mid-release: attack restarts from the current 0.75, not from 0.
    env.noteOn();
    const float first = env.getNextSample();
    REQUIRE(first > 0.75f);                        // rising again
    REQUIRE_THAT(first, WithinAbs(0.8125f, 1e-6)); // 0.75 + (1-0.75)/4
    for (int i = 0; i < 3; ++i)
        env.getNextSample();
    REQUIRE(env.isActive());
}
