#include "dsp/Engine.h"

#include <algorithm>
#include <cmath>

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
    bitCrusher.prepare(hostSampleRate);
    saturator.prepare(hostSampleRate);
    compressor.prepare(hostSampleRate);

    for (int i = 0; i < polyphony(); ++i)
        configureVoice(i, currentIndex);
}

void Engine::setPolyphony(int numVoices)
{
    const auto count = static_cast<size_t>(std::max(1, numVoices));
    voices.resize(count);
    voiceNote.resize(count, -1);
    voiceAge.resize(count, 0);

    for (size_t i = 0; i < count; ++i)
        configureVoice(static_cast<int>(i), currentIndex);
}

void Engine::setSample(const float* const* channels, int numChannels, int numFrames,
                       double newSourceSampleRate)
{
    setSampleList({SampleRef{channels, numChannels, numFrames, newSourceSampleRate}});
}

void Engine::setSampleList(const std::vector<SampleRef>& sampleList)
{
    samples = sampleList;
    baseSampleIndex = std::clamp(baseSampleIndex, 0, std::max(0, sampleCount() - 1));
    currentIndex = baseSampleIndex;

    // A new sample set invalidates the loop window and every in-flight note.
    loopStartFrame = 0;
    loopEndFrame = 0;
    reset();

    for (int i = 0; i < polyphony(); ++i)
        configureVoice(i, currentIndex);
}

void Engine::setBaseSampleIndex(int index)
{
    baseSampleIndex = std::clamp(index, 0, std::max(0, sampleCount() - 1));
}

void Engine::setLayerMode(LayerMode newMode)
{
    layerStackMode = newMode;
}

void Engine::setLayers(const std::vector<Layer>& newLayers)
{
    layers = newLayers;
}

void Engine::setLoopMode(SampleVoice::LoopMode mode)
{
    loopMode = mode;
    for (int i = 0; i < polyphony(); ++i)
        voices[static_cast<size_t>(i)].setLoopMode(mode);
}

void Engine::setReverse(bool shouldReverse)
{
    reversePlayback = shouldReverse;
    for (int i = 0; i < polyphony(); ++i)
        voices[static_cast<size_t>(i)].setReversed(shouldReverse);
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

void Engine::setAmpEnvelopeDAH(float delaySeconds, float holdSeconds)
{
    ampDelay = delaySeconds;
    ampHold = holdSeconds;
    for (int i = 0; i < polyphony(); ++i)
        voices[static_cast<size_t>(i)].setAmpEnvelopeDAH(ampDelay, ampHold);
}

void Engine::setAmpCurves(float attackCurve, float decayCurve, float releaseCurve)
{
    ampAttackCurve = attackCurve;
    ampDecayCurve = decayCurve;
    ampReleaseCurve = releaseCurve;
    for (int i = 0; i < polyphony(); ++i)
        voices[static_cast<size_t>(i)].setAmpCurves(ampAttackCurve, ampDecayCurve, ampReleaseCurve);
}

void Engine::setAmpVelocity(float sensitivity)
{
    ampVelocity = sensitivity;
    for (int i = 0; i < polyphony(); ++i)
        voices[static_cast<size_t>(i)].setAmpVelocity(ampVelocity);
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

void Engine::setLfoTrigMode(LfoTrigMode mode)
{
    lfo.setTrigMode(mode);
}

void Engine::setLfoCustomShape(const float* points, int count)
{
    lfo.setCustomShape(points, count);
}

void Engine::addModRoute(ModDestination destination, float amount)
{
    modMatrix.addRoute(destination, amount);
    if (destination == ModDestination::SampleSelect)
        lfoToSampleSelect = amount; // cached for note-time sample selection
}

void Engine::clearModRoutes()
{
    modMatrix.clearRoutes();
    lfoToSampleSelect = 0.0f;
}

void Engine::setModEnvelope(float attackSeconds, float decaySeconds, float sustainLevel,
                            float releaseSeconds)
{
    modAttack = attackSeconds;
    modDecay = decaySeconds;
    modSustain = sustainLevel;
    modRelease = releaseSeconds;
    for (int i = 0; i < polyphony(); ++i)
        voices[static_cast<size_t>(i)].setModEnvelope(modAttack, modDecay, modSustain, modRelease);
}

void Engine::setModRouting(float toCutoffOctaves, float toTypeOffset)
{
    modToCutoff = toCutoffOctaves;
    modToType = toTypeOffset;
    for (int i = 0; i < polyphony(); ++i)
        voices[static_cast<size_t>(i)].setModRouting(modToCutoff, modToType);
}

void Engine::setPitchEnvelope(float amountSemitones, float decaySeconds)
{
    pitchEnvAmount = amountSemitones;
    pitchEnvDecay = decaySeconds;
    for (int i = 0; i < polyphony(); ++i)
        voices[static_cast<size_t>(i)].setPitchEnvelope(pitchEnvAmount, pitchEnvDecay);
}

void Engine::setVelToPitch(float semitones)
{
    velToPitch = semitones;
    for (int i = 0; i < polyphony(); ++i)
        voices[static_cast<size_t>(i)].setVelToPitch(velToPitch);
}

void Engine::setVelToCutoff(float octaves)
{
    velToCutoff = octaves;
    for (int i = 0; i < polyphony(); ++i)
        voices[static_cast<size_t>(i)].setVelToCutoff(velToCutoff);
}

void Engine::setHumanize(float pitchCents, float levelDb)
{
    humanizePitch = pitchCents;
    humanizeLevel = levelDb;
    for (int i = 0; i < polyphony(); ++i)
        voices[static_cast<size_t>(i)].setHumanize(humanizePitch, humanizeLevel);
}

void Engine::setPitchBend(float semitones)
{
    pitchBend = semitones;
    for (int i = 0; i < polyphony(); ++i)
        voices[static_cast<size_t>(i)].setBendSemitones(pitchBend);
}

void Engine::setWheelCutoff(float octaves)
{
    wheelCutoff = octaves;
    for (int i = 0; i < polyphony(); ++i)
        voices[static_cast<size_t>(i)].setWheelCutoff(wheelCutoff);
}

void Engine::setCompressor(bool enabled, float thresholdDb, float ratio, float attackSeconds,
                           float releaseSeconds, float makeupDb)
{
    compressor.setParameters(thresholdDb, ratio, attackSeconds, releaseSeconds, makeupDb);
    compressor.setEnabled(enabled);
}

void Engine::setSaturator(bool enabled, float drive, float mix)
{
    saturator.setParameters(drive, mix);
    saturator.setEnabled(enabled);
}

void Engine::setBitCrusher(bool enabled, float bits, int downsample)
{
    bitCrusher.setParameters(bits, downsample);
    bitCrusher.setEnabled(enabled);
}

void Engine::setOutput(float gain, float pan)
{
    outGain = gain;
    outPan = std::clamp(pan, -1.0f, 1.0f);
}

void Engine::configureVoice(int voiceIndex, int sampleIndex)
{
    auto& voice = voices[static_cast<size_t>(voiceIndex)];
    voice.prepare(hostSampleRate);

    if (sampleIndex >= 0 && sampleIndex < sampleCount() &&
        samples[static_cast<size_t>(sampleIndex)].valid())
    {
        const auto& s = samples[static_cast<size_t>(sampleIndex)];
        voice.setSample(s.channels, s.numChannels, s.numFrames, s.sampleRate);
    }
    voice.setLoopMode(loopMode);
    voice.setReversed(reversePlayback);
    if (loopEndFrame > loopStartFrame)
        voice.setLoopPoints(loopStartFrame, loopEndFrame);
    voice.setAmpEnvelope(ampAttack, ampDecay, ampSustain, ampRelease);
    voice.setAmpEnvelopeDAH(ampDelay, ampHold);
    voice.setAmpCurves(ampAttackCurve, ampDecayCurve, ampReleaseCurve);
    voice.setAmpVelocity(ampVelocity);
    voice.setModEnvelope(modAttack, modDecay, modSustain, modRelease);
    voice.setPitchEnvelope(pitchEnvAmount, pitchEnvDecay);
    voice.setVelToPitch(velToPitch);
    voice.setVelToCutoff(velToCutoff);
    voice.setHumanize(humanizePitch, humanizeLevel);
    voice.setBendSemitones(pitchBend);
    voice.setWheelCutoff(wheelCutoff);
    // A distinct non-zero seed per voice so the pool's humanize sequences diverge.
    voice.seedHumanize(0x9E3779B9u + static_cast<unsigned int>(voiceIndex) * 0x85EBCA6Bu + 1u);
    voice.setModRouting(modToCutoff, modToType);
    voice.setFilterEnabled(filterEnabled);
    if (filterEnabled)
        voice.setFilter(filterCutoff, filterResonance, filterType);
    // No per-layer band filter by default; triggerLayer re-enables it per layer, and
    // the Single/Select path leaves it off (so a reused voice never keeps a stale band).
    voice.setLowCut(false, 20.0f);
    voice.setHighCut(false, 20000.0f);
}

int Engine::selectSampleIndex() const
{
    const int base = std::clamp(baseSampleIndex, 0, std::max(0, sampleCount() - 1));
    if (sampleCount() <= 1 || std::abs(lfoToSampleSelect) < 1.0e-6f)
        return base;

    // Read the LFO at the trig and step that many LOADED samples away from the base
    // (LFO -> sample slot). Empty slots are skipped, so an offset never
    // lands on a hole and silences the hit. Two passes, no allocation (audio thread).
    int loaded = 0;
    int baseRank = 0; // rank of the base (or the nearest loaded slot below it)
    for (int i = 0; i < sampleCount(); ++i)
        if (samples[static_cast<size_t>(i)].valid())
        {
            if (i <= base)
                baseRank = loaded;
            ++loaded;
        }
    if (loaded == 0)
        return base;

    const int offset = static_cast<int>(std::lround(lfo.peek() * lfoToSampleSelect));
    const int rank = std::clamp(baseRank + offset, 0, loaded - 1);
    for (int i = 0, r = 0; i < sampleCount(); ++i)
        if (samples[static_cast<size_t>(i)].valid())
        {
            if (r == rank)
                return i;
            ++r;
        }
    return base;
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
    if (samples.empty())
        return;

    // Apply the LFO's trig-mode reaction to this note before anything reads it, so
    // Trig/Hold/OneShot affect the sample-select peek and the block modulation.
    lfo.noteTrigger();

    if (layerStackMode == LayerMode::Stack && !layers.empty())
    {
        triggerStack(midiNote, velocity, rootNote);
        return;
    }
    if (layerStackMode == LayerMode::RoundRobin && !layers.empty())
    {
        triggerRoundRobin(midiNote, velocity, rootNote);
        return;
    }
    if (layerStackMode == LayerMode::Velocity && !layers.empty())
    {
        triggerVelocity(midiNote, velocity, rootNote);
        return;
    }

    // Select mode: pick the sample for this trig (LFO/mod -> sample-select) and
    // hand it to the allocated voice, re-applying the voice config for the chosen
    // sample. Gate on the *selected* slot (not slot 0): with layering, a track is
    // fed the whole bank and earlier slots may be empty, but its base slot loaded.
    currentIndex = selectSampleIndex();
    if (currentIndex < 0 || currentIndex >= sampleCount() ||
        !samples[static_cast<size_t>(currentIndex)].valid())
        return;

    const int index = allocateVoice();
    configureVoice(index, currentIndex);
    voiceNote[static_cast<size_t>(index)] = midiNote;
    voiceAge[static_cast<size_t>(index)] = ++noteCounter;
    auto& voice = voices[static_cast<size_t>(index)];
    voice.setTuneSemitones(0.0f); // Select: no per-layer tune
    voice.setPan(0.0f);           // Select: centred (unity fan-out)
    voice.startNote(midiNote, velocity, rootNote);
}

bool Engine::layerPlayable(const Layer& layer) const
{
    return layer.enabled && layer.sampleIndex >= 0 && layer.sampleIndex < sampleCount() &&
           samples[static_cast<size_t>(layer.sampleIndex)].valid();
}

void Engine::triggerLayer(const Layer& layer, int midiNote, float velocity, int rootNote)
{
    // Fire one layer on its own voice: its sample, per-layer gain (scales velocity),
    // per-layer tune (semitones, independent of the pitch mod channel) and pan.
    const int index = allocateVoice();
    configureVoice(index, layer.sampleIndex);
    voiceNote[static_cast<size_t>(index)] = midiNote;
    voiceAge[static_cast<size_t>(index)] = ++noteCounter;

    auto& voice = voices[static_cast<size_t>(index)];
    voice.setTuneSemitones(layer.tuneSemitones);
    voice.setPan(layer.pan);
    // Per-layer filter overrides the track filter base for this voice.
    if (layer.filterOverride)
        voice.setFilter(layer.cutoffHz, layer.resonance, layer.type);
    // Per-layer amp envelope overrides the shared DAHDSR for this voice (curves and
    // velocity stay shared, applied by configureVoice above).
    if (layer.ampOverride)
    {
        voice.setAmpEnvelope(layer.ampAttack, layer.ampDecay, layer.ampSustain, layer.ampRelease);
        voice.setAmpEnvelopeDAH(layer.ampDelay, layer.ampHold);
    }
    voice.setReversed(layer.reversed); // per-layer reverse (overrides the shared flag)
    voice.setLowCut(layer.lowCutOn, layer.lowCutHz);
    voice.setHighCut(layer.highCutOn, layer.highCutHz);
    voice.startNote(midiNote, std::clamp(velocity * layer.gain, 0.0f, 1.0f), rootNote);
    currentIndex = layer.sampleIndex; // last sounding layer, for queries
}

void Engine::triggerStack(int midiNote, float velocity, int rootNote)
{
    // Trigger every enabled, valid layer together (a drum built from layers).
    for (const auto& layer : layers)
        if (layerPlayable(layer))
            triggerLayer(layer, midiNote, velocity, rootNote);
}

void Engine::triggerRoundRobin(int midiNote, float velocity, int rootNote)
{
    // Trigger ONE playable layer per trig, advancing round-robin over the enabled,
    // valid layers so consecutive hits alternate takes (anti-machine-gun). Disabled
    // / out-of-range layers are skipped without consuming a step.
    const int count = static_cast<int>(layers.size());
    for (int step = 0; step < count; ++step)
    {
        const int idx = ((roundRobinCursor % count) + count) % count;
        roundRobinCursor = idx + 1;
        const auto& layer = layers[static_cast<size_t>(idx)];
        if (layerPlayable(layer))
        {
            triggerLayer(layer, midiNote, velocity, rootNote);
            return;
        }
    }
}

void Engine::triggerVelocity(int midiNote, float velocity, int rootNote)
{
    // Trigger ONE layer chosen by hit strength. Gather the playable layers in order
    // (layer 0 = softest), then split [0, 1] into equal velocity bands over them and
    // fire the band the velocity falls into. Full velocity clamps to the last layer.
    std::vector<const Layer*> playable;
    playable.reserve(layers.size());
    for (const auto& layer : layers)
        if (layerPlayable(layer))
            playable.push_back(&layer);
    if (playable.empty())
        return;

    const int count = static_cast<int>(playable.size());
    const float v = std::clamp(velocity, 0.0f, 1.0f);
    int band = static_cast<int>(v * static_cast<float>(count));
    band = std::clamp(band, 0, count - 1);
    triggerLayer(*playable[static_cast<size_t>(band)], midiNote, velocity, rootNote);
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

void Engine::stopAllNotes()
{
    for (int i = 0; i < polyphony(); ++i)
    {
        auto& voice = voices[static_cast<size_t>(i)];
        if (voice.isActive())
        {
            voice.stopNote();
            if (!voice.isActive())
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
    roundRobinCursor = 0;
}

void Engine::renderNextBlock(float* const* out, int numOutChannels, int startSample, int numSamples)
{
    // Advance the LFO whenever anything is routed (so sample-select's note-time
    // peek keeps moving), then steer the per-voice pitch (always) and filters
    // (when the filter is on) from the same modulation reading.
    if (modMatrix.hasRoutes())
    {
        const float source = lfo.advance(numSamples);
        const auto mod = modMatrix.apply(filterCutoff, filterType, source);
        const auto fmod = modMatrix.filterMod(source); // relative filter deltas
        for (int i = 0; i < polyphony(); ++i)
        {
            auto& voice = voices[static_cast<size_t>(i)];
            voice.setPitchModSemitones(mod.pitchSemitones);
            // Modulate each voice around its OWN base filter, so stacked layers with
            // independent filters stay independent under the shared track LFO.
            if (filterEnabled)
                voice.setFilterLfoMod(fmod.cutoffOctaves, fmod.typeOffset);
        }
    }

    const bool outputTrim = outGain != 1.0f || outPan != 0.0f;
    if (!compressor.enabled() && !saturator.enabled() && !bitCrusher.enabled() && !outputTrim)
    {
        // Fast path: voices sum straight into the output, no scratch buffer.
        for (auto& voice : voices)
            voice.renderNextBlock(out, numOutChannels, startSample, numSamples);
        return;
    }

    // FX path: render the whole track into a cleared scratch buffer, run the
    // per-track bit crusher, then saturator, then compressor over it (each a no-op if
    // disabled), then sum the result into the output.
    const auto ch = static_cast<size_t>(numOutChannels);
    if (compBuffer.size() < ch)
        compBuffer.resize(ch);
    compPtrs.clear();
    for (int c = 0; c < numOutChannels; ++c)
    {
        auto& row = compBuffer[static_cast<size_t>(c)];
        if (static_cast<int>(row.size()) < numSamples)
            row.resize(static_cast<size_t>(numSamples));
        std::fill_n(row.data(), numSamples, 0.0f);
        compPtrs.push_back(row.data());
    }

    for (auto& voice : voices)
        voice.renderNextBlock(compPtrs.data(), numOutChannels, 0, numSamples);

    bitCrusher.processBlock(compPtrs.data(), numOutChannels, numSamples);
    saturator.processBlock(compPtrs.data(), numOutChannels, numSamples);
    compressor.processBlock(compPtrs.data(), numOutChannels, numSamples);

    // Sum into the output, applying the per-track output level + balance (same law as
    // the per-voice pan: centre keeps both sides at unity).
    const float panL = outPan > 0.0f ? 1.0f - outPan : 1.0f;
    const float panR = outPan < 0.0f ? 1.0f + outPan : 1.0f;
    for (int c = 0; c < numOutChannels; ++c)
    {
        const float g = outGain * (c == 0 ? panL : (c == 1 ? panR : 1.0f));
        for (int n = 0; n < numSamples; ++n)
            out[c][startSample + n] += compPtrs[static_cast<size_t>(c)][n] * g;
    }
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
