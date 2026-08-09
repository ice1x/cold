#include "dsp/Engine.h"

#include <algorithm>

namespace cold::dsp
{

Engine::Engine()
{
    setPolyphony(8);
}

void Engine::prepare(double newHostSampleRate)
{
    if (newHostSampleRate > 0.0)
        hostSampleRate = newHostSampleRate;

    lfo.setSampleRate(hostSampleRate);

    for (int i = 0; i < polyphony(); ++i)
        applyConfigToVoice(i);
}

void Engine::setPolyphony(int numVoices)
{
    const auto count = static_cast<size_t>(std::max(1, numVoices));
    voices.resize(count);
    voiceNote.resize(count, -1);
    voiceAge.resize(count, 0);

    for (size_t i = 0; i < count; ++i)
        applyConfigToVoice(static_cast<int>(i));
}

void Engine::setSample(const float* const* channels, int numChannels, int numFrames,
                       double newSourceSampleRate)
{
    sourceChannels = channels;
    sourceNumChannels = numChannels;
    sourceNumFrames = numFrames;
    sourceSampleRate = newSourceSampleRate;

    // A new sample invalidates the loop window and every in-flight note.
    loopStartFrame = 0;
    loopEndFrame = 0;
    reset();

    for (int i = 0; i < polyphony(); ++i)
        applyConfigToVoice(i);
}

void Engine::setLoopMode(SampleVoice::LoopMode mode)
{
    loopMode = mode;
    for (int i = 0; i < polyphony(); ++i)
        voices[static_cast<size_t>(i)].setLoopMode(mode);
}

void Engine::setLoopPoints(int startFrame, int endFrame)
{
    loopStartFrame = startFrame;
    loopEndFrame = endFrame;
    for (int i = 0; i < polyphony(); ++i)
        voices[static_cast<size_t>(i)].setLoopPoints(startFrame, endFrame);
}

void Engine::setAmpEnvelope(float attackSeconds, float decaySeconds, float sustainLevel,
                            float releaseSeconds)
{
    ampAttack = attackSeconds;
    ampDecay = decaySeconds;
    ampSustain = sustainLevel;
    ampRelease = releaseSeconds;
    for (int i = 0; i < polyphony(); ++i)
        voices[static_cast<size_t>(i)].setAmpEnvelope(ampAttack, ampDecay, ampSustain, ampRelease);
}

void Engine::setFilter(float cutoffHz, float resonance, float type)
{
    filterEnabled = true;
    filterCutoff = cutoffHz;
    filterResonance = resonance;
    filterType = type;
    for (int i = 0; i < polyphony(); ++i)
        voices[static_cast<size_t>(i)].setFilter(cutoffHz, resonance, type);
}

void Engine::setLfo(LfoShape shape, float rateHz, float depth)
{
    lfo.setShape(shape);
    lfo.setRate(rateHz);
    lfo.setDepth(depth);
}

void Engine::addModRoute(ModDestination destination, float amount)
{
    modMatrix.addRoute(destination, amount);
}

void Engine::clearModRoutes()
{
    modMatrix.clearRoutes();
}

void Engine::applyConfigToVoice(int index)
{
    auto& voice = voices[static_cast<size_t>(index)];
    voice.prepare(hostSampleRate);
    if (sourceChannels != nullptr)
        voice.setSample(sourceChannels, sourceNumChannels, sourceNumFrames, sourceSampleRate);
    voice.setLoopMode(loopMode);
    if (loopEndFrame > loopStartFrame)
        voice.setLoopPoints(loopStartFrame, loopEndFrame);
    voice.setAmpEnvelope(ampAttack, ampDecay, ampSustain, ampRelease);
    voice.setFilterEnabled(filterEnabled);
    if (filterEnabled)
        voice.setFilter(filterCutoff, filterResonance, filterType);
}

int Engine::allocateVoice()
{
    // Prefer an idle voice.
    for (int i = 0; i < polyphony(); ++i)
        if (!voices[static_cast<size_t>(i)].isActive())
            return i;

    // Otherwise steal the oldest-started voice.
    int oldest = 0;
    for (int i = 1; i < polyphony(); ++i)
        if (voiceAge[static_cast<size_t>(i)] < voiceAge[static_cast<size_t>(oldest)])
            oldest = i;
    return oldest;
}

void Engine::noteOn(int midiNote, float velocity, int rootNote)
{
    if (sourceChannels == nullptr || sourceNumFrames == 0)
        return;

    const int index = allocateVoice();
    voiceNote[static_cast<size_t>(index)] = midiNote;
    voiceAge[static_cast<size_t>(index)] = ++noteCounter;
    voices[static_cast<size_t>(index)].startNote(midiNote, velocity, rootNote);
}

void Engine::noteOff(int midiNote)
{
    for (int i = 0; i < polyphony(); ++i)
    {
        if (voiceNote[static_cast<size_t>(i)] == midiNote &&
            voices[static_cast<size_t>(i)].isActive())
        {
            voices[static_cast<size_t>(i)].stopNote();
            voiceNote[static_cast<size_t>(i)] = -1;
        }
    }
}

void Engine::reset()
{
    for (int i = 0; i < polyphony(); ++i)
    {
        voices[static_cast<size_t>(i)].reset();
        voiceNote[static_cast<size_t>(i)] = -1;
        voiceAge[static_cast<size_t>(i)] = 0;
    }
    noteCounter = 0;
}

void Engine::renderNextBlock(float* const* out, int numOutChannels, int startSample, int numSamples)
{
    // Block-rate modulation: take one LFO value for this chunk and steer the
    // (smoothed) per-voice filters. Only meaningful when the filter is engaged.
    if (filterEnabled && modMatrix.hasRoutes())
    {
        const float source = lfo.advance(numSamples);
        const auto mod = modMatrix.apply(filterCutoff, filterType, source);
        for (int i = 0; i < polyphony(); ++i)
            voices[static_cast<size_t>(i)].setFilter(mod.cutoffHz, filterResonance, mod.type);
    }

    for (auto& voice : voices)
        voice.renderNextBlock(out, numOutChannels, startSample, numSamples);
}

int Engine::activeVoiceCount() const
{
    int count = 0;
    for (const auto& voice : voices)
        if (voice.isActive())
            ++count;
    return count;
}

bool Engine::isNotePlaying(int midiNote) const
{
    for (int i = 0; i < polyphony(); ++i)
        if (voiceNote[static_cast<size_t>(i)] == midiNote &&
            voices[static_cast<size_t>(i)].isActive())
            return true;
    return false;
}

} // namespace cold::dsp
