#pragma once

// Framework-agnostic (no JUCE) polyphonic playback engine for one sample.
//
// Owns a pool of SampleVoices plus the allocator that assigns a voice to each
// incoming note — so a single sample can play chords. This is the M1 single-track
// engine; the 8-track architecture (M5) composes one of these per track. Loop
// mode / points and the assigned sample are shared configuration applied to every
// voice; each note gets its own read position and pitch.

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

    // Assign the sample every voice reads from. Non-owning: the channel pointers
    // and their data must outlive playback. Resets the loop region to the whole
    // sample unless narrowed afterwards with setLoopPoints.
    void setSample(const float* const* channels, int numChannels, int numFrames,
                   double sourceSampleRate);

    void setLoopMode(SampleVoice::LoopMode mode);
    void setLoopPoints(int startFrame, int endFrame);

    // Amp ADSR shared by every voice (seconds; sustain in [0, 1]).
    void setAmpEnvelope(float attackSeconds, float decaySeconds, float sustainLevel,
                        float releaseSeconds);

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
    void applyConfigToVoice(int index);

    std::vector<SampleVoice> voices;
    std::vector<int> voiceNote;          // MIDI note each voice was triggered with (-1 idle)
    std::vector<std::uint64_t> voiceAge; // start order, for oldest-first stealing

    std::uint64_t noteCounter = 0;

    // Shared configuration applied to each voice.
    double hostSampleRate = 44100.0;
    const float* const* sourceChannels = nullptr;
    int sourceNumChannels = 0;
    int sourceNumFrames = 0;
    double sourceSampleRate = 0.0;
    SampleVoice::LoopMode loopMode = SampleVoice::LoopMode::OneShot;
    int loopStartFrame = 0;
    int loopEndFrame = 0; // 0 => leave voice default (whole sample)

    float ampAttack = 0.0f;
    float ampDecay = 0.0f;
    float ampSustain = 1.0f;
    float ampRelease = 0.0f;
};

} // namespace cold::dsp
