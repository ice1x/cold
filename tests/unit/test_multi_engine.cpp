#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/MultiEngine.h"

#include <vector>

using Catch::Matchers::WithinAbs;
using cold::dsp::MultiEngine;

namespace
{
constexpr int kRootNote = 60;

struct SourceSample
{
    std::vector<std::vector<float>> channels;
    std::vector<const float*> ptrs;
    void rebuild()
    {
        ptrs.clear();
        for (const auto& c : channels)
            ptrs.push_back(c.data());
    }
    [[nodiscard]] const float* const* data() const { return ptrs.data(); }
    [[nodiscard]] int numChannels() const { return static_cast<int>(channels.size()); }
    [[nodiscard]] int numFrames() const
    {
        return channels.empty() ? 0 : static_cast<int>(channels.front().size());
    }
};

SourceSample makeConstant(int numFrames, float value)
{
    SourceSample s;
    s.channels.assign(1, std::vector<float>(static_cast<size_t>(numFrames), value));
    s.rebuild();
    return s;
}

void assign(MultiEngine& e, int track, const SourceSample& s)
{
    e.track(track).setSample(s.data(), s.numChannels(), s.numFrames(), 44100.0);
}

std::vector<float> render(MultiEngine& e, int numSamples)
{
    std::vector<float> out(static_cast<size_t>(numSamples), 0.0f);
    float* ptr = out.data();
    e.renderNextBlock(&ptr, 1, 0, numSamples);
    return out;
}
} // namespace

TEST_CASE("MultiEngine has eight tracks", "[multiengine][dsp][unit]")
{
    MultiEngine e;
    e.prepare(44100.0);
    REQUIRE(e.numTracks() == 8);
    REQUIRE(e.activeVoiceCount() == 0);
}

TEST_CASE("MultiEngine tracks play independent samples", "[multiengine][dsp][unit]")
{
    MultiEngine e;
    e.prepare(44100.0);

    auto a = makeConstant(64, 1.0f);
    auto b = makeConstant(64, 0.25f);
    assign(e, 0, a);
    assign(e, 1, b);

    SECTION("only the triggered track sounds")
    {
        e.noteOn(0, kRootNote, 1.0f, kRootNote);
        auto out = render(e, 8);
        for (float v : out)
            REQUIRE_THAT(v, WithinAbs(1.0f, 1e-4)); // track 0 only
    }

    SECTION("two tracks sum")
    {
        e.noteOn(0, kRootNote, 1.0f, kRootNote);
        e.noteOn(1, kRootNote, 1.0f, kRootNote);
        auto out = render(e, 8);
        for (float v : out)
            REQUIRE_THAT(v, WithinAbs(1.25f, 1e-4)); // 1.0 + 0.25
    }
}

TEST_CASE("MultiEngine tracks have independent voice pools", "[multiengine][dsp][unit]")
{
    MultiEngine e;
    e.prepare(44100.0);
    auto s = makeConstant(64, 1.0f);
    assign(e, 0, s);
    assign(e, 2, s);

    e.track(0).setPolyphony(1);
    e.noteOn(0, kRootNote, 1.0f, kRootNote);
    e.noteOn(0, kRootNote + 5, 1.0f, kRootNote); // steals track 0's single voice
    e.noteOn(2, kRootNote, 1.0f, kRootNote);     // unaffected track

    REQUIRE(e.track(0).activeVoiceCount() == 1);
    REQUIRE(e.track(2).activeVoiceCount() == 1);
    REQUIRE(e.activeVoiceCount() == 2);
}

TEST_CASE("MultiEngine maps MIDI channels to tracks Digitakt-style",
          "[multiengine][dsp][unit][routing]")
{
    REQUIRE(MultiEngine::trackForChannel(1) == 0);
    REQUIRE(MultiEngine::trackForChannel(8) == 7);
    REQUIRE(MultiEngine::trackForChannel(9) == -1);  // beyond the 8 tracks
    REQUIRE(MultiEngine::trackForChannel(0) == -1);  // invalid
    REQUIRE(MultiEngine::trackForChannel(17) == -1); // invalid
}

TEST_CASE("MultiEngine routes note on/off by channel", "[multiengine][dsp][unit][routing]")
{
    MultiEngine e;
    e.prepare(44100.0);
    auto s = makeConstant(64, 1.0f);
    for (int t = 0; t < 8; ++t)
        assign(e, t, s);

    e.routeNoteOn(3, kRootNote, 1.0f, kRootNote); // channel 3 -> track 2
    REQUIRE(e.track(2).isNotePlaying(kRootNote));
    REQUIRE(e.track(0).activeVoiceCount() == 0);

    e.routeNoteOff(3, kRootNote);
    REQUIRE_FALSE(e.track(2).isNotePlaying(kRootNote));
}

TEST_CASE("MultiEngine ignores notes on unmapped channels", "[multiengine][dsp][unit][routing]")
{
    MultiEngine e;
    e.prepare(44100.0);
    auto s = makeConstant(64, 1.0f);
    for (int t = 0; t < 8; ++t)
        assign(e, t, s);

    e.routeNoteOn(12, kRootNote, 1.0f, kRootNote); // channel 12 -> no track
    REQUIRE(e.activeVoiceCount() == 0);
}

TEST_CASE("MultiEngine reset frees all tracks", "[multiengine][dsp][unit]")
{
    MultiEngine e;
    e.prepare(44100.0);
    auto s = makeConstant(64, 1.0f);
    assign(e, 0, s);
    assign(e, 1, s);
    e.noteOn(0, kRootNote, 1.0f, kRootNote);
    e.noteOn(1, kRootNote, 1.0f, kRootNote);
    REQUIRE(e.activeVoiceCount() == 2);

    e.reset();
    REQUIRE(e.activeVoiceCount() == 0);
}
