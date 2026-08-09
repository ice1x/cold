#pragma once

#include "dsp/Engine.h"
#include "io/SampleBank.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace cold
{

// M1 polyphonic sampler processor. Owns the framework-agnostic dsp::Engine and
// bridges the host to it: APVTS parameters (gain, polyphony, loop mode/points,
// amp ADSR, root note), sample-accurate MIDI -> note on/off, and file loading.
class ColdAudioProcessor : public juce::AudioProcessor
{
public:
    ColdAudioProcessor();
    ~ColdAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Cold"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Decode a file and hand it to the engine. Thread-safe against the audio
    // thread (guards the engine while the sample buffers are swapped). Returns
    // false if the file could not be decoded. Called from the UI / state restore.
    // Load a file into the currently selected slot and play it.
    bool loadSampleFile(const juce::File& file);

    // Load a file into a specific bank slot. If it is the selected slot, the
    // engine starts playing it. Returns false on decode failure / bad index.
    bool loadSampleToSlot(int slot, const juce::File& file);

    // Load every decodable audio file in a folder into consecutive slots from 0.
    // Returns how many slots were filled.
    int loadFolder(const juce::File& directory);

    // Make `slot` the active sample (fed to the engine). Clamped to the bank.
    void selectSlot(int slot);

    [[nodiscard]] int numSlots() const;
    [[nodiscard]] juce::String loadedSampleName() const; // current slot's name

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateEngineParameters();

    void pushSlotToEngine(); // feed the selected slot's windowed data to the engine

    dsp::Engine engine;
    io::SampleBank bank;

    // Channel-pointer view (into the selected slot's buffers, offset by its start
    // window) handed to the engine.
    std::vector<const float*> channelPtrs;
    int currentNumFrames = 0;
    int currentSlot = 0;

    // Cached parameter handles (avoid string lookups on the audio thread).
    std::atomic<float>* gainParam = nullptr;
    std::atomic<float>* polyphonyParam = nullptr;
    std::atomic<float>* loopModeParam = nullptr;
    std::atomic<float>* loopStartParam = nullptr;
    std::atomic<float>* loopEndParam = nullptr;
    std::atomic<float>* rootNoteParam = nullptr;
    std::atomic<float>* attackParam = nullptr;
    std::atomic<float>* decayParam = nullptr;
    std::atomic<float>* sustainParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* filterCutoffParam = nullptr;
    std::atomic<float>* filterResonanceParam = nullptr;
    std::atomic<float>* filterTypeParam = nullptr;
    std::atomic<float>* lfoShapeParam = nullptr;
    std::atomic<float>* lfoRateParam = nullptr;
    std::atomic<float>* lfoDepthParam = nullptr;
    std::atomic<float>* lfoSyncParam = nullptr;
    std::atomic<float>* lfoDivisionParam = nullptr;
    std::atomic<float>* lfoToCutoffParam = nullptr;
    std::atomic<float>* lfoToTypeParam = nullptr;
    std::atomic<float>* slotParam = nullptr;

    int lastSlot = -1;

    double hostBpm = 120.0; // last known host tempo (for LFO sync)

    int lastPolyphony = -1;

    // Guards the engine + sample buffers across the audio thread and loads.
    juce::CriticalSection engineLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ColdAudioProcessor)
};

} // namespace cold
