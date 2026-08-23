#pragma once

// Framework-agnostic (no JUCE) multi-track engine (M5).
//
// Owns eight independent per-track voice chains (each an Engine: sample + amp env
// + morph filter + LFO + mod matrix + polyphonic voice pool) and routes incoming
// notes to a track. This is Cold's Digitakt-style 8-track layout — but each track
// is polyphonic (Cold's deliberate deviation). MIDI channel N drives track N-1.

#include "dsp/Engine.h"

#include <array>

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

    // Digitakt-style routing: MIDI channel (1..16) -> track (0..7); returns -1 for
    // channels outside the eight tracks.
    [[nodiscard]] static int trackForChannel(int midiChannel);
    void routeNoteOn(int midiChannel, int midiNote, float velocity = 1.0f, int rootNote = 60);
    void routeNoteOff(int midiChannel, int midiNote);

    void reset();

    // Sum every track's contribution into the output buffer.
    void renderNextBlock(float* const* out, int numOutChannels, int startSample, int numSamples);

    [[nodiscard]] int activeVoiceCount() const;

private:
    [[nodiscard]] bool validTrack(int index) const noexcept
    {
        return index >= 0 && index < kNumTracks;
    }

    std::array<Engine, kNumTracks> tracks;
};

} // namespace cold::dsp
