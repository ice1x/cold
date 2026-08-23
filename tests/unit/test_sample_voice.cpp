#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/SampleVoice.h"

#include <algorithm>
#include <cmath>
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

// A single-channel sine of `freqHz` (bright but below Nyquist, so a low-pass can
// actually open up to let it through).
SourceSample makeSine(int numFrames, double freqHz, double sampleRate)
{
    SourceSample s;
    s.channels.assign(1, std::vector<float>(static_cast<size_t>(numFrames)));
    for (int n = 0; n < numFrames; ++n)
        s.channels[0][static_cast<size_t>(n)] =
            static_cast<float>(std::sin(2.0 * 3.14159265358979 * freqHz * n / sampleRate));
    s.rebuild();
    return s;
}

// A single-channel Nyquist-ish alternating +1/-1 sample (bright, high-frequency).
SourceSample makeAlternating(int numFrames)
{
    SourceSample s;
    s.channels.assign(1, std::vector<float>(static_cast<size_t>(numFrames)));
    for (int n = 0; n < numFrames; ++n)
        s.channels[0][static_cast<size_t>(n)] = (n % 2 == 0) ? 1.0f : -1.0f;
    s.rebuild();
    return s;
}

// Peak magnitude over the tail of a buffer (skip the filter settling transient).
float tailPeak(const std::vector<float>& buf, int tail)
{
    float peak = 0.0f;
    const int start = std::max(0, static_cast<int>(buf.size()) - tail);
    for (int n = start; n < static_cast<int>(buf.size()); ++n)
        peak = std::max(peak, std::abs(buf[static_cast<size_t>(n)]));
    return peak;
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

TEST_CASE("SampleVoice reversed plays the sample back-to-front then frees",
          "[samplevoice][dsp][unit][reverse]")
{
    constexpr int frames = 16;
    auto ramp = makeRamp(frames); // ramp[n] == n

    SampleVoice voice;
    voice.prepare(44100.0);
    voice.setSample(ramp.data(), ramp.numChannels(), ramp.numFrames(), 44100.0);
    voice.setReversed(true);
    voice.startNote(kRootNote, 1.0f, kRootNote);

    // Reversed one-shot reads from the last frame down to the first: 15, 14, ..., 0.
    auto out = render(voice, frames);
    for (int n = 0; n < frames; ++n)
        REQUIRE_THAT(out[static_cast<size_t>(n)],
                     WithinAbs(static_cast<float>(frames - 1 - n), 1e-4));

    // Past the start (position < 0) it frees itself and renders silence (a one-shot,
    // now exhausted).
    auto tail = render(voice, 4);
    REQUIRE_FALSE(voice.isActive());
    for (float v : tail)
        REQUIRE_THAT(v, WithinAbs(0.0f, 1e-4));
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

TEST_CASE("SampleVoice velocity maps to pitch", "[samplevoice][dsp][unit][velocity]")
{
    constexpr int frames = 64;
    auto ramp = makeRamp(frames);

    SampleVoice voice;
    voice.prepare(44100.0);
    voice.setSample(ramp.data(), ramp.numChannels(), ramp.numFrames(), 44100.0);
    voice.setVelToPitch(12.0f); // +1 octave at full velocity

    SECTION("full velocity raises pitch by the full amount")
    {
        voice.startNote(kRootNote, 1.0f, kRootNote);
        REQUIRE_THAT(voice.increment(), WithinAbs(2.0, 1e-6)); // one octave up
    }

    SECTION("half velocity raises pitch by half the semitone amount")
    {
        voice.startNote(kRootNote, 0.5f, kRootNote);
        REQUIRE_THAT(voice.increment(), WithinAbs(std::pow(2.0, 6.0 / 12.0), 1e-6));
    }

    SECTION("a negative amount lowers pitch with velocity")
    {
        voice.setVelToPitch(-12.0f);
        voice.startNote(kRootNote, 1.0f, kRootNote);
        REQUIRE_THAT(voice.increment(), WithinAbs(0.5, 1e-6)); // one octave down
    }

    SECTION("zero amount leaves the note at its natural pitch")
    {
        voice.setVelToPitch(0.0f);
        voice.startNote(kRootNote, 1.0f, kRootNote);
        REQUIRE_THAT(voice.increment(), WithinAbs(1.0, 1e-6));
    }

    SECTION("stacks additively with a static tune")
    {
        voice.setVelToPitch(12.0f);
        voice.setTuneSemitones(12.0f); // +1 octave tune
        voice.startNote(kRootNote, 1.0f, kRootNote);
        REQUIRE_THAT(voice.increment(), WithinAbs(4.0, 1e-6)); // +24 semitones total
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

TEST_CASE("SampleVoice filter is bypassed by default", "[samplevoice][dsp][unit][filter]")
{
    constexpr int frames = 32;
    auto alt = makeAlternating(frames);

    SampleVoice voice;
    voice.prepare(48000.0);
    voice.setSample(alt.data(), alt.numChannels(), alt.numFrames(), 48000.0);
    REQUIRE_FALSE(voice.filterEnabled());
    voice.startNote(kRootNote, 1.0f, kRootNote);

    auto out = render(voice, frames);
    // Unfiltered: the bright ±1 sample passes through untouched.
    for (int n = 0; n < frames; ++n)
        REQUIRE_THAT(out[static_cast<size_t>(n)], WithinAbs((n % 2 == 0) ? 1.0f : -1.0f, 1e-4));
}

TEST_CASE("SampleVoice low-pass filter attenuates a bright sample",
          "[samplevoice][dsp][unit][filter]")
{
    constexpr int frames = 4000;
    auto alt = makeAlternating(frames);

    SampleVoice bypass;
    bypass.prepare(48000.0);
    bypass.setSample(alt.data(), alt.numChannels(), alt.numFrames(), 48000.0);
    bypass.startNote(kRootNote, 1.0f, kRootNote);
    const float rawPeak = tailPeak(render(bypass, frames), 1000);

    SampleVoice filtered;
    filtered.prepare(48000.0);
    filtered.setSample(alt.data(), alt.numChannels(), alt.numFrames(), 48000.0);
    filtered.setFilter(300.0f, 0.707f, 0.0f); // low LP -> kills the bright content
    REQUIRE(filtered.filterEnabled());
    filtered.startNote(kRootNote, 1.0f, kRootNote);
    const float filtPeak = tailPeak(render(filtered, frames), 1000);

    REQUIRE(rawPeak > 0.9f);
    REQUIRE(filtPeak < 0.1f * rawPeak); // heavily attenuated
}

TEST_CASE("SampleVoice LFO filter mod attenuates relative to the base cutoff",
          "[samplevoice][dsp][unit][filter]")
{
    constexpr int frames = 8000;
    auto sine = makeSine(frames, 6000.0, 48000.0); // bright but below Nyquist

    // Base filter wide open -> the 6 kHz tone passes; a -6 octave LFO mod pulls the
    // cutoff down around that base, so the same voice now attenuates it.
    SampleVoice open;
    open.prepare(48000.0);
    open.setSample(sine.data(), sine.numChannels(), sine.numFrames(), 48000.0);
    open.setFilter(12000.0f, 0.707f, 0.0f);
    open.startNote(kRootNote, 1.0f, kRootNote);
    const float openPeak = tailPeak(render(open, frames), 1000);

    SampleVoice modded;
    modded.prepare(48000.0);
    modded.setSample(sine.data(), sine.numChannels(), sine.numFrames(), 48000.0);
    modded.setFilter(12000.0f, 0.707f, 0.0f); // same open base
    modded.setFilterLfoMod(-6.0f, 0.0f);      // pull cutoff down 6 octaves -> ~187 Hz
    modded.startNote(kRootNote, 1.0f, kRootNote);
    const float moddedPeak = tailPeak(render(modded, frames), 1000);

    REQUIRE(openPeak > 0.7f);
    REQUIRE(moddedPeak < 0.3f * openPeak); // relative mod closed the filter
}

TEST_CASE("SampleVoice mod envelope opens the filter over the note",
          "[samplevoice][dsp][unit][modenv]")
{
    constexpr int frames = 8000;
    auto sine = makeSine(frames, 6000.0, 48000.0); // 6 kHz: bright but below Nyquist

    SampleVoice voice;
    voice.prepare(48000.0);
    voice.setSample(sine.data(), sine.numChannels(), sine.numFrames(), 48000.0);
    voice.setFilter(300.0f, 0.707f, 0.0f);         // low LP: 6 kHz muted at first
    voice.setModEnvelope(0.05f, 0.0f, 1.0f, 0.0f); // ~2400-sample attack to full
    voice.setModRouting(6.0f, 0.0f);               // env -> +6 octaves of cutoff
    REQUIRE(voice.modEnvelopeActive());
    voice.startNote(kRootNote, 1.0f, kRootNote);

    auto out = render(voice, frames);
    auto peak = [&out](int start, int len)
    {
        float p = 0.0f;
        for (int n = start; n < start + len; ++n)
            p = std::max(p, std::abs(out[static_cast<size_t>(n)]));
        return p;
    };
    const float early = peak(100, 400);         // filter still closed
    const float late = peak(frames - 500, 400); // filter opened by the envelope
    REQUIRE(late > 4.0f * early);
}

TEST_CASE("SampleVoice mod envelope is inert without routing", "[samplevoice][dsp][unit][modenv]")
{
    constexpr int frames = 16;
    auto ramp = makeRamp(frames);

    SampleVoice voice;
    voice.prepare(44100.0);
    voice.setSample(ramp.data(), ramp.numChannels(), ramp.numFrames(), 44100.0);
    voice.setModEnvelope(0.01f, 0.0f, 1.0f, 0.0f); // configured but not routed
    REQUIRE_FALSE(voice.modEnvelopeActive());
    voice.startNote(kRootNote, 1.0f, kRootNote);

    auto out = render(voice, frames);
    for (int n = 0; n < frames; ++n)
        REQUIRE_THAT(out[static_cast<size_t>(n)], WithinAbs(static_cast<float>(n), 1e-4));
}

TEST_CASE("SampleVoice uses Hermite interpolation between samples", "[samplevoice][dsp][unit]")
{
    // A parabola y[n] = n^2. The 4-point Hermite interpolator is exact for
    // quadratics, so the interpolated midpoint is the true value (2.5^2 = 6.25),
    // whereas 2-point linear would give (4 + 9) / 2 = 6.5.
    constexpr int frames = 16;
    SourceSample s;
    s.channels.assign(1, std::vector<float>(static_cast<size_t>(frames)));
    for (int n = 0; n < frames; ++n)
        s.channels[0][static_cast<size_t>(n)] = static_cast<float>(n) * static_cast<float>(n);
    s.rebuild();

    SampleVoice voice;
    voice.prepare(44100.0);
    voice.setSample(s.data(), s.numChannels(), s.numFrames(), 44100.0);
    voice.startNote(kRootNote - 12, 1.0f, kRootNote); // step 0.5 -> reads 0, 0.5, 1.0, ...

    auto out = render(voice, 8);
    // out[5] is read at pos 2.5 (an interior position): Hermite -> 6.25, not 6.5.
    REQUIRE_THAT(out[5], WithinAbs(6.25f, 1e-3));
}

TEST_CASE("SampleVoice pitch modulation shifts playback speed",
          "[samplevoice][dsp][unit][pitchmod]")
{
    constexpr int frames = 64;
    auto ramp = makeRamp(frames);

    SampleVoice voice;
    voice.prepare(44100.0);
    voice.setSample(ramp.data(), ramp.numChannels(), ramp.numFrames(), 44100.0);
    voice.startNote(kRootNote, 1.0f, kRootNote);
    REQUIRE_THAT(voice.increment(), WithinAbs(1.0, 1e-9)); // base step at the root

    voice.setPitchModSemitones(12.0f); // +1 octave -> double the playback speed
    REQUIRE_THAT(voice.increment(), WithinAbs(2.0, 1e-9));

    auto out = render(voice, 8);
    for (int n = 0; n < 8; ++n)
        REQUIRE_THAT(out[static_cast<size_t>(n)], WithinAbs(static_cast<float>(2 * n), 1e-4));
}

TEST_CASE("SampleVoice pitch modulation stacks on the note ratio",
          "[samplevoice][dsp][unit][pitchmod]")
{
    constexpr int frames = 64;
    auto ramp = makeRamp(frames);

    SampleVoice voice;
    voice.prepare(44100.0);
    voice.setSample(ramp.data(), ramp.numChannels(), ramp.numFrames(), 44100.0);
    voice.startNote(kRootNote + 12, 1.0f, kRootNote); // note one octave up -> base step 2
    voice.setPitchModSemitones(12.0f);                // +1 more octave -> step 4
    REQUIRE_THAT(voice.increment(), WithinAbs(4.0, 1e-9));

    voice.setPitchModSemitones(0.0f); // clearing the mod returns to the note ratio
    REQUIRE_THAT(voice.increment(), WithinAbs(2.0, 1e-9));
}

TEST_CASE("SampleVoice tune stacks with pitch modulation", "[samplevoice][dsp][unit][pitchmod]")
{
    constexpr int frames = 64;
    auto ramp = makeRamp(frames);

    SampleVoice voice;
    voice.prepare(44100.0);
    voice.setSample(ramp.data(), ramp.numChannels(), ramp.numFrames(), 44100.0);
    voice.startNote(kRootNote, 1.0f, kRootNote);

    voice.setTuneSemitones(12.0f); // static per-note tune: +1 octave -> step 2
    REQUIRE_THAT(voice.increment(), WithinAbs(2.0, 1e-9));

    voice.setPitchModSemitones(12.0f); // +1 more octave from the mod source -> step 4
    REQUIRE_THAT(voice.increment(), WithinAbs(4.0, 1e-9));

    voice.setTuneSemitones(0.0f); // clearing tune leaves just the pitch mod -> step 2
    REQUIRE_THAT(voice.increment(), WithinAbs(2.0, 1e-9));
}

TEST_CASE("SampleVoice pan places a mono source in the stereo field",
          "[samplevoice][dsp][unit][pan]")
{
    constexpr int frames = 16;
    auto flat = makeConstant(frames, 1.0f);

    auto renderStereo = [&](float pan)
    {
        SampleVoice voice;
        voice.prepare(44100.0);
        voice.setSample(flat.data(), flat.numChannels(), flat.numFrames(), 44100.0);
        voice.setPan(pan);
        voice.startNote(kRootNote, 1.0f, kRootNote);

        std::vector<std::vector<float>> out(2, std::vector<float>(frames, 0.0f));
        std::vector<float*> ptrs{out[0].data(), out[1].data()};
        voice.renderNextBlock(ptrs.data(), 2, 0, frames);
        return out;
    };

    SECTION("center keeps both channels at unity (unchanged fan-out)")
    {
        auto out = renderStereo(0.0f);
        REQUIRE_THAT(out[0][4], WithinAbs(1.0f, 1e-4));
        REQUIRE_THAT(out[1][4], WithinAbs(1.0f, 1e-4));
    }

    SECTION("hard left puts the signal on the left channel only")
    {
        auto out = renderStereo(-1.0f);
        REQUIRE_THAT(out[0][4], WithinAbs(1.0f, 1e-4));
        REQUIRE_THAT(out[1][4], WithinAbs(0.0f, 1e-4));
    }

    SECTION("hard right puts the signal on the right channel only")
    {
        auto out = renderStereo(1.0f);
        REQUIRE_THAT(out[0][4], WithinAbs(0.0f, 1e-4));
        REQUIRE_THAT(out[1][4], WithinAbs(1.0f, 1e-4));
    }
}

TEST_CASE("SampleVoice pitch envelope sweeps playback speed then settles",
          "[samplevoice][dsp][unit][pitchenv]")
{
    constexpr int frames = 4000;
    auto ramp = makeRamp(frames);

    // Reference: no pitch envelope -> reads one frame per sample.
    SampleVoice plain;
    plain.prepare(48000.0);
    plain.setSample(ramp.data(), ramp.numChannels(), ramp.numFrames(), 48000.0);
    plain.startNote(kRootNote, 1.0f, kRootNote);
    REQUIRE_FALSE(plain.pitchEnvelopeActive());

    // +12 semitones of pitch envelope over a long decay -> starts ~double speed.
    SampleVoice blip;
    blip.prepare(48000.0);
    blip.setSample(ramp.data(), ramp.numChannels(), ramp.numFrames(), 48000.0);
    blip.setPitchEnvelope(12.0f, 1.0f); // long decay: nearly full over a short block
    REQUIRE(blip.pitchEnvelopeActive());
    blip.startNote(kRootNote, 1.0f, kRootNote);

    render(plain, 200);
    render(blip, 200);

    // The blip voice raced ahead (higher pitch at the start).
    REQUIRE(blip.readPosition() > 1.7 * plain.readPosition());

    // With a very short decay the envelope collapses, so the tail advances at the
    // base rate again (settled back to the note pitch).
    SampleVoice settle;
    settle.prepare(48000.0);
    settle.setSample(ramp.data(), ramp.numChannels(), ramp.numFrames(), 48000.0);
    settle.setPitchEnvelope(12.0f, 0.002f); // ~96-sample decay
    settle.startNote(kRootNote, 1.0f, kRootNote);
    render(settle, 400);
    const double before = settle.readPosition();
    render(settle, 100);
    const double advanced = settle.readPosition() - before;
    REQUIRE_THAT(advanced, WithinAbs(100.0, 2.0)); // back to ~1 frame/sample
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
