// End-to-end tests: instantiate the real Cold plugin processor and drive it the
// way a host (Logic) does — prepare, push MIDI + audio blocks, query the plugin
// contract, save/restore state, open the editor. The sampler engine is wired in
// from M1 onward; today these lock down the M0 contract (silent, stable, valid)
// and grow into "load a WAV -> hear non-silent output" as playback lands.

#include <catch2/catch_test_macros.hpp>

#include "PluginProcessor.h"
#include "support/SignalFixtures.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>

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

TEST_CASE("Cold processor exposes its M1 parameters", "[processor][e2e]")
{
    ColdAudioProcessor proc;
    for (const auto* id :
         {"gain",          "polyphony",       "loopMode",         "loopStart",
          "loopEnd",       "rootNote",        "ampAttack",        "ampDecay",
          "ampSustain",    "ampRelease",      "filterCutoff",     "filterResonance",
          "filterType",    "lfoShape",        "lfoRate",          "lfoDepth",
          "lfoSync",       "lfoDivision",     "lfoToCutoff",      "lfoToType",
          "track",         "ampDelay",        "ampHold",          "ampAttackCurve",
          "ampDecayCurve", "ampReleaseCurve", "ampVelocity",      "modAttack",
          "modDecay",      "modSustain",      "modRelease",       "modToCutoff",
          "modToType",     "lfoToPitch",      "lfoToSampleSelect"})
        REQUIRE(proc.apvts.getParameter(id) != nullptr);
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
