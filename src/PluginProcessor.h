#pragma once

#include "dsp/Engine.h"
#include "io/SampleLoader.h"

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
    bool loadSampleFile(const juce::File& file);

    [[nodiscard]] juce::String loadedSampleName() const;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateEngineParameters();

    dsp::Engine engine;
    io::SampleLoader loader;

    // Owned decoded sample + the channel-pointer view handed to the engine.
    io::SampleData sampleData;
    std::vector<const float*> channelPtrs;
    int currentNumFrames = 0;
    juce::String sampleFilePath;

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

    int lastPolyphony = -1;

    // Guards the engine + sample buffers across the audio thread and loads.
    juce::CriticalSection engineLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ColdAudioProcessor)
};

} // namespace cold
