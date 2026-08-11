#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace cold
{

// Custom sectioned editor: a fixed top bar (Load buttons + sample name) over a
// scrollable panel of grouped knobs/combos. Global sound params (amp/mod env,
// filter, LFO, loop, master) attach once; the per-track panel (layer mode + the
// four layers) re-attaches to the selected track when the Track selector changes.
class ColdAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit ColdAudioProcessorEditor(ColdAudioProcessor&);
    ~ColdAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void chooseSample();
    void chooseFolder();
    void refreshSampleName();

    class Content; // the scrolled panel; defined in the .cpp

    ColdAudioProcessor& processorRef;

    juce::TextButton loadButton;
    juce::TextButton loadFolderButton;
    juce::Label sampleNameLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Viewport viewport;
    std::unique_ptr<Content> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ColdAudioProcessorEditor)
};

} // namespace cold
