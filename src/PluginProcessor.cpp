#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace cold
{

ColdAudioProcessor::ColdAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void ColdAudioProcessor::prepareToPlay(double, int) {}

void ColdAudioProcessor::releaseResources() {}

bool ColdAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void ColdAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    // M0: silent instrument. The sampler engine renders here from M1.
    buffer.clear();
}

juce::AudioProcessorEditor* ColdAudioProcessor::createEditor()
{
    return new ColdAudioProcessorEditor(*this);
}

void ColdAudioProcessor::getStateInformation(juce::MemoryBlock&) {}

void ColdAudioProcessor::setStateInformation(const void*, int) {}

} // namespace cold

// Entry point used by all plugin formats.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cold::ColdAudioProcessor();
}
