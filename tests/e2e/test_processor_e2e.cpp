// End-to-end tests: instantiate the real Cold plugin processor and drive it the
// way a host (Logic) does — prepare, push MIDI + audio blocks, query the plugin
// contract, save/restore state, open the editor. The sampler engine is wired in
// from M1 onward; today these lock down the M0 contract (silent, stable, valid)
// and grow into "load a WAV -> hear non-silent output" as playback lands.

#include <catch2/catch_test_macros.hpp>

#include "PluginProcessor.h"

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

TEST_CASE("Cold processor renders finite, silent output and survives MIDI (M0)", "[processor][e2e]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 512;

    ColdAudioProcessor proc;
    proc.setPlayConfigDetails(0, 2, sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    juce::AudioBuffer<float> buffer(2, blockSize);

    SECTION("empty MIDI -> silence")
    {
        buffer.clear();
        for (int n = 0; n < blockSize; ++n) // dirty the buffer first
            buffer.setSample(0, n, 0.123f);

        juce::MidiBuffer midi;
        renderBlock(proc, buffer, midi);

        REQUIRE(bufferIsFinite(buffer));
        REQUIRE(buffer.getMagnitude(0, blockSize) == 0.0f); // M0 is silent
    }

    SECTION("a played chord does not crash or denormalise (still silent in M0)")
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 64, 1.0f), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 67, 1.0f), 0);

        for (int block = 0; block < 8; ++block)
        {
            buffer.clear();
            renderBlock(proc, buffer, midi);
            REQUIRE(bufferIsFinite(buffer));
            midi.clear();
        }
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
