// Unit tests for the framework-agnostic master brickwall limiter.

#include "dsp/Limiter.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using cold::dsp::Limiter;

namespace
{
// A constant-amplitude signal (alternating +/-amp) so the peak detector settles
// immediately to `amp`.
std::vector<float> makeTone(int n, float amp)
{
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        v[static_cast<size_t>(i)] = (i % 2 == 0) ? amp : -amp;
    return v;
}

float peak(const std::vector<float>& v)
{
    float p = 0.0f;
    for (float s : v)
        p = std::max(p, std::abs(s));
    return p;
}
} // namespace

TEST_CASE("Limiter is bypassed until enabled", "[limiter][dsp][unit]")
{
    Limiter lim;
    lim.prepare(48000.0);
    lim.setParameters(-6.0f, 0.05f); // would pull a 0 dBFS signal down hard

    auto data = makeTone(2000, 1.0f);
    float* ch[1] = {data.data()};
    lim.processBlock(ch, 1, static_cast<int>(data.size()));

    // Disabled: every sample is untouched.
    REQUIRE_THAT(peak(data), WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("Limiter leaves a signal below the ceiling alone", "[limiter][dsp][unit]")
{
    Limiter lim;
    lim.prepare(48000.0);
    lim.setParameters(0.0f, 0.05f); // ceiling at 0 dBFS (1.0)
    lim.setEnabled(true);

    // -20 dBFS (amp ~0.1) is well under the ceiling -> no reduction.
    auto data = makeTone(4000, 0.1f);
    float* ch[1] = {data.data()};
    lim.processBlock(ch, 1, static_cast<int>(data.size()));

    REQUIRE_THAT(peak(data), WithinAbs(0.1f, 1e-6f));
    REQUIRE(lim.gainReductionDb() > -0.01f); // essentially none
}

TEST_CASE("Limiter holds the output at the ceiling", "[limiter][dsp][unit]")
{
    Limiter lim;
    lim.prepare(48000.0);
    // Ceiling -6 dBFS ~= 0.5011 linear; a 0 dBFS input must be clamped to it.
    const float ceilingLin = std::pow(10.0f, -6.0f / 20.0f);
    lim.setParameters(-6.0f, 0.05f);
    lim.setEnabled(true);

    auto data = makeTone(4000, 1.0f);
    float* ch[1] = {data.data()};
    lim.processBlock(ch, 1, static_cast<int>(data.size()));

    // Instant attack: nothing exceeds the ceiling, from the very first sample.
    REQUIRE(peak(data) <= ceilingLin + 1e-4f);
    // And it is actually working (near the ceiling, not silenced).
    REQUIRE(peak(data) > ceilingLin * 0.9f);
    REQUIRE(lim.gainReductionDb() < -5.0f);
}

TEST_CASE("Limiter never lets the very first sample overshoot", "[limiter][dsp][unit]")
{
    Limiter lim;
    lim.prepare(48000.0);
    const float ceilingLin = std::pow(10.0f, -6.0f / 20.0f);
    lim.setParameters(-6.0f, 0.5f); // slow release must not delay the attack
    lim.setEnabled(true);

    // A hard transient on sample 0 (a click) then silence.
    std::vector<float> data(64, 0.0f);
    data[0] = 1.0f;
    float* ch[1] = {data.data()};
    lim.processBlock(ch, 1, static_cast<int>(data.size()));

    REQUIRE(std::abs(data[0]) <= ceilingLin + 1e-4f);
}

TEST_CASE("Limiter releases back toward unity after a peak", "[limiter][dsp][unit]")
{
    Limiter lim;
    lim.prepare(48000.0);
    lim.setParameters(-6.0f, 0.01f); // fast release
    lim.setEnabled(true);

    // Loud burst pulls the gain down...
    auto loud = makeTone(2000, 1.0f);
    float* a[1] = {loud.data()};
    lim.processBlock(a, 1, static_cast<int>(loud.size()));
    const float reducedDb = lim.gainReductionDb();
    REQUIRE(reducedDb < -5.0f);

    // ...then a long quiet stretch lets the gain recover toward 0 dB.
    auto quiet = makeTone(48000, 0.05f);
    float* b[1] = {quiet.data()};
    lim.processBlock(b, 1, static_cast<int>(quiet.size()));
    REQUIRE(lim.gainReductionDb() > -0.1f);
}

TEST_CASE("Limiter is stereo-linked", "[limiter][dsp][unit]")
{
    Limiter lim;
    lim.prepare(48000.0);
    const float ceilingLin = std::pow(10.0f, -6.0f / 20.0f);
    lim.setParameters(-6.0f, 0.05f);
    lim.setEnabled(true);

    // Left loud (1.0), right quiet (0.1). The loud channel drives the shared gain,
    // so the right channel is scaled by the same factor (well below its input).
    auto left = makeTone(4000, 1.0f);
    auto right = makeTone(4000, 0.1f);
    float* ch[2] = {left.data(), right.data()};
    lim.processBlock(ch, 2, static_cast<int>(left.size()));

    REQUIRE(peak(left) <= ceilingLin + 1e-4f);
    REQUIRE(peak(right) < 0.1f); // pulled down by the linked gain
}
