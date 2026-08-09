#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/Engine.h"

#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using cold::dsp::Engine;
using cold::dsp::SampleVoice;

namespace
{

constexpr int kRootNote = 60;

// A non-owning channel-pointer view over owned vectors.
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

// A single-channel constant-1 sample: each voice outputs its gain per sample,
// independent of pitch, so the mix is just (active voices × gain) — easy to check.
SourceSample makeConstant(int numFrames, float value = 1.0f)
{
    SourceSample s;
    s.channels.assign(1, std::vector<float>(static_cast<size_t>(numFrames), value));
    s.rebuild();
    return s;
}

std::vector<float> render(Engine& engine, int numSamples)
{
    std::vector<float> out(static_cast<size_t>(numSamples), 0.0f);
    float* ptr = out.data();
    engine.renderNextBlock(&ptr, 1, 0, numSamples);
    return out;
}

Engine makeEngine(const SourceSample& s, int polyphony)
{
    Engine engine;
    engine.prepare(44100.0);
    engine.setPolyphony(polyphony);
    engine.setSample(s.data(), s.numChannels(), s.numFrames(), 44100.0);
    return engine;
}

// Render `total` samples in small blocks (like the host) so block-rate modulation
// updates over the buffer. Returns the mono output.
std::vector<float> renderChunked(Engine& engine, int total, int blockSize)
{
    std::vector<float> out(static_cast<size_t>(total), 0.0f);
    int pos = 0;
    while (pos < total)
    {
        const int n = std::min(blockSize, total - pos);
        float* ptr = out.data() + pos;
        engine.renderNextBlock(&ptr, 1, 0, n);
        pos += n;
    }
    return out;
}

// RMS of a window [start, start+len).
double windowRms(const std::vector<float>& buf, int start, int len)
{
    double sum = 0.0;
    for (int n = start; n < start + len && n < static_cast<int>(buf.size()); ++n)
        sum += static_cast<double>(buf[static_cast<size_t>(n)]) * buf[static_cast<size_t>(n)];
    return std::sqrt(sum / len);
}

} // namespace

TEST_CASE("Engine starts with no active voices", "[engine][dsp][unit]")
{
    auto s = makeConstant(64);
    auto engine = makeEngine(s, 4);
    REQUIRE(engine.activeVoiceCount() == 0);
}

TEST_CASE("Engine plays a chord across the voice pool", "[engine][dsp][unit]")
{
    auto s = makeConstant(64);
    auto engine = makeEngine(s, 4);

    engine.noteOn(kRootNote, 1.0f, kRootNote);
    engine.noteOn(kRootNote + 4, 1.0f, kRootNote);
    engine.noteOn(kRootNote + 7, 1.0f, kRootNote);
    REQUIRE(engine.activeVoiceCount() == 3);

    // Constant-1 sample: three voices at unity gain sum to 3.0 per sample.
    auto out = render(engine, 8);
    for (float v : out)
        REQUIRE_THAT(v, WithinAbs(3.0f, 1e-4));
}

TEST_CASE("Engine mixes voices additively with velocity gain", "[engine][dsp][unit]")
{
    auto s = makeConstant(64);
    auto engine = makeEngine(s, 4);

    engine.noteOn(kRootNote, 0.5f, kRootNote);
    engine.noteOn(kRootNote + 7, 0.25f, kRootNote);

    auto out = render(engine, 8);
    for (float v : out)
        REQUIRE_THAT(v, WithinAbs(0.75f, 1e-4));
}

TEST_CASE("Engine steals the oldest voice when the pool is full", "[engine][dsp][unit]")
{
    auto s = makeConstant(64);
    auto engine = makeEngine(s, 2);

    engine.noteOn(kRootNote + 0, 1.0f, kRootNote); // oldest
    engine.noteOn(kRootNote + 2, 1.0f, kRootNote);
    engine.noteOn(kRootNote + 4, 1.0f, kRootNote); // forces a steal

    REQUIRE(engine.activeVoiceCount() == 2);
    REQUIRE_FALSE(engine.isNotePlaying(kRootNote + 0)); // oldest was stolen
    REQUIRE(engine.isNotePlaying(kRootNote + 2));
    REQUIRE(engine.isNotePlaying(kRootNote + 4));
}

TEST_CASE("Engine noteOff stops a sustaining looped voice", "[engine][dsp][unit]")
{
    auto s = makeConstant(64);
    auto engine = makeEngine(s, 4);
    engine.setLoopMode(SampleVoice::LoopMode::Forward);
    engine.setLoopPoints(0, 64);

    engine.noteOn(kRootNote, 1.0f, kRootNote);
    REQUIRE(engine.isNotePlaying(kRootNote));
    REQUIRE(engine.activeVoiceCount() == 1);

    engine.noteOff(kRootNote);
    REQUIRE_FALSE(engine.isNotePlaying(kRootNote));
    REQUIRE(engine.activeVoiceCount() == 0);
}

TEST_CASE("Engine reuses a voice that freed itself at end of sample", "[engine][dsp][unit]")
{
    auto s = makeConstant(8); // short one-shot
    auto engine = makeEngine(s, 2);

    engine.noteOn(kRootNote, 1.0f, kRootNote);
    render(engine, 12); // play past the end; the one-shot voice frees itself
    REQUIRE(engine.activeVoiceCount() == 0);

    engine.noteOn(kRootNote + 5, 1.0f, kRootNote);
    REQUIRE(engine.activeVoiceCount() == 1); // reused the freed slot, not a 2nd
}

TEST_CASE("Engine polyphony is at least one voice", "[engine][dsp][unit]")
{
    auto s = makeConstant(64);
    auto engine = makeEngine(s, 0); // clamped up to 1
    REQUIRE(engine.polyphony() >= 1);

    engine.noteOn(kRootNote, 1.0f, kRootNote);
    engine.noteOn(kRootNote + 3, 1.0f, kRootNote); // steals the single voice
    REQUIRE(engine.activeVoiceCount() == 1);
}

TEST_CASE("Engine setFilter puts a filter in every voice's path", "[engine][dsp][unit][filter]")
{
    constexpr int frames = 4000;
    // Bright alternating +1/-1 sample.
    SourceSample s;
    s.channels.assign(1, std::vector<float>(static_cast<size_t>(frames)));
    for (int n = 0; n < frames; ++n)
        s.channels[0][static_cast<size_t>(n)] = (n % 2 == 0) ? 1.0f : -1.0f;
    s.rebuild();

    Engine engine;
    engine.prepare(48000.0);
    engine.setPolyphony(4);
    engine.setSample(s.data(), s.numChannels(), s.numFrames(), 48000.0);
    engine.setFilter(300.0f, 0.707f, 0.0f); // low-pass the whole pool

    engine.noteOn(kRootNote, 1.0f, kRootNote);
    auto out = render(engine, frames);

    // Measure the settled tail; the bright content is heavily attenuated.
    float peak = 0.0f;
    for (int n = frames - 1000; n < frames; ++n)
        peak = std::max(peak, std::abs(out[static_cast<size_t>(n)]));
    REQUIRE(peak < 0.2f);
}

TEST_CASE("Engine LFO->cutoff modulation makes the output sweep over time",
          "[engine][dsp][unit][mod]")
{
    constexpr int frames = 48000;
    // Bright alternating +1/-1 sample so cutoff changes are audible as level.
    SourceSample s;
    s.channels.assign(1, std::vector<float>(static_cast<size_t>(frames)));
    for (int n = 0; n < frames; ++n)
        s.channels[0][static_cast<size_t>(n)] = (n % 2 == 0) ? 1.0f : -1.0f;
    s.rebuild();

    Engine engine;
    engine.prepare(48000.0);
    engine.setPolyphony(4);
    engine.setSample(s.data(), s.numChannels(), s.numFrames(), 48000.0);
    engine.setFilter(500.0f, 0.707f, 0.0f);                            // low-pass base
    engine.setLfo(cold::dsp::LfoShape::Sine, 5.0f, 1.0f);              // slow 5 Hz sweep
    engine.addModRoute(cold::dsp::ModDestination::FilterCutoff, 4.0f); // +/- 4 octaves

    engine.noteOn(kRootNote, 1.0f, kRootNote);
    // ~2 LFO cycles at 5 Hz, rendered in host-sized blocks so the LFO updates.
    auto out = renderChunked(engine, 19200, 64);

    // Scan windowed RMS; the sweeping cutoff must produce clear level variation.
    double lo = 1e9, hi = 0.0;
    for (int start = 2000; start + 800 <= 19200; start += 800)
    {
        const double r = windowRms(out, start, 800);
        lo = std::min(lo, r);
        hi = std::max(hi, r);
    }
    REQUIRE(hi > 3.0 * lo); // amplitude clearly modulated by the LFO
}

TEST_CASE("Engine with no mod routes leaves the filter static", "[engine][dsp][unit][mod]")
{
    constexpr int frames = 48000;
    SourceSample s;
    s.channels.assign(1, std::vector<float>(static_cast<size_t>(frames), 0.25f));
    s.rebuild();

    Engine engine;
    engine.prepare(48000.0);
    engine.setSample(s.data(), s.numChannels(), s.numFrames(), 48000.0);
    engine.setFilter(2000.0f, 0.707f, 0.0f);
    engine.setLfo(cold::dsp::LfoShape::Sine, 5.0f, 1.0f); // LFO set but not routed

    engine.noteOn(kRootNote, 1.0f, kRootNote);
    auto out = renderChunked(engine, 9600, 64);

    // A constant DC-ish input through a static LP settles to a steady level.
    const double early = windowRms(out, 4000, 800);
    const double late = windowRms(out, 8000, 800);
    REQUIRE_THAT(early, WithinAbs(late, 1e-3));
}

TEST_CASE("Engine reset frees every voice", "[engine][dsp][unit]")
{
    auto s = makeConstant(64);
    auto engine = makeEngine(s, 4);
    engine.noteOn(kRootNote, 1.0f, kRootNote);
    engine.noteOn(kRootNote + 4, 1.0f, kRootNote);
    REQUIRE(engine.activeVoiceCount() == 2);

    engine.reset();
    REQUIRE(engine.activeVoiceCount() == 0);
}
