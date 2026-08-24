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

    const float forwardHead = play(); // fade-in: quiet at the start
    setParam("t0_reverse", 1.0f);
    const float reverseHead = play(); // reversed: loud at the start

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
                           "t0_reverse",
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
                           "t7_crushRate"})
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
