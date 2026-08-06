#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/SampleVoice.h"

#include <vector>

using Catch::Matchers::WithinAbs;
using cold::dsp::SampleVoice;

namespace
{

constexpr int kRootNote = 60; // middle C — sample plays at native pitch here.

// A non-owning channel-pointer view over owned vectors, as SampleVoice consumes.
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

// A single-channel constant sample, so output equals the applied gain per sample.
SourceSample makeConstant(int numFrames, float value = 1.0f)
{
    SourceSample s;
    s.channels.assign(1, std::vector<float>(static_cast<size_t>(numFrames), value));
    s.rebuild();
    return s;
}

// A single-channel ramp 0,1,2,... so read position is directly observable in output.
SourceSample makeRamp(int numFrames, int numChannels = 1)
{
    SourceSample s;
    s.channels.assign(static_cast<size_t>(numChannels),
                      std::vector<float>(static_cast<size_t>(numFrames)));
    for (int ch = 0; ch < numChannels; ++ch)
        for (int n = 0; n < numFrames; ++n)
            s.channels[static_cast<size_t>(ch)][static_cast<size_t>(n)] = static_cast<float>(n);
    s.rebuild();
    return s;
}

// Render one mono block into a fresh buffer (voice adds into it — start cleared).
std::vector<float> render(SampleVoice& voice, int numSamples, int numOutChannels = 1)
{
    std::vector<std::vector<float>> out(static_cast<size_t>(numOutChannels),
                                        std::vector<float>(static_cast<size_t>(numSamples), 0.0f));
    std::vector<float*> ptrs;
    for (auto& c : out)
        ptrs.push_back(c.data());
    voice.renderNextBlock(ptrs.data(), numOutChannels, 0, numSamples);
    return out.front();
}

} // namespace

TEST_CASE("SampleVoice is inactive until a note starts", "[samplevoice][dsp][unit]")
{
    SampleVoice voice;
    voice.prepare(44100.0);
    REQUIRE_FALSE(voice.isActive());

    auto ramp = makeRamp(64);
    voice.setSample(ramp.data(), ramp.numChannels(), ramp.numFrames(), 44100.0);
    REQUIRE_FALSE(voice.isActive());

    voice.startNote(kRootNote, 1.0f, kRootNote);
    REQUIRE(voice.isActive());
    REQUIRE(voice.currentMidiNote() == kRootNote);
}

TEST_CASE("SampleVoice note-to-ratio is an equal-tempered repitch", "[samplevoice][dsp][unit]")
{
    REQUIRE_THAT(SampleVoice::noteToRatio(kRootNote, kRootNote), WithinAbs(1.0, 1e-9));
    REQUIRE_THAT(SampleVoice::noteToRatio(kRootNote + 12, kRootNote), WithinAbs(2.0, 1e-9));
    REQUIRE_THAT(SampleVoice::noteToRatio(kRootNote - 12, kRootNote), WithinAbs(0.5, 1e-9));
    REQUIRE_THAT(SampleVoice::noteToRatio(kRootNote + 7, kRootNote), WithinAbs(1.4983070768, 1e-6));
}

TEST_CASE("SampleVoice at root pitch reproduces the sample sample-for-sample",
          "[samplevoice][dsp][unit]")
{
    constexpr int frames = 16;
    auto ramp = makeRamp(frames);

    SampleVoice voice;
    voice.prepare(44100.0);
    voice.setSample(ramp.data(), ramp.numChannels(), ramp.numFrames(), 44100.0);
    voice.startNote(kRootNote, 1.0f, kRootNote);

    auto out = render(voice, frames);
    for (int n = 0; n < frames; ++n)
        REQUIRE_THAT(out[static_cast<size_t>(n)], WithinAbs(static_cast<float>(n), 1e-4));
}

TEST_CASE("SampleVoice reads position at the pitch ratio", "[samplevoice][dsp][unit]")
{
    constexpr int frames = 64;
    auto ramp = makeRamp(frames);

    SampleVoice voice;
    voice.prepare(44100.0);
    voice.setSample(ramp.data(), ramp.numChannels(), ramp.numFrames(), 44100.0);

    SECTION("one octave up advances two input frames per output sample")
    {
        voice.startNote(kRootNote + 12, 1.0f, kRootNote);
        auto out = render(voice, 8);
        // ramp[position] == position, and position advances by 2 each output sample.
        for (int n = 0; n < 8; ++n)
            REQUIRE_THAT(out[static_cast<size_t>(n)], WithinAbs(static_cast<float>(2 * n), 1e-4));
    }

    SECTION("one octave down advances half an input frame per output sample")
    {
        voice.startNote(kRootNote - 12, 1.0f, kRootNote);
        auto out = render(voice, 8);
        for (int n = 0; n < 8; ++n)
            REQUIRE_THAT(out[static_cast<size_t>(n)],
                         WithinAbs(0.5f * static_cast<float>(n), 1e-4));
    }
}

TEST_CASE("SampleVoice source sample rate corrects playback speed", "[samplevoice][dsp][unit]")
{
    constexpr int frames = 64;
    auto ramp = makeRamp(frames);

    SampleVoice voice;
    voice.prepare(44100.0);
    // Source recorded at twice the host rate: to preserve native pitch the voice
    // must consume two source frames per output sample (step = Fs / Fo = 2).
    voice.setSample(ramp.data(), ramp.numChannels(), ramp.numFrames(), 88200.0);
    voice.startNote(kRootNote, 1.0f, kRootNote);

    auto out = render(voice, 8);
    for (int n = 0; n < 8; ++n)
        REQUIRE_THAT(out[static_cast<size_t>(n)], WithinAbs(2.0f * static_cast<float>(n), 1e-4));
}

TEST_CASE("SampleVoice one-shot deactivates at end of sample", "[samplevoice][dsp][unit]")
{
    constexpr int frames = 16;
    auto ramp = makeRamp(frames);

    SampleVoice voice;
    voice.prepare(44100.0);
    voice.setSample(ramp.data(), ramp.numChannels(), ramp.numFrames(), 44100.0);
    voice.startNote(kRootNote, 1.0f, kRootNote);

    // Render well past the end; the voice must free itself.
    render(voice, frames + 8);
    REQUIRE_FALSE(voice.isActive());

    // Once inactive it contributes silence (additive: buffer stays zero).
    auto tail = render(voice, 8);
    for (float v : tail)
        REQUIRE_THAT(v, WithinAbs(0.0f, 1e-9));
}

TEST_CASE("SampleVoice mixes additively for polyphony", "[samplevoice][dsp][unit]")
{
    constexpr int frames = 16;
    auto ramp = makeRamp(frames);

    SampleVoice a;
    SampleVoice b;
    a.prepare(44100.0);
    b.prepare(44100.0);
    a.setSample(ramp.data(), ramp.numChannels(), ramp.numFrames(), 44100.0);
    b.setSample(ramp.data(), ramp.numChannels(), ramp.numFrames(), 44100.0);
    a.startNote(kRootNote, 1.0f, kRootNote);
    b.startNote(kRootNote, 1.0f, kRootNote);

    std::vector<std::vector<float>> out(1, std::vector<float>(frames, 0.0f));
    std::vector<float*> ptrs{out.front().data()};
    a.renderNextBlock(ptrs.data(), 1, 0, frames);
    b.renderNextBlock(ptrs.data(), 1, 0, frames);

    for (int n = 0; n < frames; ++n)
        REQUIRE_THAT(out.front()[static_cast<size_t>(n)],
                     WithinAbs(2.0f * static_cast<float>(n), 1e-4));
}

TEST_CASE("SampleVoice mono source fans out to all output channels", "[samplevoice][dsp][unit]")
{
    constexpr int frames = 16;
    auto ramp = makeRamp(frames, 1);

    SampleVoice voice;
    voice.prepare(44100.0);
    voice.setSample(ramp.data(), ramp.numChannels(), ramp.numFrames(), 44100.0);
    voice.startNote(kRootNote, 1.0f, kRootNote);

    std::vector<std::vector<float>> out(2, std::vector<float>(frames, 0.0f));
    std::vector<float*> ptrs{out[0].data(), out[1].data()};
    voice.renderNextBlock(ptrs.data(), 2, 0, frames);

    for (int n = 0; n < frames; ++n)
    {
        REQUIRE_THAT(out[0][static_cast<size_t>(n)], WithinAbs(static_cast<float>(n), 1e-4));
        REQUIRE_THAT(out[1][static_cast<size_t>(n)], WithinAbs(static_cast<float>(n), 1e-4));
    }
}

TEST_CASE("SampleVoice forward loop wraps at the loop end", "[samplevoice][dsp][unit][loop]")
{
    constexpr int frames = 8;
    auto ramp = makeRamp(frames); // 0,1,2,3,4,5,6,7

    SampleVoice voice;
    voice.prepare(44100.0);
    voice.setSample(ramp.data(), ramp.numChannels(), ramp.numFrames(), 44100.0);
    voice.setLoopMode(SampleVoice::LoopMode::Forward);
    voice.setLoopPoints(2, 6); // loop region [2, 6): frames 2,3,4,5
    voice.startNote(kRootNote, 1.0f, kRootNote);

    auto out = render(voice, 12);
    const float expected[] = {0, 1, 2, 3, 4, 5, 2, 3, 4, 5, 2, 3};
    for (int n = 0; n < 12; ++n)
        REQUIRE_THAT(out[static_cast<size_t>(n)], WithinAbs(expected[n], 1e-4));

    // A looping voice never frees itself.
    REQUIRE(voice.isActive());
}

TEST_CASE("SampleVoice loop speed follows pitch", "[samplevoice][dsp][unit][loop]")
{
    constexpr int frames = 8;
    auto ramp = makeRamp(frames);

    SampleVoice voice;
    voice.prepare(44100.0);
    voice.setSample(ramp.data(), ramp.numChannels(), ramp.numFrames(), 44100.0);
    voice.setLoopMode(SampleVoice::LoopMode::Forward);
    voice.setLoopPoints(0, 4); // loop region [0, 4): length 4
    // One octave up: step 2, so the same loop is traversed twice as fast.
    voice.startNote(kRootNote + 12, 1.0f, kRootNote);

    auto out = render(voice, 6);
    const float expected[] = {0, 2, 0, 2, 0, 2};
    for (int n = 0; n < 6; ++n)
        REQUIRE_THAT(out[static_cast<size_t>(n)], WithinAbs(expected[n], 1e-4));
}

TEST_CASE("SampleVoice ping-pong bounces between loop points", "[samplevoice][dsp][unit][loop]")
{
    constexpr int frames = 8;
    auto ramp = makeRamp(frames);

    SampleVoice voice;
    voice.prepare(44100.0);
    voice.setSample(ramp.data(), ramp.numChannels(), ramp.numFrames(), 44100.0);
    voice.setLoopMode(SampleVoice::LoopMode::PingPong);
    voice.setLoopPoints(0, 4); // turn around at 0 and 4
    voice.startNote(kRootNote, 1.0f, kRootNote);

    auto out = render(voice, 11);
    const float expected[] = {0, 1, 2, 3, 4, 3, 2, 1, 0, 1, 2};
    for (int n = 0; n < 11; ++n)
        REQUIRE_THAT(out[static_cast<size_t>(n)], WithinAbs(expected[n], 1e-4));

    REQUIRE(voice.isActive());
}

TEST_CASE("SampleVoice one-shot is the default loop mode", "[samplevoice][dsp][unit][loop]")
{
    constexpr int frames = 8;
    auto ramp = makeRamp(frames);

    SampleVoice voice;
    voice.prepare(44100.0);
    voice.setSample(ramp.data(), ramp.numChannels(), ramp.numFrames(), 44100.0);
    REQUIRE(voice.loopMode() == SampleVoice::LoopMode::OneShot);

    voice.startNote(kRootNote, 1.0f, kRootNote);
    render(voice, frames + 4);
    REQUIRE_FALSE(voice.isActive());
}

TEST_CASE("SampleVoice applies the amp envelope to its output", "[samplevoice][dsp][unit][env]")
{
    constexpr int frames = 16;
    auto flat = makeConstant(frames, 1.0f);

    SampleVoice voice;
    voice.prepare(1000.0); // 1 kHz makes the attack land on whole samples
    voice.setSample(flat.data(), flat.numChannels(), flat.numFrames(), 1000.0);
    voice.setAmpEnvelope(0.004f, 0.0f, 1.0f, 0.0f); // 4-sample attack, sustain at 1
    voice.startNote(kRootNote, 1.0f, kRootNote);

    auto out = render(voice, 8);
    const float expected[] = {0.25f, 0.5f, 0.75f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    for (int n = 0; n < 8; ++n)
        REQUIRE_THAT(out[static_cast<size_t>(n)], WithinAbs(expected[n], 1e-4));
}

TEST_CASE("SampleVoice release from stopNote fades out then frees", "[samplevoice][dsp][unit][env]")
{
    constexpr int frames = 64;
    auto flat = makeConstant(frames, 1.0f);

    SampleVoice voice;
    voice.prepare(1000.0);
    voice.setSample(flat.data(), flat.numChannels(), flat.numFrames(), 1000.0);
    voice.setAmpEnvelope(0.0f, 0.0f, 1.0f, 0.004f); // instant on, 4-sample release
    voice.startNote(kRootNote, 1.0f, kRootNote);

    render(voice, 4); // sustaining at 1.0
    REQUIRE(voice.isActive());

    voice.stopNote(); // begin release; a positive release keeps the voice alive
    REQUIRE(voice.isActive());

    auto tail = render(voice, 6);
    const float expected[] = {0.75f, 0.5f, 0.25f, 0.0f, 0.0f, 0.0f};
    for (int n = 0; n < 6; ++n)
        REQUIRE_THAT(tail[static_cast<size_t>(n)], WithinAbs(expected[n], 1e-4));

    REQUIRE_FALSE(voice.isActive()); // freed once the release completed
}

TEST_CASE("SampleVoice velocity scales output gain", "[samplevoice][dsp][unit]")
{
    constexpr int frames = 16;
    auto ramp = makeRamp(frames);

    SampleVoice voice;
    voice.prepare(44100.0);
    voice.setSample(ramp.data(), ramp.numChannels(), ramp.numFrames(), 44100.0);
    voice.startNote(kRootNote, 0.5f, kRootNote);

    auto out = render(voice, frames);
    for (int n = 0; n < frames; ++n)
        REQUIRE_THAT(out[static_cast<size_t>(n)], WithinAbs(0.5f * static_cast<float>(n), 1e-4));
}
