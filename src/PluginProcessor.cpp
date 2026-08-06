#include "PluginProcessor.h"

#include "PluginEditor.h"

#include "dsp/SampleVoice.h"

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
}

void ColdAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const juce::ScopedLock sl(engineLock);

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

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
