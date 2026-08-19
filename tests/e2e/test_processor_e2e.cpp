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

    // Bank slot 2 feeds track 2, which is driven by MIDI channel 3.
    auto wav = writeTempWav(sampleRate, 2, blockSize);
    REQUIRE(proc.loadSampleToSlot(2, wav));
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

    // Slots 0 and 1 hold the same signal; track 0 (channel 1) stacks both, so the
    // in-phase sum is ~2x a single layer.
    auto wav = writeTempWav(sampleRate, 1, blockSize);
    REQUIRE(proc.loadSampleToSlot(0, wav));
    REQUIRE(proc.loadSampleToSlot(1, wav));
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

    // Switch track 0 to Stack with layer 0 = slot 0 (default) and layer 1 = slot 1.
    setParam("t0_layerMode", 1.0f); // Stack
    setParam("t0_lyr1_slot", 1.0f); // enable a second layer on slot 1
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

    // Samples resolved into slots 0 and 1.
    REQUIRE(proc.slotSampleName(0).isNotEmpty());
    REQUIRE(proc.slotSampleName(1).isNotEmpty());

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
    REQUIRE_THAT(value("noteMapBase"), WithinAbs(36.0f, 0.5f));   // first cell's root key

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
                           "t0_lyr0_slot",
                           "t0_lyr1_gain",
                           "t0_lyr3_tune",
                           "t7_layerMode",
                           "t0_lyr0_pan",
                           "t3_lyr2_pan",
                           "midiAddressing",
                           "noteMapBase",
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
                           "t5_filterCutoff"})
        REQUIRE(proc.apvts.getParameter(id) != nullptr);
}

TEST_CASE("Cold filter is independent per track", "[processor][e2e][pertrack][filter]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int frames = 8000; // long enough for the filter smoother to settle

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, frames);
    proc.prepareToPlay(sampleRate, frames);

    // A ~750 Hz tone in slots 0 and 1 -> tracks 0 and 1 (channels 1 and 2).
    auto wav = writeTempWav(sampleRate, 1, frames);
    REQUIRE(proc.loadSampleToSlot(0, wav));
    REQUIRE(proc.loadSampleToSlot(1, wav));
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

    // Same signal in slots 0 and 1 -> tracks 0 and 1 (channels 1 and 2).
    auto wav = writeTempWav(sampleRate, 1, blockSize);
    REQUIRE(proc.loadSampleToSlot(0, wav));
    REQUIRE(proc.loadSampleToSlot(1, wav));
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
