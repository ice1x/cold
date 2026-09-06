#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/Reverb.h"

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using cold::dsp::Reverb;

namespace
{
// Feed an impulse, return the mono output over `frames`.
std::vector<float> impulseResponse(float roomSize, float damping, int frames, double sr = 48000.0)
{
    Reverb r;
    r.prepare(sr);
    r.setParameters(roomSize, damping);
    std::vector<float> buf(static_cast<size_t>(frames), 0.0f);
    buf[0] = 1.0f;
    float* ch = buf.data();
    r.processBlock(&ch, 1, frames);
    return buf;
}

double windowRms(const std::vector<float>& b, int from, int to)
{
    double sum = 0.0;
    for (int n = from; n < to; ++n)
        sum += static_cast<double>(b[static_cast<size_t>(n)]) * b[static_cast<size_t>(n)];
    return std::sqrt(sum / (to - from));
}
} // namespace

TEST_CASE("Reverb produces a decaying diffuse tail", "[reverb][dsp][unit]")
{
    auto ir = impulseResponse(0.7f, 0.3f, 48000);

    REQUIRE(std::abs(ir[0]) < 0.5f);           // pure wet: the dry impulse is not passed through
    REQUIRE(windowRms(ir, 2000, 6000) > 1e-4); // there is a tail after the input
    // The tail decays: an early window is louder than a much later one.
    REQUIRE(windowRms(ir, 2000, 6000) > windowRms(ir, 30000, 34000));
}

TEST_CASE("Reverb room size lengthens the tail", "[reverb][dsp][unit]")
{
    auto small = impulseResponse(0.2f, 0.3f, 48000);
    auto big = impulseResponse(0.95f, 0.3f, 48000);
    // Late in the response, the bigger room still has more energy.
    REQUIRE(windowRms(big, 24000, 32000) > windowRms(small, 24000, 32000) * 1.5);
}

TEST_CASE("Reverb reset clears the tail", "[reverb][dsp][unit]")
{
    Reverb r;
    r.prepare(48000.0);
    r.setParameters(0.8f, 0.2f);
    std::vector<float> in(4000, 0.0f);
    in[0] = 1.0f;
    float* ch = in.data();
    r.processBlock(&ch, 1, 4000);
    r.reset();

    std::vector<float> silence(4000, 0.0f);
    float* ch2 = silence.data();
    r.processBlock(&ch2, 1, 4000);
    for (float v : silence)
        REQUIRE_THAT(v, WithinAbs(0.0f, 1e-6f)); // nothing left ringing
}

TEST_CASE("Reverb is finite and bounded", "[reverb][dsp][unit]")
{
    auto ir = impulseResponse(0.99f, 0.0f, 20000); // max room, no damping (worst case)
    for (float v : ir)
    {
        REQUIRE(std::isfinite(v));
        REQUIRE(std::abs(v) < 4.0f); // no runaway feedback
    }
}
