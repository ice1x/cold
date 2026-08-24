#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/BitCrusher.h"

#include <vector>

using Catch::Matchers::WithinAbs;
using cold::dsp::BitCrusher;

namespace
{
// Run a mono block through a crusher and return the processed samples.
std::vector<float> crush(BitCrusher& bc, std::vector<float> samples)
{
    float* ch = samples.data();
    bc.processBlock(&ch, 1, static_cast<int>(samples.size()));
    return samples;
}
} // namespace

TEST_CASE("BitCrusher is a passthrough while disabled", "[bitcrusher][dsp][unit]")
{
    BitCrusher bc;
    bc.prepare(44100.0);
    bc.setParameters(2.0f, 4); // heavy settings, but disabled
    REQUIRE_FALSE(bc.enabled());

    const std::vector<float> in{0.13f, -0.47f, 0.91f, -0.02f};
    auto out = crush(bc, in);
    for (size_t i = 0; i < in.size(); ++i)
        REQUIRE_THAT(out[i], WithinAbs(in[i], 1e-6));
}

TEST_CASE("BitCrusher quantizes to a coarse bit depth", "[bitcrusher][dsp][unit]")
{
    BitCrusher bc;
    bc.prepare(44100.0);
    bc.setParameters(2.0f, 1); // 2 bits, no sample-rate reduction
    bc.setEnabled(true);

    // 2 bits -> scale = 2^(2-1) = 2, so values snap to the nearest multiple of 0.5.
    const std::vector<float> in{0.30f, 0.10f, -0.30f, 0.80f, -0.90f};
    const std::vector<float> want{0.5f, 0.0f, -0.5f, 1.0f, -1.0f};
    auto out = crush(bc, in);
    for (size_t i = 0; i < in.size(); ++i)
        REQUIRE_THAT(out[i], WithinAbs(want[i], 1e-6));
}

TEST_CASE("BitCrusher sample-and-holds for sample-rate reduction", "[bitcrusher][dsp][unit]")
{
    BitCrusher bc;
    bc.prepare(44100.0);
    bc.setParameters(16.0f, 2); // transparent bit depth, halve the rate (hold every 2)
    bc.setEnabled(true);

    // Downsample 2: each pair holds the first sample of the pair.
    const std::vector<float> in{0.10f, 0.20f, 0.30f, 0.40f, 0.50f, 0.60f};
    const std::vector<float> want{0.10f, 0.10f, 0.30f, 0.30f, 0.50f, 0.50f};
    auto out = crush(bc, in);
    for (size_t i = 0; i < in.size(); ++i)
        REQUIRE_THAT(out[i], WithinAbs(want[i], 2e-3)); // small quantization slack
}

TEST_CASE("BitCrusher transparent settings barely change the signal", "[bitcrusher][dsp][unit]")
{
    BitCrusher bc;
    bc.prepare(44100.0);
    bc.setParameters(16.0f, 1); // full depth, no rate reduction
    bc.setEnabled(true);

    const std::vector<float> in{0.13f, -0.47f, 0.91f, -0.02f};
    auto out = crush(bc, in);
    for (size_t i = 0; i < in.size(); ++i)
        REQUIRE_THAT(out[i], WithinAbs(in[i], 1e-3));
}

TEST_CASE("BitCrusher reset clears the sample-and-hold state", "[bitcrusher][dsp][unit]")
{
    BitCrusher bc;
    bc.prepare(44100.0);
    bc.setParameters(16.0f, 4); // hold every 4 samples
    bc.setEnabled(true);

    crush(bc, {0.9f, 0.9f}); // primes the held value and advances the counter
    bc.reset();

    // After reset the very first sample of the next block is captured afresh.
    auto out = crush(bc, {0.2f, 0.7f, 0.7f});
    REQUIRE_THAT(out[0], WithinAbs(0.2f, 2e-3)); // held = first sample, not the old 0.9
    REQUIRE_THAT(out[1], WithinAbs(0.2f, 2e-3)); // still holding within the period
}
