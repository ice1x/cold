#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/Lfo.h"

#include <cmath>

using Catch::Matchers::WithinAbs;
using cold::dsp::Lfo;
using cold::dsp::LfoShape;

namespace
{
constexpr double kSr = 1000.0;

Lfo makeLfo(LfoShape shape, float rateHz, float depth = 1.0f)
{
    Lfo lfo;
    lfo.setSampleRate(kSr);
    lfo.setShape(shape);
    lfo.setRate(rateHz);
    lfo.setDepth(depth);
    lfo.reset();
    return lfo;
}
} // namespace

TEST_CASE("Lfo sine hits its quarter-phase values", "[lfo][dsp][unit]")
{
    auto lfo = makeLfo(LfoShape::Sine, 10.0f); // 100 samples / cycle at 1 kHz

    REQUIRE_THAT(lfo.getNextSample(), WithinAbs(0.0f, 1e-4)); // phase 0.00
    for (int i = 0; i < 24; ++i)
        lfo.getNextSample();
    REQUIRE_THAT(lfo.getNextSample(), WithinAbs(1.0f, 1e-3)); // phase 0.25
    for (int i = 0; i < 24; ++i)
        lfo.getNextSample();
    REQUIRE_THAT(lfo.getNextSample(), WithinAbs(0.0f, 1e-3)); // phase 0.50
    for (int i = 0; i < 24; ++i)
        lfo.getNextSample();
    REQUIRE_THAT(lfo.getNextSample(), WithinAbs(-1.0f, 1e-3)); // phase 0.75
}

TEST_CASE("Lfo ramp and square shapes have the expected levels", "[lfo][dsp][unit]")
{
    SECTION("saw up ramps -1 -> +1")
    {
        auto lfo = makeLfo(LfoShape::SawUp, 10.0f);
        REQUIRE_THAT(lfo.getNextSample(), WithinAbs(-1.0f, 1e-4)); // phase 0
        for (int i = 0; i < 49; ++i)
            lfo.getNextSample();
        REQUIRE_THAT(lfo.getNextSample(), WithinAbs(0.0f, 0.03f)); // ~phase 0.5
    }

    SECTION("saw down ramps +1 -> -1")
    {
        auto lfo = makeLfo(LfoShape::SawDown, 10.0f);
        REQUIRE_THAT(lfo.getNextSample(), WithinAbs(1.0f, 1e-4));
    }

    SECTION("square is +1 for the first half, -1 for the second")
    {
        auto lfo = makeLfo(LfoShape::Square, 10.0f);
        REQUIRE_THAT(lfo.getNextSample(), WithinAbs(1.0f, 1e-6)); // phase 0
        for (int i = 0; i < 49; ++i)
            lfo.getNextSample();                                   // advance to phase ~0.5
        REQUIRE_THAT(lfo.getNextSample(), WithinAbs(-1.0f, 1e-6)); // phase >= 0.5
    }

    SECTION("triangle rises -1 -> +1 -> -1")
    {
        auto lfo = makeLfo(LfoShape::Triangle, 10.0f);
        REQUIRE_THAT(lfo.getNextSample(), WithinAbs(-1.0f, 1e-4)); // phase 0
        for (int i = 0; i < 49; ++i)
            lfo.getNextSample();
        REQUIRE_THAT(lfo.getNextSample(), WithinAbs(1.0f, 0.05f)); // ~phase 0.5 peak
    }
}

TEST_CASE("Lfo output stays within +/- depth for every shape", "[lfo][dsp][unit]")
{
    for (auto shape : {LfoShape::Sine, LfoShape::Triangle, LfoShape::SawUp, LfoShape::SawDown,
                       LfoShape::Square, LfoShape::Random})
    {
        const float depth = 0.5f;
        auto lfo = makeLfo(shape, 7.0f, depth);
        for (int n = 0; n < 4000; ++n)
        {
            const float v = lfo.getNextSample();
            REQUIRE(v <= depth + 1e-5f);
            REQUIRE(v >= -depth - 1e-5f);
        }
    }
}

TEST_CASE("Lfo depth scales the amplitude", "[lfo][dsp][unit]")
{
    auto full = makeLfo(LfoShape::Sine, 10.0f, 1.0f);
    auto half = makeLfo(LfoShape::Sine, 10.0f, 0.5f);

    for (int i = 0; i < 25; ++i) // advance to the sine peak (phase 0.25)
    {
        full.getNextSample();
        half.getNextSample();
    }
    // Peak was at the 25th sample already consumed; compare running peaks instead.
    float fp = 0.0f, hp = 0.0f;
    auto f2 = makeLfo(LfoShape::Sine, 10.0f, 1.0f);
    auto h2 = makeLfo(LfoShape::Sine, 10.0f, 0.5f);
    for (int n = 0; n < 100; ++n)
    {
        fp = std::max(fp, f2.getNextSample());
        hp = std::max(hp, h2.getNextSample());
    }
    REQUIRE_THAT(fp, WithinAbs(1.0f, 1e-3));
    REQUIRE_THAT(hp, WithinAbs(0.5f, 1e-3));
}

TEST_CASE("Lfo rate sets the cycle length", "[lfo][dsp][unit]")
{
    // 10 Hz at 1 kHz -> a full cycle every 100 samples; the sine returns near 0.
    auto lfo = makeLfo(LfoShape::Sine, 10.0f);
    float first = lfo.getNextSample();
    for (int i = 0; i < 99; ++i)
        lfo.getNextSample();
    float afterOneCycle = lfo.getNextSample(); // sample 100 == phase 0 again
    REQUIRE_THAT(first, WithinAbs(0.0f, 1e-4));
    REQUIRE_THAT(afterOneCycle, WithinAbs(0.0f, 1e-4));
}

TEST_CASE("Lfo random is stepped sample-and-hold", "[lfo][dsp][unit]")
{
    auto lfo = makeLfo(LfoShape::Random, 10.0f); // holds for 100 samples per cycle

    const float held = lfo.getNextSample();
    for (int i = 0; i < 99; ++i)
        REQUIRE_THAT(lfo.getNextSample(), WithinAbs(held, 1e-6)); // constant across the cycle

    // Deterministic: a second LFO with the same config reproduces the sequence.
    auto a = makeLfo(LfoShape::Random, 10.0f);
    auto b = makeLfo(LfoShape::Random, 10.0f);
    for (int n = 0; n < 500; ++n)
        REQUIRE_THAT(a.getNextSample(), WithinAbs(b.getNextSample(), 1e-9));
}

TEST_CASE("Lfo retrigger restarts the phase", "[lfo][dsp][unit]")
{
    auto lfo = makeLfo(LfoShape::SawUp, 10.0f);
    for (int i = 0; i < 30; ++i)
        lfo.getNextSample(); // move into the ramp

    lfo.retrigger();
    REQUIRE_THAT(lfo.getNextSample(), WithinAbs(-1.0f, 1e-4)); // back to phase 0
}

TEST_CASE("Lfo converts host tempo divisions to Hz", "[lfo][dsp][unit]")
{
    // 120 BPM: one cycle per beat = 2 Hz; per whole note (4 beats) = 0.5 Hz.
    REQUIRE_THAT(Lfo::divisionToHz(120.0, 1.0), WithinAbs(2.0, 1e-9));
    REQUIRE_THAT(Lfo::divisionToHz(120.0, 4.0), WithinAbs(0.5, 1e-9));
    REQUIRE_THAT(Lfo::divisionToHz(60.0, 0.5), WithinAbs(2.0, 1e-9)); // eighth at 60 BPM
}
