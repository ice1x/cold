#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace cold
{

// Placeholder editor (M0). A real UI arrives with the sampler params (M1+).
class ColdAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit ColdAudioProcessorEditor(ColdAudioProcessor&);
    ~ColdAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    [[maybe_unused]] ColdAudioProcessor& processorRef;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ColdAudioProcessorEditor)
};

} // namespace cold
