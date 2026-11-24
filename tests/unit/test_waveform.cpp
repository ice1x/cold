// Unit tests for the framework-agnostic waveform peak reducer (I5c).

#include "dsp/Waveform.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using cold::dsp::computeWaveformPeaks;
using cold::dsp::EnvelopeShape;
using cold::dsp::sampleEnvelopeShape;

TEST_CASE("computeWaveformPeaks bins a ramp into ascending min/max", "[waveform]")
{
    // A monotonically increasing ramp -1 .. +1 over 1000 frames.
    std::vector<float> ramp(1000);
    for (int i = 0; i < 1000; ++i)
        ramp[static_cast<size_t>(i)] = -1.0f + 2.0f * static_cast<float>(i) / 999.0f;
    const float* ch[1] = {ramp.data()};

    const auto peaks = computeWaveformPeaks(ch, 1, 0, 1000, 10);
    REQUIRE(peaks.numBins() == 10);

    // Each bin's max >= its min, and bins ascend (ramp is increasing).
    for (int b = 0; b < 10; ++b)
        REQUIRE(peaks.maxValues[static_cast<size_t>(b)] >= peaks.minValues[static_cast<size_t>(b)]);
    for (int b = 1; b < 10; ++b)
        REQUIRE(peaks.minValues[static_cast<size_t>(b)] >
                peaks.minValues[static_cast<size_t>(b - 1)]);

    // First bin starts near -1, last bin ends near +1.
    REQUIRE_THAT(peaks.minValues.front(), WithinAbs(-1.0f, 0.05f));
    REQUIRE_THAT(peaks.maxValues.back(), WithinAbs(1.0f, 0.05f));
}

TEST_CASE("computeWaveformPeaks captures a bin's min and max", "[waveform]")
{
    // Alternating +/-1: every non-trivial bin should span the full range.
    std::vector<float> alt(800);
    for (int i = 0; i < 800; ++i)
        alt[static_cast<size_t>(i)] = (i % 2 == 0) ? 1.0f : -1.0f;
    const float* ch[1] = {alt.data()};

    const auto peaks = computeWaveformPeaks(ch, 1, 0, 800, 8);
    REQUIRE(peaks.numBins() == 8);
    for (int b = 0; b < 8; ++b)
    {
        REQUIRE_THAT(peaks.maxValues[static_cast<size_t>(b)], WithinAbs(1.0f, 1e-6f));
        REQUIRE_THAT(peaks.minValues[static_cast<size_t>(b)], WithinAbs(-1.0f, 1e-6f));
    }
}

TEST_CASE("computeWaveformPeaks averages channels to mono", "[waveform]")
{
    // L = +1, R = -1 => mono average 0 everywhere.
    std::vector<float> left(400, 1.0f);
    std::vector<float> right(400, -1.0f);
    const float* ch[2] = {left.data(), right.data()};

    const auto peaks = computeWaveformPeaks(ch, 2, 0, 400, 4);
    REQUIRE(peaks.numBins() == 4);
    for (int b = 0; b < 4; ++b)
    {
        REQUIRE_THAT(peaks.minValues[static_cast<size_t>(b)], WithinAbs(0.0f, 1e-6f));
        REQUIRE_THAT(peaks.maxValues[static_cast<size_t>(b)], WithinAbs(0.0f, 1e-6f));
    }
}

TEST_CASE("computeWaveformPeaks honors the start/length window", "[waveform]")
{
    // First half silent, second half +0.5. Windowing onto the second half must
    // read only the loud region.
    std::vector<float> data(1000, 0.0f);
    for (int i = 500; i < 1000; ++i)
        data[static_cast<size_t>(i)] = 0.5f;
    const float* ch[1] = {data.data()};

    const auto peaks = computeWaveformPeaks(ch, 1, 500, 500, 5);
    REQUIRE(peaks.numBins() == 5);
    for (int b = 0; b < 5; ++b)
        REQUIRE_THAT(peaks.maxValues[static_cast<size_t>(b)], WithinAbs(0.5f, 1e-6f));
}

TEST_CASE("computeWaveformPeaks handles fewer frames than bins", "[waveform]")
{
    // 3 frames into 6 bins: every bin still filled, spanning the data range.
    std::vector<float> data = {-1.0f, 0.0f, 1.0f};
    const float* ch[1] = {data.data()};

    const auto peaks = computeWaveformPeaks(ch, 1, 0, 3, 6);
    REQUIRE(peaks.numBins() == 6);
    // No NaNs / uninitialized bins; every value in range.
    for (int b = 0; b < 6; ++b)
    {
        REQUIRE(std::isfinite(peaks.minValues[static_cast<size_t>(b)]));
        REQUIRE(std::isfinite(peaks.maxValues[static_cast<size_t>(b)]));
        REQUIRE(peaks.maxValues[static_cast<size_t>(b)] >= peaks.minValues[static_cast<size_t>(b)]);
    }
}

TEST_CASE("computeWaveformPeaks returns empty on degenerate input", "[waveform]")
{
    std::vector<float> data(10, 1.0f);
    const float* ch[1] = {data.data()};

    REQUIRE(computeWaveformPeaks(ch, 1, 0, 10, 0).empty());      // no bins
    REQUIRE(computeWaveformPeaks(ch, 1, 0, 0, 8).empty());       // no frames
    REQUIRE(computeWaveformPeaks(nullptr, 0, 0, 10, 8).empty()); // no channels
}

TEST_CASE("sampleEnvelopeShape traces a held DAHDSR contour", "[waveform][envelope]")
{
    EnvelopeShape s;
    s.attack = 0.1f;  // rise over the first 0.1s
    s.decay = 0.0f;   // no decay
    s.sustain = 1.0f; // hold at full
    s.release = 0.0f;

    SECTION("attack fills the whole span: rises 0 -> 1, monotonic")
    {
        auto c = sampleEnvelopeShape(s, 0.1, 64);
        REQUIRE(c.size() == 64);
        REQUIRE(c.front() < 0.25f);
        REQUIRE(c.back() > 0.9f);
        for (size_t i = 1; i < c.size(); ++i)
            REQUIRE(c[i] >= c[i - 1] - 1e-4f); // non-decreasing through the attack
    }

    SECTION("longer span: attack finishes early, then holds at sustain")
    {
        auto c = sampleEnvelopeShape(s, 0.2, 64); // attack (0.1s) = first half
        REQUIRE(c.size() == 64);
        REQUIRE(c[16] < 0.9f);    // ~0.05s in: still climbing the attack
        REQUIRE(c[40] > 0.9f);    // ~0.125s: attack done, holding at sustain
        REQUIRE(c.back() > 0.9f); // second half holds at sustain 1.0
    }

    SECTION("degenerate input is empty")
    {
        REQUIRE(sampleEnvelopeShape(s, 0.0, 64).empty());
        REQUIRE(sampleEnvelopeShape(s, 0.1, 0).empty());
    }
}
