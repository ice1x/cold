#pragma once

#include "dsp/MultiEngine.h"
#include "io/SampleBank.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
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
    // Load a file into the selected track's bank slot and play it on that track.
    bool loadSampleFile(const juce::File& file);

    // Load a file into bank slot `slot`. Bank slot t feeds track t (t < 8), so the
    // matching track starts playing it. Returns false on decode failure / bad index.
    bool loadSampleToSlot(int slot, const juce::File& file);

    // Load every decodable audio file in a folder into consecutive slots from 0,
    // so a folder of N samples fills tracks 0..N-1. Returns how many were loaded.
    int loadFolder(const juce::File& directory);

    // Focus a track for editing / the Load buttons (0..7). Clamped.
    void selectTrack(int track);
    [[nodiscard]] int selectedTrack() const;

    [[nodiscard]] int numSlots() const;
    [[nodiscard]] juce::String loadedSampleName() const; // selected track's sample name

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateEngineParameters();

    // Feed bank slot `track` (windowed) to track `track`'s engine.
    void pushTrackSample(int track);
    void applySharedParamsToTrack(int track, bool polyChanged);

    dsp::MultiEngine multi;
    io::SampleBank bank;

    // Per-track channel-pointer views (into each track's slot buffers, offset by
    // the slot start window) handed to the engines.
    std::array<std::vector<const float*>, dsp::MultiEngine::kNumTracks> trackPtrs;
    std::array<int, dsp::MultiEngine::kNumTracks> trackNumFrames{};

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
    std::atomic<float>* trackParam = nullptr;

    double hostBpm = 120.0; // last known host tempo (for LFO sync)

    int lastPolyphony = -1;

    // Guards the engine + sample buffers across the audio thread and loads.
    juce::CriticalSection engineLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ColdAudioProcessor)
};

} // namespace cold
