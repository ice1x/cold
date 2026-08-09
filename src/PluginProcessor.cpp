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
    layout.add(std::make_unique<AudioParameterFloat>(
        juce::ParameterID{kLfoToCutoffId, 1}, "LFO -> Cutoff",
        juce::NormalisableRange<float>(-4.0f, 4.0f, 0.01f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(
        juce::ParameterID{kLfoToTypeId, 1}, "LFO -> Filter Type",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));

    return layout;
}

ColdAudioProcessor::ColdAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParameterLayout())
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
}

void ColdAudioProcessor::prepareToPlay(double sampleRate, int)
{
    const juce::ScopedLock sl(engineLock);
    engine.prepare(sampleRate);
    lastPolyphony = -1; // force a re-apply of polyphony on the next block
    updateEngineParameters();
}

void ColdAudioProcessor::releaseResources() {}

bool ColdAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void ColdAudioProcessor::updateEngineParameters()
{
    const int poly = static_cast<int>(polyphonyParam->load());
    if (poly != lastPolyphony)
    {
        engine.setPolyphony(poly);
        lastPolyphony = poly;
    }

    const auto mode =
        static_cast<dsp::SampleVoice::LoopMode>(static_cast<int>(loopModeParam->load()));
    engine.setLoopMode(mode);

    if (currentNumFrames > 0)
    {
        const int start =
            static_cast<int>(loopStartParam->load() * static_cast<float>(currentNumFrames));
        const int end =
            static_cast<int>(loopEndParam->load() * static_cast<float>(currentNumFrames));
        if (end > start)
            engine.setLoopPoints(start, end);
    }

    engine.setAmpEnvelope(attackParam->load(), decayParam->load(), sustainParam->load(),
                          releaseParam->load());

    engine.setFilter(filterCutoffParam->load(), filterResonanceParam->load(),
                     filterTypeParam->load());

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
    engine.setLfo(shape, rateHz, lfoDepthParam->load());

    // Rebuild the (small) route set from the two mod-amount params.
    engine.clearModRoutes();
    if (std::abs(lfoToCutoffParam->load()) > 1.0e-4f)
        engine.addModRoute(dsp::ModDestination::FilterCutoff, lfoToCutoffParam->load());
    if (std::abs(lfoToTypeParam->load()) > 1.0e-4f)
        engine.addModRoute(dsp::ModDestination::FilterType, lfoToTypeParam->load());
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
    int pos = 0;
    for (const auto meta : midi)
    {
        const int eventTime = juce::jlimit(0, numSamples, meta.samplePosition);
        if (eventTime > pos)
        {
            engine.renderNextBlock(channels, engineChannels, pos, eventTime - pos);
            pos = eventTime;
        }

        const auto msg = meta.getMessage();
        if (msg.isNoteOn() && msg.getFloatVelocity() > 0.0f)
            engine.noteOn(msg.getNoteNumber(), msg.getFloatVelocity(), rootNote);
        else if (msg.isNoteOff() || msg.isNoteOn()) // note-on with velocity 0 is a note-off
            engine.noteOff(msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            engine.reset();
    }

    if (pos < numSamples)
        engine.renderNextBlock(channels, engineChannels, pos, numSamples - pos);

    const float gainLinear = juce::Decibels::decibelsToGain(gainParam->load(), -60.0f);
    buffer.applyGain(gainLinear);
}

bool ColdAudioProcessor::loadSampleFile(const juce::File& file)
{
    auto decoded = loader.loadFile(file);
    if (!decoded.has_value() || decoded->isEmpty())
        return false;

    const juce::ScopedLock sl(engineLock);
    sampleData = std::move(*decoded);

    channelPtrs.clear();
    for (const auto& ch : sampleData.channels)
        channelPtrs.push_back(ch.data());
    currentNumFrames = sampleData.numFrames();
    sampleFilePath = file.getFullPathName();

    engine.setSample(channelPtrs.data(), sampleData.numChannels(), currentNumFrames,
                     sampleData.sampleRate);
    // setSample cleared the loop window; re-apply the current parameter state.
    lastPolyphony = -1;
    updateEngineParameters();
    return true;
}

juce::String ColdAudioProcessor::loadedSampleName() const
{
    return sampleFilePath.isEmpty() ? juce::String("(no sample)")
                                    : juce::File(sampleFilePath).getFileName();
}

juce::AudioProcessorEditor* ColdAudioProcessor::createEditor()
{
    return new ColdAudioProcessorEditor(*this);
}

void ColdAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (sampleFilePath.isNotEmpty())
        state.setProperty("samplePath", sampleFilePath, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void ColdAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto tree = juce::ValueTree::fromXml(*xml);
    if (!tree.isValid())
        return;

    const juce::String path = tree.getProperty("samplePath", juce::String()).toString();
    apvts.replaceState(tree);

    if (path.isNotEmpty())
        loadSampleFile(juce::File(path));
}

} // namespace cold

// Entry point used by all plugin formats.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cold::ColdAudioProcessor();
}
