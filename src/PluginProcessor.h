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

    // Import an NI Battery 3 ".kit": load its samples (resolved by filename under
    // `sampleFolder`, recursively) into the bank and apply each cell's settings to
    // the matching track — amp AHDSR (per track), tune/pan/volume (layer 0), and a
    // Note Map so the cells' root keys play them. Only the first 8 cells fit the 8
    // tracks. Returns how many cells were applied (0 if the kit could not be read).
    int importBatteryKit(const juce::File& kitFile, const juce::File& sampleFolder);

    // Focus a track for editing / the Load buttons (0..7). Clamped.
    void selectTrack(int track);
    [[nodiscard]] int selectedTrack() const;

    [[nodiscard]] int numSlots() const;
    [[nodiscard]] juce::String loadedSampleName() const;       // selected track's sample name
    [[nodiscard]] juce::String slotSampleName(int slot) const; // "" for an empty slot

    // Max layers per track (Stack mode). Public so the editor can lay out the rows.
    static constexpr int kNumLayers = 4;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void updateEngineParameters();

    // Rebuild every track's sample-select list from the whole bank (layering):
    // track t defaults to slot t, but an LFO/mod -> sample-select can roam.
    void rebuildTrackSamples();
    void applySharedParamsToTrack(int track, bool polyChanged);

    dsp::MultiEngine multi;
    io::SampleBank bank;

    // Per-bank-slot channel-pointer views (into each slot's buffer, offset by the
    // slot start window). Shared by every track's sample list, so they must outlive
    // playback. Sized to bank.numSlots() at construction.
    std::vector<std::vector<const float*>> slotPtrs;
    std::vector<int> slotNumFrames;

    // Cached parameter handles (avoid string lookups on the audio thread).
    std::atomic<float>* gainParam = nullptr;
    std::atomic<float>* polyphonyParam = nullptr;
    std::atomic<float>* loopModeParam = nullptr;
    std::atomic<float>* loopStartParam = nullptr;
    std::atomic<float>* loopEndParam = nullptr;
    std::atomic<float>* rootNoteParam = nullptr;
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
    std::atomic<float>* lfoToPitchParam = nullptr;
    std::atomic<float>* lfoToSampleParam = nullptr;
    std::atomic<float>* lfoTrigModeParam = nullptr;
    // The LFO designer: kNumCustomSteps step levels forming the Custom waveform.
    static constexpr int kNumCustomSteps = 8;
    std::array<std::atomic<float>*, kNumCustomSteps> lfoStepParams{};
    std::atomic<float>* trackParam = nullptr;
    std::atomic<float>* modAttackParam = nullptr;
    std::atomic<float>* modDecayParam = nullptr;
    std::atomic<float>* modSustainParam = nullptr;
    std::atomic<float>* modReleaseParam = nullptr;
    std::atomic<float>* modToCutoffParam = nullptr;
    std::atomic<float>* modToTypeParam = nullptr;
    std::atomic<float>* midiAddressParam = nullptr; // 0 = channel->track, 1 = note map
    std::atomic<float>* noteMapBaseParam = nullptr; // note that maps to track 0

    // Multilayer one-shot stacks: per-track layer mode + up to kNumLayers layers
    // (kNumLayers declared public above), each a bank slot (-1 = off) with
    // per-layer gain and tune.
    std::array<std::atomic<float>*, dsp::MultiEngine::kNumTracks> layerModeParam{};

    // Per-track amp envelope (each track/cell is an independent sound).
    using TrackFloats = std::array<std::atomic<float>*, dsp::MultiEngine::kNumTracks>;
    TrackFloats attackParam{};
    TrackFloats decayParam{};
    TrackFloats sustainParam{};
    TrackFloats releaseParam{};
    TrackFloats ampDelayParam{};
    TrackFloats ampHoldParam{};
    TrackFloats ampAtkCurveParam{};
    TrackFloats ampDecCurveParam{};
    TrackFloats ampRelCurveParam{};
    TrackFloats ampVelParam{};

    std::array<std::array<std::atomic<float>*, kNumLayers>, dsp::MultiEngine::kNumTracks>
        layerSlotParam{};
    std::array<std::array<std::atomic<float>*, kNumLayers>, dsp::MultiEngine::kNumTracks>
        layerGainParam{};
    std::array<std::array<std::atomic<float>*, kNumLayers>, dsp::MultiEngine::kNumTracks>
        layerTuneParam{};
    std::array<std::array<std::atomic<float>*, kNumLayers>, dsp::MultiEngine::kNumTracks>
        layerPanParam{};
    std::array<std::array<std::atomic<float>*, kNumLayers>, dsp::MultiEngine::kNumTracks>
        layerFilterOnParam{};
    std::array<std::array<std::atomic<float>*, kNumLayers>, dsp::MultiEngine::kNumTracks>
        layerCutoffParam{};
    std::array<std::array<std::atomic<float>*, kNumLayers>, dsp::MultiEngine::kNumTracks>
        layerTypeParam{};
    std::vector<dsp::Engine::Layer> layerScratch; // reused in applyParamsToTrack (avoids re-alloc)

    double hostBpm = 120.0; // last known host tempo (for LFO sync)

    int lastPolyphony = -1;

    // Guards the engine + sample buffers across the audio thread and loads.
    juce::CriticalSection engineLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ColdAudioProcessor)
};

} // namespace cold
