#include "dsp/MultiEngine.h"

#include <algorithm>

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

void MultiEngine::stopAllNotes(int trackIndex)
{
    if (validTrack(trackIndex))
        tracks[static_cast<size_t>(trackIndex)].stopAllNotes();
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

void MultiEngine::setPitchBend(float semitones)
{
    for (auto& t : tracks)
        t.setPitchBend(semitones);
}

void MultiEngine::setWheelCutoff(float octaves)
{
    for (auto& t : tracks)
        t.setWheelCutoff(octaves);
}

void MultiEngine::setSendGain(int trackIndex, int sendIndex, float gain)
{
    if (validTrack(trackIndex) && sendIndex >= 0 && sendIndex < kNumSends)
        sendGain[static_cast<size_t>(sendIndex)][static_cast<size_t>(trackIndex)] = gain;
}

void MultiEngine::renderNextBlock(float* const* out, int numOutChannels, int startSample,
                                  int numSamples)
{
    for (auto& t : tracks)
        t.renderNextBlock(out, numOutChannels, startSample, numSamples);
}

void MultiEngine::renderNextBlock(float* const* out, float* const* send0, float* const* send1,
                                  int numOutChannels, int startSample, int numSamples)
{
    if (send0 == nullptr && send1 == nullptr)
    {
        renderNextBlock(out, numOutChannels, startSample, numSamples);
        return;
    }

    // Render each track into an isolated scratch, then add it to the main out and,
    // scaled by that track's send gains, to each send bus.
    const auto ch = static_cast<size_t>(numOutChannels);
    if (trackScratch.size() < ch)
        trackScratch.resize(ch);
    scratchPtrs.clear();
    for (int c = 0; c < numOutChannels; ++c)
    {
        auto& row = trackScratch[static_cast<size_t>(c)];
        if (static_cast<int>(row.size()) < numSamples)
            row.resize(static_cast<size_t>(numSamples));
        scratchPtrs.push_back(row.data());
    }

    for (int ti = 0; ti < kNumTracks; ++ti)
    {
        for (int c = 0; c < numOutChannels; ++c)
            std::fill_n(scratchPtrs[static_cast<size_t>(c)], numSamples, 0.0f);

        tracks[static_cast<size_t>(ti)].renderNextBlock(scratchPtrs.data(), numOutChannels, 0,
                                                        numSamples);

        const float g0 = sendGain[0][static_cast<size_t>(ti)];
        const float g1 = sendGain[1][static_cast<size_t>(ti)];
        for (int c = 0; c < numOutChannels; ++c)
        {
            const float* src = scratchPtrs[static_cast<size_t>(c)];
            float* dst = out[c];
            for (int n = 0; n < numSamples; ++n)
            {
                const float v = src[n];
                dst[startSample + n] += v;
                if (send0 != nullptr)
                    send0[c][startSample + n] += v * g0;
                if (send1 != nullptr)
                    send1[c][startSample + n] += v * g1;
            }
        }
    }
}

int MultiEngine::activeVoiceCount() const
{
    int count = 0;
    for (const auto& t : tracks)
        count += t.activeVoiceCount();
    return count;
}

} // namespace cold::dsp
