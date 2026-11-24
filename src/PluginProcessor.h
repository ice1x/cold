#pragma once

#include "dsp/Compressor.h"
#include "dsp/Delay.h"
#include "dsp/Limiter.h"
#include "dsp/MultiEngine.h"
#include "dsp/Reverb.h"
#include "dsp/StereoWidth.h"
#include "dsp/TiltEq.h"
#include "dsp/Waveform.h"
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
    // Load a file into the selected cell's primary sample (its layer 0).
    bool loadSampleFile(const juce::File& file);

    // Load a file into a specific cell layer (track 0..7, layer 0..kNumLayers-1) or
    // the cell's primary sample (layer 0). Each cell owns its layers, so the sample
    // plays on that cell only. Returns false on decode failure / bad index.
    bool loadSampleToLayer(int track, int layer, const juce::File& file);
    bool loadSampleToTrack(int track, const juce::File& file);

    // Load a file into raw bank slot `slot` (internal store: slot = track*kNumLayers
    // + layer). Prefer loadSampleToLayer / loadSampleToTrack. Returns false on
    // decode failure / bad index.
    bool loadSampleToSlot(int slot, const juce::File& file);

    // Load up to 8 decodable audio files from a folder into cells 0..7 (each as the
    // cell's primary sample). Returns how many were loaded.
    int loadFolder(const juce::File& directory);

    // The sample pool: up to kPoolSize samples shared by every cell. A cell whose
    // Source is Pool plays pool slot "Pool Slot", and LFO -> Sample steps through
    // the pool from there (depth = number of slots). loadPoolFolder replaces the
    // pool with the folder's audio files (sorted by name, not recursive) and returns
    // how many were loaded; files past kPoolSize are ignored.
    static constexpr int kPoolSize = 127;
    int loadPoolFolder(const juce::File& directory);
    void clearPool();
    [[nodiscard]] int poolSize() const;
    [[nodiscard]] juce::String poolSampleName(int index) const; // "" past the end

    // Import an NI Battery 3 ".kit": load its samples (resolved by filename under
    // `sampleFolder`, recursively) into the bank and apply each cell's settings to
    // the matching track — amp AHDSR (per track), tune/pan/volume (layer 0), and a
    // Note Map so the cells' root keys play them. Only the first 8 cells fit the 8
    // tracks. Returns how many cells were applied (0 if the kit could not be read).
    int importBatteryKit(const juce::File& kitFile, const juce::File& sampleFolder);

    // Focus a track for editing / the Load buttons (0..7). Clamped.
    void selectTrack(int track);
    [[nodiscard]] int selectedTrack() const;

    // Audition a cell from the editor (pad click): trigger the cell's sound at the
    // root note, like a MIDI hit, so clicking a pad plays it. `releaseCell` releases
    // only sustaining (looping) cells — one-shot drums play through so a quick click
    // still sounds the whole hit. No-op for a bad index.
    void auditionCell(int track);
    void releaseCell(int track);

    [[nodiscard]] int numSlots() const;
    [[nodiscard]] juce::String loadedSampleName() const;       // selected track's sample name
    [[nodiscard]] juce::String slotSampleName(int slot) const; // "" for an empty slot

    // Waveform view of slot `slot` (I5c): a `numBins`-column min/max envelope of
    // the WHOLE decoded sample (not just the playable window). Empty if the slot
    // is unloaded or the request is degenerate. The editor draws this behind the
    // start/end markers, which come from the window accessors below.
    [[nodiscard]] dsp::WaveformPeaks slotWaveform(int slot, int numBins) const;

    // Slot geometry for the waveform markers: total decoded frames, and the
    // playable window [start, start+length) clamped to the sample. 0 for an
    // unloaded slot. Fractions (start/total, length/total) place the markers.
    [[nodiscard]] int slotTotalFrames(int slot) const;
    [[nodiscard]] int slotWindowStart(int slot) const;
    [[nodiscard]] int slotWindowLength(int slot) const;
    // Native sample rate of the loaded slot (0 if unloaded). Lets the editor map the
    // amp envelope's time onto the waveform, scaled by the layer's tune.
    [[nodiscard]] double slotSampleRate(int slot) const;

    // Set a slot's playable window (I5e): first frame / length in frames (0 length
    // = to the end). Clamped to the sample; rebuilds the tracks' sample views under
    // the engine lock so playback follows immediately. No-op on a bad/empty slot.
    // The editor calls these when the waveform start/end markers are dragged.
    void setSlotStartFrames(int slot, int startFrames);
    void setSlotLengthFrames(int slot, int lengthFrames);

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
    dsp::Delay delay;             // shared send/return delay fed by the per-cell delay sends
    dsp::Reverb reverb;           // shared send/return reverb fed by the per-cell reverb sends
    dsp::TiltEq tiltEq;           // master tilt tone control on the summed mix (pre comp)
    dsp::Compressor masterComp;   // master glue compressor on the summed mix (pre gain)
    dsp::StereoWidth stereoWidth; // master stereo-width (mid/side) on the output (pre limiter)
    dsp::Limiter limiter;         // master brickwall limiter on the final output (post gain)
    io::SampleBank bank;
    std::unique_ptr<io::SampleBank> pool;            // the shared sample pool
    std::vector<std::vector<const float*>> poolPtrs; // channel views into the pool
    // Replace the pool with these files (decoded outside the engine lock).
    int setPoolFiles(const juce::Array<juce::File>& files);

    // Send buses (per block) feeding the delay + reverb, and their pointer views.
    std::vector<std::vector<float>> delayBuffer;
    std::vector<float*> delayPtrs;
    std::vector<std::vector<float>> reverbBuffer;
    std::vector<float*> reverbPtrs;

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
    std::atomic<float>* pitchBendRangeParam = nullptr;
    std::atomic<float>* modWheelToCutoffParam = nullptr;
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
    std::atomic<float>* delayTimeParam = nullptr;
    std::atomic<float>* delayFeedbackParam = nullptr;
    std::atomic<float>* delayToneParam = nullptr;
    std::atomic<float>* delayReturnParam = nullptr;
    std::atomic<float>* delaySyncParam = nullptr;
    std::atomic<float>* delayDivisionParam = nullptr;
    std::atomic<float>* reverbRoomParam = nullptr;
    std::atomic<float>* reverbDampParam = nullptr;
    std::atomic<float>* reverbReturnParam = nullptr;
    std::atomic<float>* masterTiltOnParam = nullptr;
    std::atomic<float>* masterTiltParam = nullptr;
    std::atomic<float>* masterTiltPivotParam = nullptr;
    std::atomic<float>* masterCompOnParam = nullptr;
    std::atomic<float>* masterCompThresholdParam = nullptr;
    std::atomic<float>* masterCompRatioParam = nullptr;
    std::atomic<float>* masterCompAttackParam = nullptr;
    std::atomic<float>* masterCompReleaseParam = nullptr;
    std::atomic<float>* masterCompMakeupParam = nullptr;
    std::atomic<float>* masterWidthOnParam = nullptr;
    std::atomic<float>* masterWidthParam = nullptr;
    std::atomic<float>* limiterOnParam = nullptr;
    std::atomic<float>* limiterCeilingParam = nullptr;
    std::atomic<float>* limiterReleaseParam = nullptr;

    // Multilayer one-shot stacks: per-track layer mode + up to kNumLayers layers
    // (kNumLayers declared public above), each a bank slot (-1 = off) with
    // per-layer gain and tune.
    std::array<std::atomic<float>*, dsp::MultiEngine::kNumTracks> layerModeParam{};

    // Per-cell choke / mute group (0 = off, 1..8). Cells sharing a group cut each
    // other off on trig.
    std::array<std::atomic<float>*, dsp::MultiEngine::kNumTracks> chokeGroupParam{};

    // Per-cell output level (dB) + pan (balance) into the mix.
    std::array<std::atomic<float>*, dsp::MultiEngine::kNumTracks> levelParam{};
    std::array<std::atomic<float>*, dsp::MultiEngine::kNumTracks> panParam{};

    // Per-cell send amounts into the shared delay + reverb buses.
    std::array<std::atomic<float>*, dsp::MultiEngine::kNumTracks> delaySendParam{};
    std::array<std::atomic<float>*, dsp::MultiEngine::kNumTracks> reverbSendParam{};

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

    // Per-track morph filter.
    TrackFloats filterCutoffParam{};
    TrackFloats filterResonanceParam{};
    TrackFloats filterTypeParam{};

    // Per-track pitch envelope (drum blip).
    TrackFloats pitchEnvAmtParam{};
    TrackFloats pitchEnvDecayParam{};
    TrackFloats velToPitchParam{};
    TrackFloats velToCutoffParam{};
    // Each cell's two LFOs (wave, speed, multiplier, fade, destination, phase / slew,
    // trig mode, depth), cached per cell.
    struct CellLfoParams
    {
        TrackFloats wave{}, speed{}, mult{}, fade{}, dest{}, phase{}, mode{}, depth{};
    };
    std::array<CellLfoParams, 2> cellLfoParams{};
    TrackFloats sourceParam{};   // 0 = the cell's layers, 1 = the sample pool
    TrackFloats poolSlotParam{}; // the pool slot a Pool cell plays (1-based)
    TrackFloats humanizePitchParam{};
    TrackFloats humanizeLevelParam{};
    TrackFloats mapNoteParam{}; // per-cell Note Map key (MIDI note)
    TrackFloats compOnParam{};
    TrackFloats compThreshParam{};
    TrackFloats compRatioParam{};
    TrackFloats compAttackParam{};
    TrackFloats compReleaseParam{};
    TrackFloats compMakeupParam{};
    TrackFloats satOnParam{};
    TrackFloats satDriveParam{};
    TrackFloats satMixParam{};
    TrackFloats crushOnParam{};
    TrackFloats crushBitsParam{};
    TrackFloats crushRateParam{};

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
    // Optional per-layer amp envelope (core of layering).
    using LayerFloats =
        std::array<std::array<std::atomic<float>*, kNumLayers>, dsp::MultiEngine::kNumTracks>;
    LayerFloats layerAmpOnParam{};
    LayerFloats layerAmpDelayParam{};
    LayerFloats layerAmpAttackParam{};
    LayerFloats layerAmpHoldParam{};
    LayerFloats layerAmpDecayParam{};
    LayerFloats layerAmpSustainParam{};
    LayerFloats layerAmpReleaseParam{};
    LayerFloats layerMuteParam{};
    LayerFloats layerSoloParam{};
    LayerFloats layerReverseParam{};
    LayerFloats layerHpOnParam{};
    LayerFloats layerHpCutParam{};
    LayerFloats layerLpOnParam{};
    LayerFloats layerLpCutParam{};
    std::vector<dsp::Engine::Layer> layerScratch; // reused in applyParamsToTrack (avoids re-alloc)

    bool sendsWereActive = false; // FX path used last block (to clear tails once on exit)

    double hostBpm = 120.0; // last known host tempo (for LFO sync)

    int lastPolyphony = -1;

    // Guards the engine + sample buffers across the audio thread and loads.
    juce::CriticalSection engineLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ColdAudioProcessor)
};

} // namespace cold
