#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/Envelope.h"

using Catch::Matchers::WithinAbs;
using cold::dsp::Envelope;

namespace
{
constexpr double kSr = 1000.0; // exact seconds->samples

// Configure a plain ADSR (no delay/hold, linear) for baseline checks.
Envelope makeAdsr(float a, float d, float s, float r)
{
    Envelope env;
    env.setSampleRate(kSr);
    env.setParameters(0.0f, a, 0.0f, d, s, r); // delay, attack, hold, decay, sustain, release
    return env;
}
} // namespace

TEST_CASE("Envelope is idle and silent before a note", "[envelope][dsp][unit]")
{
    Envelope env;
    env.setSampleRate(kSr);
    REQUIRE_FALSE(env.isActive());
    REQUIRE_THAT(env.getNextSample(), WithinAbs(0.0f, 1e-9));
}

TEST_CASE("Envelope linear attack matches a straight ramp", "[envelope][dsp][unit]")
{
    auto env = makeAdsr(0.004f, 0.0f, 1.0f, 0.0f); // 4-sample attack, curves default linear
    env.noteOn();
    for (float e : {0.25f, 0.5f, 0.75f, 1.0f})
        REQUIRE_THAT(env.getNextSample(), WithinAbs(e, 1e-5));
    REQUIRE_THAT(env.getNextSample(), WithinAbs(1.0f, 1e-5)); // sustain hold
}

TEST_CASE("Envelope DAHDSR runs delay and hold stages", "[envelope][dsp][unit]")
{
    Envelope env;
    env.setSampleRate(kSr);
    // delay 2, attack 2, hold 2, decay 2, sustain 0.5, release 2
    env.setParameters(0.002f, 0.002f, 0.002f, 0.002f, 0.5f, 0.002f);
    env.noteOn();

    // Delay: two samples of silence while active.
    REQUIRE(env.isActive());
    REQUIRE_THAT(env.getNextSample(), WithinAbs(0.0f, 1e-6));
    REQUIRE_THAT(env.getNextSample(), WithinAbs(0.0f, 1e-6));
    // Attack 0->1 over 2 samples.
    REQUIRE_THAT(env.getNextSample(), WithinAbs(0.5f, 1e-5));
    REQUIRE_THAT(env.getNextSample(), WithinAbs(1.0f, 1e-5));
    // Hold at 1 for 2 samples.
    REQUIRE_THAT(env.getNextSample(), WithinAbs(1.0f, 1e-6));
    REQUIRE_THAT(env.getNextSample(), WithinAbs(1.0f, 1e-6));
    // Decay 1->0.5 over 2 samples.
    REQUIRE_THAT(env.getNextSample(), WithinAbs(0.75f, 1e-5));
    REQUIRE_THAT(env.getNextSample(), WithinAbs(0.5f, 1e-5));
    // Sustain holds.
    REQUIRE_THAT(env.getNextSample(), WithinAbs(0.5f, 1e-6));
}

TEST_CASE("Envelope release ramps to zero and frees", "[envelope][dsp][unit]")
{
    auto env = makeAdsr(0.0f, 0.0f, 1.0f, 0.004f); // instant to sustain 1, 4-sample release
    env.noteOn();
    REQUIRE_THAT(env.getNextSample(), WithinAbs(1.0f, 1e-6));

    env.noteOff();
    for (float e : {0.75f, 0.5f, 0.25f, 0.0f})
        REQUIRE_THAT(env.getNextSample(), WithinAbs(e, 1e-5));
    REQUIRE_FALSE(env.isActive());
}

TEST_CASE("Envelope positive attack curve is convex (slow start)", "[envelope][dsp][unit][curve]")
{
    Envelope env;
    env.setSampleRate(kSr);
    env.setParameters(0.0f, 0.010f, 0.0f, 0.0f, 1.0f, 0.0f); // 10-sample attack
    env.setCurves(1.0f, 0.0f, 0.0f);                         // strong convex attack
    env.noteOn();

    for (int i = 0; i < 4; ++i)
        env.getNextSample();
    const float mid = env.getNextSample(); // ~halfway through attack (sample 5/10)
    REQUIRE(mid < 0.5f);                   // convex: below the linear midpoint
    REQUIRE(mid > 0.0f);
}

TEST_CASE("Envelope curves keep the segment endpoints exact", "[envelope][dsp][unit][curve]")
{
    Envelope env;
    env.setSampleRate(kSr);
    env.setParameters(0.0f, 0.004f, 0.0f, 0.0f, 1.0f, 0.0f);
    env.setCurves(-0.8f, 0.0f, 0.0f); // concave attack
    env.noteOn();

    float last = 0.0f;
    for (int i = 0; i < 4; ++i)
        last = env.getNextSample();
    REQUIRE_THAT(last, WithinAbs(1.0f, 1e-4)); // still reaches exactly 1 at the end
}

TEST_CASE("Envelope velocity sensitivity scales the output", "[envelope][dsp][unit][vel]")
{
    Envelope env;
    env.setSampleRate(kSr);
    env.setParameters(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f); // instant to sustain 1
    env.setVelocitySensitivity(1.0f);                      // fully velocity-dependent
    env.noteOn(0.5f);
    REQUIRE_THAT(env.getNextSample(), WithinAbs(0.5f, 1e-5)); // 1.0 * 0.5

    env.setVelocitySensitivity(0.0f); // velocity-independent
    env.noteOn(0.5f);
    REQUIRE_THAT(env.getNextSample(), WithinAbs(1.0f, 1e-5));
}

TEST_CASE("Envelope retriggers from the current level", "[envelope][dsp][unit]")
{
    auto env = makeAdsr(0.004f, 0.0f, 1.0f, 0.008f);
    env.noteOn();
    for (int i = 0; i < 4; ++i)
        env.getNextSample(); // reach sustain 1
    env.noteOff();
    env.getNextSample();                   // 0.875
    const float mid = env.getNextSample(); // 0.75
    REQUIRE_THAT(mid, WithinAbs(0.75f, 1e-5));

    env.noteOn(); // retrigger mid-release: attack resumes from 0.75
    const float first = env.getNextSample();
    REQUIRE(first > 0.75f);
}
