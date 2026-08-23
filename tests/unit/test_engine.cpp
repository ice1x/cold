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

TEST_CASE("Engine LFO->pitch modulation repitches the voice", "[engine][dsp][unit][mod][pitchmod]")
{
    constexpr int frames = 64;
    // A ramp sample (0,1,2,...) so the read position is directly observable.
    SourceSample s;
    s.channels.assign(1, std::vector<float>(static_cast<size_t>(frames)));
    for (int n = 0; n < frames; ++n)
        s.channels[0][static_cast<size_t>(n)] = static_cast<float>(n);
    s.rebuild();

    Engine engine;
    engine.prepare(44100.0);
    engine.setPolyphony(1);
    engine.setSample(s.data(), s.numChannels(), s.numFrames(), 44100.0);
    // A near-static square LFO sits at phase 0 -> +1 for the whole short block.
    engine.setLfo(cold::dsp::LfoShape::Square, 0.001f, 1.0f);
    engine.addModRoute(cold::dsp::ModDestination::Pitch, 12.0f); // +12 semis -> double speed

    engine.noteOn(kRootNote, 1.0f, kRootNote);
    auto out = render(engine, 8);
    // Pitch mod doubles the step, so the ramp is read 0,2,4,... within the block.
    for (int n = 0; n < 8; ++n)
        REQUIRE_THAT(out[static_cast<size_t>(n)], WithinAbs(static_cast<float>(2 * n), 1e-4));
}

TEST_CASE("Engine plays the selected sample from its list", "[engine][dsp][unit][sampleselect]")
{
    auto a = makeConstant(64, 0.2f);
    auto b = makeConstant(64, 0.5f);
    auto c = makeConstant(64, 1.0f);

    Engine engine;
    engine.prepare(44100.0);
    engine.setSampleList({{a.data(), a.numChannels(), a.numFrames(), 44100.0},
                          {b.data(), b.numChannels(), b.numFrames(), 44100.0},
                          {c.data(), c.numChannels(), c.numFrames(), 44100.0}});
    REQUIRE(engine.sampleCount() == 3);

    SECTION("base index 0 plays the first sample")
    {
        engine.noteOn(kRootNote, 1.0f, kRootNote);
        REQUIRE_THAT(render(engine, 4).front(), WithinAbs(0.2f, 1e-4));
    }

    SECTION("base index selects a different sample")
    {
        engine.setBaseSampleIndex(2);
        engine.noteOn(kRootNote, 1.0f, kRootNote);
        REQUIRE(engine.currentSampleIndex() == 2);
        REQUIRE_THAT(render(engine, 4).front(), WithinAbs(1.0f, 1e-4));
    }
}

TEST_CASE("Engine plays the base slot even when an earlier slot is empty",
          "[engine][dsp][unit][sampleselect]")
{
    // Layering feeds every track the whole bank; leading slots may be empty. A
    // track must still play its (loaded) base slot rather than gate on slot 0.
    auto b = makeConstant(64, 0.5f);

    Engine engine;
    engine.prepare(44100.0);
    engine.setSampleList({Engine::SampleRef{}, // slot 0 empty (invalid)
                          {b.data(), b.numChannels(), b.numFrames(), 44100.0}});
    engine.setBaseSampleIndex(1);
    engine.noteOn(kRootNote, 1.0f, kRootNote);

    REQUIRE(engine.currentSampleIndex() == 1);
    REQUIRE(engine.activeVoiceCount() == 1);
    REQUIRE_THAT(render(engine, 4).front(), WithinAbs(0.5f, 1e-4));
}

TEST_CASE("Engine is silent when the selected slot is empty", "[engine][dsp][unit][sampleselect]")
{
    auto b = makeConstant(64, 0.5f);

    Engine engine;
    engine.prepare(44100.0);
    engine.setSampleList({Engine::SampleRef{}, // slot 0 empty
                          {b.data(), b.numChannels(), b.numFrames(), 44100.0}});
    engine.setBaseSampleIndex(0); // base points at the empty slot
    engine.noteOn(kRootNote, 1.0f, kRootNote);

    REQUIRE(engine.activeVoiceCount() == 0);
}

TEST_CASE("Engine LFO drives sample-select at the trig", "[engine][dsp][unit][sampleselect]")
{
    auto a = makeConstant(64, 0.2f);
    auto b = makeConstant(64, 0.5f);
    auto c = makeConstant(64, 1.0f);

    Engine engine;
    engine.prepare(44100.0);
    engine.setSampleList({{a.data(), a.numChannels(), a.numFrames(), 44100.0},
                          {b.data(), b.numChannels(), b.numFrames(), 44100.0},
                          {c.data(), c.numChannels(), c.numFrames(), 44100.0}});
    engine.setBaseSampleIndex(1);                           // middle sample by default
    engine.setLfo(cold::dsp::LfoShape::Square, 1.0f, 1.0f); // phase 0 -> +1
    engine.addModRoute(cold::dsp::ModDestination::SampleSelect, 1.0f);

    // At the trig the square LFO reads +1 -> offset +1 -> index 2 (the 1.0 sample).
    engine.noteOn(kRootNote, 1.0f, kRootNote);
    REQUIRE(engine.currentSampleIndex() == 2);
    REQUIRE_THAT(render(engine, 4).front(), WithinAbs(1.0f, 1e-4));
}

TEST_CASE("Engine LFO custom shape feeds the designer waveform", "[engine][dsp][unit][custom]")
{
    auto a = makeConstant(64, 0.2f);
    auto b = makeConstant(64, 0.5f);
    auto c = makeConstant(64, 1.0f);

    auto build = [&](Engine& e)
    {
        e.prepare(44100.0);
        e.setSampleList({{a.data(), a.numChannels(), a.numFrames(), 44100.0},
                         {b.data(), b.numChannels(), b.numFrames(), 44100.0},
                         {c.data(), c.numChannels(), c.numFrames(), 44100.0}});
        e.setBaseSampleIndex(1);
        e.setLfo(cold::dsp::LfoShape::Custom, 0.001f, 1.0f); // near-static, phase ~0
        e.addModRoute(cold::dsp::ModDestination::SampleSelect, 1.0f);
    };

    SECTION("a +1 designer step selects the upper neighbour at the trig")
    {
        Engine e;
        build(e);
        const float up[] = {1.0f, 1.0f};
        e.setLfoCustomShape(up, 2);
        e.noteOn(kRootNote, 1.0f, kRootNote);
        REQUIRE(e.currentSampleIndex() == 2); // +1 offset -> base+1
    }

    SECTION("a -1 designer step selects the lower neighbour at the trig")
    {
        Engine e;
        build(e);
        const float down[] = {-1.0f, -1.0f};
        e.setLfoCustomShape(down, 2);
        e.noteOn(kRootNote, 1.0f, kRootNote);
        REQUIRE(e.currentSampleIndex() == 0); // -1 offset -> base-1
    }
}

TEST_CASE("Engine LFO trig mode resets the phase at the note", "[engine][dsp][unit][trigmode]")
{
    auto a = makeConstant(64, 0.2f);
    auto b = makeConstant(64, 0.5f);
    auto c = makeConstant(64, 1.0f);

    auto build = [&](Engine& e)
    {
        e.prepare(44100.0);
        e.setSampleList({{a.data(), a.numChannels(), a.numFrames(), 44100.0},
                         {b.data(), b.numChannels(), b.numFrames(), 44100.0},
                         {c.data(), c.numChannels(), c.numFrames(), 44100.0}});
        e.setBaseSampleIndex(1);
        // A fast square LFO: rendering 100 samples pushes the free-running phase
        // past 0.5 (square flips +1 -> -1) before the trig.
        e.setLfo(cold::dsp::LfoShape::Square, 264.6f, 1.0f);
        e.addModRoute(cold::dsp::ModDestination::SampleSelect, 1.0f);
    };

    SECTION("Trig resets phase, so the trig reads the cycle start (+1 -> upper sample)")
    {
        Engine e;
        build(e);
        e.setLfoTrigMode(cold::dsp::LfoTrigMode::Trig);
        render(e, 100); // advance the free-running phase past 0.5
        e.noteOn(kRootNote, 1.0f, kRootNote);
        REQUIRE(e.currentSampleIndex() == 2); // reset -> phase 0 -> +1 -> base+1
    }

    SECTION("Free keeps running, so the trig reads the advanced (-1) value")
    {
        Engine e;
        build(e);
        e.setLfoTrigMode(cold::dsp::LfoTrigMode::Free);
        render(e, 100);
        e.noteOn(kRootNote, 1.0f, kRootNote);
        REQUIRE(e.currentSampleIndex() == 0); // phase past 0.5 -> -1 -> base-1
    }
}

TEST_CASE("Engine stack mode triggers all layers together", "[engine][dsp][unit][stack]")
{
    auto a = makeConstant(64, 0.2f);
    auto b = makeConstant(64, 0.3f);
    auto c = makeConstant(64, 0.5f);

    Engine engine;
    engine.prepare(44100.0);
    engine.setSampleList({{a.data(), a.numChannels(), a.numFrames(), 44100.0},
                          {b.data(), b.numChannels(), b.numFrames(), 44100.0},
                          {c.data(), c.numChannels(), c.numFrames(), 44100.0}});
    engine.setLayerMode(Engine::LayerMode::Stack);
    engine.setLayers({{0, 1.0f, 0.0f, true}, {1, 1.0f, 0.0f, true}, {2, 1.0f, 0.0f, true}});

    engine.noteOn(kRootNote, 1.0f, kRootNote);
    REQUIRE(engine.activeVoiceCount() == 3); // one voice per layer

    auto out = render(engine, 4);
    for (float v : out)
        REQUIRE_THAT(v, WithinAbs(1.0f, 1e-4)); // 0.2 + 0.3 + 0.5 summed
}

TEST_CASE("Engine stack per-layer gain scales each layer", "[engine][dsp][unit][stack]")
{
    auto a = makeConstant(64, 0.2f);
    auto b = makeConstant(64, 0.3f);

    Engine engine;
    engine.prepare(44100.0);
    engine.setSampleList({{a.data(), a.numChannels(), a.numFrames(), 44100.0},
                          {b.data(), b.numChannels(), b.numFrames(), 44100.0}});
    engine.setLayerMode(Engine::LayerMode::Stack);
    engine.setLayers({{0, 0.5f, 0.0f, true}, {1, 1.0f, 0.0f, true}});

    engine.noteOn(kRootNote, 1.0f, kRootNote);
    auto out = render(engine, 4);
    for (float v : out)
        REQUIRE_THAT(v, WithinAbs(0.4f, 1e-4)); // 0.2*0.5 + 0.3*1.0
}

TEST_CASE("Engine stack per-layer tune repitches a layer", "[engine][dsp][unit][stack]")
{
    // A single ramp layer tuned +12 semitones plays back at double speed.
    SourceSample s;
    s.channels.assign(1, std::vector<float>(64));
    for (int n = 0; n < 64; ++n)
        s.channels[0][static_cast<size_t>(n)] = static_cast<float>(n);
    s.rebuild();

    Engine engine;
    engine.prepare(44100.0);
    engine.setPolyphony(1);
    engine.setSampleList({{s.data(), s.numChannels(), s.numFrames(), 44100.0}});
    engine.setLayerMode(Engine::LayerMode::Stack);
    engine.setLayers({{0, 1.0f, 12.0f, true}});

    engine.noteOn(kRootNote, 1.0f, kRootNote);
    auto out = render(engine, 8);
    for (int n = 0; n < 8; ++n)
        REQUIRE_THAT(out[static_cast<size_t>(n)], WithinAbs(static_cast<float>(2 * n), 1e-4));
}

TEST_CASE("Engine stack skips disabled and invalid layers", "[engine][dsp][unit][stack]")
{
    auto a = makeConstant(64, 0.2f);
    auto b = makeConstant(64, 0.3f);

    Engine engine;
    engine.prepare(44100.0);
    engine.setSampleList({{a.data(), a.numChannels(), a.numFrames(), 44100.0},
                          {b.data(), b.numChannels(), b.numFrames(), 44100.0}});
    engine.setLayerMode(Engine::LayerMode::Stack);
    // layer 0 on; layer 1 disabled; layer 2 out-of-range index -> skipped.
    engine.setLayers({{0, 1.0f, 0.0f, true}, {1, 1.0f, 0.0f, false}, {5, 1.0f, 0.0f, true}});

    engine.noteOn(kRootNote, 1.0f, kRootNote);
    REQUIRE(engine.activeVoiceCount() == 1); // only layer 0 sounds
    REQUIRE_THAT(render(engine, 4).front(), WithinAbs(0.2f, 1e-4));
}

TEST_CASE("Engine stack per-layer pan spreads layers across the stereo field",
          "[engine][dsp][unit][stack][pan]")
{
    auto a = makeConstant(64, 0.4f);
    auto b = makeConstant(64, 0.6f);

    Engine engine;
    engine.prepare(44100.0);
    engine.setSampleList({{a.data(), a.numChannels(), a.numFrames(), 44100.0},
                          {b.data(), b.numChannels(), b.numFrames(), 44100.0}});
    engine.setLayerMode(Engine::LayerMode::Stack);
    engine.setLayers({{0, 1.0f, 0.0f, true, -1.0f},  // layer A hard left
                      {1, 1.0f, 0.0f, true, 1.0f}}); // layer B hard right
    engine.noteOn(kRootNote, 1.0f, kRootNote);

    std::vector<std::vector<float>> out(2, std::vector<float>(4, 0.0f));
    std::vector<float*> ptrs{out[0].data(), out[1].data()};
    engine.renderNextBlock(ptrs.data(), 2, 0, 4);

    // Left carries only layer A (0.4), right only layer B (0.6).
    REQUIRE_THAT(out[0][0], WithinAbs(0.4f, 1e-4));
    REQUIRE_THAT(out[1][0], WithinAbs(0.6f, 1e-4));
}

TEST_CASE("Engine stack per-layer filter shapes each layer independently",
          "[engine][dsp][unit][stack][filter]")
{
    constexpr int frames = 8000;
    // A 6 kHz sine (bright, below Nyquist) in both slots.
    SourceSample s;
    s.channels.assign(1, std::vector<float>(static_cast<size_t>(frames)));
    for (int n = 0; n < frames; ++n)
        s.channels[0][static_cast<size_t>(n)] =
            static_cast<float>(std::sin(2.0 * 3.14159265358979 * 6000.0 * n / 48000.0));
    s.rebuild();

    Engine engine;
    engine.prepare(48000.0);
    engine.setSampleList({{s.data(), s.numChannels(), s.numFrames(), 48000.0},
                          {s.data(), s.numChannels(), s.numFrames(), 48000.0}});
    engine.setLayerMode(Engine::LayerMode::Stack);
    // Layer A hard-left with a low LP (dark); layer B hard-right wide open (bright).
    Engine::Layer a{0, 1.0f, 0.0f, true, -1.0f, true, 300.0f, 0.707f, 0.0f};
    Engine::Layer b{1, 1.0f, 0.0f, true, 1.0f, true, 12000.0f, 0.707f, 0.0f};
    engine.setLayers({a, b});
    engine.noteOn(kRootNote, 1.0f, kRootNote);

    std::vector<std::vector<float>> out(2, std::vector<float>(static_cast<size_t>(frames), 0.0f));
    std::vector<float*> ptrs{out[0].data(), out[1].data()};
    engine.renderNextBlock(ptrs.data(), 2, 0, frames);

    auto tail = [&](int ch)
    {
        float p = 0.0f;
        for (int n = frames - 1000; n < frames; ++n)
            p = std::max(p, std::abs(out[static_cast<size_t>(ch)][static_cast<size_t>(n)]));
        return p;
    };
    REQUIRE(tail(0) < 0.2f); // left (layer A) heavily low-passed
    REQUIRE(tail(1) > 0.7f); // right (layer B) passes the 6 kHz tone
}

TEST_CASE("Engine round-robin plays one layer per trig, cycling in order",
          "[engine][dsp][unit][roundrobin]")
{
    // Three distinct constant samples; each trig should sound exactly ONE of them,
    // advancing 0 -> 1 -> 2 -> 0 on successive note-ons (anti-machine-gun).
    auto a = makeConstant(64, 0.2f);
    auto b = makeConstant(64, 0.3f);
    auto c = makeConstant(64, 0.5f);

    Engine engine;
    engine.prepare(44100.0);
    engine.setPolyphony(1); // each trig steals the single voice, so we read it cleanly
    engine.setSampleList({{a.data(), a.numChannels(), a.numFrames(), 44100.0},
                          {b.data(), b.numChannels(), b.numFrames(), 44100.0},
                          {c.data(), c.numChannels(), c.numFrames(), 44100.0}});
    engine.setLayerMode(Engine::LayerMode::RoundRobin);
    engine.setLayers({{0, 1.0f, 0.0f, true}, {1, 1.0f, 0.0f, true}, {2, 1.0f, 0.0f, true}});

    const float expected[] = {0.2f, 0.3f, 0.5f, 0.2f, 0.3f};
    for (float want : expected)
    {
        engine.noteOn(kRootNote, 1.0f, kRootNote);
        REQUIRE(engine.activeVoiceCount() == 1); // one layer, not a stack
        REQUIRE_THAT(render(engine, 2).front(), WithinAbs(want, 1e-4));
    }
}

TEST_CASE("Engine round-robin honours per-layer gain, tune and pan",
          "[engine][dsp][unit][roundrobin]")
{
    auto a = makeConstant(64, 0.4f);
    auto b = makeConstant(64, 0.6f);

    Engine engine;
    engine.prepare(44100.0);
    engine.setPolyphony(1);
    engine.setSampleList({{a.data(), a.numChannels(), a.numFrames(), 44100.0},
                          {b.data(), b.numChannels(), b.numFrames(), 44100.0}});
    engine.setLayerMode(Engine::LayerMode::RoundRobin);
    engine.setLayers({{0, 0.5f, 0.0f, true, -1.0f},  // layer A: half gain, hard left
                      {1, 1.0f, 0.0f, true, 1.0f}}); // layer B: full gain, hard right

    // First trig -> layer A on the left only, scaled by 0.5.
    engine.noteOn(kRootNote, 1.0f, kRootNote);
    std::vector<std::vector<float>> out(2, std::vector<float>(2, 0.0f));
    std::vector<float*> ptrs{out[0].data(), out[1].data()};
    engine.renderNextBlock(ptrs.data(), 2, 0, 2);
    REQUIRE_THAT(out[0][0], WithinAbs(0.2f, 1e-4)); // 0.4 * 0.5, left
    REQUIRE_THAT(out[1][0], WithinAbs(0.0f, 1e-4)); // nothing on the right

    // Second trig -> layer B on the right only, full gain.
    engine.noteOn(kRootNote, 1.0f, kRootNote);
    for (auto& ch : out)
        std::fill(ch.begin(), ch.end(), 0.0f);
    engine.renderNextBlock(ptrs.data(), 2, 0, 2);
    REQUIRE_THAT(out[0][0], WithinAbs(0.0f, 1e-4));
    REQUIRE_THAT(out[1][0], WithinAbs(0.6f, 1e-4));
}

TEST_CASE("Engine round-robin skips disabled and invalid layers in the cycle",
          "[engine][dsp][unit][roundrobin]")
{
    auto a = makeConstant(64, 0.2f);
    auto b = makeConstant(64, 0.3f);

    Engine engine;
    engine.prepare(44100.0);
    engine.setPolyphony(1);
    engine.setSampleList({{a.data(), a.numChannels(), a.numFrames(), 44100.0},
                          {b.data(), b.numChannels(), b.numFrames(), 44100.0}});
    engine.setLayerMode(Engine::LayerMode::RoundRobin);
    // layer 0 on; layer 1 disabled; layer 2 out-of-range -> only 0 and 3 sound.
    engine.setLayers({{0, 1.0f, 0.0f, true},
                      {1, 1.0f, 0.0f, false},
                      {5, 1.0f, 0.0f, true},
                      {1, 1.0f, 0.0f, true}});

    const float expected[] = {0.2f, 0.3f, 0.2f, 0.3f}; // cycles only the playable pair
    for (float want : expected)
    {
        engine.noteOn(kRootNote, 1.0f, kRootNote);
        REQUIRE(engine.activeVoiceCount() == 1);
        REQUIRE_THAT(render(engine, 2).front(), WithinAbs(want, 1e-4));
    }
}

TEST_CASE("Engine velocity mode picks the layer by hit strength", "[engine][dsp][unit][velolayers]")
{
    // Three layers panned hard-left / centre / hard-right so the sounding layer is
    // identified by WHICH channel carries signal, independent of velocity level.
    // Equal velocity bands over 3 layers: [0,1/3)->L, [1/3,2/3)->C, [2/3,1]->R.
    auto s = makeConstant(64, 0.5f);
    Engine engine;
    engine.prepare(44100.0);
    engine.setPolyphony(1);
    engine.setSampleList({{s.data(), s.numChannels(), s.numFrames(), 44100.0},
                          {s.data(), s.numChannels(), s.numFrames(), 44100.0},
                          {s.data(), s.numChannels(), s.numFrames(), 44100.0}});
    engine.setLayerMode(Engine::LayerMode::Velocity);
    engine.setLayers({{0, 1.0f, 0.0f, true, -1.0f},  // soft: hard left
                      {1, 1.0f, 0.0f, true, 0.0f},   // medium: centre
                      {2, 1.0f, 0.0f, true, 1.0f}}); // loud: hard right

    auto hit = [&](float vel)
    {
        engine.noteOn(kRootNote, vel, kRootNote);
        std::vector<std::vector<float>> out(2, std::vector<float>(2, 0.0f));
        std::vector<float*> ptrs{out[0].data(), out[1].data()};
        engine.renderNextBlock(ptrs.data(), 2, 0, 2);
        REQUIRE(engine.activeVoiceCount() == 1); // one layer, not a stack
        return std::pair{std::abs(out[0][0]), std::abs(out[1][0])};
    };

    auto soft = hit(0.2f);
    REQUIRE(soft.first > 0.05f);  // left carries the soft layer
    REQUIRE(soft.second < 0.01f); // right silent

    auto medium = hit(0.5f);
    REQUIRE(medium.first > 0.05f); // centre spreads to both channels
    REQUIRE(medium.second > 0.05f);

    auto loud = hit(0.95f);
    REQUIRE(loud.second > 0.05f); // right carries the loud layer
    REQUIRE(loud.first < 0.01f);  // left silent

    // Full velocity clamps to the last (loudest) layer, not out of range.
    auto full = hit(1.0f);
    REQUIRE(full.second > 0.05f);
    REQUIRE(full.first < 0.01f);
}

TEST_CASE("Engine velocity mode maps bands over only the playable layers",
          "[engine][dsp][unit][velolayers]")
{
    // Two playable layers (0 and 3) with a disabled and an out-of-range layer
    // between them: bands split in HALF over the two that sound, not over 4 slots.
    auto s = makeConstant(64, 0.5f);
    Engine engine;
    engine.prepare(44100.0);
    engine.setPolyphony(1);
    engine.setSampleList({{s.data(), s.numChannels(), s.numFrames(), 44100.0},
                          {s.data(), s.numChannels(), s.numFrames(), 44100.0}});
    engine.setLayerMode(Engine::LayerMode::Velocity);
    engine.setLayers({{0, 1.0f, 0.0f, true, -1.0f},  // playable, hard left
                      {1, 1.0f, 0.0f, false, 0.0f},  // disabled
                      {5, 1.0f, 0.0f, true, 0.0f},   // out-of-range index
                      {1, 1.0f, 0.0f, true, 1.0f}}); // playable, hard right

    auto hit = [&](float vel)
    {
        engine.noteOn(kRootNote, vel, kRootNote);
        std::vector<std::vector<float>> out(2, std::vector<float>(2, 0.0f));
        std::vector<float*> ptrs{out[0].data(), out[1].data()};
        engine.renderNextBlock(ptrs.data(), 2, 0, 2);
        return std::pair{std::abs(out[0][0]), std::abs(out[1][0])};
    };

    auto lo = hit(0.3f); // lower half -> first playable (left)
    REQUIRE(lo.first > 0.05f);
    REQUIRE(lo.second < 0.01f);

    auto hi = hit(0.8f); // upper half -> last playable (right)
    REQUIRE(hi.second > 0.05f);
    REQUIRE(hi.first < 0.01f);
}

TEST_CASE("Engine defaults to Select mode (one voice per note)", "[engine][dsp][unit][stack]")
{
    auto s = makeConstant(64);
    auto engine = makeEngine(s, 4);
    REQUIRE(engine.layerMode() == Engine::LayerMode::Select);

    engine.noteOn(kRootNote, 1.0f, kRootNote);
    REQUIRE(engine.activeVoiceCount() == 1); // Select: a single voice, not a stack
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

TEST_CASE("Engine stopAllNotes releases every active voice", "[engine][dsp][unit][choke]")
{
    // Sustaining (looped) voices keep sounding until stopped; stopAllNotes releases
    // them all at once (a zero-length release frees them immediately) — the primitive
    // the processor uses to implement choke / mute groups.
    auto s = makeConstant(64);
    auto engine = makeEngine(s, 4);
    engine.setLoopMode(SampleVoice::LoopMode::Forward);
    engine.setLoopPoints(0, 64);

    engine.noteOn(kRootNote, 1.0f, kRootNote);
    engine.noteOn(kRootNote + 4, 1.0f, kRootNote);
    REQUIRE(engine.activeVoiceCount() == 2);

    engine.stopAllNotes();
    REQUIRE(engine.activeVoiceCount() == 0);
}

TEST_CASE("Engine per-track compressor attenuates the loud mix", "[engine][dsp][unit][compressor]")
{
    constexpr int kRoot = 60;
    auto s = makeConstant(20000, 1.0f); // constant full-scale -> 0 dBFS mix

    auto engine = makeEngine(s, 4);

    // Baseline: no compressor -> the mix sits near full level.
    engine.noteOn(kRoot, 1.0f, kRoot);
    auto dry = renderChunked(engine, 4000, 256);
    REQUIRE(windowRms(dry, 3000, 500) > 0.9);

    // Heavy compressor on a fresh note: the summed track output is pulled down.
    engine.reset();
    engine.setCompressor(true, -20.0f, 8.0f, 0.001f, 0.05f, 0.0f);
    engine.noteOn(kRoot, 1.0f, kRoot);
    auto wet = renderChunked(engine, 4000, 256);
    const double wetRms = windowRms(wet, 3000, 500);
    REQUIRE(wetRms < 0.5);  // clearly compressed from ~1.0
    REQUIRE(wetRms > 0.05); // but still audible

    // Disabling it restores the full-level path.
    engine.reset();
    engine.setCompressor(false, -20.0f, 8.0f, 0.001f, 0.05f, 0.0f);
    engine.noteOn(kRoot, 1.0f, kRoot);
    auto restored = renderChunked(engine, 4000, 256);
    REQUIRE(windowRms(restored, 3000, 500) > 0.9);
}

TEST_CASE("Engine per-track saturator shapes the summed mix", "[engine][dsp][unit][saturator]")
{
    constexpr int kRoot = 60;
    auto s = makeConstant(20000, 0.5f); // constant 0.5 -> a steady 0.5 mix

    auto engine = makeEngine(s, 4);

    // Dry baseline: the mix sits at ~0.5.
    engine.noteOn(kRoot, 1.0f, kRoot);
    auto dry = renderChunked(engine, 2000, 256);
    REQUIRE_THAT(windowRms(dry, 1000, 500), WithinAbs(0.5, 0.02));

    // Drive 4, full wet: tanh(4 * 0.5) = tanh(2) ~= 0.964 -> the mix is pushed up.
    engine.reset();
    engine.setSaturator(true, 4.0f, 1.0f);
    engine.noteOn(kRoot, 1.0f, kRoot);
    auto wet = renderChunked(engine, 2000, 256);
    REQUIRE_THAT(windowRms(wet, 1000, 500), WithinAbs(std::tanh(2.0), 0.02));

    // Disabling restores the dry level.
    engine.reset();
    engine.setSaturator(false, 4.0f, 1.0f);
    engine.noteOn(kRoot, 1.0f, kRoot);
    auto restored = renderChunked(engine, 2000, 256);
    REQUIRE_THAT(windowRms(restored, 1000, 500), WithinAbs(0.5, 0.02));
}
