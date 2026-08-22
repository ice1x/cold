// Unit tests for the framework-agnostic waveshaping saturator.

#include "dsp/Saturator.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using cold::dsp::Saturator;

namespace
{
// Alternating +/-amp so every sample has magnitude `amp`.
std::vector<float> makeTone(int n, float amp)
{
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        v[static_cast<size_t>(i)] = (i % 2 == 0) ? amp : -amp;
    return v;
}

float maxAbs(const std::vector<float>& v)
{
    float p = 0.0f;
    for (float x : v)
        p = std::max(p, std::abs(x));
    return p;
}
} // namespace

TEST_CASE("Saturator is bypassed until enabled", "[saturator][dsp][unit]")
{
    Saturator sat;
    sat.prepare(48000.0);
    sat.setParameters(8.0f, 1.0f); // heavy drive, full wet

    auto data = makeTone(64, 0.5f);
    float* ch[1] = {data.data()};
    sat.processBlock(ch, 1, static_cast<int>(data.size()));

    REQUIRE_THAT(maxAbs(data), WithinAbs(0.5f, 1e-6f)); // untouched
}

TEST_CASE("Saturator at mix 0 is a dry passthrough", "[saturator][dsp][unit]")
{
    Saturator sat;
    sat.prepare(48000.0);
    sat.setParameters(8.0f, 0.0f); // fully dry
    sat.setEnabled(true);

    auto data = makeTone(64, 0.5f);
    float* ch[1] = {data.data()};
    sat.processBlock(ch, 1, static_cast<int>(data.size()));

    REQUIRE_THAT(maxAbs(data), WithinAbs(0.5f, 1e-6f));
}

TEST_CASE("Saturator shapes with the tanh curve", "[saturator][dsp][unit]")
{
    Saturator sat;
    sat.prepare(48000.0);
    sat.setParameters(4.0f, 1.0f); // drive 4, full wet
    sat.setEnabled(true);

    auto data = makeTone(64, 0.5f);
    float* ch[1] = {data.data()};
    sat.processBlock(ch, 1, static_cast<int>(data.size()));

    // y = tanh(drive * x); |x| = 0.5, drive 4 -> tanh(2) ~= 0.9640.
    REQUIRE_THAT(maxAbs(data), WithinAbs(std::tanh(2.0f), 1e-4f));
}

TEST_CASE("Saturator output stays bounded by unity", "[saturator][dsp][unit]")
{
    Saturator sat;
    sat.prepare(48000.0);
    sat.setParameters(20.0f, 1.0f); // extreme drive
    sat.setEnabled(true);

    auto data = makeTone(64, 1.0f);
    float* ch[1] = {data.data()};
    sat.processBlock(ch, 1, static_cast<int>(data.size()));

    REQUIRE(maxAbs(data) <= 1.0f);
}

TEST_CASE("Saturator mix blends dry and wet", "[saturator][dsp][unit]")
{
    Saturator sat;
    sat.prepare(48000.0);
    sat.setParameters(4.0f, 0.5f); // half wet
    sat.setEnabled(true);

    auto data = makeTone(64, 0.5f);
    float* ch[1] = {data.data()};
    sat.processBlock(ch, 1, static_cast<int>(data.size()));

    // out = dry*(1-mix) + wet*mix = 0.5*0.5 + tanh(2)*0.5.
    const float expected = 0.5f * 0.5f + std::tanh(2.0f) * 0.5f;
    REQUIRE_THAT(maxAbs(data), WithinAbs(expected, 1e-4f));
}

TEST_CASE("Saturator is stereo-independent", "[saturator][dsp][unit]")
{
    Saturator sat;
    sat.prepare(48000.0);
    sat.setParameters(4.0f, 1.0f);
    sat.setEnabled(true);

    auto left = makeTone(64, 0.5f);
    auto right = makeTone(64, 0.25f);
    float* ch[2] = {left.data(), right.data()};
    sat.processBlock(ch, 2, static_cast<int>(left.size()));

    // Each channel is shaped by its own value: tanh(4*0.5) and tanh(4*0.25).
    REQUIRE_THAT(maxAbs(left), WithinAbs(std::tanh(2.0f), 1e-4f));
    REQUIRE_THAT(maxAbs(right), WithinAbs(std::tanh(1.0f), 1e-4f));
}
