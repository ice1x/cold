#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/Delay.h"

#include <vector>

using Catch::Matchers::WithinAbs;
using cold::dsp::Delay;

namespace
{
// Process one mono block in place and return it.
std::vector<float> run(Delay& d, std::vector<float> mono)
{
    float* ch = mono.data();
    d.processBlock(&ch, 1, static_cast<int>(mono.size()));
    return mono;
}
} // namespace

TEST_CASE("Delay echoes an impulse after the delay time", "[delay][dsp][unit]")
{
    constexpr double sr = 48000.0;
    Delay d;
    d.prepare(sr, 1.0);
    d.setParameters(0.01f, 0.0f, 0.0f); // 480-sample delay, no feedback, bright

    std::vector<float> in(2000, 0.0f);
    in[0] = 1.0f; // impulse
    auto out = run(d, in);

    REQUIRE_THAT(out[0], WithinAbs(0.0f, 1e-6f));   // pure wet: dry not present at n=0
    REQUIRE_THAT(out[480], WithinAbs(1.0f, 1e-4f)); // one echo at the delay time
    REQUIRE_THAT(out[960], WithinAbs(0.0f, 1e-4f)); // no feedback -> no second echo
}

TEST_CASE("Delay feedback produces decaying repeats", "[delay][dsp][unit]")
{
    constexpr double sr = 48000.0;
    Delay d;
    d.prepare(sr, 1.0);
    d.setParameters(0.01f, 0.5f, 0.0f); // 480 samples, 50% feedback, bright

    std::vector<float> in(2500, 0.0f);
    in[0] = 1.0f;
    auto out = run(d, in);

    REQUIRE_THAT(out[480], WithinAbs(1.0f, 1e-4f));   // first echo
    REQUIRE_THAT(out[960], WithinAbs(0.5f, 1e-4f));   // second = feedback * first
    REQUIRE_THAT(out[1440], WithinAbs(0.25f, 1e-4f)); // third = feedback^2
}

TEST_CASE("Delay tone darkens the repeats (feedback low-pass)", "[delay][dsp][unit]")
{
    constexpr double sr = 48000.0;
    // A bright alternating +1/-1 fed once; with max tone the fed-back copy is heavily
    // low-passed, so the second echo's high-frequency content is much smaller than
    // with no tone.
    auto energy = [&](float tone)
    {
        Delay d;
        d.prepare(sr, 1.0);
        d.setParameters(0.005f, 0.8f, tone); // 240-sample delay
        std::vector<float> in(2000, 0.0f);
        for (int n = 0; n < 240; ++n)
            in[static_cast<size_t>(n)] = (n % 2 == 0) ? 1.0f : -1.0f; // bright burst
        auto out = run(d, in);
        float e = 0.0f;
        for (int n = 720; n < 960; ++n) // the third echo window
            e += std::abs(out[static_cast<size_t>(n)]);
        return e;
    };

    REQUIRE(energy(1.0f) < energy(0.0f) * 0.6f); // dark repeats lose the bright content
}

TEST_CASE("Delay reset clears the line", "[delay][dsp][unit]")
{
    constexpr double sr = 48000.0;
    Delay d;
    d.prepare(sr, 1.0);
    d.setParameters(0.01f, 0.5f, 0.0f);

    std::vector<float> in(1000, 0.0f);
    in[0] = 1.0f;
    run(d, in);
    d.reset();

    std::vector<float> silence(1000, 0.0f);
    auto out = run(d, silence);
    for (float v : out)
        REQUIRE_THAT(v, WithinAbs(0.0f, 1e-6f)); // nothing left ringing
}
