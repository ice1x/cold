#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace cold
{

// Minimal M1 UI: a "Load Sample…" button + the loaded file name on top, with
// JUCE's generic parameter panel (auto-built from the APVTS) filling the rest.
class ColdAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit ColdAudioProcessorEditor(ColdAudioProcessor&);
    ~ColdAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void chooseSample();

    ColdAudioProcessor& processorRef;

    juce::TextButton loadButton{"Load Sample…"};
    juce::Label sampleNameLabel;
    juce::GenericAudioProcessorEditor genericEditor{processorRef};
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ColdAudioProcessorEditor)
};

} // namespace cold
