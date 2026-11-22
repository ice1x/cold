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

TEST_CASE("Lfo custom designer shape interpolates its points", "[lfo][dsp][unit][custom]")
{
    Lfo lfo;
    lfo.setSampleRate(kSr);
    lfo.setRate(10.0f); // 100 samples / cycle
    lfo.setDepth(1.0f);
    const float points[] = {-1.0f, 1.0f}; // 2-point designer waveform
    lfo.setCustomShape(points, 2);
    lfo.reset();

    REQUIRE_THAT(lfo.getNextSample(), WithinAbs(-1.0f, 1e-5)); // phase 0 -> point 0
    for (int i = 0; i < 24; ++i)
        lfo.getNextSample();
    REQUIRE_THAT(lfo.getNextSample(), WithinAbs(0.0f, 0.03f)); // phase 0.25 -> midway
    for (int i = 0; i < 24; ++i)
        lfo.getNextSample();
    REQUIRE_THAT(lfo.getNextSample(), WithinAbs(1.0f, 1e-4)); // phase 0.5 -> point 1
}

TEST_CASE("Lfo trig mode Trig restarts phase on note, Free does not", "[lfo][dsp][unit][trigmode]")
{
    SECTION("Trig resets to the phase start")
    {
        auto lfo = makeLfo(LfoShape::SawUp, 10.0f);
        lfo.setTrigMode(cold::dsp::LfoTrigMode::Trig);
        for (int i = 0; i < 30; ++i)
            lfo.getNextSample(); // ramp partway up
        lfo.noteTrigger();
        REQUIRE_THAT(lfo.getNextSample(), WithinAbs(-1.0f, 1e-4)); // back to start
    }

    SECTION("Free ignores the trig")
    {
        auto lfo = makeLfo(LfoShape::SawUp, 10.0f);
        lfo.setTrigMode(cold::dsp::LfoTrigMode::Free);
        for (int i = 0; i < 30; ++i)
            lfo.getNextSample();
        lfo.noteTrigger();
        REQUIRE(lfo.getNextSample() > -0.9f); // kept advancing, not reset
    }
}

TEST_CASE("Lfo trig mode Hold latches the output at the trig", "[lfo][dsp][unit][trigmode]")
{
    auto lfo = makeLfo(LfoShape::Sine, 10.0f);
    for (int i = 0; i < 25; ++i)
        lfo.getNextSample(); // advance to ~peak (phase 0.25)

    lfo.setTrigMode(cold::dsp::LfoTrigMode::Hold);
    lfo.noteTrigger(); // latch the current value
    const float held = lfo.getNextSample();
    REQUIRE_THAT(held, WithinAbs(1.0f, 0.05f)); // near the sine peak
    for (int i = 0; i < 20; ++i)
        REQUIRE_THAT(lfo.getNextSample(), WithinAbs(held, 1e-6)); // frozen
}

TEST_CASE("Lfo trig mode OneShot runs one cycle then freezes", "[lfo][dsp][unit][trigmode]")
{
    auto lfo = makeLfo(LfoShape::SawUp, 10.0f); // 100 samples / cycle
    lfo.setTrigMode(cold::dsp::LfoTrigMode::OneShot);
    lfo.noteTrigger();

    float prev = lfo.getNextSample();
    bool moved = false;
    for (int i = 0; i < 99; ++i)
    {
        const float v = lfo.getNextSample();
        if (std::abs(v - prev) > 1e-4f)
            moved = true;
        prev = v;
    }
    REQUIRE(moved); // it oscillated during the single cycle

    const float a = lfo.getNextSample();
    const float b = lfo.getNextSample();
    REQUIRE_THAT(a, WithinAbs(b, 1e-6)); // frozen after one cycle
}

TEST_CASE("Lfo converts host tempo divisions to Hz", "[lfo][dsp][unit]")
{
    // 120 BPM: one cycle per beat = 2 Hz; per whole note (4 beats) = 0.5 Hz.
    REQUIRE_THAT(Lfo::divisionToHz(120.0, 1.0), WithinAbs(2.0, 1e-9));
    REQUIRE_THAT(Lfo::divisionToHz(120.0, 4.0), WithinAbs(0.5, 1e-9));
    REQUIRE_THAT(Lfo::divisionToHz(60.0, 0.5), WithinAbs(2.0, 1e-9)); // eighth at 60 BPM
}

// ---- Trig re-roll, unipolar waves, direction, Half, slew, fade, tempo ------

TEST_CASE("Lfo random draws a new value on every trig in Trig mode", "[lfo][dsp][unit]")
{
    // A 10 s cycle: no wrap happens here, so only the trigs can change the value.
    auto lfo = makeLfo(LfoShape::Random, 0.1f);
    lfo.setTrigMode(cold::dsp::LfoTrigMode::Trig);

    lfo.noteTrigger();
    const float a = lfo.peek();
    lfo.noteTrigger();
    const float b = lfo.peek();
    lfo.noteTrigger();
    const float c = lfo.peek();
    REQUIRE(std::abs(a - b) > 1e-6f); // a fresh random value per hit
    REQUIRE(std::abs(b - c) > 1e-6f);
}

TEST_CASE("Lfo random keeps its value across trigs in Free mode", "[lfo][dsp][unit]")
{
    auto lfo = makeLfo(LfoShape::Random, 0.1f);
    lfo.setTrigMode(cold::dsp::LfoTrigMode::Free);
    lfo.noteTrigger();
    const float a = lfo.peek();
    lfo.noteTrigger();
    REQUIRE_THAT(lfo.peek(), WithinAbs(a, 1e-9)); // free-running: trigs don't redraw
}

TEST_CASE("Lfo exponential and ramp are unipolar", "[lfo][dsp][unit]")
{
    SECTION("exponential falls from full to zero over the cycle")
    {
        auto lfo = makeLfo(LfoShape::Exponential, 10.0f); // 100 samples / cycle
        float prev = lfo.getNextSample();
        REQUIRE_THAT(prev, WithinAbs(1.0f, 1e-4));
        for (int i = 1; i < 100; ++i)
        {
            const float v = lfo.getNextSample();
            REQUIRE(v >= 0.0f);
            REQUIRE(v <= prev + 1e-6f); // never rises within the cycle
            prev = v;
        }
        REQUIRE(prev < 0.05f);
    }

    SECTION("ramp rises from zero to full over the cycle")
    {
        auto lfo = makeLfo(LfoShape::Ramp, 10.0f);
        float prev = lfo.getNextSample();
        REQUIRE_THAT(prev, WithinAbs(0.0f, 1e-4));
        for (int i = 1; i < 100; ++i)
        {
            const float v = lfo.getNextSample();
            REQUIRE(v >= prev - 1e-6f);
            REQUIRE(v <= 1.0f);
            prev = v;
        }
        REQUIRE(prev > 0.95f);
    }
}

TEST_CASE("Lfo negative rate plays the cycle backwards", "[lfo][dsp][unit]")
{
    auto lfo = makeLfo(LfoShape::SawUp, -10.0f); // a negative rate runs backward
    lfo.getNextSample();                         // phase 0, then wraps to the top
    float prev = lfo.getNextSample();
    for (int i = 0; i < 60; ++i)
    {
        const float v = lfo.getNextSample();
        REQUIRE(v < prev); // a rising saw read backwards falls
        prev = v;
    }
}

TEST_CASE("Lfo trig mode Half runs to mid-cycle then freezes", "[lfo][dsp][unit]")
{
    auto lfo = makeLfo(LfoShape::Triangle, 10.0f); // peak (+1) at mid-cycle
    lfo.setTrigMode(cold::dsp::LfoTrigMode::Half);
    lfo.noteTrigger();
    for (int i = 0; i < 70; ++i)
        lfo.getNextSample(); // past the middle
    const float a = lfo.getNextSample();
    const float b = lfo.getNextSample();
    REQUIRE_THAT(a, WithinAbs(1.0f, 0.05f)); // stopped at the middle (the peak)
    REQUIRE_THAT(a, WithinAbs(b, 1e-6));     // and holds there
}

TEST_CASE("Lfo random slew glides between values", "[lfo][dsp][unit]")
{
    auto maxStep = [](float slew)
    {
        auto lfo = makeLfo(LfoShape::Random, 10.0f); // a new value every 100 samples
        lfo.setSlew(slew);
        float prev = lfo.getNextSample();
        float biggest = 0.0f;
        for (int i = 0; i < 500; ++i)
        {
            const float v = lfo.getNextSample();
            biggest = std::max(biggest, std::abs(v - prev));
            prev = v;
        }
        return biggest;
    };
    REQUIRE(maxStep(0.0f) > 0.2f);  // stepped: jumps at each new value
    REQUIRE(maxStep(1.0f) < 0.05f); // full slew: a smooth glide
}

TEST_CASE("Lfo fade in and fade out shape the depth after a trig", "[lfo][dsp][unit]")
{
    // Square at +1 for the first half cycle; fade spans |fade| x 4 cycles.
    SECTION("negative fade fades the modulation in")
    {
        auto lfo = makeLfo(LfoShape::Square, 1.0f); // 1000 samples / cycle
        lfo.setTrigMode(cold::dsp::LfoTrigMode::Trig);
        lfo.setFade(-0.025f); // 0.1 cycle = 100 samples
        lfo.noteTrigger();
        REQUIRE(std::abs(lfo.getNextSample()) < 0.05f); // starts silent
        for (int i = 0; i < 150; ++i)
            lfo.getNextSample();
        REQUIRE_THAT(lfo.getNextSample(), WithinAbs(1.0f, 1e-4)); // full after the fade
    }

    SECTION("positive fade fades the modulation out")
    {
        auto lfo = makeLfo(LfoShape::Square, 1.0f);
        lfo.setTrigMode(cold::dsp::LfoTrigMode::Trig);
        lfo.setFade(0.025f);
        lfo.noteTrigger();
        REQUIRE_THAT(lfo.getNextSample(), WithinAbs(1.0f, 0.02f)); // starts full
        for (int i = 0; i < 150; ++i)
            lfo.getNextSample();
        REQUIRE(std::abs(lfo.getNextSample()) < 1e-4f); // gone after the fade
    }
}

TEST_CASE("Lfo speed x multiplier follows the tempo", "[lfo][dsp][unit]")
{
    // SPD x MULT = 128 is one cycle per bar (16 steps); at 120 BPM a bar is 2 s.
    REQUIRE_THAT(Lfo::tempoRateHz(16.0, 8.0, 120.0), WithinAbs(0.5, 1e-9));
    REQUIRE_THAT(Lfo::tempoRateHz(32.0, 8.0, 120.0), WithinAbs(1.0, 1e-9));
    REQUIRE_THAT(Lfo::tempoRateHz(16.0, 8.0, 60.0), WithinAbs(0.25, 1e-9));
    REQUIRE_THAT(Lfo::tempoRateHz(-16.0, 8.0, 120.0), WithinAbs(-0.5, 1e-9)); // backward
}
