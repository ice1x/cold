#include "PluginEditor.h"

#include <array>
#include <vector>

namespace cold
{

using APVTS = juce::AudioProcessorValueTreeState;

namespace
{

// Interpret a UTF-8 source literal correctly (JUCE's const char* ctor is not
// UTF-8 by default, so "→" / "…" would otherwise render as mojibake).
juce::String u8(const char* s)
{
    return juce::String(juce::CharPointer_UTF8(s));
}

// A caption above a single control (knob / combo / toggle). Owns its control so
// the whole grid is a flat list of these.
class LabeledControl : public juce::Component
{
public:
    LabeledControl(const juce::String& name, std::unique_ptr<juce::Component> c)
        : control(std::move(c))
    {
        caption.setText(name, juce::dontSendNotification);
        caption.setJustificationType(juce::Justification::centred);
        caption.setFont(juce::Font(11.0f));
        caption.setColour(juce::Label::textColourId, juce::Colour(0xffb9c0cc));
        addAndMakeVisible(caption);
        addAndMakeVisible(*control);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        caption.setBounds(b.removeFromTop(14));
        control->setBounds(b.reduced(2, 0));
    }

    [[nodiscard]] juce::Component& inner() const noexcept { return *control; }

private:
    juce::Label caption;
    std::unique_ptr<juce::Component> control;
};

// Per-track / per-layer id helpers (mirror the processor's scheme).
juce::String trackId(int track, const juce::String& base)
{
    return "t" + juce::String(track) + "_" + base;
}
juce::String layerId(int track, int layer, const juce::String& base)
{
    return "t" + juce::String(track) + "_lyr" + juce::String(layer) + "_" + base;
}

} // namespace

// ============================================================================

class ColdAudioProcessorEditor::Content : public juce::Component
{
public:
    explicit Content(ColdAudioProcessor& p) : proc(p)
    {
        buildGlobals();
        buildTrackPanel();

        ready = true;
        setTrack(proc.selectedTrack());
    }

    void resized() override
    {
        constexpr int pad = 10;
        constexpr int cellW = 74;
        constexpr int cellH = 62;
        constexpr int headerH = 22;
        constexpr int gap = 6;

        const int width = juce::jmax(getWidth(), 320);
        const int cols = juce::jmax(1, (width - 2 * pad + gap) / (cellW + gap));

        int y = pad;
        for (auto& s : sections)
        {
            s.header->setBounds(pad, y, width - 2 * pad, headerH);
            y += headerH;

            int col = 0;
            int rowTop = y;
            for (auto* cell : s.cells)
            {
                const int x = pad + col * (cellW + gap);
                cell->setBounds(x, rowTop, cellW, cellH);
                if (++col >= cols)
                {
                    col = 0;
                    rowTop += cellH + gap;
                }
            }
            if (col != 0)
                rowTop += cellH + gap;
            y = rowTop + gap;
        }

        y += pad;
        if (y != getHeight())
            setSize(width, y); // grow so the Viewport can scroll to everything
    }

private:
    struct Section
    {
        juce::Label* header;
        std::vector<juce::Component*> cells; // LabeledControl*
    };

    // ---- control factories ---------------------------------------------------

    juce::Slider* makeSlider()
    {
        auto s = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                juce::Slider::TextBoxBelow);
        s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 66, 14);
        s->setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        auto* ptr = s.get();
        pendingControl = std::move(s);
        return ptr;
    }

    juce::ComboBox* makeCombo(const juce::String& id)
    {
        auto c = std::make_unique<juce::ComboBox>();
        if (auto* param = proc.apvts.getParameter(id))
            c->addItemList(param->getAllValueStrings(), 1);
        auto* ptr = c.get();
        pendingControl = std::move(c);
        return ptr;
    }

    juce::ToggleButton* makeToggle()
    {
        auto t = std::make_unique<juce::ToggleButton>();
        auto* ptr = t.get();
        pendingControl = std::move(t);
        return ptr;
    }

    // Wrap the last-made control in a caption and file it under `sec`.
    void place(Section& sec, const juce::String& name)
    {
        auto lc = std::make_unique<LabeledControl>(name, std::move(pendingControl));
        auto* ptr = lc.get();
        addAndMakeVisible(*ptr);
        owned.push_back(std::move(lc));
        sec.cells.push_back(ptr);
    }

    Section& newSection(const juce::String& title)
    {
        auto h = std::make_unique<juce::Label>();
        h->setText(title, juce::dontSendNotification);
        h->setFont(juce::Font(13.0f, juce::Font::bold));
        h->setColour(juce::Label::textColourId, juce::Colour(0xff8fd0ff));
        auto* hp = h.get();
        addAndMakeVisible(*hp);
        owned.push_back(std::move(h));
        sections.push_back({hp, {}});
        return sections.back();
    }

    // Global knob / combo / toggle: attach once, permanently.
    void gKnob(Section& s, const juce::String& id, const juce::String& name)
    {
        auto* sl = makeSlider();
        globalAtt.slider.push_back(std::make_unique<APVTS::SliderAttachment>(proc.apvts, id, *sl));
        place(s, name);
    }
    void gCombo(Section& s, const juce::String& id, const juce::String& name)
    {
        auto* cb = makeCombo(id);
        globalAtt.combo.push_back(std::make_unique<APVTS::ComboBoxAttachment>(proc.apvts, id, *cb));
        place(s, name);
    }
    void gToggle(Section& s, const juce::String& id, const juce::String& name)
    {
        auto* tg = makeToggle();
        globalAtt.button.push_back(std::make_unique<APVTS::ButtonAttachment>(proc.apvts, id, *tg));
        place(s, name);
    }

    // ---- section construction ------------------------------------------------

    void buildGlobals()
    {
        {
            auto& s = newSection("Master");
            gKnob(s, "gain", "Gain");
            gKnob(s, "polyphony", "Poly");
            gKnob(s, "rootNote", "Root");
            gCombo(s, "midiAddressing", "MIDI");
            gKnob(s, "noteMapBase", "Map Base");
        }
        {
            auto& s = newSection("Amp Envelope");
            gKnob(s, "ampDelay", "Delay");
            gKnob(s, "ampAttack", "Attack");
            gKnob(s, "ampHold", "Hold");
            gKnob(s, "ampDecay", "Decay");
            gKnob(s, "ampSustain", "Sustain");
            gKnob(s, "ampRelease", "Release");
            gKnob(s, "ampAttackCurve", "Atk Crv");
            gKnob(s, "ampDecayCurve", "Dec Crv");
            gKnob(s, "ampReleaseCurve", "Rel Crv");
            gKnob(s, "ampVelocity", "Vel");
        }
        {
            auto& s = newSection("Mod Envelope");
            gKnob(s, "modAttack", "Attack");
            gKnob(s, "modDecay", "Decay");
            gKnob(s, "modSustain", "Sustain");
            gKnob(s, "modRelease", "Release");
            gKnob(s, "modToCutoff", u8("→Cutoff"));
            gKnob(s, "modToType", u8("→Type"));
        }
        {
            auto& s = newSection("Filter");
            gKnob(s, "filterCutoff", "Cutoff");
            gKnob(s, "filterResonance", "Reso");
            gKnob(s, "filterType", "Type");
        }
        {
            auto& s = newSection("LFO");
            gCombo(s, "lfoShape", "Shape");
            gKnob(s, "lfoRate", "Rate");
            gKnob(s, "lfoDepth", "Depth");
            gToggle(s, "lfoSync", "Sync");
            gCombo(s, "lfoDivision", "Division");
            gCombo(s, "lfoTrigMode", "Trig");
            gKnob(s, "lfoToCutoff", u8("→Cutoff"));
            gKnob(s, "lfoToType", u8("→Type"));
            gKnob(s, "lfoToPitch", u8("→Pitch"));
            gKnob(s, "lfoToSampleSelect", u8("→Sample"));
        }
        {
            auto& s = newSection("Loop");
            gCombo(s, "loopMode", "Mode");
            gKnob(s, "loopStart", "Start");
            gKnob(s, "loopEnd", "End");
        }
    }

    void buildTrackPanel()
    {
        auto& s = newSection("Track (selected)");

        // Track selector: an int param; drive the per-track re-attach on change.
        auto* sel = makeSlider();
        sel->setSliderStyle(juce::Slider::IncDecButtons);
        sel->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 40, 20);
        globalAtt.slider.push_back(
            std::make_unique<APVTS::SliderAttachment>(proc.apvts, "track", *sel));
        sel->onValueChange = [this, sel]
        {
            if (ready)
                setTrack(static_cast<int>(sel->getValue()));
        };
        place(s, "Track");

        // Layer mode is per-track; its attachment is rebuilt on track change.
        layerModeCombo = makeCombo("t0_layerMode");
        place(s, "Layer Mode");

        for (int l = 0; l < kNumLayers; ++l)
        {
            auto& row = newSection("Layer " + juce::String(l + 1));
            auto& c = layerCtl[static_cast<size_t>(l)];
            c.slot = makeSlider();
            place(row, "Slot");
            c.gain = makeSlider();
            place(row, "Gain");
            c.tune = makeSlider();
            place(row, "Tune");
            c.pan = makeSlider();
            place(row, "Pan");
            c.flt = makeToggle();
            place(row, "Filter");
            c.cutoff = makeSlider();
            place(row, "Cutoff");
            c.ftype = makeSlider();
            place(row, "F.Type");
        }
    }

    // Rebuild every per-track attachment to point at track `t`.
    void setTrack(int t)
    {
        const int track = juce::jlimit(0, dsp::MultiEngine::kNumTracks - 1, t);
        trackAtt.slider.clear();
        trackAtt.combo.clear();
        trackAtt.button.clear();

        trackAtt.combo.push_back(std::make_unique<APVTS::ComboBoxAttachment>(
            proc.apvts, trackId(track, "layerMode"), *layerModeCombo));

        for (int l = 0; l < kNumLayers; ++l)
        {
            auto& c = layerCtl[static_cast<size_t>(l)];
            trackAtt.slider.push_back(std::make_unique<APVTS::SliderAttachment>(
                proc.apvts, layerId(track, l, "slot"), *c.slot));
            trackAtt.slider.push_back(std::make_unique<APVTS::SliderAttachment>(
                proc.apvts, layerId(track, l, "gain"), *c.gain));
            trackAtt.slider.push_back(std::make_unique<APVTS::SliderAttachment>(
                proc.apvts, layerId(track, l, "tune"), *c.tune));
            trackAtt.slider.push_back(std::make_unique<APVTS::SliderAttachment>(
                proc.apvts, layerId(track, l, "pan"), *c.pan));
            trackAtt.button.push_back(std::make_unique<APVTS::ButtonAttachment>(
                proc.apvts, layerId(track, l, "filterOn"), *c.flt));
            trackAtt.slider.push_back(std::make_unique<APVTS::SliderAttachment>(
                proc.apvts, layerId(track, l, "cutoff"), *c.cutoff));
            trackAtt.slider.push_back(std::make_unique<APVTS::SliderAttachment>(
                proc.apvts, layerId(track, l, "ftype"), *c.ftype));
        }
    }

    // ---- state ---------------------------------------------------------------

    static constexpr int kNumLayers = ColdAudioProcessor::kNumLayers;

    struct LayerCtl
    {
        juce::Slider* slot = nullptr;
        juce::Slider* gain = nullptr;
        juce::Slider* tune = nullptr;
        juce::Slider* pan = nullptr;
        juce::ToggleButton* flt = nullptr;
        juce::Slider* cutoff = nullptr;
        juce::Slider* ftype = nullptr;
    };

    struct Attachments
    {
        std::vector<std::unique_ptr<APVTS::SliderAttachment>> slider;
        std::vector<std::unique_ptr<APVTS::ComboBoxAttachment>> combo;
        std::vector<std::unique_ptr<APVTS::ButtonAttachment>> button;
    };

    ColdAudioProcessor& proc;

    std::vector<std::unique_ptr<juce::Component>> owned; // labels + LabeledControls
    std::vector<Section> sections;
    std::unique_ptr<juce::Component> pendingControl; // set by makeX, consumed by place

    juce::ComboBox* layerModeCombo = nullptr;
    std::array<LayerCtl, static_cast<size_t>(kNumLayers)> layerCtl{};

    Attachments globalAtt;
    Attachments trackAtt;

    bool ready = false;
};

// ============================================================================

ColdAudioProcessorEditor::ColdAudioProcessorEditor(ColdAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    loadButton.setButtonText(u8("Load Sample…"));
    addAndMakeVisible(loadButton);
    loadButton.onClick = [this] { chooseSample(); };

    loadFolderButton.setButtonText(u8("Load Folder…"));
    addAndMakeVisible(loadFolderButton);
    loadFolderButton.onClick = [this] { chooseFolder(); };

    sampleNameLabel.setJustificationType(juce::Justification::centredLeft);
    sampleNameLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd6dbe4));
    addAndMakeVisible(sampleNameLabel);
    refreshSampleName();

    content = std::make_unique<Content>(processorRef);
    viewport.setViewedComponent(content.get(), false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);

    setSize(660, 560);
}

ColdAudioProcessorEditor::~ColdAudioProcessorEditor() = default;

void ColdAudioProcessorEditor::refreshSampleName()
{
    sampleNameLabel.setText("Track " + juce::String(processorRef.selectedTrack() + 1) + ": " +
                                processorRef.loadedSampleName(),
                            juce::dontSendNotification);
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
                             if (ok)
                                 refreshSampleName();
                             else
                                 sampleNameLabel.setText(juce::String("failed to load: ") +
                                                             file.getFileName(),
                                                         juce::dontSendNotification);
                         });
}

void ColdAudioProcessorEditor::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser>("Load a folder of samples", juce::File{});
    const auto flags =
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories;

    chooser->launchAsync(flags,
                         [this](const juce::FileChooser& fc)
                         {
                             const auto dir = fc.getResult();
                             if (dir == juce::File{})
                                 return;

                             const int n = processorRef.loadFolder(dir);
                             sampleNameLabel.setText(juce::String(n) + " sample(s) loaded — " +
                                                         processorRef.loadedSampleName(),
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
    loadButton.setBounds(top.removeFromLeft(130));
    top.removeFromLeft(6);
    loadFolderButton.setBounds(top.removeFromLeft(130));
    top.removeFromLeft(8);
    sampleNameLabel.setBounds(top);

    area.removeFromTop(8);
    viewport.setBounds(area);
    if (content != nullptr)
        content->setSize(viewport.getMaximumVisibleWidth(), content->getHeight());
}

} // namespace cold
