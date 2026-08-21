// Unit tests for the framework-agnostic dynamics compressor.

#include "dsp/Compressor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using cold::dsp::Compressor;

namespace
{
// A constant-amplitude full-scale-ish signal (alternating +/-amp) so the peak
// detector settles quickly to `amp`.
std::vector<float> makeTone(int n, float amp)
{
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        v[static_cast<size_t>(i)] = (i % 2 == 0) ? amp : -amp;
    return v;
}

float tailPeak(const std::vector<float>& v, int lastN)
{
    float p = 0.0f;
    for (int i = std::max(0, static_cast<int>(v.size()) - lastN); i < static_cast<int>(v.size());
         ++i)
        p = std::max(p, std::abs(v[static_cast<size_t>(i)]));
    return p;
}
} // namespace

TEST_CASE("Compressor is bypassed until enabled", "[compressor][dsp][unit]")
{
    Compressor comp;
    comp.prepare(48000.0);
    comp.setParameters(-20.0f, 4.0f, 0.001f, 0.05f, 0.0f); // would compress heavily

    auto data = makeTone(2000, 1.0f);
    float* ch[1] = {data.data()};
    comp.processBlock(ch, 1, static_cast<int>(data.size()));

    // Disabled: every sample is untouched.
    REQUIRE_THAT(tailPeak(data, 500), WithinAbs(1.0f, 1e-6f));
}

TEST_CASE("Compressor leaves a signal below threshold alone", "[compressor][dsp][unit]")
{
    Compressor comp;
    comp.prepare(48000.0);
    comp.setParameters(-6.0f, 4.0f, 0.001f, 0.05f, 0.0f);
    comp.setEnabled(true);

    // -20 dBFS (amp ~0.1) is well below the -6 dB threshold -> no reduction.
    auto data = makeTone(4000, 0.1f);
    float* ch[1] = {data.data()};
    comp.processBlock(ch, 1, static_cast<int>(data.size()));

    REQUIRE_THAT(tailPeak(data, 500), WithinAbs(0.1f, 1e-3f));
    REQUIRE(comp.gainReductionDb() > -0.01f); // essentially none
}

TEST_CASE("Compressor attenuates a signal above threshold", "[compressor][dsp][unit]")
{
    Compressor comp;
    comp.prepare(48000.0);
    // threshold -20 dB, ratio 4, makeup 0: a 0 dBFS input (20 dB over) should be
    // reduced by ~15 dB -> ~0.178 linear at steady state.
    comp.setParameters(-20.0f, 4.0f, 0.001f, 0.1f, 0.0f);
    comp.setEnabled(true);

    auto data = makeTone(8000, 1.0f);
    float* ch[1] = {data.data()};
    comp.processBlock(ch, 1, static_cast<int>(data.size()));

    const float tail = tailPeak(data, 1000);
    REQUIRE(tail < 0.5f);  // clearly compressed from 1.0
    REQUIRE(tail > 0.05f); // but not silenced
    REQUIRE(comp.gainReductionDb() < -5.0f);
}

TEST_CASE("Compressor makeup gain restores level", "[compressor][dsp][unit]")
{
    Compressor comp;
    comp.prepare(48000.0);
    comp.setParameters(-20.0f, 4.0f, 0.001f, 0.1f, 0.0f);
    comp.setEnabled(true);

    auto noMakeup = makeTone(8000, 1.0f);
    float* a[1] = {noMakeup.data()};
    comp.processBlock(a, 1, static_cast<int>(noMakeup.size()));
    const float without = tailPeak(noMakeup, 1000);

    comp.reset();
    comp.setParameters(-20.0f, 4.0f, 0.001f, 0.1f, 12.0f); // +12 dB makeup
    auto withMakeup = makeTone(8000, 1.0f);
    float* b[1] = {withMakeup.data()};
    comp.processBlock(b, 1, static_cast<int>(withMakeup.size()));
    const float with = tailPeak(withMakeup, 1000);

    REQUIRE(with > without * 1.5f); // makeup raised the output
}

TEST_CASE("Compressor is stereo-linked", "[compressor][dsp][unit]")
{
    Compressor comp;
    comp.prepare(48000.0);
    comp.setParameters(-20.0f, 4.0f, 0.001f, 0.1f, 0.0f);
    comp.setEnabled(true);

    // Left loud (1.0), right quiet (0.1). The link means the gain from the loud
    // channel is applied to both, so the right channel is scaled by the same factor.
    auto left = makeTone(8000, 1.0f);
    auto right = makeTone(8000, 0.1f);
    float* ch[2] = {left.data(), right.data()};
    comp.processBlock(ch, 2, static_cast<int>(left.size()));

    // Right ends up well below its input 0.1 because the loud left drove the gain.
    REQUIRE(tailPeak(right, 1000) < 0.09f);
}
