#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace cold
{

// Custom sectioned editor: a fixed top bar (Load Folder + sample name) over a
// scrollable panel of grouped knobs/combos. Per-slot loading lives in the Sample
// Bank section. Global sound params (amp/mod env, filter, LFO, loop, master)
// attach once; the per-track panel (layer mode + the four layers) re-attaches to
// the selected track when the Track selector changes.
class ColdAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit ColdAudioProcessorEditor(ColdAudioProcessor&);
    ~ColdAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void chooseFolder();
    void chooseKit();
    void refreshSampleName();

    class Content; // the scrolled panel; defined in the .cpp

    ColdAudioProcessor& processorRef;

    juce::TextButton loadFolderButton;
    juce::TextButton importKitButton;
    juce::Label sampleNameLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    // Battery-style knob look, applied to the whole editor (children inherit it).
    // Must outlive the components and be cleared in the destructor.
    std::unique_ptr<juce::LookAndFeel> knobLookAndFeel;

    juce::Viewport viewport;
    std::unique_ptr<Content> content;
    juce::TooltipWindow tooltipWindow{this, 400}; // hover help for every control

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ColdAudioProcessorEditor)
};

} // namespace cold
