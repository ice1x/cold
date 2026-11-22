#pragma once

// Framework-agnostic (no JUCE) multi-track engine (M5).
//
// Owns eight independent per-track voice chains (each an Engine: sample + amp env
// + morph filter + LFO + mod matrix + polyphonic voice pool) and routes incoming
// notes to a track. This is Cold's multi-track layout — but each track
// is polyphonic (Cold's deliberate deviation). MIDI channel N drives track N-1.

#include "dsp/Engine.h"

#include <array>
#include <vector>

namespace cold::dsp
{

class MultiEngine
{
public:
    static constexpr int kNumTracks = 16;

    void prepare(double hostSampleRate);

    [[nodiscard]] int numTracks() const noexcept { return kNumTracks; }
    [[nodiscard]] Engine& track(int index);
    [[nodiscard]] const Engine& track(int index) const;

    // Per-track note control (index 0..7).
    void noteOn(int trackIndex, int midiNote, float velocity = 1.0f, int rootNote = 60);
    void noteOff(int trackIndex, int midiNote);

    // Release every active voice on a track (choke / mute groups). No-op for a bad
    // index.
    void stopAllNotes(int trackIndex);

    // Channel routing: MIDI channel (1..16) -> track (0..7); returns -1 for
    // channels outside the eight tracks.
    [[nodiscard]] static int trackForChannel(int midiChannel);
    void routeNoteOn(int midiChannel, int midiNote, float velocity = 1.0f, int rootNote = 60);
    void routeNoteOff(int midiChannel, int midiNote);

    void reset();

    // Global pitch-bend in semitones (MIDI pitch wheel) applied to every track, so a
    // live bend moves all sounding and future voices.
    void setPitchBend(float semitones);

    // Global mod-wheel -> filter cutoff offset in octaves applied to every track.
    void setWheelCutoff(float octaves);

    // Number of shared FX send buses (0 = delay, 1 = reverb).
    static constexpr int kNumSends = 2;

    // Per-track amount into FX send `sendIndex` (0..1). 0 = dry only.
    void setSendGain(int trackIndex, int sendIndex, float gain);

    // Sum every track's contribution into the output buffer.
    void renderNextBlock(float* const* out, int numOutChannels, int startSample, int numSamples);

    // As above, but also sum each track's contribution — scaled by its send gains —
    // into the two send buses (same channel count / indexing as `out`). Either send
    // may be null to skip it. Used for the delay + reverb sends.
    void renderNextBlock(float* const* out, float* const* send0, float* const* send1,
                         int numOutChannels, int startSample, int numSamples);

    [[nodiscard]] int activeVoiceCount() const;

private:
    [[nodiscard]] bool validTrack(int index) const noexcept
    {
        return index >= 0 && index < kNumTracks;
    }

    std::array<Engine, kNumTracks> tracks;
    // sendGain[send][track]: per-track amount into each FX send bus (0..1).
    std::array<std::array<float, kNumTracks>, kNumSends> sendGain{};

    // Reused scratch for the send-bus render path (one track's isolated output).
    std::vector<std::vector<float>> trackScratch;
    std::vector<float*> scratchPtrs;
};

} // namespace cold::dsp
