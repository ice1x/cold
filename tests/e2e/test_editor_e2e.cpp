// End-to-end tests for the editor's layer strips: the selected cell's four layers
// are shown at once, each strip edits its own layer, and strips follow the cell.
// Controls are found by componentID so the tests do not depend on pixel layout.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "PluginProcessor.h"
#include "support/SignalFixtures.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

using Catch::Matchers::WithinAbs;
using cold::ColdAudioProcessor;

namespace
{

juce::Component* findById(juce::Component* root, const juce::String& id)
{
    if (root == nullptr)
        return nullptr;
    if (root->getComponentID() == id)
        return root;
    for (auto* child : root->getChildren())
        if (auto* found = findById(child, id))
            return found;
    return nullptr;
}

// A control inside layer strip `layer` (e.g. "gain", "mute", "load").
template <typename T> T* stripControl(juce::Component* editor, int layer, const juce::String& id)
{
    auto* strip = findById(editor, "layerStrip" + juce::String(layer));
    return dynamic_cast<T*>(findById(strip, id));
}

float param(ColdAudioProcessor& proc, const juce::String& id)
{
    auto* p = proc.apvts.getRawParameterValue(id);
    REQUIRE(p != nullptr);
    return p->load();
}

juce::File writeTempWav()
{
    auto file = juce::File::createTempFile(".wav");
    juce::WavAudioFormat wav;
    cold::test::writeKnownSignal(wav, file, 48000.0, 1, 2048);
    return file;
}

} // namespace

TEST_CASE("Cold editor shows all four layer strips at once", "[editor][e2e][strips]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    ColdAudioProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());

    for (int l = 0; l < ColdAudioProcessor::kNumLayers; ++l)
    {
        auto* strip = findById(editor.get(), "layerStrip" + juce::String(l));
        REQUIRE(strip != nullptr);
        REQUIRE(strip->isVisible());
        // Each strip carries its own core controls.
        REQUIRE(stripControl<juce::Slider>(editor.get(), l, "gain") != nullptr);
        REQUIRE(stripControl<juce::Slider>(editor.get(), l, "tune") != nullptr);
        REQUIRE(stripControl<juce::Slider>(editor.get(), l, "pan") != nullptr);
        REQUIRE(stripControl<juce::Button>(editor.get(), l, "mute") != nullptr);
        REQUIRE(stripControl<juce::Button>(editor.get(), l, "solo") != nullptr);
        REQUIRE(stripControl<juce::Button>(editor.get(), l, "reverse") != nullptr);
        REQUIRE(stripControl<juce::Button>(editor.get(), l, "load") != nullptr);
    }
}

TEST_CASE("Each layer strip edits its own layer", "[editor][e2e][strips]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    ColdAudioProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());

    for (int l = 0; l < ColdAudioProcessor::kNumLayers; ++l)
    {
        const double g = 0.2 + 0.15 * l;
        stripControl<juce::Slider>(editor.get(), l, "gain")
            ->setValue(g, juce::sendNotificationSync);
        REQUIRE_THAT(param(proc, "t0_lyr" + juce::String(l) + "_gain"), WithinAbs(g, 1e-3));
    }

    // Tune on layer 3 only moves layer 3.
    stripControl<juce::Slider>(editor.get(), 2, "tune")->setValue(7.0, juce::sendNotificationSync);
    REQUIRE_THAT(param(proc, "t0_lyr2_tune"), WithinAbs(7.0, 1e-3));
    REQUIRE_THAT(param(proc, "t0_lyr0_tune"), WithinAbs(0.0, 1e-3));
    REQUIRE_THAT(param(proc, "t0_lyr1_tune"), WithinAbs(0.0, 1e-3));

    // The toggles are real, per-layer switches.
    stripControl<juce::Button>(editor.get(), 1, "mute")
        ->setToggleState(true, juce::sendNotificationSync);
    stripControl<juce::Button>(editor.get(), 3, "solo")
        ->setToggleState(true, juce::sendNotificationSync);
    stripControl<juce::Button>(editor.get(), 0, "reverse")
        ->setToggleState(true, juce::sendNotificationSync);
    REQUIRE(param(proc, "t0_lyr1_mute") > 0.5f);
    REQUIRE(param(proc, "t0_lyr0_mute") < 0.5f);
    REQUIRE(param(proc, "t0_lyr3_solo") > 0.5f);
    REQUIRE(param(proc, "t0_lyr0_reverse") > 0.5f);
}

TEST_CASE("Layer strips follow the selected cell", "[editor][e2e][strips]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    ColdAudioProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());

    auto* pad = dynamic_cast<juce::Button*>(findById(editor.get(), "cellPad3"));
    REQUIRE(pad != nullptr);
    REQUIRE(pad->onClick);
    pad->onClick(); // select cell 4

    stripControl<juce::Slider>(editor.get(), 1, "gain")->setValue(0.4, juce::sendNotificationSync);
    REQUIRE_THAT(param(proc, "t3_lyr1_gain"), WithinAbs(0.4, 1e-3));
    REQUIRE_THAT(param(proc, "t0_lyr1_gain"), WithinAbs(1.0, 1e-3)); // cell 1 untouched
}

TEST_CASE("A layer strip shows its own sample", "[editor][e2e][strips]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    ColdAudioProcessor proc;
    auto wav = writeTempWav();
    REQUIRE(proc.loadSampleToLayer(0, 1, wav));
    wav.deleteFile();

    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());

    const auto name = proc.slotSampleName(1); // cell 0, layer 1
    REQUIRE(name.isNotEmpty());
    REQUIRE(stripControl<juce::Button>(editor.get(), 1, "load")->getButtonText() == name);
    // An empty layer invites a load instead.
    REQUIRE(stripControl<juce::Button>(editor.get(), 2, "load")->getButtonText().contains("Load"));
}

TEST_CASE("Layer strip knobs show readable values", "[editor][e2e][strips]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    ColdAudioProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());

    auto* gain = stripControl<juce::Slider>(editor.get(), 0, "gain");
    auto* tune = stripControl<juce::Slider>(editor.get(), 0, "tune");
    auto* pan = stripControl<juce::Slider>(editor.get(), 0, "pan");

    // Gain in dB, tune in signed semitones, pan as C / L / R — no "-0.00".
    REQUIRE(gain->getTextFromValue(1.0) == "0.0 dB");
    REQUIRE(gain->getTextFromValue(0.5) == "-6.0 dB");
    REQUIRE(gain->getTextFromValue(0.0) == "-inf dB");
    REQUIRE(tune->getTextFromValue(0.0) == "0 st");
    REQUIRE(tune->getTextFromValue(-0.0001) == "0 st");
    REQUIRE(tune->getTextFromValue(5.0) == "+5.0 st");
    REQUIRE(tune->getTextFromValue(-12.0) == "-12.0 st");
    REQUIRE(pan->getTextFromValue(0.0) == "C");
    REQUIRE(pan->getTextFromValue(-0.0001) == "C");
    REQUIRE(pan->getTextFromValue(-0.5) == "L50");
    REQUIRE(pan->getTextFromValue(1.0) == "R100");

    // Typed values parse back the same way.
    REQUIRE_THAT(gain->getValueFromText("-6 dB"), WithinAbs(0.501, 1e-3));
    REQUIRE_THAT(tune->getValueFromText("+7 st"), WithinAbs(7.0, 1e-6));
    REQUIRE_THAT(pan->getValueFromText("L25"), WithinAbs(-0.25, 1e-6));
    REQUIRE_THAT(pan->getValueFromText("C"), WithinAbs(0.0, 1e-6));

    // The formatting survives switching cells (the strips re-attach).
    auto* pad = dynamic_cast<juce::Button*>(findById(editor.get(), "cellPad2"));
    pad->onClick();
    REQUIRE(stripControl<juce::Slider>(editor.get(), 0, "pan")->getTextFromValue(0.0) == "C");
}

TEST_CASE("Focusing a layer strip shows that layer's details", "[editor][e2e][strips]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    ColdAudioProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());

    auto* header = dynamic_cast<juce::Label*>(findById(editor.get(), "layerDetailsHeader"));
    REQUIRE(header != nullptr);
    REQUIRE(header->getText().contains("Layer 1"));

    auto* focus = stripControl<juce::Button>(editor.get(), 2, "focus");
    REQUIRE(focus != nullptr);
    REQUIRE(focus->onClick);
    focus->onClick(); // focus layer 3

    REQUIRE(header->getText().contains("Layer 3"));
    auto* cutoff = dynamic_cast<juce::Slider*>(findById(editor.get(), "detailCutoff"));
    REQUIRE(cutoff != nullptr);
    cutoff->setValue(1234.0, juce::sendNotificationSync);
    REQUIRE_THAT(param(proc, "t0_lyr2_cutoff"), WithinAbs(1234.0, 1.0));
}
