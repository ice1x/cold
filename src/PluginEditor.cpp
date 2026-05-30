#include "PluginEditor.h"

namespace cold
{

ColdAudioProcessorEditor::ColdAudioProcessorEditor(ColdAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    setSize(400, 220);
}

void ColdAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff101216));
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    g.drawFittedText("COLD", getLocalBounds().removeFromTop(140), juce::Justification::centred, 1);

    g.setColour(juce::Colours::grey);
    g.setFont(juce::FontOptions(13.0f));
    g.drawFittedText("polyphonic sampler - scaffold (M0)",
                     getLocalBounds().removeFromBottom(80), juce::Justification::centred, 1);
}

void ColdAudioProcessorEditor::resized() {}

} // namespace cold
