// End-to-end tests: instantiate the real Cold plugin processor and drive it the
// way a host (Logic) does — prepare, push MIDI + audio blocks, query the plugin
// contract, save/restore state, open the editor. The sampler engine is wired in
// from M1 onward; today these lock down the M0 contract (silent, stable, valid)
// and grow into "load a WAV -> hear non-silent output" as playback lands.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "PluginProcessor.h"
#include "support/SignalFixtures.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>

using Catch::Matchers::WithinAbs;
using cold::ColdAudioProcessor;

namespace
{

bool bufferIsFinite(const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int n = 0; n < buffer.getNumSamples(); ++n)
            if (!std::isfinite(buffer.getSample(ch, n)))
                return false;
    return true;
}

// Render one block through a prepared processor with the given MIDI.
void renderBlock(ColdAudioProcessor& proc, juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    proc.processBlock(buffer, midi);
}

// Write a short known WAV to a temp file and return it (caller deletes).
juce::File writeTempWav(double sampleRate, int numChannels, int numFrames)
{
    auto file = juce::File::createTempFile(".wav");
    juce::WavAudioFormat wav;
    cold::test::writeKnownSignal(wav, file, sampleRate, numChannels, numFrames);
    return file;
}

// Write a mono WAV whose amplitude ramps linearly 0 -> 0.9 (a fade-in). Played
// forwards it starts quiet and ends loud; reversed it starts loud — so the two
// directions are distinguishable by the level of the first samples.
juce::File writeFadeInWav(double sampleRate, int numFrames)
{
    auto file = juce::File::createTempFile(".wav");
    juce::AudioBuffer<float> buffer(1, numFrames);
    for (int n = 0; n < numFrames; ++n)
        buffer.setSample(0, n, 0.9f * static_cast<float>(n) / static_cast<float>(numFrames - 1));

    juce::WavAudioFormat wav;
    if (auto os = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream()))
    {
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(os.get(), sampleRate, 1, 16, {}, 0));
        if (writer != nullptr)
        {
            os.release(); // the writer owns the stream now
            writer->writeFromAudioSampleBuffer(buffer, 0, numFrames);
        }
    }
    return file;
}

// Write a mono sine WAV at `freqHz` (for filter tests).
juce::File writeSineWav(double sampleRate, int numFrames, double freqHz)
{
    auto file = juce::File::createTempFile(".wav");
    juce::AudioBuffer<float> buffer(1, numFrames);
    for (int n = 0; n < numFrames; ++n)
        buffer.setSample(
            0, n, static_cast<float>(std::sin(2.0 * 3.14159265358979 * freqHz * n / sampleRate)));
    juce::WavAudioFormat wav;
    if (auto os = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream()))
    {
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(os.get(), sampleRate, 1, 16, {}, 0));
        if (writer != nullptr)
        {
            os.release();
            writer->writeFromAudioSampleBuffer(buffer, 0, numFrames);
        }
    }
    return file;
}

} // namespace

TEST_CASE("Cold processor exposes the expected plugin contract", "[processor][e2e]")
{
    ColdAudioProcessor proc;

    REQUIRE(proc.getName() == juce::String("Cold"));
    REQUIRE(proc.acceptsMidi()); // it is a MIDI-driven instrument
    REQUIRE_FALSE(proc.producesMidi());
    REQUIRE_FALSE(proc.isMidiEffect());
    REQUIRE(proc.hasEditor());
}

TEST_CASE("Cold processor accepts mono and stereo output layouts", "[processor][e2e]")
{
    ColdAudioProcessor proc;

    juce::AudioProcessor::BusesLayout stereo;
    stereo.outputBuses.add(juce::AudioChannelSet::stereo());
    REQUIRE(proc.checkBusesLayoutSupported(stereo));

    juce::AudioProcessor::BusesLayout mono;
    mono.outputBuses.add(juce::AudioChannelSet::mono());
    REQUIRE(proc.checkBusesLayoutSupported(mono));

    juce::AudioProcessor::BusesLayout surround;
    surround.outputBuses.add(juce::AudioChannelSet::create5point1());
    REQUIRE_FALSE(proc.checkBusesLayoutSupported(surround));
}

TEST_CASE("Cold processor is silent with no sample loaded", "[processor][e2e]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    juce::AudioBuffer<float> buffer(2, blockSize);
    buffer.clear();

    // A chord with no sample must still be finite silence (nothing to play).
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    midi.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 0);
    renderBlock(proc, buffer, midi);

    REQUIRE(bufferIsFinite(buffer));
    REQUIRE(buffer.getMagnitude(0, blockSize) == 0.0f);

    proc.releaseResources();
}

TEST_CASE("Cold processor plays a loaded sample on a MIDI note", "[processor][e2e]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    auto wav = writeTempWav(sampleRate, 2, blockSize);
    REQUIRE(proc.loadSampleFile(wav));
    wav.deleteFile();

    juce::AudioBuffer<float> buffer(2, blockSize);

    SECTION("a note at the root produces non-silent, finite output")
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0); // root note default 60
        renderBlock(proc, buffer, midi);

        REQUIRE(bufferIsFinite(buffer));
        REQUIRE(buffer.getMagnitude(0, blockSize) > 0.0f);
    }

    SECTION("no note -> the loaded sample stays silent until triggered")
    {
        buffer.clear();
        juce::MidiBuffer midi;
        renderBlock(proc, buffer, midi);
        REQUIRE(buffer.getMagnitude(0, blockSize) == 0.0f);
    }

    proc.releaseResources();
}

TEST_CASE("Cold processor routes MIDI channels to tracks", "[processor][e2e][multitrack]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    // Cell 2's primary sample; track 2 is driven by MIDI channel 3.
    auto wav = writeTempWav(sampleRate, 2, blockSize);
    REQUIRE(proc.loadSampleToTrack(2, wav));
    wav.deleteFile();

    juce::AudioBuffer<float> buffer(2, blockSize);

    SECTION("the channel mapped to the loaded track plays")
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(3, 60, 1.0f), 0); // channel 3 -> track 2
        renderBlock(proc, buffer, midi);
        REQUIRE(buffer.getMagnitude(0, blockSize) > 0.0f);
    }

    SECTION("a channel mapped to an empty track is silent")
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0); // channel 1 -> track 0 (empty)
        renderBlock(proc, buffer, midi);
        REQUIRE(buffer.getMagnitude(0, blockSize) == 0.0f);
    }

    proc.releaseResources();
}

TEST_CASE("Cold per-layer filter darkens a stacked layer", "[processor][e2e][stack][filter]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int frames = 8000; // long enough for the filter smoother to settle

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, frames);
    proc.prepareToPlay(sampleRate, frames);

    // The known signal (a ~750 Hz sine) into slot 0 / track 0 in Stack mode.
    auto wav = writeTempWav(sampleRate, 1, frames);
    REQUIRE(proc.loadSampleToSlot(0, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    auto tailPeak = [](const juce::AudioBuffer<float>& b)
    {
        float p = 0.0f;
        for (int n = b.getNumSamples() - 1000; n < b.getNumSamples(); ++n) // settled tail
            p = std::max(p, std::abs(b.getSample(0, n)));
        return p;
    };

    setParam("t0_layerMode", 1.0f); // Stack (layer 0 = slot 0 by default)
    juce::AudioBuffer<float> buffer(2, frames);

    float openPeak = 0.0f;
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
        openPeak = tailPeak(buffer);
        REQUIRE(openPeak > 0.1f);
    }

    // Enable the layer filter with a very low cutoff -> the tone is attenuated.
    setParam("t0_lyr0_filterOn", 1.0f);
    setParam("t0_lyr0_cutoff", 80.0f);
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
        REQUIRE(tailPeak(buffer) < 0.3f * openPeak);
    }

    proc.releaseResources();
}

TEST_CASE("Cold Note Map addressing routes by note number", "[processor][e2e][notemap]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    // Slot 0 feeds track 0; base note 36 (default) maps note 36 -> track 0.
    auto wav = writeTempWav(sampleRate, 2, blockSize);
    REQUIRE(proc.loadSampleToSlot(0, wav));
    wav.deleteFile();

    auto* addr = proc.apvts.getParameter("midiAddressing");
    addr->setValueNotifyingHost(addr->convertTo0to1(1.0f)); // Note Map

    juce::AudioBuffer<float> buffer(2, blockSize);

    SECTION("the base note triggers track 0")
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0); // note 36 -> track 0
        renderBlock(proc, buffer, midi);
        REQUIRE(buffer.getMagnitude(0, blockSize) > 0.0f);
    }

    SECTION("a note mapping to an empty track is silent")
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 37, 1.0f), 0); // note 37 -> track 1 (empty)
        renderBlock(proc, buffer, midi);
        REQUIRE(buffer.getMagnitude(0, blockSize) == 0.0f);
    }

    SECTION("a note outside the map range is ignored")
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 44, 1.0f), 0); // 44 - 36 = 8, out of range
        renderBlock(proc, buffer, midi);
        REQUIRE(buffer.getMagnitude(0, blockSize) == 0.0f);
    }

    proc.releaseResources();
}

TEST_CASE("Cold Note Map routes non-consecutive per-cell keys", "[processor][e2e][notemap]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    // Two cells on GM-drum-style non-consecutive keys: cell 0 = 36, cell 3 = 42.
    auto wav = writeTempWav(sampleRate, 2, blockSize);
    REQUIRE(proc.loadSampleToTrack(0, wav));
    REQUIRE(proc.loadSampleToTrack(3, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    setParam("midiAddressing", 1.0f); // Note Map
    setParam("t0_mapNote", 36.0f);
    setParam("t3_mapNote", 42.0f);

    juce::AudioBuffer<float> buffer(2, blockSize);

    auto plays = [&](int note)
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);
        renderBlock(proc, buffer, midi);
        return buffer.getMagnitude(0, blockSize) > 0.0f;
    };

    // Silent cases first, so no earlier loaded note can bleed across the block.
    REQUIRE_FALSE(plays(37)); // cell 1's key, but cell 1 is empty
    REQUIRE_FALSE(plays(50)); // no cell maps 50
    REQUIRE(plays(36));       // -> cell 0 (loaded)
    REQUIRE(plays(42));       // -> cell 3 (loaded), a non-consecutive key

    proc.releaseResources();
}

TEST_CASE("Cold LFO->sample-select roams the bank for layering", "[processor][e2e][sampleselect]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    // Track 0's base slot 0 is a short one-shot (frees after 8 samples -> silent
    // tail); its neighbour slot 1 is long (sounds across the whole block). Which
    // one track 0 plays is therefore observable in the block's tail.
    auto shortWav = writeTempWav(sampleRate, 1, 8);
    auto longWav = writeTempWav(sampleRate, 1, blockSize);
    REQUIRE(proc.loadSampleToSlot(0, shortWav));
    REQUIRE(proc.loadSampleToSlot(1, longWav));
    shortWav.deleteFile();
    longWav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };

    auto tailPeak = [](const juce::AudioBuffer<float>& b)
    {
        float peak = 0.0f;
        for (int n = 100; n < b.getNumSamples(); ++n)
            peak = std::max(peak, std::abs(b.getSample(0, n)));
        return peak;
    };

    juce::AudioBuffer<float> buffer(2, blockSize);

    SECTION("with no sample-select the track plays its short base slot")
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0); // channel 1 -> track 0
        renderBlock(proc, buffer, midi);
        REQUIRE(bufferIsFinite(buffer));
        REQUIRE(tailPeak(buffer) < 1.0e-4f); // short one-shot: tail is silent
    }

    SECTION("LFO->sample-select shifts the track onto the long neighbour slot")
    {
        setParam("lfoShape", 4.0f);          // Square -> reads +1 at phase 0
        setParam("lfoToSampleSelect", 1.0f); // +1 slot offset at the trig
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0); // channel 1 -> track 0
        renderBlock(proc, buffer, midi);
        REQUIRE(bufferIsFinite(buffer));
        REQUIRE(tailPeak(buffer) > 0.1f); // now sounding the long slot across the block
    }
}

TEST_CASE("Cold LFO Custom designer shape drives sample-select", "[processor][e2e][custom]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    // As in the layering test: track 0's base slot 0 is short (silent tail); the
    // neighbour slot 1 is long (sounding tail). The Custom LFO's designer steps
    // decide whether sample-select shifts track 0 onto slot 1 at the trig.
    auto shortWav = writeTempWav(sampleRate, 1, 8);
    auto longWav = writeTempWav(sampleRate, 1, blockSize);
    REQUIRE(proc.loadSampleToSlot(0, shortWav));
    REQUIRE(proc.loadSampleToSlot(1, longWav));
    shortWav.deleteFile();
    longWav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    auto setAllSteps = [&](float v)
    {
        for (int i = 1; i <= 8; ++i)
            setParam(("lfoStep" + juce::String(i)).toRawUTF8(), v);
    };
    auto tailPeak = [](const juce::AudioBuffer<float>& b)
    {
        float peak = 0.0f;
        for (int n = 100; n < b.getNumSamples(); ++n)
            peak = std::max(peak, std::abs(b.getSample(0, n)));
        return peak;
    };

    setParam("lfoShape", 6.0f);          // Custom
    setParam("lfoToSampleSelect", 1.0f); // +1 slot per +1 of shape
    juce::AudioBuffer<float> buffer(2, blockSize);

    SECTION("designer steps at +1 shift the track onto the long neighbour")
    {
        setAllSteps(1.0f); // Custom reads +1 at phase 0 -> +1 slot offset
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
        REQUIRE(bufferIsFinite(buffer));
        REQUIRE(tailPeak(buffer) > 0.1f);
    }

    SECTION("designer steps at 0 leave the track on its short base slot")
    {
        setAllSteps(0.0f); // Custom reads 0 -> no offset
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
        REQUIRE(bufferIsFinite(buffer));
        REQUIRE(tailPeak(buffer) < 1.0e-4f);
    }
}

TEST_CASE("Cold Stack layer mode sums a track's layers into one hit", "[processor][e2e][stack]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    // Cell 0's layers 0 and 1 hold the same signal; in Stack, track 0 (channel 1)
    // sounds both, so the in-phase sum is ~2x a single layer.
    auto wav = writeTempWav(sampleRate, 1, blockSize);
    REQUIRE(proc.loadSampleToLayer(0, 0, wav));
    REQUIRE(proc.loadSampleToLayer(0, 1, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    auto peak = [](const juce::AudioBuffer<float>& b)
    {
        float p = 0.0f;
        for (int n = 0; n < b.getNumSamples(); ++n)
            p = std::max(p, std::abs(b.getSample(0, n)));
        return p;
    };

    juce::AudioBuffer<float> buffer(2, blockSize);

    float selectPeak = 0.0f;
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0); // track 0, Select default
        renderBlock(proc, buffer, midi);
        REQUIRE(bufferIsFinite(buffer));
        selectPeak = peak(buffer);
        REQUIRE(selectPeak > 0.1f); // the single base layer sounds
    }

    // Switch track 0 to Stack: layers 0 and 1 are both loaded, so both sound.
    setParam("t0_layerMode", 1.0f); // Stack
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
        REQUIRE(bufferIsFinite(buffer));
        REQUIRE(peak(buffer) > 1.5f * selectPeak); // two in-phase layers sum louder
    }

    proc.releaseResources();
}

TEST_CASE("Cold Round-Robin layer mode alternates a cell's layers per hit",
          "[processor][e2e][roundrobin]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    // Cell 0 holds the same signal in layers 0 and 1, but layer 1 is quieter. In
    // Round-Robin each hit sounds ONE layer, cycling 0 -> 1 -> 0, so the peak
    // alternates loud/soft/loud (anti-machine-gun; not summed like Stack).
    auto wav = writeTempWav(sampleRate, 1, blockSize);
    REQUIRE(proc.loadSampleToLayer(0, 0, wav));
    REQUIRE(proc.loadSampleToLayer(0, 1, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    auto peak = [](const juce::AudioBuffer<float>& b)
    {
        float p = 0.0f;
        for (int n = 0; n < b.getNumSamples(); ++n)
            p = std::max(p, std::abs(b.getSample(0, n)));
        return p;
    };

    setParam("polyphony", 1.0f);     // one voice: each hit steals it, read cleanly
    setParam("t0_layerMode", 2.0f);  // Round-Robin
    setParam("t0_lyr0_gain", 1.0f);  // layer 0 loud
    setParam("t0_lyr1_gain", 0.25f); // layer 1 soft

    juce::AudioBuffer<float> buffer(2, blockSize);
    auto hit = [&]()
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0); // channel 1 -> cell 0
        renderBlock(proc, buffer, midi);
        REQUIRE(bufferIsFinite(buffer));
        return peak(buffer);
    };

    const float first = hit();  // layer 0 (loud)
    const float second = hit(); // layer 1 (soft)
    const float third = hit();  // back to layer 0 (loud)

    REQUIRE(first > 0.1f);
    REQUIRE(second < 0.5f * first); // the soft layer, not a sum of both
    REQUIRE(third > 1.8f * second); // cycled back to the loud layer
    REQUIRE_THAT(third, WithinAbs(first, 0.05f));

    proc.releaseResources();
}

TEST_CASE("Cold Velocity layer mode selects a cell's layer by hit strength",
          "[processor][e2e][velolayers]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    // Cell 0's layers 0 and 1 hold the same signal but are panned hard left / hard
    // right, so the sounding layer shows up in one channel only. In Velocity mode a
    // soft note picks layer 0 (left), a hard note picks layer 1 (right).
    auto wav = writeTempWav(sampleRate, 1, blockSize);
    REQUIRE(proc.loadSampleToLayer(0, 0, wav));
    REQUIRE(proc.loadSampleToLayer(0, 1, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    auto chanPeak = [](const juce::AudioBuffer<float>& b, int ch)
    {
        float p = 0.0f;
        for (int n = 0; n < b.getNumSamples(); ++n)
            p = std::max(p, std::abs(b.getSample(ch, n)));
        return p;
    };

    setParam("polyphony", 1.0f);
    setParam("t0_layerMode", 3.0f); // Velocity
    setParam("t0_lyr0_pan", -1.0f); // soft layer hard left
    setParam("t0_lyr1_pan", 1.0f);  // loud layer hard right

    juce::AudioBuffer<float> buffer(2, blockSize);
    auto hit = [&](float vel)
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, vel), 0); // channel 1 -> cell 0
        renderBlock(proc, buffer, midi);
        REQUIRE(bufferIsFinite(buffer));
        return std::pair{chanPeak(buffer, 0), chanPeak(buffer, 1)};
    };

    auto soft = hit(0.2f); // -> layer 0, left only
    REQUIRE(soft.first > 0.05f);
    REQUIRE(soft.second < 0.01f);

    auto loud = hit(0.95f); // -> layer 1, right only
    REQUIRE(loud.second > 0.05f);
    REQUIRE(loud.first < 0.01f);

    proc.releaseResources();
}

TEST_CASE("Cold choke group cuts off another cell in the same group", "[processor][e2e][choke]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int frames = 2000; // one long block: cell A rings, then B fires mid-block

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, frames);
    proc.prepareToPlay(sampleRate, frames);

    // Cell 0 (channel 1) panned hard left, cell 1 (channel 2) hard right, so each
    // cell's sound is identifiable by channel. Stack mode applies the layer pan.
    auto wav = writeTempWav(sampleRate, 1, frames);
    REQUIRE(proc.loadSampleToLayer(0, 0, wav));
    REQUIRE(proc.loadSampleToLayer(1, 0, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    // Peak of channel `ch` over the second half of the block (after cell 1 fires).
    auto tailPeak = [](const juce::AudioBuffer<float>& b, int ch)
    {
        float p = 0.0f;
        for (int n = b.getNumSamples() / 2 + 50; n < b.getNumSamples(); ++n)
            p = std::max(p, std::abs(b.getSample(ch, n)));
        return p;
    };

    setParam("t0_layerMode", 1.0f); // Stack (so layer pan applies)
    setParam("t1_layerMode", 1.0f);
    setParam("t0_lyr0_pan", -1.0f);  // cell 0 hard left
    setParam("t1_lyr0_pan", 1.0f);   // cell 1 hard right
    setParam("t0_ampRelease", 0.0f); // fast release so a choke silences cell 0 at once

    juce::AudioBuffer<float> buffer(2, frames);
    auto run = [&]()
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);    // cell 0 at the start
        midi.addEvent(juce::MidiMessage::noteOn(2, 60, 1.0f), 1000); // cell 1 mid-block
        renderBlock(proc, buffer, midi);
        REQUIRE(bufferIsFinite(buffer));
    };

    SECTION("same group: cell 1 chokes cell 0")
    {
        setParam("t0_chokeGroup", 1.0f);
        setParam("t1_chokeGroup", 1.0f);
        run();
        REQUIRE(tailPeak(buffer, 1) > 0.05f); // cell 1 (right) sounds
        REQUIRE(tailPeak(buffer, 0) < 0.01f); // cell 0 (left) was choked
    }

    SECTION("different groups: cell 0 keeps ringing")
    {
        setParam("t0_chokeGroup", 1.0f);
        setParam("t1_chokeGroup", 2.0f);
        run();
        REQUIRE(tailPeak(buffer, 1) > 0.05f); // cell 1 (right) sounds
        REQUIRE(tailPeak(buffer, 0) > 0.05f); // cell 0 (left) still rings — no choke
    }

    proc.releaseResources();
}

TEST_CASE("Cold reverse plays a cell's sample backwards", "[processor][e2e][reverse]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int frames = 1024;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, frames);
    proc.prepareToPlay(sampleRate, frames);

    // A fade-in sample on cell 0: forwards it starts near-silent, reversed it starts
    // loud. So the peak over the FIRST quarter of the block flips with Reverse.
    auto wav = writeFadeInWav(sampleRate, frames);
    REQUIRE(proc.loadSampleToTrack(0, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    auto headPeak = [](const juce::AudioBuffer<float>& b)
    {
        float p = 0.0f;
        for (int n = 0; n < b.getNumSamples() / 8; ++n) // first eighth
            p = std::max(p, std::abs(b.getSample(0, n)));
        return p;
    };

    juce::AudioBuffer<float> buffer(2, frames);
    auto play = [&]()
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0); // channel 1 -> cell 0
        renderBlock(proc, buffer, midi);
        REQUIRE(bufferIsFinite(buffer));
        return headPeak(buffer);
    };

    const float forwardHead = play();  // fade-in: quiet at the start
    setParam("t0_lyr0_reverse", 1.0f); // per-layer reverse; Single plays layer 1's flag
    const float reverseHead = play();  // reversed: loud at the start

    REQUIRE(forwardHead < 0.2f);
    REQUIRE(reverseHead > 0.5f);
    REQUIRE(reverseHead > 3.0f * forwardHead);

    proc.releaseResources();
}

TEST_CASE("Cold lo-fi bit crusher quantizes a cell to a coarse grid",
          "[processor][e2e][bitcrusher]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    auto wav = writeTempWav(sampleRate, 1, blockSize);
    REQUIRE(proc.loadSampleToTrack(0, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    // Largest distance from any sample to the nearest 1-bit grid step (integers, as
    // 1 bit -> scale 2^0 = 1). A clean signal sits well off the grid; a crushed one
    // snaps onto it.
    auto maxGridError = [](const juce::AudioBuffer<float>& b)
    {
        float e = 0.0f;
        for (int n = 0; n < b.getNumSamples(); ++n)
        {
            const float x = b.getSample(0, n);
            e = std::max(e, std::abs(x - std::round(x)));
        }
        return e;
    };

    juce::AudioBuffer<float> buffer(2, blockSize);
    auto play = [&]()
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
        REQUIRE(bufferIsFinite(buffer));
        return maxGridError(buffer);
    };

    const float cleanError = play(); // off the integer grid
    setParam("t0_crushOn", 1.0f);
    setParam("t0_crushBits", 1.0f); // 1 bit: values snap to {-1, 0, 1}
    const float crushedError = play();

    REQUIRE(cleanError > 0.1f);    // the clean sine sits between grid steps
    REQUIRE(crushedError < 1e-4f); // crushed output lands exactly on the grid

    proc.releaseResources();
}

TEST_CASE("Cold auditions a cell on demand (editor pad click)", "[processor][e2e][audition]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    auto wav = writeTempWav(sampleRate, 1, blockSize);
    REQUIRE(proc.loadSampleToTrack(0, wav)); // cell 0 loaded; cell 1 left empty
    wav.deleteFile();

    juce::AudioBuffer<float> buffer(2, blockSize);
    auto renderSilentBlock = [&]()
    {
        buffer.clear();
        juce::MidiBuffer midi; // no MIDI: only an auditioned voice can sound
        renderBlock(proc, buffer, midi);
        REQUIRE(bufferIsFinite(buffer));
        return buffer.getMagnitude(0, blockSize);
    };

    SECTION("auditioning a loaded cell makes it sound with no MIDI")
    {
        REQUIRE(renderSilentBlock() == 0.0f); // nothing playing yet
        proc.auditionCell(0);
        REQUIRE(renderSilentBlock() > 0.0f); // the pad click triggered the sample
    }

    SECTION("auditioning an empty cell stays silent")
    {
        proc.auditionCell(1); // cell 1 has no sample
        REQUIRE(renderSilentBlock() == 0.0f);
    }

    proc.releaseResources();
}

TEST_CASE("Cold per-layer envelope shapes stacked layers independently",
          "[processor][e2e][stack][layerenv]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int frames = 8000;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, frames);
    proc.prepareToPlay(sampleRate, frames);

    // Cell 0, layers 0 and 1: same signal, panned L/R, Stack mode. Layer 0 gets a
    // fast decay-to-silence envelope, layer 1 sustains — so after the decay only the
    // right channel (layer 1) rings.
    auto wav = writeTempWav(sampleRate, 1, frames);
    REQUIRE(proc.loadSampleToLayer(0, 0, wav));
    REQUIRE(proc.loadSampleToLayer(0, 1, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    auto tailPeak = [](const juce::AudioBuffer<float>& b, int ch)
    {
        float p = 0.0f;
        for (int n = b.getNumSamples() - 1500; n < b.getNumSamples(); ++n)
            p = std::max(p, std::abs(b.getSample(ch, n)));
        return p;
    };

    setParam("t0_layerMode", 1.0f); // Stack
    setParam("t0_lyr0_pan", -1.0f); // layer 0 hard left
    setParam("t0_lyr1_pan", 1.0f);  // layer 1 hard right

    // Layer 0: own envelope, fast decay to zero sustain -> silent tail.
    setParam("t0_lyr0_ampOn", 1.0f);
    setParam("t0_lyr0_ampDecay", 0.01f);
    setParam("t0_lyr0_ampSustain", 0.0f);
    // Layer 1: own envelope, full sustain -> keeps ringing.
    setParam("t0_lyr1_ampOn", 1.0f);
    setParam("t0_lyr1_ampSustain", 1.0f);

    juce::AudioBuffer<float> buffer(2, frames);
    buffer.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    renderBlock(proc, buffer, midi);
    REQUIRE(bufferIsFinite(buffer));

    REQUIRE(tailPeak(buffer, 0) < 0.02f); // layer 0 decayed to silence (left)
    REQUIRE(tailPeak(buffer, 1) > 0.2f);  // layer 1 still sustaining (right)

    proc.releaseResources();
}

TEST_CASE("Cold per-layer mute and solo gate the stack mix", "[processor][e2e][stack][layermix]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    // Cell 0, layers 0 and 1: same signal, panned L/R, Stack mode.
    auto wav = writeTempWav(sampleRate, 1, blockSize);
    REQUIRE(proc.loadSampleToLayer(0, 0, wav));
    REQUIRE(proc.loadSampleToLayer(0, 1, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    auto chanPeak = [](const juce::AudioBuffer<float>& b, int ch)
    {
        float p = 0.0f;
        for (int n = 0; n < b.getNumSamples(); ++n)
            p = std::max(p, std::abs(b.getSample(ch, n)));
        return p;
    };

    setParam("t0_layerMode", 1.0f); // Stack
    setParam("t0_lyr0_pan", -1.0f); // layer 0 -> left
    setParam("t0_lyr1_pan", 1.0f);  // layer 1 -> right

    juce::AudioBuffer<float> buffer(2, blockSize);
    auto play = [&]()
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
        REQUIRE(bufferIsFinite(buffer));
        return std::pair{chanPeak(buffer, 0), chanPeak(buffer, 1)};
    };

    SECTION("both layers sound by default")
    {
        auto p = play();
        REQUIRE(p.first > 0.05f);
        REQUIRE(p.second > 0.05f);
    }

    SECTION("muting layer 0 silences the left, layer 1 still sounds")
    {
        setParam("t0_lyr0_mute", 1.0f);
        auto p = play();
        REQUIRE(p.first < 0.01f);  // layer 0 muted
        REQUIRE(p.second > 0.05f); // layer 1 still sounds
    }

    SECTION("soloing layer 1 silences every non-soloed layer")
    {
        setParam("t0_lyr1_solo", 1.0f);
        auto p = play();
        REQUIRE(p.first < 0.01f);  // layer 0 not soloed -> silent
        REQUIRE(p.second > 0.05f); // layer 1 soloed
    }

    proc.releaseResources();
}

TEST_CASE("Cold solo / mute take effect even in Single mode", "[processor][e2e][layermix]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    // Cell 0 in the DEFAULT Single mode, with two layers loaded, panned L/R. Single
    // normally plays only layer 1, but an active solo/mute must still isolate a layer.
    auto wav = writeTempWav(sampleRate, 1, blockSize);
    REQUIRE(proc.loadSampleToLayer(0, 0, wav));
    REQUIRE(proc.loadSampleToLayer(0, 1, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    auto chanPeak = [](const juce::AudioBuffer<float>& b, int ch)
    {
        float p = 0.0f;
        for (int n = 0; n < b.getNumSamples(); ++n)
            p = std::max(p, std::abs(b.getSample(ch, n)));
        return p;
    };

    setParam("t0_lyr0_pan", -1.0f); // layer 0 -> left
    setParam("t0_lyr1_pan", 1.0f);  // layer 1 -> right
    // Layer Mode left at Single (index 0) on purpose.

    juce::AudioBuffer<float> buffer(2, blockSize);
    auto play = [&]()
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
        REQUIRE(bufferIsFinite(buffer));
        return std::pair{chanPeak(buffer, 0), chanPeak(buffer, 1)};
    };

    SECTION("plain Single plays one centered layer (per-layer pan not applied)")
    {
        auto p = play();
        REQUIRE(p.first > 0.05f);  // Single plays layer 0 centered, so both
        REQUIRE(p.second > 0.05f); // channels carry it (pan is a stack feature)
    }

    SECTION("soloing layer 1 isolates it even though the cell is Single")
    {
        setParam("t0_lyr1_solo", 1.0f);
        auto p = play();
        REQUIRE(p.first < 0.01f);  // layer 0 not soloed -> silent
        REQUIRE(p.second > 0.05f); // layer 1 (right) now sounds
    }

    SECTION("muting layer 0 in Single lets layer 1 through")
    {
        setParam("t0_lyr0_mute", 1.0f);
        auto p = play();
        REQUIRE(p.first < 0.01f);  // layer 0 muted
        REQUIRE(p.second > 0.05f); // layer 1 sounds instead
    }

    proc.releaseResources();
}

TEST_CASE("Cold per-layer low-cut band-limits a stacked layer", "[processor][e2e][stack][filter]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int frames = 9600;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, frames);
    proc.prepareToPlay(sampleRate, frames);

    // A 100 Hz sine into cell 0's layers 0 and 1, panned L/R, Stack mode. Low-cut
    // layer 1 at 2 kHz -> the right channel's lows are removed.
    auto wav = writeSineWav(sampleRate, frames, 100.0);
    REQUIRE(proc.loadSampleToLayer(0, 0, wav));
    REQUIRE(proc.loadSampleToLayer(0, 1, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    auto tail = [](const juce::AudioBuffer<float>& b, int ch)
    {
        float p = 0.0f;
        for (int n = b.getNumSamples() - 2000; n < b.getNumSamples(); ++n)
            p = std::max(p, std::abs(b.getSample(ch, n)));
        return p;
    };

    setParam("t0_layerMode", 1.0f); // Stack
    setParam("t0_lyr0_pan", -1.0f);
    setParam("t0_lyr1_pan", 1.0f);
    setParam("t0_lyr1_hpOn", 1.0f); // low-cut layer 1
    setParam("t0_lyr1_hpCut", 2000.0f);

    juce::AudioBuffer<float> buffer(2, frames);
    buffer.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    renderBlock(proc, buffer, midi);
    REQUIRE(bufferIsFinite(buffer));

    REQUIRE(tail(buffer, 0) > 0.4f); // layer 0 (left) keeps the 100 Hz tone
    REQUIRE(tail(buffer, 1) < 0.1f); // layer 1 (right) lows removed
}

TEST_CASE("Cold per-cell level and pan trim the mix", "[processor][e2e][output]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    auto wav = writeTempWav(sampleRate, 1, blockSize);
    REQUIRE(proc.loadSampleToTrack(0, wav)); // cell 0, channel 1
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    auto peak = [](const juce::AudioBuffer<float>& b, int ch)
    {
        float p = 0.0f;
        for (int n = 0; n < b.getNumSamples(); ++n)
            p = std::max(p, std::abs(b.getSample(ch, n)));
        return p;
    };
    juce::AudioBuffer<float> buffer(2, blockSize);
    auto play = [&]()
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
        REQUIRE(bufferIsFinite(buffer));
        return std::pair{peak(buffer, 0), peak(buffer, 1)};
    };

    const float unity = play().first;
    REQUIRE(unity > 0.1f);

    SECTION("lowering the cell level makes it quieter")
    {
        setParam("t0_level", -12.0f); // ~0.25x
        REQUIRE(play().first < unity * 0.4f);
    }

    SECTION("panning the cell hard left silences the right")
    {
        setParam("t0_pan", -1.0f);
        auto p = play();
        REQUIRE(p.first > 0.1f);   // left keeps the cell
        REQUIRE(p.second < 0.01f); // right silenced
    }

    proc.releaseResources();
}

TEST_CASE("Cold per-cell delay send produces an echo", "[processor][e2e][delay]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int frames = 4096;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, frames);
    proc.prepareToPlay(sampleRate, frames);

    // A short (256-frame) sample in cell 0, so the dry hit ends early and any later
    // energy is the delayed echo.
    auto wav = writeTempWav(sampleRate, 1, 256);
    REQUIRE(proc.loadSampleToTrack(0, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    auto windowPeak = [](const juce::AudioBuffer<float>& b, int from, int to)
    {
        float p = 0.0f;
        for (int n = from; n < to; ++n)
            p = std::max(p, std::abs(b.getSample(0, n)));
        return p;
    };

    setParam("delayTime", 20.0f);    // ~882 samples
    setParam("delayFeedback", 0.0f); // single echo
    setParam("delayReturn", 1.0f);

    juce::AudioBuffer<float> buffer(2, frames);
    auto play = [&]()
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
        REQUIRE(bufferIsFinite(buffer));
    };

    SECTION("no send -> silence after the dry hit")
    {
        setParam("t0_delaySend", 0.0f);
        play();
        REQUIRE(windowPeak(buffer, 0, 256) > 0.1f);      // the dry hit
        REQUIRE(windowPeak(buffer, 1200, 2000) < 0.01f); // nothing later
    }

    SECTION("send on -> an echo appears one delay time later")
    {
        setParam("t0_delaySend", 1.0f);
        play();
        REQUIRE(windowPeak(buffer, 0, 256) > 0.1f);     // dry hit
        REQUIRE(windowPeak(buffer, 900, 1300) > 0.05f); // echo ~882 samples in
    }

    proc.releaseResources();
}

TEST_CASE("Cold per-cell reverb send produces a tail", "[processor][e2e][reverb]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int frames = 8192;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, frames);
    proc.prepareToPlay(sampleRate, frames);

    auto wav = writeTempWav(sampleRate, 1, 256); // short hit
    REQUIRE(proc.loadSampleToTrack(0, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    auto windowRms = [](const juce::AudioBuffer<float>& b, int from, int to)
    {
        double s = 0.0;
        for (int n = from; n < to; ++n)
            s += static_cast<double>(b.getSample(0, n)) * b.getSample(0, n);
        return std::sqrt(s / (to - from));
    };

    setParam("reverbRoom", 0.8f);
    setParam("reverbReturn", 1.0f);

    juce::AudioBuffer<float> buffer(2, frames);
    auto play = [&]()
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
        REQUIRE(bufferIsFinite(buffer));
    };

    setParam("t0_reverbSend", 0.0f);
    play();
    REQUIRE(windowRms(buffer, 3000, 7000) < 1e-4); // dry: silent long after the hit

    setParam("t0_reverbSend", 1.0f);
    play();
    REQUIRE(windowRms(buffer, 3000, 7000) > 1e-3); // wet: a reverb tail lingers

    proc.releaseResources();
}

TEST_CASE("Cold tempo-synced delay places the echo on the grid", "[processor][e2e][delay]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int frames = 8192;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, frames);
    proc.prepareToPlay(sampleRate, frames);

    auto wav = writeTempWav(sampleRate, 1, 256);
    REQUIRE(proc.loadSampleToTrack(0, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    auto windowPeak = [](const juce::AudioBuffer<float>& b, int from, int to)
    {
        float p = 0.0f;
        for (int n = from; n < to; ++n)
            p = std::max(p, std::abs(b.getSample(0, n)));
        return p;
    };

    // No playhead in the test harness -> host tempo defaults to 120 BPM. A 1/16
    // (0.25 beat) echo at 120 BPM = 0.125 s = 5512 samples.
    setParam("t0_delaySend", 1.0f);
    setParam("delayFeedback", 0.0f);
    setParam("delayReturn", 1.0f);
    setParam("delaySync", 1.0f);
    setParam("delayDivision", 6.0f); // "1/16"

    juce::AudioBuffer<float> buffer(2, frames);
    buffer.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    renderBlock(proc, buffer, midi);
    REQUIRE(bufferIsFinite(buffer));

    REQUIRE(windowPeak(buffer, 0, 256) > 0.1f);      // dry hit
    REQUIRE(windowPeak(buffer, 5300, 5800) > 0.05f); // echo ~5512 samples in (1/16 @ 120)
    REQUIRE(windowPeak(buffer, 2000, 4000) < 0.01f); // nothing before the grid point

    proc.releaseResources();
}

TEST_CASE("Cold velocity to cutoff brightens harder hits", "[processor][e2e][velocity][filter]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int frames = 8000;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, frames);
    proc.prepareToPlay(sampleRate, frames);

    // A ~750 Hz sine (writeKnownSignal) into cell 0, low-passed dark, with a strong
    // velocity->cutoff. A hard hit opens the filter far more than a soft hit.
    auto wav = writeSineWav(sampleRate, frames, 6000.0);
    REQUIRE(proc.loadSampleToTrack(0, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    auto tail = [](const juce::AudioBuffer<float>& b)
    {
        float p = 0.0f;
        for (int n = b.getNumSamples() - 1500; n < b.getNumSamples(); ++n)
            p = std::max(p, std::abs(b.getSample(0, n)));
        return p;
    };

    setParam("t0_filterCutoff", 500.0f); // dark base; the cell morph filter is always on
    setParam("t0_velToCutoff", 6.0f);

    juce::AudioBuffer<float> buffer(2, frames);
    auto hit = [&](float vel)
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, vel), 0);
        renderBlock(proc, buffer, midi);
        REQUIRE(bufferIsFinite(buffer));
        return tail(buffer);
    };

    const float hard = hit(1.0f);
    const float soft = hit(0.2f);
    REQUIRE(hard > 0.2f);
    REQUIRE(hard > soft * 4.0f);

    proc.releaseResources();
}

TEST_CASE("Cold addresses cells beyond the first eight", "[processor][e2e][multitrack]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    REQUIRE(proc.numSlots() == cold::ColdAudioProcessor::kNumLayers * 16); // 16 cells * layers

    // Cell 12 (MIDI channel 13 in Channel mode) is beyond the old 8-track limit.
    auto wav = writeTempWav(sampleRate, 1, blockSize);
    REQUIRE(proc.loadSampleToTrack(12, wav));
    wav.deleteFile();

    juce::AudioBuffer<float> buffer(2, blockSize);
    buffer.clear();
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(13, 60, 1.0f), 0); // channel 13 -> cell 12
    renderBlock(proc, buffer, midi);
    REQUIRE(buffer.getMagnitude(0, blockSize) > 0.0f);

    proc.releaseResources();
}

TEST_CASE("Cold cells own their layer samples", "[processor][e2e][stack][layers]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    // Load two samples directly into cell 3's own layers 0 and 1 (channel 4).
    auto wav = writeTempWav(sampleRate, 1, blockSize);
    REQUIRE(proc.loadSampleToLayer(3, 0, wav));
    REQUIRE(proc.loadSampleToLayer(3, 1, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    auto peak = [](const juce::AudioBuffer<float>& b)
    {
        float p = 0.0f;
        for (int n = 0; n < b.getNumSamples(); ++n)
            p = std::max(p, std::abs(b.getSample(0, n)));
        return p;
    };

    juce::AudioBuffer<float> buffer(2, blockSize);

    // A different cell is unaffected: cell 4 (channel 5) has no samples -> silent.
    // Checked first so no earlier note can bleed across the block boundary.
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(5, 60, 1.0f), 0); // channel 5 -> cell 4
        renderBlock(proc, buffer, midi);
        REQUIRE(buffer.getMagnitude(0, blockSize) == 0.0f);
    }

    // Single mode: cell 3 plays only its primary layer.
    float single = 0.0f;
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(4, 60, 1.0f), 0); // channel 4 -> cell 3
        renderBlock(proc, buffer, midi);
        single = peak(buffer);
        REQUIRE(single > 0.1f);
    }

    // Stack mode: both of cell 3's loaded layers sound together (in-phase ~2x).
    setParam("t3_layerMode", 1.0f);
    {
        buffer.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(4, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
        REQUIRE(peak(buffer) > 1.5f * single);
    }

    proc.releaseResources();
}

TEST_CASE("Cold processor loads a folder into consecutive slots", "[processor][e2e][bank]")
{
    constexpr double sampleRate = 44100.0;

    auto dir = juce::File::createTempFile("_cold_bank");
    dir.deleteFile();
    REQUIRE(dir.createDirectory());
    for (int i = 0; i < 3; ++i)
    {
        juce::WavAudioFormat wav;
        cold::test::writeKnownSignal(wav, dir.getChildFile("s" + juce::String(i) + ".wav"),
                                     sampleRate, 1, 128);
    }

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, 512);
    proc.prepareToPlay(sampleRate, 512);

    REQUIRE(proc.loadFolder(dir) == 3);
    dir.deleteRecursively();
}

TEST_CASE("Cold imports a Battery .kit onto its tracks", "[processor][e2e][battery]")
{
    constexpr double sampleRate = 48000.0;

    // A temp folder with a .kit and the two samples it references.
    auto dir = juce::File::createTempFile("_cold_kit");
    dir.deleteFile();
    REQUIRE(dir.createDirectory());
    for (const char* name : {"Kick.wav", "Snare.wav"})
    {
        juce::WavAudioFormat wav;
        cold::test::writeKnownSignal(wav, dir.getChildFile(name), sampleRate, 1, 256);
    }
    const juce::String kitXml =
        "<?xml version=\"1.0\"?><BatteryPatch name=\"Kit\"><Cells>"
        "<SampleSlot label=\"Kick\" vattack=\"0\" vhold=\"0\" vdecay=\"40\" vsustain=\"0\" "
        "vrelease=\"20\" pitch=\"1.0\" rootKey=\"36\" pan=\"0.5\" volume=\"1.0\">"
        "<Sample file=\"Kick.wav\"/></SampleSlot>"
        "<SampleSlot label=\"Snare\" vattack=\"0\" vhold=\"0\" vdecay=\"60\" vsustain=\"80\" "
        "vrelease=\"40\" pitch=\"2.0\" rootKey=\"38\" pan=\"0.0\" volume=\"0.5\">"
        "<Sample file=\"Snare.wav\"/></SampleSlot>"
        "</Cells></BatteryPatch>";
    auto kitFile = dir.getChildFile("Kit.kit");
    kitFile.replaceWithText(kitXml);

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, 512);
    proc.prepareToPlay(sampleRate, 512);

    REQUIRE(proc.importBatteryKit(kitFile, dir) == 2);

    // Samples resolved into cell 0 and cell 1 (their primary slots 0 and 4).
    REQUIRE(proc.slotSampleName(0).isNotEmpty());                              // cell 0 layer 0
    REQUIRE(proc.slotSampleName(ColdAudioProcessor::kNumLayers).isNotEmpty()); // cell 1 layer 0

    auto value = [&proc](const char* id)
    {
        auto* p = proc.apvts.getParameter(id);
        return p->convertFrom0to1(p->getValue());
    };

    // Per-cell settings landed on the right tracks/layers.
    REQUIRE(value("t0_ampDecay") > 0.0f);                         // decay knob 40 -> >0 s
    REQUIRE(value("t1_ampDecay") > value("t0_ampDecay"));         // 60 > 40
    REQUIRE_THAT(value("t1_lyr0_tune"), WithinAbs(12.0f, 0.05f)); // pitch 2 -> +12 st
    REQUIRE_THAT(value("t1_lyr0_pan"), WithinAbs(-1.0f, 0.01f));  // pan 0 -> hard left
    REQUIRE_THAT(value("t1_lyr0_gain"), WithinAbs(0.5f, 0.01f));  // volume 0.5
    REQUIRE(value("midiAddressing") > 0.5f);                      // Note Map enabled
    REQUIRE_THAT(value("t0_mapNote"), WithinAbs(36.0f, 0.5f));    // Kick's own root key
    REQUIRE_THAT(value("t1_mapNote"), WithinAbs(38.0f, 0.5f));    // Snare's (non-consecutive) key

    dir.deleteRecursively();
}

TEST_CASE("Cold imports a Battery .kit with truncated sample names", "[processor][e2e][battery]")
{
    constexpr double sampleRate = 48000.0;

    auto dir = juce::File::createTempFile("_cold_kit_trunc");
    dir.deleteFile();
    REQUIRE(dir.createDirectory());
    // Real files have full names; the .kit stores TRUNCATED stubs (as Battery does).
    for (const char* name : {"KickDrumLong.wav", "SnareTight.wav"})
    {
        juce::WavAudioFormat wav;
        cold::test::writeKnownSignal(wav, dir.getChildFile(name), sampleRate, 1, 256);
    }
    const juce::String kitXml =
        "<?xml version=\"1.0\"?><BatteryPatch name=\"Kit\"><Cells>"
        "<SampleSlot label=\"Kick\" vattack=\"0\" vhold=\"0\" vdecay=\"40\" vsustain=\"0\" "
        "vrelease=\"20\" pitch=\"1.0\" rootKey=\"36\" pan=\"0.5\" volume=\"1.0\">"
        "<Sample file=\"KickDr\"/></SampleSlot>" // truncated stub -> KickDrumLong.wav
        "<SampleSlot label=\"Snare\" vattack=\"0\" vhold=\"0\" vdecay=\"60\" vsustain=\"80\" "
        "vrelease=\"40\" pitch=\"1.0\" rootKey=\"38\" pan=\"0.0\" volume=\"0.5\">"
        "<Sample file=\"SnareTi\"/></SampleSlot>" // truncated stub -> SnareTight.wav
        "</Cells></BatteryPatch>";
    auto kitFile = dir.getChildFile("Kit.kit");
    kitFile.replaceWithText(kitXml);

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, 512);
    proc.prepareToPlay(sampleRate, 512);

    REQUIRE(proc.importBatteryKit(kitFile, dir) == 2);
    // The truncated stubs resolved to the full-named files on disk (cells 0 and 1,
    // primary slots 0 and kNumLayers).
    REQUIRE(proc.slotSampleName(0) == juce::String("KickDrumLong"));
    REQUIRE(proc.slotSampleName(ColdAudioProcessor::kNumLayers) == juce::String("SnareTight"));

    dir.deleteRecursively();
}

TEST_CASE("Cold processor exposes its M1 parameters", "[processor][e2e]")
{
    ColdAudioProcessor proc;
    for (const auto* id : {"gain",
                           "polyphony",
                           "loopMode",
                           "loopStart",
                           "loopEnd",
                           "rootNote",
                           "lfoShape",
                           "lfoRate",
                           "lfoDepth",
                           "lfoSync",
                           "lfoDivision",
                           "lfoToCutoff",
                           "lfoToType",
                           "track",
                           "modAttack",
                           "modDecay",
                           "modSustain",
                           "modRelease",
                           "modToCutoff",
                           "modToType",
                           "lfoToPitch",
                           "lfoToSampleSelect",
                           "lfoTrigMode",
                           "lfoStep1",
                           "lfoStep4",
                           "lfoStep8",
                           "t0_layerMode",
                           "t0_lyr0_gain",
                           "t0_lyr1_gain",
                           "t0_lyr3_tune",
                           "t7_layerMode",
                           "t0_lyr0_pan",
                           "t3_lyr2_pan",
                           "midiAddressing",
                           "t0_mapNote",
                           "t7_mapNote",
                           "t0_chokeGroup",
                           "t7_chokeGroup",
                           "t0_level",
                           "t7_pan",
                           "t0_delaySend",
                           "delayTime",
                           "delayReturn",
                           "delaySync",
                           "delayDivision",
                           "t0_reverbSend",
                           "reverbRoom",
                           "reverbReturn",
                           "masterTiltOn",
                           "masterTilt",
                           "masterTiltPivot",
                           "masterCompOn",
                           "masterCompThreshold",
                           "masterCompRatio",
                           "masterCompAttack",
                           "masterCompRelease",
                           "masterCompMakeup",
                           "pitchBendRange",
                           "masterWidthOn",
                           "masterWidth",
                           "limiterOn",
                           "limiterCeiling",
                           "limiterRelease",
                           "t0_velToCutoff",
                           "t0_humanizePitch",
                           "t7_humanizeLevel",
                           "t0_lyr0_reverse",
                           "t0_lyr0_filterOn",
                           "t0_lyr0_cutoff",
                           "t2_lyr1_ftype",
                           "t0_ampAttack",
                           "t0_ampRelease",
                           "t0_ampVelocity",
                           "t7_ampDecay",
                           "t3_ampHold",
                           "t0_filterCutoff",
                           "t0_filterResonance",
                           "t0_filterType",
                           "t5_filterCutoff",
                           "t0_pitchEnvAmount",
                           "t0_pitchEnvDecay",
                           "t4_pitchEnvAmount",
                           "t0_velToPitch",
                           "t6_velToPitch",
                           "t0_compOn",
                           "t0_compThreshold",
                           "t0_compRatio",
                           "t3_compMakeup",
                           "t0_satOn",
                           "t0_satDrive",
                           "t5_satMix",
                           "t0_crushOn",
                           "t0_crushBits",
                           "t7_crushRate",
                           "t0_lyr0_ampOn",
                           "t0_lyr1_ampDecay",
                           "t7_lyr3_ampRelease",
                           "t0_lyr0_mute",
                           "t0_lyr1_solo",
                           "t0_lyr0_hpOn",
                           "t0_lyr1_hpCut",
                           "t7_lyr3_lpCut"})
        REQUIRE(proc.apvts.getParameter(id) != nullptr);
}

TEST_CASE("Cold per-cell saturator drives a track", "[processor][e2e][pertrack][saturator]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int frames = 4000;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, frames);
    proc.prepareToPlay(sampleRate, frames);

    auto wav = writeTempWav(sampleRate, 1, frames);
    REQUIRE(proc.loadSampleToTrack(0, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };

    juce::AudioBuffer<float> buffer(2, frames);

    // Dry.
    buffer.clear();
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
    }
    const float dryPeak = buffer.getMagnitude(0, frames);
    REQUIRE(dryPeak > 0.0f);

    // Heavy drive: the tanh shaper squares off the sub-full-scale tone, pushing its
    // peak up toward full scale -> measurably louder / reshaped vs the dry tone.
    setParam("t0_satOn", 1.0f);
    setParam("t0_satDrive", 36.0f);
    setParam("t0_satMix", 1.0f);
    buffer.clear();
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
    }
    REQUIRE(buffer.getMagnitude(0, frames) > dryPeak * 1.2f);

    proc.releaseResources();
}

TEST_CASE("Cold per-cell compressor tames a loud track", "[processor][e2e][pertrack][compressor]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int frames = 8000;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, frames);
    proc.prepareToPlay(sampleRate, frames);

    auto wav = writeTempWav(sampleRate, 1, frames);
    REQUIRE(proc.loadSampleToSlot(0, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    auto tailPeak = [](const juce::AudioBuffer<float>& b)
    {
        float p = 0.0f;
        for (int n = b.getNumSamples() - 1000; n < b.getNumSamples(); ++n)
            p = std::max(p, std::abs(b.getSample(0, n)));
        return p;
    };

    juce::AudioBuffer<float> buffer(2, frames);

    // Dry: no compressor.
    buffer.clear();
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
    }
    const float dry = tailPeak(buffer);
    REQUIRE(dry > 0.1f);

    // Heavy compressor on track 0 -> the settled tail is pulled down.
    setParam("t0_compOn", 1.0f);
    setParam("t0_compThreshold", -24.0f);
    setParam("t0_compRatio", 10.0f);
    setParam("t0_compAttack", 0.001f);
    setParam("t0_compRelease", 0.05f);
    setParam("t0_compMakeup", 0.0f);
    buffer.clear();
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
    }
    REQUIRE(tailPeak(buffer) < dry * 0.7f); // clearly compressed

    proc.releaseResources();
}

TEST_CASE("Cold master limiter holds the output under the ceiling",
          "[processor][e2e][master][limiter]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int frames = 8000;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, frames);
    proc.prepareToPlay(sampleRate, frames);

    // A full-scale sine so the master output peaks near 0 dBFS.
    auto wav = writeSineWav(sampleRate, frames, 220.0);
    REQUIRE(proc.loadSampleToSlot(0, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    auto peak = [](const juce::AudioBuffer<float>& b)
    {
        float p = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int n = 0; n < b.getNumSamples(); ++n)
                p = std::max(p, std::abs(b.getSample(ch, n)));
        return p;
    };

    juce::AudioBuffer<float> buffer(2, frames);

    // Dry: no limiter -> the loud sine reaches near full scale.
    buffer.clear();
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0); // A4 = the sample's pitch
        renderBlock(proc, buffer, midi);
    }
    const float dry = peak(buffer);
    REQUIRE(dry > 0.5f);

    // Limiter on with a low ceiling: nothing may exceed it (a small margin for the
    // dB->linear rounding), and it must actually be clamping the loud dry signal.
    const float ceilingDb = -12.0f;
    const float ceilingLin = std::pow(10.0f, ceilingDb / 20.0f);
    REQUIRE(dry > ceilingLin); // the dry signal really is over the ceiling
    setParam("limiterOn", 1.0f);
    setParam("limiterCeiling", ceilingDb);
    setParam("limiterRelease", 100.0f);
    buffer.clear();
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
        renderBlock(proc, buffer, midi);
    }
    REQUIRE(peak(buffer) <= ceilingLin + 1e-3f);

    proc.releaseResources();
}

TEST_CASE("Cold master compressor glues the summed mix", "[processor][e2e][master][compressor]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int frames = 8000;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, frames);
    proc.prepareToPlay(sampleRate, frames);

    // A sustained known signal so the master detector settles well above threshold.
    auto wav = writeTempWav(sampleRate, 1, frames);
    REQUIRE(proc.loadSampleToSlot(0, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    auto tailPeak = [](const juce::AudioBuffer<float>& b)
    {
        float p = 0.0f;
        for (int n = b.getNumSamples() - 1000; n < b.getNumSamples(); ++n)
            p = std::max(p, std::abs(b.getSample(0, n)));
        return p;
    };

    juce::AudioBuffer<float> buffer(2, frames);

    // Dry: no master compression.
    buffer.clear();
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
    }
    const float dry = tailPeak(buffer);
    REQUIRE(dry > 0.1f);

    // Heavy master compressor -> the settled master tail is pulled down.
    setParam("masterCompOn", 1.0f);
    setParam("masterCompThreshold", -24.0f);
    setParam("masterCompRatio", 10.0f);
    setParam("masterCompAttack", 0.001f);
    setParam("masterCompRelease", 0.05f);
    setParam("masterCompMakeup", 0.0f);
    buffer.clear();
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
    }
    REQUIRE(tailPeak(buffer) < dry * 0.7f); // clearly compressed on the master bus

    proc.releaseResources();
}

TEST_CASE("Cold master tilt EQ shapes the mix tone", "[processor][e2e][master][tilteq]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int frames = 8000;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, frames);
    proc.prepareToPlay(sampleRate, frames);

    // A low tone, well below the tilt pivot, so a positive (bright) tilt cuts it.
    auto wav = writeSineWav(sampleRate, frames, 100.0);
    REQUIRE(proc.loadSampleToSlot(0, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    // Peak over the whole buffer past the filter's brief start-up.
    auto peak = [](const juce::AudioBuffer<float>& b)
    {
        float p = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int n = 64; n < b.getNumSamples(); ++n)
                p = std::max(p, std::abs(b.getSample(ch, n)));
        return p;
    };

    juce::AudioBuffer<float> buffer(2, frames);

    // Dry: no tilt. Note 60 = the sample's root, so the 100 Hz stays 100 Hz.
    buffer.clear();
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
    }
    const float dry = peak(buffer);
    REQUIRE(dry > 0.3f);

    // Strong positive tilt with a 700 Hz pivot -> the 100 Hz tone is attenuated.
    setParam("masterTiltOn", 1.0f);
    setParam("masterTilt", 12.0f);
    setParam("masterTiltPivot", 700.0f);
    buffer.clear();
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
    }
    REQUIRE(peak(buffer) < dry * 0.7f); // the lows were cut by the bright tilt

    proc.releaseResources();
}

TEST_CASE("Cold master stereo width collapses a panned mix to mono",
          "[processor][e2e][master][width]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int frames = 4000;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, frames);
    proc.prepareToPlay(sampleRate, frames);

    auto wav = writeTempWav(sampleRate, 1, frames);
    REQUIRE(proc.loadSampleToSlot(0, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    auto maxChannelDiff = [](const juce::AudioBuffer<float>& b)
    {
        float d = 0.0f;
        for (int n = 0; n < b.getNumSamples(); ++n)
            d = std::max(d, std::abs(b.getSample(0, n) - b.getSample(1, n)));
        return d;
    };

    // Hard-pan the cell to one side so L and R clearly differ.
    setParam("t0_pan", -1.0f);

    juce::AudioBuffer<float> buffer(2, frames);

    // Dry: no width processing -> the panned hit leaves L and R different.
    buffer.clear();
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
    }
    const float dryDiff = maxChannelDiff(buffer);
    REQUIRE(dryDiff > 0.05f); // there really is a stereo difference

    // Width 0 -> fully mono: L and R become identical.
    setParam("masterWidthOn", 1.0f);
    setParam("masterWidth", 0.0f);
    buffer.clear();
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
    }
    REQUIRE(maxChannelDiff(buffer) < 1e-4f); // collapsed to mono

    proc.releaseResources();
}

TEST_CASE("Cold humanize varies repeated hits", "[processor][e2e][pertrack][humanize]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int frames = 2000;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, frames);
    proc.prepareToPlay(sampleRate, frames);

    auto wav = writeTempWav(sampleRate, 1, frames);
    REQUIRE(proc.loadSampleToSlot(0, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    auto peak = [](const juce::AudioBuffer<float>& b)
    {
        float p = 0.0f;
        for (int n = 0; n < b.getNumSamples(); ++n)
            p = std::max(p, std::abs(b.getSample(0, n)));
        return p;
    };
    // Fire one note, render a fresh block, and return its peak.
    auto hit = [&](juce::AudioBuffer<float>& buf)
    {
        buf.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buf, midi);
        return peak(buf);
    };

    juce::AudioBuffer<float> buffer(2, frames);

    // Humanize off: repeated identical hits are bit-for-bit identical.
    setParam("t0_humanizePitch", 0.0f);
    setParam("t0_humanizeLevel", 0.0f);
    const float a = hit(buffer);
    const float b = hit(buffer);
    REQUIRE(a > 0.0f);
    REQUIRE_THAT(a, Catch::Matchers::WithinAbs(b, 1e-6f)); // deterministic

    // Strong level humanize: over several hits the peaks are no longer all equal.
    setParam("t0_humanizeLevel", 12.0f);
    float lo = 1.0e9f;
    float hi = 0.0f;
    for (int i = 0; i < 12; ++i)
    {
        const float p = hit(buffer);
        lo = std::min(lo, p);
        hi = std::max(hi, p);
    }
    REQUIRE(hi - lo > 0.01f); // the hits actually vary

    proc.releaseResources();
}

TEST_CASE("Cold pitch wheel bends the playback", "[processor][e2e][bend]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int frames = 4000;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, frames);
    proc.prepareToPlay(sampleRate, frames);

    // A linear fade-in ramp: the output value tracks read position, so playing
    // faster (a bend up) makes the ramp rise sooner.
    auto wav = writeFadeInWav(sampleRate, frames);
    REQUIRE(proc.loadSampleToSlot(0, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    constexpr int probe = 500; // an early index, before either playback runs out

    juce::AudioBuffer<float> buffer(2, frames);

    // No bend: the ramp rises at its natural rate.
    setParam("pitchBendRange", 12.0f);
    buffer.clear();
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
    }
    const float natural = std::abs(buffer.getSample(0, probe));
    REQUIRE(natural > 0.0f);

    // Full wheel up with a 12-semitone range ~= +1 octave = 2x speed, so the ramp
    // reaches a clearly higher value by the same probe index.
    buffer.clear();
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
    }
    const float bent = std::abs(buffer.getSample(0, probe));
    REQUIRE(bent > natural * 1.5f);

    proc.releaseResources();
}

TEST_CASE("Cold velocity modulates pitch per track", "[processor][e2e][pertrack][velocity]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int frames = 4000;
    constexpr int sampleFrames = 16000; // outlast the render window even sped up

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, frames);
    proc.prepareToPlay(sampleRate, frames);

    // A tonal sample (~750 Hz), longer than the block so a sped-up one-shot still
    // fills the whole render window (otherwise it would end early and undercount).
    auto wav = writeTempWav(sampleRate, 1, sampleFrames);
    REQUIRE(proc.loadSampleToSlot(0, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    // Count sign changes on channel 0 — a scale-independent pitch proxy.
    auto zeroCrossings = [](const juce::AudioBuffer<float>& b)
    {
        int count = 0;
        for (int n = 1; n < b.getNumSamples(); ++n)
            if ((b.getSample(0, n - 1) <= 0.0f) != (b.getSample(0, n) <= 0.0f))
                ++count;
        return count;
    };

    juce::AudioBuffer<float> buffer(2, frames);

    // Same full velocity in both renders, so only the pitch (not amplitude) changes.
    setParam("t0_velToPitch", 0.0f);
    buffer.clear();
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
    }
    const int baseCrossings = zeroCrossings(buffer);
    REQUIRE(baseCrossings > 0);

    // +12 semitones at full velocity -> ~one octave up -> ~twice the crossings.
    setParam("t0_velToPitch", 12.0f);
    buffer.clear();
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        renderBlock(proc, buffer, midi);
    }
    const int highCrossings = zeroCrossings(buffer);

    REQUIRE(highCrossings > baseCrossings * 3 / 2); // markedly higher pitch

    proc.releaseResources();
}

TEST_CASE("Cold filter is independent per track", "[processor][e2e][pertrack][filter]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int frames = 8000; // long enough for the filter smoother to settle

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, frames);
    proc.prepareToPlay(sampleRate, frames);

    // A ~750 Hz tone into cells 0 and 1 (channels 1 and 2).
    auto wav = writeTempWav(sampleRate, 1, frames);
    REQUIRE(proc.loadSampleToTrack(0, wav));
    REQUIRE(proc.loadSampleToTrack(1, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };
    auto tailPeak = [](const juce::AudioBuffer<float>& b)
    {
        float p = 0.0f;
        for (int n = b.getNumSamples() - 1000; n < b.getNumSamples(); ++n)
            p = std::max(p, std::abs(b.getSample(0, n)));
        return p;
    };

    // Track 0 filter wide open; track 1 clamped to a low cutoff (dark).
    setParam("t0_filterCutoff", 20000.0f);
    setParam("t1_filterCutoff", 120.0f);

    juce::AudioBuffer<float> buffer(2, frames);

    buffer.clear();
    juce::MidiBuffer m0;
    m0.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0); // channel 1 -> track 0
    renderBlock(proc, buffer, m0);
    const float open = tailPeak(buffer);

    buffer.clear();
    juce::MidiBuffer m1;
    m1.addEvent(juce::MidiMessage::noteOn(2, 60, 1.0f), 0); // channel 2 -> track 1
    renderBlock(proc, buffer, m1);
    const float dark = tailPeak(buffer);

    REQUIRE(open > 0.1f);        // track 0 passes the tone
    REQUIRE(dark < 0.3f * open); // track 1's low cutoff attenuates it
    proc.releaseResources();
}

TEST_CASE("Cold amp envelope is independent per track", "[processor][e2e][pertrack]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    // Same signal into cells 0 and 1 (channels 1 and 2).
    auto wav = writeTempWav(sampleRate, 1, blockSize);
    REQUIRE(proc.loadSampleToTrack(0, wav));
    REQUIRE(proc.loadSampleToTrack(1, wav));
    wav.deleteFile();

    auto setParam = [&proc](const char* id, float value)
    {
        auto* p = proc.apvts.getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(value));
    };

    // Track 0: instant attack. Track 1: a long (1 s) attack — barely open over one
    // 512-sample block. Proves each track has its own amp envelope.
    setParam("t0_ampAttack", 0.0f);
    setParam("t1_ampAttack", 1.0f);

    juce::AudioBuffer<float> buffer(2, blockSize);

    buffer.clear();
    juce::MidiBuffer m0;
    m0.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0); // channel 1 -> track 0
    renderBlock(proc, buffer, m0);
    const float track0Peak = buffer.getMagnitude(0, blockSize);

    buffer.clear();
    juce::MidiBuffer m1;
    m1.addEvent(juce::MidiMessage::noteOn(2, 60, 1.0f), 0); // channel 2 -> track 1
    renderBlock(proc, buffer, m1);
    const float track1Peak = buffer.getMagnitude(0, blockSize);

    REQUIRE(track0Peak > 0.1f);              // instant attack: sounds at once
    REQUIRE(track1Peak < 0.1f * track0Peak); // long attack: still near silent
    proc.releaseResources();
}

TEST_CASE("Cold exposes a waveform view of a loaded slot", "[processor][e2e][waveform]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;
    constexpr int numFrames = 2000;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    auto wav = writeTempWav(sampleRate, 2, numFrames);
    REQUIRE(proc.loadSampleToSlot(3, wav));
    wav.deleteFile();

    SECTION("a loaded slot yields the requested number of finite peak bins")
    {
        const auto peaks = proc.slotWaveform(3, 128);
        REQUIRE(peaks.numBins() == 128);
        float maxAbs = 0.0f;
        for (int b = 0; b < peaks.numBins(); ++b)
        {
            REQUIRE(std::isfinite(peaks.minValues[static_cast<size_t>(b)]));
            REQUIRE(std::isfinite(peaks.maxValues[static_cast<size_t>(b)]));
            REQUIRE(peaks.maxValues[static_cast<size_t>(b)] >=
                    peaks.minValues[static_cast<size_t>(b)]);
            maxAbs = std::max(maxAbs, std::abs(peaks.maxValues[static_cast<size_t>(b)]));
        }
        REQUIRE(maxAbs > 0.0f); // the known signal is not silent
    }

    SECTION("slot geometry reports the full sample and its window")
    {
        REQUIRE(proc.slotTotalFrames(3) == numFrames);
        REQUIRE(proc.slotWindowStart(3) == 0);
        REQUIRE(proc.slotWindowLength(3) == numFrames); // whole sample by default
    }

    SECTION("an unloaded slot has an empty waveform and zero geometry")
    {
        REQUIRE(proc.slotWaveform(5, 128).empty());
        REQUIRE(proc.slotTotalFrames(5) == 0);
        REQUIRE(proc.slotWindowLength(5) == 0);
    }

    proc.releaseResources();
}

TEST_CASE("Cold slot start/length window the playback", "[processor][e2e][window]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 4000;
    constexpr int sampleFrames = 16000;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    auto wav = writeTempWav(sampleRate, 1, sampleFrames);
    REQUIRE(proc.loadSampleToSlot(0, wav));
    wav.deleteFile();

    SECTION("geometry reflects the set start and length")
    {
        proc.setSlotStartFrames(0, 8000);
        REQUIRE(proc.slotWindowStart(0) == 8000);
        REQUIRE(proc.slotWindowLength(0) == sampleFrames - 8000); // to the end

        proc.setSlotLengthFrames(0, 4000);
        REQUIRE(proc.slotWindowLength(0) == 4000);

        // Out-of-range start clamps to the sample bounds.
        proc.setSlotStartFrames(0, 999999);
        REQUIRE(proc.slotWindowStart(0) == sampleFrames);
    }

    SECTION("a short length frees the one-shot early (silent tail)")
    {
        auto tailPeak = [](const juce::AudioBuffer<float>& b)
        {
            float p = 0.0f;
            for (int n = b.getNumSamples() - 1000; n < b.getNumSamples(); ++n)
                p = std::max(p, std::abs(b.getSample(0, n)));
            return p;
        };

        juce::AudioBuffer<float> buffer(2, blockSize);

        // Full window: the one-shot plays through the whole block.
        buffer.clear();
        {
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
            renderBlock(proc, buffer, midi);
        }
        const float fullTail = tailPeak(buffer);
        REQUIRE(fullTail > 0.1f);

        // Narrow the window to 200 frames -> the voice frees long before the block
        // ends, so the tail is silent.
        proc.setSlotStartFrames(0, 0);
        proc.setSlotLengthFrames(0, 200);
        buffer.clear();
        {
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
            renderBlock(proc, buffer, midi);
        }
        REQUIRE(tailPeak(buffer) < fullTail * 0.05f);
    }

    proc.releaseResources();
}

TEST_CASE("Cold processor state save/restore round-trips", "[processor][e2e]")
{
    ColdAudioProcessor proc;

    juce::MemoryBlock state;
    proc.getStateInformation(state);

    // Restoring the plugin's own serialised state must not throw or corrupt it.
    REQUIRE_NOTHROW(proc.setStateInformation(state.getData(), static_cast<int>(state.getSize())));

    juce::MemoryBlock again;
    proc.getStateInformation(again);
    REQUIRE(again.getSize() == state.getSize()); // stable round-trip
}

TEST_CASE("Cold processor creates its editor", "[processor][e2e]")
{
    juce::ScopedJuceInitialiser_GUI guiInitialiser; // editors need a message manager

    ColdAudioProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());

    REQUIRE(editor != nullptr);
    REQUIRE(editor->getWidth() > 0);
    REQUIRE(editor->getHeight() > 0);
}
