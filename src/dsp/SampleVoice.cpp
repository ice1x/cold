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
    env.setSampleRate(hostSampleRate);
}

void SampleVoice::setAmpEnvelope(float attackSeconds, float decaySeconds, float sustainLevel,
                                 float releaseSeconds)
{
    env.setParameters(attackSeconds, decaySeconds, sustainLevel, releaseSeconds);
}

void SampleVoice::setSample(const float* const* channels, int numChannels, int numFrames,
                            double newSourceSampleRate)
{
    sourceChannels = channels;
    sourceNumChannels = std::max(0, numChannels);
    sourceNumFrames = std::max(0, numFrames);
    sourceSampleRate = newSourceSampleRate > 0.0 ? newSourceSampleRate : hostSampleRate;
    // Default the loop region to the whole sample; the caller can narrow it.
    loopStartFrame = 0;
    loopEndFrame = sourceNumFrames;
    reset();
}

void SampleVoice::clearSample()
{
    reset();
    sourceChannels = nullptr;
    sourceNumChannels = 0;
    sourceNumFrames = 0;
    sourceSampleRate = 0.0;
    loopStartFrame = 0;
    loopEndFrame = 0;
}

void SampleVoice::setLoopMode(LoopMode newMode)
{
    mode = newMode;
}

void SampleVoice::setLoopPoints(int startFrame, int endFrame)
{
    const int start = std::clamp(startFrame, 0, sourceNumFrames);
    const int end = std::clamp(endFrame, 0, sourceNumFrames);
    if (end > start)
    {
        loopStartFrame = start;
        loopEndFrame = end;
    }
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
    direction = 1;

    const double rateCorrection = sourceSampleRate / hostSampleRate;
    step = noteToRatio(newMidiNote, rootNote) * rateCorrection;

    env.noteOn();
    active = true;
}

void SampleVoice::stopNote()
{
    // Enter the amp release; a zero-length release frees the voice at once.
    env.noteOff();
    if (!env.isActive())
        active = false;
}

void SampleVoice::reset()
{
    active = false;
    midiNote = -1;
    position = 0.0;
    step = 1.0;
    direction = 1;
    env.reset();
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

    const double loopLen = static_cast<double>(loopEndFrame - loopStartFrame);
    const bool looping = mode != LoopMode::OneShot && loopLen > 0.0;

    for (int n = 0; n < numSamples; ++n)
    {
        if (position >= static_cast<double>(sourceNumFrames) || position < 0.0)
        {
            active = false;
            break;
        }

        const float envGain = env.getNextSample();
        const float voiceGain = gain * envGain;

        const int dest = startSample + n;
        for (int ch = 0; ch < numOutChannels; ++ch)
        {
            // Mono source fans out; otherwise map 1:1 and drop extra source channels.
            const int srcCh = sourceNumChannels == 1 ? 0 : ch;
            if (srcCh >= sourceNumChannels)
                continue;

            out[ch][dest] += voiceGain * readInterpolated(srcCh, position);
        }

        // A finished amp release frees the voice (a transparent env never idles
        // on its own; it is ended by stopNote or by the one-shot running out).
        if (!env.isActive())
        {
            active = false;
            break;
        }

        position += static_cast<double>(direction) * step;

        if (!looping)
            continue;

        if (mode == LoopMode::Forward)
        {
            // Wrap forward; a while-loop covers steps larger than the loop length.
            while (position >= static_cast<double>(loopEndFrame))
                position -= loopLen;
        }
        else // PingPong: reflect off each boundary, flipping direction.
        {
            bool bounced = true;
            while (bounced)
            {
                bounced = false;
                if (direction > 0 && position > static_cast<double>(loopEndFrame))
                {
                    position = 2.0 * static_cast<double>(loopEndFrame) - position;
                    direction = -1;
                    bounced = true;
                }
                else if (direction < 0 && position < static_cast<double>(loopStartFrame))
                {
                    position = 2.0 * static_cast<double>(loopStartFrame) - position;
                    direction = 1;
                    bounced = true;
                }
            }
        }
    }
}

} // namespace cold::dsp
