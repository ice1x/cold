// Unit tests for the framework-agnostic master tilt EQ.

#include "dsp/TiltEq.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using cold::dsp::TiltEq;

namespace
{
constexpr double kPi = 3.14159265358979323846;

// A mono sine at `freqHz`, amplitude 1.
std::vector<float> makeSine(int n, double freqHz, double sampleRate)
{
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        v[static_cast<size_t>(i)] =
            static_cast<float>(std::sin(2.0 * kPi * freqHz * i / sampleRate));
    return v;
}

// Peak over the settled tail (past the filter's start-up).
float tailPeak(const std::vector<float>& v, int lastN)
{
    float p = 0.0f;
    for (int i = std::max(0, static_cast<int>(v.size()) - lastN); i < static_cast<int>(v.size());
         ++i)
        p = std::max(p, std::abs(v[static_cast<size_t>(i)]));
    return p;
}
} // namespace

TEST_CASE("TiltEq is bypassed until enabled", "[tilteq][dsp][unit]")
{
    TiltEq eq;
    eq.prepare(48000.0);
    eq.setParameters(12.0f, 700.0f); // a strong tilt that would clearly reshape

    auto data = makeSine(4000, 5000.0, 48000.0);
    const float before = tailPeak(data, 1000);
    float* ch[1] = {data.data()};
    eq.processBlock(ch, 1, static_cast<int>(data.size()));

    REQUIRE_THAT(tailPeak(data, 1000), WithinAbs(before, 1e-6f));
}

TEST_CASE("TiltEq at zero tilt is flat", "[tilteq][dsp][unit]")
{
    TiltEq eq;
    eq.prepare(48000.0);
    eq.setParameters(0.0f, 700.0f);
    eq.setEnabled(true);

    // Both a low and a high tone pass through essentially unchanged.
    for (double f : {120.0, 6000.0})
    {
        auto data = makeSine(4000, f, 48000.0);
        float* ch[1] = {data.data()};
        eq.processBlock(ch, 1, static_cast<int>(data.size()));
        REQUIRE_THAT(tailPeak(data, 1000), WithinAbs(1.0f, 0.02f));
    }
}

TEST_CASE("TiltEq positive tilt brightens (lifts highs, cuts lows)", "[tilteq][dsp][unit]")
{
    TiltEq eq;
    eq.prepare(48000.0);

    // A low tone well below the 700 Hz pivot is attenuated...
    auto low = makeSine(8000, 100.0, 48000.0);
    eq.setParameters(9.0f, 700.0f);
    eq.setEnabled(true);
    eq.reset();
    float* lo[1] = {low.data()};
    eq.processBlock(lo, 1, static_cast<int>(low.size()));
    REQUIRE(tailPeak(low, 1000) < 0.9f);

    // ...and a high tone well above the pivot is boosted above unity.
    auto high = makeSine(8000, 8000.0, 48000.0);
    eq.reset();
    float* hi[1] = {high.data()};
    eq.processBlock(hi, 1, static_cast<int>(high.size()));
    REQUIRE(tailPeak(high, 1000) > 1.5f);
}

TEST_CASE("TiltEq negative tilt darkens (lifts lows, cuts highs)", "[tilteq][dsp][unit]")
{
    TiltEq eq;
    eq.prepare(48000.0);
    eq.setParameters(-9.0f, 700.0f);
    eq.setEnabled(true);

    auto high = makeSine(8000, 8000.0, 48000.0);
    float* hi[1] = {high.data()};
    eq.processBlock(hi, 1, static_cast<int>(high.size()));
    REQUIRE(tailPeak(high, 1000) < 0.9f); // highs cut

    auto low = makeSine(8000, 100.0, 48000.0);
    eq.reset();
    float* lo[1] = {low.data()};
    eq.processBlock(lo, 1, static_cast<int>(low.size()));
    REQUIRE(tailPeak(low, 1000) > 1.5f); // lows lifted
}

TEST_CASE("TiltEq reset clears the crossover state", "[tilteq][dsp][unit]")
{
    TiltEq eq;
    eq.prepare(48000.0);
    eq.setParameters(9.0f, 700.0f);
    eq.setEnabled(true);

    auto a = makeSine(2000, 100.0, 48000.0);
    float* ca[1] = {a.data()};
    eq.processBlock(ca, 1, static_cast<int>(a.size()));

    // After reset the first output sample must not carry the previous tail: feeding
    // a fresh zero block leaves it silent (no residual energy from the state).
    eq.reset();
    std::vector<float> zeros(64, 0.0f);
    float* cz[1] = {zeros.data()};
    eq.processBlock(cz, 1, static_cast<int>(zeros.size()));
    REQUIRE_THAT(tailPeak(zeros, 64), WithinAbs(0.0f, 1e-6f));
}
