#include "dsp/SampleVoice.h"

#include <algorithm>
#include <cmath>

namespace cold::dsp
{

double SampleVoice::noteToRatio(int midiNote, int rootNote)
{
    return std::pow(2.0, static_cast<double>(midiNote - rootNote) / 12.0);
}

void SampleVoice::prepare(double newHostSampleRate)
{
    if (newHostSampleRate > 0.0)
        hostSampleRate = newHostSampleRate;
}

void SampleVoice::setSample(const float* const* channels, int numChannels, int numFrames,
                            double newSourceSampleRate)
{
    sourceChannels = channels;
    sourceNumChannels = std::max(0, numChannels);
    sourceNumFrames = std::max(0, numFrames);
    sourceSampleRate = newSourceSampleRate > 0.0 ? newSourceSampleRate : hostSampleRate;
    reset();
}

void SampleVoice::clearSample()
{
    reset();
    sourceChannels = nullptr;
    sourceNumChannels = 0;
    sourceNumFrames = 0;
    sourceSampleRate = 0.0;
}

void SampleVoice::startNote(int newMidiNote, float velocity, int rootNote)
{
    if (sourceChannels == nullptr || sourceNumChannels == 0 || sourceNumFrames == 0)
    {
        active = false;
        return;
    }

    midiNote = newMidiNote;
    gain = std::clamp(velocity, 0.0f, 1.0f);
    position = 0.0;

    const double rateCorrection = sourceSampleRate / hostSampleRate;
    step = noteToRatio(newMidiNote, rootNote) * rateCorrection;

    active = true;
}

void SampleVoice::stopNote()
{
    active = false;
}

void SampleVoice::reset()
{
    active = false;
    midiNote = -1;
    position = 0.0;
    step = 1.0;
}

float SampleVoice::readInterpolated(int channel, double pos) const
{
    const int i0 = static_cast<int>(pos);
    const int i1 = std::min(i0 + 1, sourceNumFrames - 1);
    const auto frac = static_cast<float>(pos - static_cast<double>(i0));

    const float* data = sourceChannels[channel];
    return data[i0] * (1.0f - frac) + data[i1] * frac;
}

void SampleVoice::renderNextBlock(float* const* out, int numOutChannels, int startSample,
                                  int numSamples)
{
    if (!active)
        return;

    for (int n = 0; n < numSamples; ++n)
    {
        if (position >= static_cast<double>(sourceNumFrames))
        {
            active = false;
            break;
        }

        const int dest = startSample + n;
        for (int ch = 0; ch < numOutChannels; ++ch)
        {
            // Mono source fans out; otherwise map 1:1 and drop extra source channels.
            const int srcCh = sourceNumChannels == 1 ? 0 : ch;
            if (srcCh >= sourceNumChannels)
                continue;

            out[ch][dest] += gain * readInterpolated(srcCh, position);
        }

        position += step;
    }
}

} // namespace cold::dsp
