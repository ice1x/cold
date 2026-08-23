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
    pitchEnv.setSampleRate(hostSampleRate);
    for (auto& f : filters)
        f.prepare(hostSampleRate);
}

void SampleVoice::setAmpEnvelope(float attackSeconds, float decaySeconds, float sustainLevel,
                                 float releaseSeconds)
{
    ampAttack = attackSeconds;
    ampDecay = decaySeconds;
    ampSustain = sustainLevel;
    ampRelease = releaseSeconds;
    ampEnv.setParameters(ampDelay, ampAttack, ampHold, ampDecay, ampSustain, ampRelease);
}

void SampleVoice::setAmpEnvelopeDAH(float delaySeconds, float holdSeconds)
{
    ampDelay = delaySeconds;
    ampHold = holdSeconds;
    ampEnv.setParameters(ampDelay, ampAttack, ampHold, ampDecay, ampSustain, ampRelease);
}

void SampleVoice::setAmpCurves(float attackCurve, float decayCurve, float releaseCurve)
{
    ampEnv.setCurves(attackCurve, decayCurve, releaseCurve);
}

void SampleVoice::setAmpVelocity(float sensitivity)
{
    ampEnv.setVelocitySensitivity(sensitivity);
}

void SampleVoice::setModEnvelope(float attackSeconds, float decaySeconds, float sustainLevel,
                                 float releaseSeconds)
{
    modEnv.setParameters(0.0f, attackSeconds, 0.0f, decaySeconds, sustainLevel, releaseSeconds);
}

void SampleVoice::setPitchEnvelope(float amountSemitones, float decaySeconds)
{
    pitchEnvAmount = amountSemitones;
    usePitchEnv = std::abs(amountSemitones) > 1.0e-4f;
    // Instant attack to full, then decay to zero over decaySeconds and stay there:
    // a one-shot decay envelope the amount rides on.
    pitchEnv.setParameters(0.0f, 0.0f, 0.0f, std::max(0.0f, decaySeconds), 0.0f, 0.0f);
}

void SampleVoice::setModRouting(float toCutoffOctaves, float toTypeOffset)
{
    modToCutoff = toCutoffOctaves;
    modToType = toTypeOffset;
    useModEnv = std::abs(modToCutoff) > 1.0e-6f || std::abs(modToType) > 1.0e-6f;
}

void SampleVoice::setPitchModSemitones(float semitones)
{
    pitchMod = semitones;
    updateStep();
}

void SampleVoice::setTuneSemitones(float semitones)
{
    tuneMod = semitones;
    updateStep();
}

void SampleVoice::setVelToPitch(float semitones)
{
    // Amount only; the actual per-note offset is captured from velocity at note-on.
    velToPitchAmount = semitones;
}

void SampleVoice::setPan(float pan)
{
    // Linear balance law: centre leaves both channels at unity; panning attenuates
    // only the opposite channel, reaching silence at a hard side.
    const float p = std::clamp(pan, -1.0f, 1.0f);
    panLeft = p > 0.0f ? 1.0f - p : 1.0f;
    panRight = p < 0.0f ? 1.0f + p : 1.0f;
}

void SampleVoice::updateStep()
{
    // Both the block-rate pitch mod and the static per-note tune ride on the note's
    // captured base step (magnitude), summed in semitones.
    const double semis = static_cast<double>(pitchMod) + static_cast<double>(tuneMod) +
                         static_cast<double>(velPitchMod);
    step = baseStep * std::pow(2.0, semis / 12.0);
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

void SampleVoice::setFilterLfoMod(float cutoffOctaves, float typeOffset)
{
    lfoCutoffOct = cutoffOctaves;
    lfoTypeOff = typeOffset;
    hasLfoFilterMod = std::abs(cutoffOctaves) > 1.0e-6f || std::abs(typeOffset) > 1.0e-6f;
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

void SampleVoice::setReversed(bool shouldReverse)
{
    playReversed = shouldReverse;
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
    // Reverse starts at the last frame of the playable region and reads downward; a
    // one-shot then frees itself when the position falls below zero.
    position = playReversed ? static_cast<double>(sourceNumFrames - 1) : 0.0;
    direction = playReversed ? -1 : 1;

    // Velocity -> pitch: harder hits ride higher, captured once for this note.
    velPitchMod = velToPitchAmount * gain;

    const double rateCorrection = sourceSampleRate / hostSampleRate;
    baseStep = noteToRatio(newMidiNote, rootNote) * rateCorrection;
    updateStep();

    ampEnv.noteOn(gain);
    if (useModEnv)
        modEnv.noteOn(gain);
    if (usePitchEnv)
        pitchEnv.noteOn(1.0f); // full-scale; the amount scales it
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
    baseStep = 1.0;
    step = 1.0;
    direction = 1;
    ampEnv.reset();
    modEnv.reset();
    pitchEnv.reset();
}

float SampleVoice::readInterpolated(int channel, double pos) const
{
    const float* data = sourceChannels[channel];
    const int i0 = static_cast<int>(pos);
    const auto frac = static_cast<float>(pos - static_cast<double>(i0));

    // Interior: 4-point, 3rd-order Hermite interpolation (exact for signals up to
    // cubic, so it reproduces ramps/parabolas exactly while adding far less
    // aliasing than linear on repitch). Falls back to 2-point linear at the first
    // and last frames, where the 4-point neighbourhood would run off the sample.
    if (i0 >= 1 && i0 + 2 < sourceNumFrames)
    {
        const float ym1 = data[i0 - 1];
        const float y0 = data[i0];
        const float y1 = data[i0 + 1];
        const float y2 = data[i0 + 2];
        const float c0 = y0;
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }

    const int j1 = std::min(i0 + 1, sourceNumFrames - 1);
    return data[i0] * (1.0f - frac) + data[j1] * frac;
}

void SampleVoice::renderNextBlock(float* const* out, int numOutChannels, int startSample,
                                  int numSamples)
{
    if (!active)
        return;

    const double loopLen = static_cast<double>(loopEndFrame - loopStartFrame);
    const bool looping = mode != LoopMode::OneShot && loopLen > 0.0;

    // When only the block-rate LFO modulates the filter (no per-sample mod env),
    // apply its relative octave/offset around this voice's base once for the block.
    if (useFilter && hasLfoFilterMod && !useModEnv)
    {
        const float cutoff = baseCutoff * std::pow(2.0f, lfoCutoffOct);
        const float type = std::clamp(baseType + lfoTypeOff, 0.0f, 1.0f);
        for (auto& f : filters)
        {
            f.setCutoff(cutoff);
            f.setFilterType(type);
        }
    }

    for (int n = 0; n < numSamples; ++n)
    {
        if (position >= static_cast<double>(sourceNumFrames) || position < 0.0)
        {
            active = false;
            break;
        }

        const float envGain = ampEnv.getNextSample();
        const float voiceGain = gain * envGain;

        // Per-sample mod envelope steers this voice's filter, composed additively
        // with the block-rate LFO mod — both are relative deltas around the base.
        if (useFilter && useModEnv)
        {
            const float m = modEnv.getNextSample();
            const float modCutoff = baseCutoff * std::pow(2.0f, lfoCutoffOct + modToCutoff * m);
            const float modType = std::clamp(baseType + lfoTypeOff + modToType * m, 0.0f, 1.0f);
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

            // Stereo placement: L/R gains from the pan; other channels unaffected.
            const float panGain = ch == 0 ? panLeft : (ch == 1 ? panRight : 1.0f);
            out[ch][dest] += value * panGain;
        }

        // A finished amp release frees the voice (a transparent env never idles
        // on its own; it is ended by stopNote or by the one-shot running out).
        if (!ampEnv.isActive())
        {
            active = false;
            break;
        }

        // Pitch envelope rides on top of the cached (block-rate) step: recompute the
        // advance per sample only while it is active, then it returns to `step`.
        double advance = step;
        if (usePitchEnv)
        {
            const float pe = pitchEnv.getNextSample();
            advance = baseStep *
                      std::pow(2.0, (static_cast<double>(pitchMod) + static_cast<double>(tuneMod) +
                                     static_cast<double>(velPitchMod) +
                                     static_cast<double>(pitchEnvAmount) * pe) /
                                        12.0);
        }
        position += static_cast<double>(direction) * advance;

        if (!looping)
            continue;

        if (mode == LoopMode::Forward)
        {
            // Wrap forward; a while-loop covers steps larger than the loop length.
            while (position >= static_cast<double>(loopEndFrame))
                position -= loopLen;
            // Reverse playback wraps at the bottom so a looped voice keeps cycling.
            // Guarded to the reverse direction so the forward lead-in (position below
            // loopStart before the first wrap) is not disturbed.
            while (direction < 0 && position < static_cast<double>(loopStartFrame))
                position += loopLen;
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
