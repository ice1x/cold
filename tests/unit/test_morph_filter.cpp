#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/MorphFilter.h"

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using cold::dsp::MorphFilter;

namespace
{
constexpr double kPi = 3.14159265358979323846;

// Run a sine of `freq` Hz through the filter and return the RMS of one output,
// measured over the settled second half of the run.
struct Rms
{
    double lp = 0, bp = 0, hp = 0, notch = 0;
};

Rms sineRms(MorphFilter& f, double sampleRate, double freq, int numSamples)
{
    double slp = 0, sbp = 0, shp = 0, sn = 0;
    const int start = numSamples / 2;
    int counted = 0;
    for (int n = 0; n < numSamples; ++n)
    {
        const auto x = static_cast<float>(std::sin(2.0 * kPi * freq * n / sampleRate));
        const auto o = f.processAll(x);
        if (n >= start)
        {
            slp += static_cast<double>(o.lp) * o.lp;
            sbp += static_cast<double>(o.bp) * o.bp;
            shp += static_cast<double>(o.hp) * o.hp;
            sn += static_cast<double>(o.notch) * o.notch;
            ++counted;
        }
    }
    const auto d = static_cast<double>(counted);
    return {std::sqrt(slp / d), std::sqrt(sbp / d), std::sqrt(shp / d), std::sqrt(sn / d)};
}
} // namespace

TEST_CASE("MorphFilter notch equals lp+hp every sample", "[morphfilter][dsp][unit]")
{
    MorphFilter f;
    f.prepare(48000.0);
    f.setCutoff(1200.0f);
    f.setResonance(1.5f);

    // Feed an arbitrary signal; the SVF identity notch == lp + hp is exact.
    for (int n = 0; n < 256; ++n)
    {
        const auto x = static_cast<float>(std::sin(0.3 * n) + 0.5 * std::sin(0.017 * n));
        const auto o = f.processAll(x);
        REQUIRE_THAT(o.notch, WithinAbs(o.lp + o.hp, 1e-4));
    }
}

TEST_CASE("MorphFilter passes DC through the low-pass only", "[morphfilter][dsp][unit]")
{
    MorphFilter f;
    f.prepare(48000.0);
    f.setCutoff(1000.0f);
    f.setResonance(0.707f);

    MorphFilter::Outputs o{};
    for (int n = 0; n < 4000; ++n)
        o = f.processAll(1.0f); // settle on a DC input

    REQUIRE_THAT(o.lp, WithinAbs(1.0f, 1e-3));    // LP passes DC
    REQUIRE_THAT(o.hp, WithinAbs(0.0f, 1e-3));    // HP blocks DC
    REQUIRE_THAT(o.bp, WithinAbs(0.0f, 1e-3));    // BP blocks DC
    REQUIRE_THAT(o.notch, WithinAbs(1.0f, 1e-3)); // notch = lp + hp
}

TEST_CASE("MorphFilter low cutoff attenuates a high-frequency sine", "[morphfilter][dsp][unit]")
{
    MorphFilter f;
    f.prepare(48000.0);
    f.setCutoff(200.0f); // well below the test tone
    f.setResonance(0.707f);

    const auto rms = sineRms(f, 48000.0, 8000.0, 8000);
    const double inputRms = 1.0 / std::sqrt(2.0); // ~0.707

    REQUIRE(rms.lp < 0.2 * inputRms); // strong LP attenuation up high
    REQUIRE(rms.hp > 0.7 * inputRms); // HP passes the high tone
}

TEST_CASE("MorphFilter high cutoff attenuates a low-frequency sine", "[morphfilter][dsp][unit]")
{
    MorphFilter f;
    f.prepare(48000.0);
    f.setCutoff(10000.0f); // well above the test tone
    f.setResonance(0.707f);

    const auto rms = sineRms(f, 48000.0, 200.0, 8000);
    const double inputRms = 1.0 / std::sqrt(2.0);

    REQUIRE(rms.hp < 0.2 * inputRms); // strong HP attenuation down low
    REQUIRE(rms.lp > 0.7 * inputRms); // LP passes the low tone
}

TEST_CASE("MorphFilter resonance raises the band-pass peak at cutoff", "[morphfilter][dsp][unit]")
{
    const double fc = 1000.0;

    MorphFilter lowQ;
    lowQ.prepare(48000.0);
    lowQ.setCutoff(static_cast<float>(fc));
    lowQ.setResonance(0.707f);
    const auto rmsLow = sineRms(lowQ, 48000.0, fc, 8000);

    MorphFilter highQ;
    highQ.prepare(48000.0);
    highQ.setCutoff(static_cast<float>(fc));
    highQ.setResonance(6.0f);
    const auto rmsHigh = sineRms(highQ, 48000.0, fc, 8000);

    REQUIRE(rmsHigh.bp > rmsLow.bp); // higher Q -> sharper, taller BP peak
}

TEST_CASE("MorphFilter stays finite and bounded on a loud burst", "[morphfilter][dsp][unit]")
{
    MorphFilter f;
    f.prepare(48000.0);
    f.setCutoff(500.0f);
    f.setResonance(8.0f); // high resonance stress

    for (int n = 0; n < 20000; ++n)
    {
        const auto x = static_cast<float>(std::sin(2.0 * kPi * 500.0 * n / 48000.0));
        const auto o = f.processAll(x);
        REQUIRE(std::isfinite(o.lp));
        REQUIRE(std::isfinite(o.bp));
        REQUIRE(std::isfinite(o.hp));
        REQUIRE(std::abs(o.bp) < 100.0f); // resonant but not blowing up
    }
}
