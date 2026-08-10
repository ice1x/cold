#include "PluginProcessor.h"

#include "PluginEditor.h"

#include "dsp/SampleVoice.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace cold
{

namespace
{
constexpr auto kGainId = "gain";
constexpr auto kPolyphonyId = "polyphony";
constexpr auto kLoopModeId = "loopMode";
constexpr auto kLoopStartId = "loopStart";
constexpr auto kLoopEndId = "loopEnd";
constexpr auto kRootNoteId = "rootNote";
constexpr auto kAttackId = "ampAttack";
constexpr auto kDecayId = "ampDecay";
constexpr auto kSustainId = "ampSustain";
constexpr auto kReleaseId = "ampRelease";
constexpr auto kFilterCutoffId = "filterCutoff";
constexpr auto kFilterResonanceId = "filterResonance";
constexpr auto kFilterTypeId = "filterType";
constexpr auto kLfoShapeId = "lfoShape";
constexpr auto kLfoRateId = "lfoRate";
constexpr auto kLfoDepthId = "lfoDepth";
constexpr auto kLfoSyncId = "lfoSync";
constexpr auto kLfoDivisionId = "lfoDivision";
constexpr auto kLfoToCutoffId = "lfoToCutoff";
constexpr auto kLfoToTypeId = "lfoToType";
constexpr auto kLfoToPitchId = "lfoToPitch";
constexpr auto kLfoToSampleId = "lfoToSampleSelect";
constexpr auto kLfoTrigModeId = "lfoTrigMode";
constexpr auto kTrackId = "track";
constexpr auto kAmpDelayId = "ampDelay";
constexpr auto kAmpHoldId = "ampHold";
constexpr auto kAmpAtkCurveId = "ampAttackCurve";
constexpr auto kAmpDecCurveId = "ampDecayCurve";
constexpr auto kAmpRelCurveId = "ampReleaseCurve";
constexpr auto kAmpVelId = "ampVelocity";
constexpr auto kModAttackId = "modAttack";
constexpr auto kModDecayId = "modDecay";
constexpr auto kModSustainId = "modSustain";
constexpr auto kModReleaseId = "modRelease";
constexpr auto kModToCutoffId = "modToCutoff";
constexpr auto kModToTypeId = "modToType";

// Number of sample-bank slots exposed in M4; bank slot t feeds track t (t < 8).
constexpr int kNumSlots = 16;

// Host-sync divisions (parallel to the lfoDivision choice list), in quarter-note
// beats per LFO cycle: "1/16" = 0.25 beats ... "4 bars" = 16 beats.
constexpr std::array<double, 7> kDivisionBeats{16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25};

// A time range in seconds with a perceptual skew (more resolution near zero).
juce::NormalisableRange<float> timeRange(float maxSeconds)
{
    juce::NormalisableRange<float> range(0.0f, maxSeconds, 0.0001f);
    range.setSkewForCentre(0.2f);
    return range;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout ColdAudioProcessor::createParameterLayout()
{
    using juce::AudioParameterChoice;
    using juce::AudioParameterFloat;
    using juce::AudioParameterInt;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<AudioParameterFloat>(
        juce::ParameterID{kGainId, 1}, "Gain", juce::NormalisableRange<float>(-60.0f, 6.0f, 0.1f),
        0.0f));

    layout.add(std::make_unique<AudioParameterInt>(juce::ParameterID{kPolyphonyId, 1}, "Polyphony",
                                                   1, 16, 8));

    layout.add(std::make_unique<AudioParameterChoice>(
        juce::ParameterID{kLoopModeId, 1}, "Loop Mode",
        juce::StringArray{"One-Shot", "Forward", "Ping-Pong"}, 0));

    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kLoopStartId, 1},
                                                     "Loop Start", 0.0f, 1.0f, 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kLoopEndId, 1}, "Loop End",
                                                     0.0f, 1.0f, 1.0f));

    layout.add(std::make_unique<AudioParameterInt>(juce::ParameterID{kRootNoteId, 1}, "Root Note",
                                                   0, 127, 60));

    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kAttackId, 1}, "Amp Attack",
                                                     timeRange(5.0f), 0.001f));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kDecayId, 1}, "Amp Decay",
                                                     timeRange(5.0f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kSustainId, 1},
                                                     "Amp Sustain", 0.0f, 1.0f, 1.0f));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kReleaseId, 1},
                                                     "Amp Release", timeRange(5.0f), 0.05f));

    // Morph filter: cutoff (log-ish skew), resonance/Q, and the LP->BP->HP->Notch
    // morph type. Cutoff defaults wide open so an unconfigured patch is bright.
    juce::NormalisableRange<float> cutoffRange(20.0f, 20000.0f, 1.0f);
    cutoffRange.setSkewForCentre(1000.0f);
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kFilterCutoffId, 1},
                                                     "Filter Cutoff", cutoffRange, 20000.0f));
    layout.add(std::make_unique<AudioParameterFloat>(
        juce::ParameterID{kFilterResonanceId, 1}, "Filter Resonance",
        juce::NormalisableRange<float>(0.1f, 20.0f, 0.01f, 0.5f), 0.707f));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kFilterTypeId, 1},
                                                     "Filter Type", 0.0f, 1.0f, 0.0f));

    // LFO: shape, free rate (Hz) or host-tempo sync + division, depth, and the two
    // modulation amounts — to cutoff (octaves) and, distinctively, to filter type.
    layout.add(std::make_unique<AudioParameterChoice>(
        juce::ParameterID{kLfoShapeId, 1}, "LFO Shape",
        juce::StringArray{"Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random"}, 0));
    juce::NormalisableRange<float> rateRange(0.01f, 40.0f, 0.01f);
    rateRange.setSkewForCentre(2.0f);
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kLfoRateId, 1}, "LFO Rate",
                                                     rateRange, 2.0f));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kLfoDepthId, 1}, "LFO Depth",
                                                     0.0f, 1.0f, 1.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{kLfoSyncId, 1},
                                                          "LFO Sync", false));
    layout.add(std::make_unique<AudioParameterChoice>(
        juce::ParameterID{kLfoDivisionId, 1}, "LFO Division",
        juce::StringArray{"4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16"}, 4));
    // How the LFO reacts to each note: Free (ignore), Trig (restart phase), Hold
    // (latch its value at the trig), One-Shot (one cycle per note, then freeze).
    layout.add(std::make_unique<AudioParameterChoice>(
        juce::ParameterID{kLfoTrigModeId, 1}, "LFO Trig Mode",
        juce::StringArray{"Free", "Trig", "Hold", "One-Shot"}, 0));
    layout.add(std::make_unique<AudioParameterFloat>(
        juce::ParameterID{kLfoToCutoffId, 1}, "LFO -> Cutoff",
        juce::NormalisableRange<float>(-4.0f, 4.0f, 0.01f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(
        juce::ParameterID{kLfoToTypeId, 1}, "LFO -> Filter Type",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(
        juce::ParameterID{kLfoToPitchId, 1}, "LFO -> Pitch",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
    // LFO/mod -> sample-select: a signed slot/layer offset applied at the trig.
    // Each track is fed the whole bank, so this roams across the loaded slots.
    layout.add(std::make_unique<AudioParameterFloat>(
        juce::ParameterID{kLfoToSampleId, 1}, "LFO -> Sample Select",
        juce::NormalisableRange<float>(-8.0f, 8.0f, 0.01f), 0.0f));

    // Amp envelope extras: delay/hold stages, per-segment curves, velocity.
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kAmpDelayId, 1}, "Amp Delay",
                                                     timeRange(5.0f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kAmpHoldId, 1}, "Amp Hold",
                                                     timeRange(5.0f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kAmpAtkCurveId, 1},
                                                     "Amp Attack Curve", -1.0f, 1.0f, 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kAmpDecCurveId, 1},
                                                     "Amp Decay Curve", -1.0f, 1.0f, 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kAmpRelCurveId, 1},
                                                     "Amp Release Curve", -1.0f, 1.0f, 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kAmpVelId, 1},
                                                     "Amp Velocity", 0.0f, 1.0f, 0.0f));

    // Modulation envelope + its routing to the filter (env -> cutoff / type).
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kModAttackId, 1},
                                                     "Mod Attack", timeRange(5.0f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kModDecayId, 1}, "Mod Decay",
                                                     timeRange(5.0f), 0.2f));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kModSustainId, 1},
                                                     "Mod Sustain", 0.0f, 1.0f, 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kModReleaseId, 1},
                                                     "Mod Release", timeRange(5.0f), 0.1f));
    layout.add(std::make_unique<AudioParameterFloat>(
        juce::ParameterID{kModToCutoffId, 1}, "Mod Env -> Cutoff",
        juce::NormalisableRange<float>(-6.0f, 6.0f, 0.01f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(
        juce::ParameterID{kModToTypeId, 1}, "Mod Env -> Filter Type",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));

    // Selected track for editing / the Load buttons. All 8 tracks play; MIDI
    // channel N routes to track N-1. Track t plays bank slot t.
    layout.add(std::make_unique<AudioParameterInt>(juce::ParameterID{kTrackId, 1}, "Track", 0,
                                                   dsp::MultiEngine::kNumTracks - 1, 0));

    return layout;
}

ColdAudioProcessor::ColdAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParameterLayout()), bank(kNumSlots)
{
    gainParam = apvts.getRawParameterValue(kGainId);
    polyphonyParam = apvts.getRawParameterValue(kPolyphonyId);
    loopModeParam = apvts.getRawParameterValue(kLoopModeId);
    loopStartParam = apvts.getRawParameterValue(kLoopStartId);
    loopEndParam = apvts.getRawParameterValue(kLoopEndId);
    rootNoteParam = apvts.getRawParameterValue(kRootNoteId);
    attackParam = apvts.getRawParameterValue(kAttackId);
    decayParam = apvts.getRawParameterValue(kDecayId);
    sustainParam = apvts.getRawParameterValue(kSustainId);
    releaseParam = apvts.getRawParameterValue(kReleaseId);
    filterCutoffParam = apvts.getRawParameterValue(kFilterCutoffId);
    filterResonanceParam = apvts.getRawParameterValue(kFilterResonanceId);
    filterTypeParam = apvts.getRawParameterValue(kFilterTypeId);
    lfoShapeParam = apvts.getRawParameterValue(kLfoShapeId);
    lfoRateParam = apvts.getRawParameterValue(kLfoRateId);
    lfoDepthParam = apvts.getRawParameterValue(kLfoDepthId);
    lfoSyncParam = apvts.getRawParameterValue(kLfoSyncId);
    lfoDivisionParam = apvts.getRawParameterValue(kLfoDivisionId);
    lfoToCutoffParam = apvts.getRawParameterValue(kLfoToCutoffId);
    lfoToTypeParam = apvts.getRawParameterValue(kLfoToTypeId);
    lfoToPitchParam = apvts.getRawParameterValue(kLfoToPitchId);
    lfoToSampleParam = apvts.getRawParameterValue(kLfoToSampleId);
    lfoTrigModeParam = apvts.getRawParameterValue(kLfoTrigModeId);
    trackParam = apvts.getRawParameterValue(kTrackId);

    // Each track is fed the whole bank as its sample-select list; size the shared
    // windowed-pointer storage to match.
    slotPtrs.resize(static_cast<size_t>(bank.numSlots()));
    slotNumFrames.assign(static_cast<size_t>(bank.numSlots()), 0);
    ampDelayParam = apvts.getRawParameterValue(kAmpDelayId);
    ampHoldParam = apvts.getRawParameterValue(kAmpHoldId);
    ampAtkCurveParam = apvts.getRawParameterValue(kAmpAtkCurveId);
    ampDecCurveParam = apvts.getRawParameterValue(kAmpDecCurveId);
    ampRelCurveParam = apvts.getRawParameterValue(kAmpRelCurveId);
    ampVelParam = apvts.getRawParameterValue(kAmpVelId);
    modAttackParam = apvts.getRawParameterValue(kModAttackId);
    modDecayParam = apvts.getRawParameterValue(kModDecayId);
    modSustainParam = apvts.getRawParameterValue(kModSustainId);
    modReleaseParam = apvts.getRawParameterValue(kModReleaseId);
    modToCutoffParam = apvts.getRawParameterValue(kModToCutoffId);
    modToTypeParam = apvts.getRawParameterValue(kModToTypeId);
}

void ColdAudioProcessor::prepareToPlay(double sampleRate, int)
{
    const juce::ScopedLock sl(engineLock);
    multi.prepare(sampleRate);
    lastPolyphony = -1; // force a re-apply of polyphony on the next block
    updateEngineParameters();
}

void ColdAudioProcessor::releaseResources() {}

bool ColdAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void ColdAudioProcessor::applySharedParamsToTrack(int track, bool polyChanged)
{
    auto& e = multi.track(track);

    if (polyChanged)
        e.setPolyphony(static_cast<int>(polyphonyParam->load()));

    const auto mode =
        static_cast<dsp::SampleVoice::LoopMode>(static_cast<int>(loopModeParam->load()));
    e.setLoopMode(mode);

    // Loop points are relative to the track's base slot (slot == track index).
    const int frames = slotNumFrames[static_cast<size_t>(track)];
    if (frames > 0)
    {
        const int start = static_cast<int>(loopStartParam->load() * static_cast<float>(frames));
        const int end = static_cast<int>(loopEndParam->load() * static_cast<float>(frames));
        if (end > start)
            e.setLoopPoints(start, end);
    }

    e.setAmpEnvelope(attackParam->load(), decayParam->load(), sustainParam->load(),
                     releaseParam->load());
    e.setAmpEnvelopeDAH(ampDelayParam->load(), ampHoldParam->load());
    e.setAmpCurves(ampAtkCurveParam->load(), ampDecCurveParam->load(), ampRelCurveParam->load());
    e.setAmpVelocity(ampVelParam->load());
    e.setModEnvelope(modAttackParam->load(), modDecayParam->load(), modSustainParam->load(),
                     modReleaseParam->load());
    e.setModRouting(modToCutoffParam->load(), modToTypeParam->load());
    e.setFilter(filterCutoffParam->load(), filterResonanceParam->load(), filterTypeParam->load());

    // LFO: free rate in Hz, or host-tempo synced via the selected division.
    const auto shape = static_cast<dsp::LfoShape>(static_cast<int>(lfoShapeParam->load()));
    float rateHz = lfoRateParam->load();
    if (lfoSyncParam->load() > 0.5f)
    {
        const auto idx =
            static_cast<size_t>(std::clamp(static_cast<int>(lfoDivisionParam->load()), 0,
                                           static_cast<int>(kDivisionBeats.size()) - 1));
        rateHz = static_cast<float>(dsp::Lfo::divisionToHz(hostBpm, kDivisionBeats[idx]));
    }
    e.setLfo(shape, rateHz, lfoDepthParam->load());
    e.setLfoTrigMode(static_cast<dsp::LfoTrigMode>(static_cast<int>(lfoTrigModeParam->load())));

    e.clearModRoutes();
    if (std::abs(lfoToCutoffParam->load()) > 1.0e-4f)
        e.addModRoute(dsp::ModDestination::FilterCutoff, lfoToCutoffParam->load());
    if (std::abs(lfoToTypeParam->load()) > 1.0e-4f)
        e.addModRoute(dsp::ModDestination::FilterType, lfoToTypeParam->load());
    if (std::abs(lfoToPitchParam->load()) > 1.0e-4f)
        e.addModRoute(dsp::ModDestination::Pitch, lfoToPitchParam->load());
    if (std::abs(lfoToSampleParam->load()) > 1.0e-4f)
        e.addModRoute(dsp::ModDestination::SampleSelect, lfoToSampleParam->load());
}

void ColdAudioProcessor::updateEngineParameters()
{
    // Shared sound params drive every track (each keeps its own voice pool + filter
    // + LFO state). Polyphony is destructive (resets the pool) so it is guarded.
    const int poly = static_cast<int>(polyphonyParam->load());
    const bool polyChanged = poly != lastPolyphony;
    if (polyChanged)
        lastPolyphony = poly;

    for (int t = 0; t < dsp::MultiEngine::kNumTracks; ++t)
        applySharedParamsToTrack(t, polyChanged);
}

void ColdAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const juce::ScopedLock sl(engineLock);

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    // Capture the host tempo for LFO sync (falls back to the last known BPM).
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm = *bpm;

    updateEngineParameters();

    // Channel-pointer view for the engine (up to a normal stereo pair).
    float* channels[2] = {nullptr, nullptr};
    for (int ch = 0; ch < numChannels && ch < 2; ++ch)
        channels[ch] = buffer.getWritePointer(ch);
    const int engineChannels = juce::jmin(numChannels, 2);

    const int rootNote = static_cast<int>(rootNoteParam->load());

    // Render in segments split on MIDI event boundaries for sample-accurate trigs.
    // Notes route to a track by MIDI channel (Digitakt-style: channel N -> track N-1).
    int pos = 0;
    for (const auto meta : midi)
    {
        const int eventTime = juce::jlimit(0, numSamples, meta.samplePosition);
        if (eventTime > pos)
        {
            multi.renderNextBlock(channels, engineChannels, pos, eventTime - pos);
            pos = eventTime;
        }

        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getFloatVelocity() > 0.0f)
            multi.routeNoteOn(msg.getChannel(), msg.getNoteNumber(), msg.getFloatVelocity(),
                              rootNote);
        else if (msg.isNoteOff() || msg.isNoteOn()) // note-on with velocity 0 is a note-off
            multi.routeNoteOff(msg.getChannel(), msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            multi.reset();
    }

    if (pos < numSamples)
        multi.renderNextBlock(channels, engineChannels, pos, numSamples - pos);

    const float gainLinear = juce::Decibels::decibelsToGain(gainParam->load(), -60.0f);
    buffer.applyGain(gainLinear);
}

void ColdAudioProcessor::rebuildTrackSamples()
{
    // Layering: every track is fed the whole bank as its sample-select list (so an
    // LFO/mod -> sample-select can roam across the loaded slots at each trig).
    // Track t's base index is slot t, so with no modulation track t still plays
    // slot t exactly. Each slot carries its own [start, length) window; empty slots
    // become invalid refs (silent if ever selected).
    const int numSlots = bank.numSlots();
    std::vector<dsp::Engine::SampleRef> refs(static_cast<size_t>(numSlots));

    for (int k = 0; k < numSlots; ++k)
    {
        const auto& s = bank.slot(k);
        auto& ptrs = slotPtrs[static_cast<size_t>(k)];
        ptrs.clear();

        if (!s.isLoaded())
        {
            slotNumFrames[static_cast<size_t>(k)] = 0;
            continue; // leave refs[k] as a default (invalid) SampleRef
        }

        const int start = s.effectiveStart();
        const int length = s.effectiveLength();
        for (const auto& ch : s.data.channels)
            ptrs.push_back(ch.data() + start);
        slotNumFrames[static_cast<size_t>(k)] = length;

        refs[static_cast<size_t>(k)] = {ptrs.data(), s.data.numChannels(), length,
                                        s.data.sampleRate};
    }

    for (int t = 0; t < dsp::MultiEngine::kNumTracks; ++t)
    {
        auto& e = multi.track(t);
        e.setSampleList(refs);
        e.setBaseSampleIndex(t); // track t defaults to slot t
    }
    lastPolyphony = -1; // setSampleList reset the pools; force a param re-apply
}

bool ColdAudioProcessor::loadSampleToSlot(int slot, const juce::File& file)
{
    const juce::ScopedLock sl(engineLock);
    if (!bank.loadSlot(slot, file))
        return false;
    // Any slot is part of every track's sample-select list, so rebuild all tracks.
    rebuildTrackSamples();
    return true;
}

bool ColdAudioProcessor::loadSampleFile(const juce::File& file)
{
    return loadSampleToSlot(selectedTrack(), file);
}

int ColdAudioProcessor::loadFolder(const juce::File& directory)
{
    if (!directory.isDirectory())
        return 0;

    auto files = directory.findChildFiles(juce::File::findFiles, false, "*.wav;*.aiff;*.aif;*.mp3");
    files.sort();

    int loaded = 0;
    for (const auto& file : files)
    {
        if (loaded >= bank.numSlots())
            break;
        if (loadSampleToSlot(loaded, file))
            ++loaded;
    }
    return loaded;
}

void ColdAudioProcessor::selectTrack(int track)
{
    track = juce::jlimit(0, dsp::MultiEngine::kNumTracks - 1, track);
    if (auto* p = apvts.getParameter(kTrackId))
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(track)));
}

int ColdAudioProcessor::selectedTrack() const
{
    return trackParam != nullptr ? static_cast<int>(trackParam->load()) : 0;
}

int ColdAudioProcessor::numSlots() const
{
    return bank.numSlots();
}

juce::String ColdAudioProcessor::loadedSampleName() const
{
    const auto& s = bank.slot(selectedTrack()); // bank slot t feeds track t
    return s.isLoaded() ? s.name : juce::String("(no sample)");
}

juce::AudioProcessorEditor* ColdAudioProcessor::createEditor()
{
    return new ColdAudioProcessorEditor(*this);
}

void ColdAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Wrapper: the APVTS params and the bank live as sibling children so the bank
    // never leaks into (and duplicates within) the parameter tree across saves.
    juce::ValueTree root("ColdState");
    root.appendChild(apvts.copyState(), nullptr);

    juce::ValueTree bankTree("bank");
    for (int i = 0; i < bank.numSlots(); ++i)
    {
        const auto& s = bank.slot(i);
        if (!s.isLoaded() || s.path.isEmpty())
            continue;
        juce::ValueTree slotTree("slot");
        slotTree.setProperty("index", i, nullptr);
        slotTree.setProperty("path", s.path, nullptr);
        slotTree.setProperty("start", s.start, nullptr);
        slotTree.setProperty("length", s.length, nullptr);
        bankTree.appendChild(slotTree, nullptr);
    }
    root.appendChild(bankTree, nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary(*xml, destData);
}

void ColdAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto root = juce::ValueTree::fromXml(*xml);
    if (!root.isValid())
        return;

    if (auto params = root.getChildWithName(apvts.state.getType()); params.isValid())
        apvts.replaceState(params);

    if (auto bankTree = root.getChildWithName("bank"); bankTree.isValid())
    {
        for (int c = 0; c < bankTree.getNumChildren(); ++c)
        {
            const auto slotTree = bankTree.getChild(c);
            const int index = slotTree.getProperty("index", -1);
            const juce::String path = slotTree.getProperty("path", juce::String()).toString();
            if (index < 0 || path.isEmpty())
                continue;
            if (bank.loadSlot(index, juce::File(path)))
            {
                bank.setSlotStart(index, slotTree.getProperty("start", 0));
                bank.setSlotLength(index, slotTree.getProperty("length", 0));
            }
        }
    }

    // Rebuild every track's sample-select list from the restored bank.
    const juce::ScopedLock sl(engineLock);
    rebuildTrackSamples();
}

} // namespace cold

// Entry point used by all plugin formats.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cold::ColdAudioProcessor();
}
