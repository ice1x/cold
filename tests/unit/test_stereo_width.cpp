// Unit tests for the framework-agnostic master stereo-width (mid/side) control.

#include "dsp/StereoWidth.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using cold::dsp::StereoWidth;

namespace
{
// A hard-panned pair: left carries `l` everywhere, right carries `r`.
void fill(std::vector<float>& l, std::vector<float>& r, float lv, float rv)
{
    std::fill(l.begin(), l.end(), lv);
    std::fill(r.begin(), r.end(), rv);
}
} // namespace

TEST_CASE("StereoWidth is bypassed until enabled", "[stereowidth][dsp][unit]")
{
    StereoWidth sw;
    sw.setWidth(0.0f); // would collapse to mono if it ran

    std::vector<float> l(256), r(256);
    fill(l, r, 1.0f, -1.0f);
    float* ch[2] = {l.data(), r.data()};
    sw.processBlock(ch, 2, 256);

    REQUIRE_THAT(l[0], WithinAbs(1.0f, 1e-6f));
    REQUIRE_THAT(r[0], WithinAbs(-1.0f, 1e-6f));
}

TEST_CASE("StereoWidth at unity leaves the image untouched", "[stereowidth][dsp][unit]")
{
    StereoWidth sw;
    sw.setWidth(1.0f);
    sw.setEnabled(true);

    std::vector<float> l(256), r(256);
    fill(l, r, 0.8f, -0.3f);
    float* ch[2] = {l.data(), r.data()};
    sw.processBlock(ch, 2, 256);

    REQUIRE_THAT(l[0], WithinAbs(0.8f, 1e-6f));
    REQUIRE_THAT(r[0], WithinAbs(-0.3f, 1e-6f));
}

TEST_CASE("StereoWidth at zero collapses to mono", "[stereowidth][dsp][unit]")
{
    StereoWidth sw;
    sw.setWidth(0.0f);
    sw.setEnabled(true);

    std::vector<float> l(256), r(256);
    fill(l, r, 1.0f, -1.0f); // mid = 0, side = 0 after collapse
    float* ch[2] = {l.data(), r.data()};
    sw.processBlock(ch, 2, 256);

    // Both channels become the mid ((1 + -1)/2 = 0), and are equal.
    REQUIRE_THAT(l[0], WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(r[0], WithinAbs(0.0f, 1e-6f));

    // A non-zero mid: L=0.6, R=0.2 -> mid 0.4, both channels -> 0.4.
    fill(l, r, 0.6f, 0.2f);
    sw.processBlock(ch, 2, 256);
    REQUIRE_THAT(l[0], WithinAbs(0.4f, 1e-6f));
    REQUIRE_THAT(r[0], WithinAbs(0.4f, 1e-6f));
}

TEST_CASE("StereoWidth above unity widens the image", "[stereowidth][dsp][unit]")
{
    StereoWidth sw;
    sw.setWidth(2.0f);
    sw.setEnabled(true);

    // L=0.6, R=0.2 -> mid 0.4, side 0.2; at width 2 side -> 0.4, so L=0.8, R=0.0.
    std::vector<float> l(256), r(256);
    fill(l, r, 0.6f, 0.2f);
    float* ch[2] = {l.data(), r.data()};
    sw.processBlock(ch, 2, 256);

    REQUIRE_THAT(l[0], WithinAbs(0.8f, 1e-6f));
    REQUIRE_THAT(r[0], WithinAbs(0.0f, 1e-6f));
    // The channel difference (the width) has doubled from 0.4 to 0.8.
    REQUIRE_THAT(l[0] - r[0], WithinAbs(0.8f, 1e-6f));
}

TEST_CASE("StereoWidth leaves a mono signal unchanged at any width", "[stereowidth][dsp][unit]")
{
    StereoWidth sw;
    sw.setEnabled(true);

    for (float w : {0.0f, 1.0f, 2.0f})
    {
        sw.setWidth(w);
        std::vector<float> l(256), r(256);
        fill(l, r, 0.5f, 0.5f); // L == R -> side 0
        float* ch[2] = {l.data(), r.data()};
        sw.processBlock(ch, 2, 256);
        REQUIRE_THAT(l[0], WithinAbs(0.5f, 1e-6f));
        REQUIRE_THAT(r[0], WithinAbs(0.5f, 1e-6f));
    }
}

TEST_CASE("StereoWidth leaves a mono buffer untouched", "[stereowidth][dsp][unit]")
{
    StereoWidth sw;
    sw.setWidth(0.0f);
    sw.setEnabled(true);

    std::vector<float> mono(256, 0.7f);
    float* ch[1] = {mono.data()};
    sw.processBlock(ch, 1, 256); // numChannels < 2 -> no-op
    REQUIRE_THAT(mono[0], WithinAbs(0.7f, 1e-6f));
}
