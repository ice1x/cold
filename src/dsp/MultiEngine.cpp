#include "dsp/MultiEngine.h"

namespace cold::dsp
{

void MultiEngine::prepare(double hostSampleRate)
{
    for (auto& t : tracks)
        t.prepare(hostSampleRate);
}

Engine& MultiEngine::track(int index)
{
    return tracks[static_cast<size_t>(validTrack(index) ? index : 0)];
}

const Engine& MultiEngine::track(int index) const
{
    return tracks[static_cast<size_t>(validTrack(index) ? index : 0)];
}

void MultiEngine::noteOn(int trackIndex, int midiNote, float velocity, int rootNote)
{
    if (validTrack(trackIndex))
        tracks[static_cast<size_t>(trackIndex)].noteOn(midiNote, velocity, rootNote);
}

void MultiEngine::noteOff(int trackIndex, int midiNote)
{
    if (validTrack(trackIndex))
        tracks[static_cast<size_t>(trackIndex)].noteOff(midiNote);
}

int MultiEngine::trackForChannel(int midiChannel)
{
    // MIDI channels are 1-based; channel N maps to track N-1 for the first 8.
    const int track = midiChannel - 1;
    return (track >= 0 && track < kNumTracks) ? track : -1;
}

void MultiEngine::routeNoteOn(int midiChannel, int midiNote, float velocity, int rootNote)
{
    noteOn(trackForChannel(midiChannel), midiNote, velocity, rootNote);
}

void MultiEngine::routeNoteOff(int midiChannel, int midiNote)
{
    noteOff(trackForChannel(midiChannel), midiNote);
}

void MultiEngine::reset()
{
    for (auto& t : tracks)
        t.reset();
}

void MultiEngine::renderNextBlock(float* const* out, int numOutChannels, int startSample,
                                  int numSamples)
{
    for (auto& t : tracks)
        t.renderNextBlock(out, numOutChannels, startSample, numSamples);
}

int MultiEngine::activeVoiceCount() const
{
    int count = 0;
    for (const auto& t : tracks)
        count += t.activeVoiceCount();
    return count;
}

} // namespace cold::dsp
