#include "PluginEditor.h"

namespace cold
{

ColdAudioProcessorEditor::ColdAudioProcessorEditor(ColdAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    addAndMakeVisible(loadButton);
    loadButton.onClick = [this] { chooseSample(); };

    sampleNameLabel.setJustificationType(juce::Justification::centredLeft);
    sampleNameLabel.setText(processorRef.loadedSampleName(), juce::dontSendNotification);
    addAndMakeVisible(sampleNameLabel);

    addAndMakeVisible(genericEditor);

    setSize(460, 420);
}

void ColdAudioProcessorEditor::chooseSample()
{
    chooser = std::make_unique<juce::FileChooser>("Load a sample", juce::File{},
                                                  "*.wav;*.aiff;*.aif;*.mp3");
    const auto flags =
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(flags,
                         [this](const juce::FileChooser& fc)
                         {
                             const auto file = fc.getResult();
                             if (file == juce::File{})
                                 return;

                             const bool ok = processorRef.loadSampleFile(file);
                             sampleNameLabel.setText(ok ? processorRef.loadedSampleName()
                                                        : juce::String("failed to load: ") +
                                                              file.getFileName(),
                                                     juce::dontSendNotification);
                         });
}

void ColdAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff101216));
}

void ColdAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(8);

    auto top = area.removeFromTop(28);
    loadButton.setBounds(top.removeFromLeft(140));
    top.removeFromLeft(8);
    sampleNameLabel.setBounds(top);

    area.removeFromTop(8);
    genericEditor.setBounds(area);
}

} // namespace cold
