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

namespace
{
// True when `c` and every ancestor up to (not including) `root` are visible.
bool shownWithin(juce::Component* c, juce::Component* root)
{
    for (; c != nullptr && c != root; c = c->getParentComponent())
        if (!c->isVisible())
            return false;
    return c == root;
}

juce::Viewport* findViewport(juce::Component* c)
{
    if (auto* vp = dynamic_cast<juce::Viewport*>(c))
        return vp;
    for (auto* child : c->getChildren())
        if (auto* vp = findViewport(child))
            return vp;
    return nullptr;
}

void clickTab(juce::Component* editor, const juce::String& id)
{
    auto* tab = dynamic_cast<juce::Button*>(findById(editor, id));
    REQUIRE(tab != nullptr);
    REQUIRE(tab->onClick);
    tab->onClick();
}

const char* const kTabIds[] = {"tabLayer", "tabCellEnv", "tabTone",   "tabFX",  "tabMod",
                               "tabSends", "tabMaster",  "tabGlobal", "tabLogs"};
} // namespace

TEST_CASE("The editor fits on one screen on every tab", "[editor][e2e][onescreen]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    ColdAudioProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());

    auto* viewport = findViewport(editor.get());
    REQUIRE(viewport != nullptr);
    auto* content = viewport->getViewedComponent();
    REQUIRE(content != nullptr);

    for (auto* id : kTabIds)
    {
        clickTab(editor.get(), id);
        INFO("tab " << id << ": content " << content->getHeight() << " px, viewport "
                    << viewport->getHeight() << " px");
        REQUIRE(content->getHeight() <= viewport->getHeight()); // no vertical scroll
    }
}

TEST_CASE("Cell pads are two rows of eight", "[editor][e2e][onescreen]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    ColdAudioProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());

    auto padY = [&](int t) { return findById(editor.get(), "cellPad" + juce::String(t))->getY(); };
    for (int t = 1; t < 8; ++t)
        REQUIRE(padY(t) == padY(0));
    for (int t = 9; t < 16; ++t)
        REQUIRE(padY(t) == padY(8));
    REQUIRE(padY(8) > padY(0));
}

TEST_CASE("The cell bar stays visible on every tab", "[editor][e2e][onescreen]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    ColdAudioProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());

    for (auto* id : kTabIds)
    {
        clickTab(editor.get(), id);
        for (auto* ctl : {"layerMode", "mapNote", "chokeGroup", "level", "delaySend", "reverbSend"})
        {
            INFO("tab " << id << ", control " << ctl);
            REQUIRE(shownWithin(findById(editor.get(), ctl), editor.get()));
        }
    }

    // Its title follows the selected cell.
    dynamic_cast<juce::Button*>(findById(editor.get(), "cellPad3"))->onClick();
    auto* title = dynamic_cast<juce::Label*>(findById(editor.get(), "cellBarTitle"));
    REQUIRE(title != nullptr);
    REQUIRE(title->getText().contains("Cell 4"));
}

TEST_CASE("Each tab shows only its own group", "[editor][e2e][onescreen]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    ColdAudioProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
    auto shown = [&](const char* id)
    { return shownWithin(findById(editor.get(), id), editor.get()); };

    // Layer (the default tab): the focused layer's own envelope + filters.
    REQUIRE(shown("detailCutoff"));
    REQUIRE_FALSE(shown("ampAttack"));

    clickTab(editor.get(), "tabCellEnv"); // the cell's shared envelope, not the layer's
    REQUIRE(shown("ampAttack"));
    REQUIRE(shown("pitchEnvAmount"));
    REQUIRE_FALSE(shown("detailCutoff"));

    clickTab(editor.get(), "tabTone");
    REQUIRE(shown("filterCutoff"));
    REQUIRE(shown("humanizePitch"));

    clickTab(editor.get(), "tabFX");
    REQUIRE(shown("compThreshold"));
    REQUIRE_FALSE(shown("ampAttack"));

    clickTab(editor.get(), "tabMod");
    REQUIRE(shown("lfoRate"));

    clickTab(editor.get(), "tabSends");
    REQUIRE(shown("delayTime"));
    REQUIRE(shown("reverbRoom"));

    // ("gain"/"pan" also name the layer-strip knobs, so globals are probed by
    // ids that are unique in the editor.)
    clickTab(editor.get(), "tabMaster");
    REQUIRE(shown("limiterCeiling"));
    REQUIRE_FALSE(shown("polyphony"));

    clickTab(editor.get(), "tabGlobal");
    REQUIRE(shown("polyphony"));
    REQUIRE(shown("midiAddressing"));
    REQUIRE(shown("pitchBendRange"));
}

TEST_CASE("Switches are readable ON/OFF buttons", "[editor][e2e][onescreen]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    ColdAudioProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());

    clickTab(editor.get(), "tabFX");
    auto* comp = dynamic_cast<juce::TextButton*>(findById(editor.get(), "compOn"));
    REQUIRE(comp != nullptr);
    REQUIRE(comp->getButtonText() == "OFF");
    comp->setToggleState(true, juce::sendNotificationSync);
    REQUIRE(comp->getButtonText() == "ON");
    REQUIRE(param(proc, "t0_compOn") > 0.5f);
}

TEST_CASE("Every knob shows its value in musician units", "[editor][e2e][onescreen]")
{
    juce::ScopedJuceInitialiser_GUI gui;
    ColdAudioProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
    auto text = [&](const char* id, double v)
    {
        auto* s = dynamic_cast<juce::Slider*>(findById(editor.get(), id));
        REQUIRE(s != nullptr);
        return s->getTextFromValue(v);
    };
    auto parse = [&](const char* id, const char* t)
    { return dynamic_cast<juce::Slider*>(findById(editor.get(), id))->getValueFromText(t); };

    // Cell bar.
    REQUIRE(text("level", 0.0) == "0.0 dB");
    REQUIRE(text("delaySend", 0.25) == "25%");
    // Times (seconds params) read in ms / s and parse back.
    REQUIRE(text("ampAttack", 0.001) == "1 ms");
    REQUIRE(text("ampAttack", 0.0) == "0 ms");
    REQUIRE(text("ampRelease", 1.2) == "1.20 s");
    REQUIRE_THAT(parse("ampAttack", "12 ms"), WithinAbs(0.012, 1e-9));
    REQUIRE_THAT(parse("ampAttack", "1.5 s"), WithinAbs(1.5, 1e-9));
    // Frequencies.
    REQUIRE(text("detailCutoff", 700.0) == "700 Hz");
    REQUIRE(text("detailCutoff", 20000.0) == "20.0 kHz");
    REQUIRE_THAT(parse("detailCutoff", "1.5 kHz"), WithinAbs(1500.0, 1e-6));
    // Levels, ratios, pitch and modulation depths.
    REQUIRE(text("ampSustain", 1.0) == "100%");
    REQUIRE(text("compRatio", 4.0) == "4.0:1");
    REQUIRE(text("compThreshold", -12.0) == "-12.0 dB");
    REQUIRE(text("pitchEnvAmount", 12.0) == "+12.0 st");
    REQUIRE(text("velToCutoff", -2.0) == "-2.0 oct");
    // No negative zero anywhere.
    REQUIRE(text("ampAttackCurve", -0.0001) == "0.00");
    REQUIRE(text("lfoToType", -0.0001) == "0.00");
    REQUIRE(text("pitchEnvAmount", -0.0001) == "0 st");
}
