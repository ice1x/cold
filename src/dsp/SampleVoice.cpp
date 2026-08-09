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
    ampEnv.setSampleRate(hostSampleRate);
    modEnv.setSampleRate(hostSampleRate);
    for (auto& f : filters)
        f.prepare(hostSampleRate);
}

void SampleVoice::setAmpEnvelope(float attackSeconds, float decaySeconds, float sustainLevel,
                                 float releaseSeconds)
{
    // Map the simple ADSR onto the DAHDSR envelope (no delay/hold, linear).
    ampEnv.setParameters(0.0f, attackSeconds, 0.0f, decaySeconds, sustainLevel, releaseSeconds);
}

void SampleVoice::setModEnvelope(float attackSeconds, float decaySeconds, float sustainLevel,
                                 float releaseSeconds)
{
    modEnv.setParameters(0.0f, attackSeconds, 0.0f, decaySeconds, sustainLevel, releaseSeconds);
}

void SampleVoice::setModRouting(float toCutoffOctaves, float toTypeOffset)
{
    modToCutoff = toCutoffOctaves;
    modToType = toTypeOffset;
    useModEnv = std::abs(modToCutoff) > 1.0e-6f || std::abs(modToType) > 1.0e-6f;
}

void SampleVoice::setFilter(float cutoffHz, float resonance, float type)
{
    baseCutoff = cutoffHz;
    baseType = type;
    for (auto& f : filters)
    {
        f.setCutoff(cutoffHz);
        f.setResonance(resonance);
        f.setFilterType(type);
    }
    useFilter = true;
}

void SampleVoice::setFilterEnabled(bool enabled)
{
    useFilter = enabled;
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

    ampEnv.noteOn(gain);
    if (useModEnv)
        modEnv.noteOn(gain);
    // Clear filter state so a new note doesn't inherit the previous one's ring.
    for (auto& f : filters)
        f.reset();
    active = true;
}

void SampleVoice::stopNote()
{
    // Enter the amp release; a zero-length release frees the voice at once.
    ampEnv.noteOff();
    modEnv.noteOff();
    if (!ampEnv.isActive())
        active = false;
}

void SampleVoice::reset()
{
    active = false;
    midiNote = -1;
    position = 0.0;
    step = 1.0;
    direction = 1;
    ampEnv.reset();
    modEnv.reset();
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

        const float envGain = ampEnv.getNextSample();
        const float voiceGain = gain * envGain;

        // Modulation envelope steers this voice's filter (env -> cutoff / type).
        if (useFilter && useModEnv)
        {
            const float m = modEnv.getNextSample();
            const float modCutoff = baseCutoff * std::pow(2.0f, modToCutoff * m);
            const float modType = std::clamp(baseType + modToType * m, 0.0f, 1.0f);
            for (auto& f : filters)
            {
                f.setCutoff(modCutoff);
                f.setFilterType(modType);
            }
        }

        const int dest = startSample + n;
        for (int ch = 0; ch < numOutChannels; ++ch)
        {
            // Mono source fans out; otherwise map 1:1 and drop extra source channels.
            const int srcCh = sourceNumChannels == 1 ? 0 : ch;
            if (srcCh >= sourceNumChannels)
                continue;

            float value = voiceGain * readInterpolated(srcCh, position);
            if (useFilter && ch < static_cast<int>(filters.size()))
                value = filters[static_cast<size_t>(ch)].process(value);

            out[ch][dest] += value;
        }

        // A finished amp release frees the voice (a transparent env never idles
        // on its own; it is ended by stopNote or by the one-shot running out).
        if (!ampEnv.isActive())
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
