#pragma once

// Framework-agnostic (no JUCE) polyphonic playback engine for one sample.
//
// Owns a pool of SampleVoices plus the allocator that assigns a voice to each
// incoming note — so a single sample can play chords. This is the M1 single-track
// engine; the 8-track architecture (M5) composes one of these per track. Loop
// mode / points and the assigned sample are shared configuration applied to every
// voice; each note gets its own read position and pitch.

#include "dsp/Lfo.h"
#include "dsp/ModMatrix.h"
#include "dsp/SampleVoice.h"

#include <cstdint>
#include <vector>

namespace cold::dsp
{

class Engine
{
public:
    Engine();

    // Host sample rate; propagated to every voice.
    void prepare(double hostSampleRate);

    // Resize the voice pool. Clamped to at least 1 (1 = Digitakt-style mono).
    void setPolyphony(int numVoices);

    // A non-owning reference to one decoded sample (channels/data must outlive
    // playback). The engine holds a list of these for sample-select modulation.
    struct SampleRef
    {
        const float* const* channels = nullptr;
        int numChannels = 0;
        int numFrames = 0;
        double sampleRate = 0.0;

        [[nodiscard]] bool valid() const
        {
            return channels != nullptr && numChannels > 0 && numFrames > 0;
        }
    };

    // Assign the single sample every voice reads from (the common case).
    void setSample(const float* const* channels, int numChannels, int numFrames,
                   double sourceSampleRate);

    // Assign a list of samples for sample-select modulation; note-on picks one by
    // index. Index 0 is the default; the LFO/mod offset shifts around baseIndex.
    void setSampleList(const std::vector<SampleRef>& sampleList);
    void setBaseSampleIndex(int index);

    [[nodiscard]] int sampleCount() const noexcept { return static_cast<int>(samples.size()); }
    [[nodiscard]] int currentSampleIndex() const noexcept { return currentIndex; }

    void setLoopMode(SampleVoice::LoopMode mode);
    void setLoopPoints(int startFrame, int endFrame);

    // Amp ADSR shared by every voice (seconds; sustain in [0, 1]), plus the extra
    // DAHDSR stages, per-segment curves and velocity sensitivity.
    void setAmpEnvelope(float attackSeconds, float decaySeconds, float sustainLevel,
                        float releaseSeconds);
    void setAmpEnvelopeDAH(float delaySeconds, float holdSeconds);
    void setAmpCurves(float attackCurve, float decayCurve, float releaseCurve);
    void setAmpVelocity(float sensitivity);

    // Second (modulation) envelope + its routing to the filter, shared by every
    // voice: env -> cutoff (octaves) and env -> morph type (offset).
    void setModEnvelope(float attackSeconds, float decaySeconds, float sustainLevel,
                        float releaseSeconds);
    void setModRouting(float toCutoffOctaves, float toTypeOffset);

    // Per-voice morph filter shared by every voice (cutoff Hz, resonance/Q, type
    // in [0, 1]). Enables the filter on the whole pool.
    void setFilter(float cutoffHz, float resonance, float type);

    // LFO modulation source shared by the pool (shape, rate Hz, depth in [0,1]).
    void setLfo(LfoShape shape, float rateHz, float depth);

    // How the LFO reacts to a note trig (Free/Trig/Hold/OneShot). The engine calls
    // the LFO's noteTrigger() on every note-on, so this selects the retrig behavior.
    void setLfoTrigMode(LfoTrigMode mode);

    // The LFO-designer waveform: `count` points in [-1, 1] over one cycle. Selects
    // the Custom shape (call after setLfo when the chosen shape is Custom).
    void setLfoCustomShape(const float* points, int count);

    // Modulation routes. Destinations include the filter morph type, so an LFO
    // can sweep the filter character (the distinctive M3 feature). Modulation is
    // applied block-rate to the (smoothed) per-voice filters when the filter is on.
    void addModRoute(ModDestination destination, float amount);
    void clearModRoutes();

    // Trigger / release notes. rootNote is the MIDI note of native pitch.
    void noteOn(int midiNote, float velocity = 1.0f, int rootNote = 60);
    void noteOff(int midiNote);

    // Free every voice immediately.
    void reset();

    // Add the whole pool's contribution into out[ch][startSample .. +numSamples).
    void renderNextBlock(float* const* out, int numOutChannels, int startSample, int numSamples);

    [[nodiscard]] int polyphony() const noexcept { return static_cast<int>(voices.size()); }
    [[nodiscard]] int activeVoiceCount() const;
    [[nodiscard]] bool isNotePlaying(int midiNote) const;

private:
    [[nodiscard]] int allocateVoice();
    void configureVoice(int voiceIndex, int sampleIndex);
    [[nodiscard]] int selectSampleIndex() const; // note-time index from the LFO/mod

    std::vector<SampleVoice> voices;
    std::vector<int> voiceNote;          // MIDI note each voice was triggered with (-1 idle)
    std::vector<std::uint64_t> voiceAge; // start order, for oldest-first stealing

    std::uint64_t noteCounter = 0;

    // Shared configuration applied to each voice.
    double hostSampleRate = 44100.0;
    std::vector<SampleRef> samples; // sample-select set; index 0 is the primary
    int baseSampleIndex = 0;
    int currentIndex = 0;
    float lfoToSampleSelect = 0.0f; // SampleSelect route amount, cached for note-time
    SampleVoice::LoopMode loopMode = SampleVoice::LoopMode::OneShot;
    int loopStartFrame = 0;
    int loopEndFrame = 0; // 0 => leave voice default (whole sample)

    float ampAttack = 0.0f;
    float ampDecay = 0.0f;
    float ampSustain = 1.0f;
    float ampRelease = 0.0f;
    float ampDelay = 0.0f;
    float ampHold = 0.0f;
    float ampAttackCurve = 0.0f;
    float ampDecayCurve = 0.0f;
    float ampReleaseCurve = 0.0f;
    float ampVelocity = 0.0f;

    float modAttack = 0.0f;
    float modDecay = 0.0f;
    float modSustain = 1.0f;
    float modRelease = 0.0f;
    float modToCutoff = 0.0f;
    float modToType = 0.0f;

    bool filterEnabled = false;
    float filterCutoff = 20000.0f;
    float filterResonance = 0.707f;
    float filterType = 0.0f;

    Lfo lfo;
    ModMatrix modMatrix;
};

} // namespace cold::dsp
