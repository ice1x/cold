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
    LabeledControl(const juce::String& name, std::unique_ptr<juce::Component> c,
                   const juce::String& tooltip = {})
        : control(std::move(c))
    {
        if (tooltip.isNotEmpty())
            if (auto* client = dynamic_cast<juce::SettableTooltipClient*>(control.get()))
                client->setTooltip(tooltip);
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
        buildCellGrid();
        buildGlobals();
        buildTrackPanel();

        selectCell(proc.selectedTrack()); // initial attachments + pad highlight
    }

    void resized() override
    {
        constexpr int pad = 10;
        constexpr int cellH = 62;
        constexpr int headerH = 22;
        constexpr int gap = 6;

        const int width = juce::jmax(getWidth(), 320);

        int y = pad;
        for (auto& s : sections)
        {
            s.header->setBounds(pad, y, width - 2 * pad, headerH);
            y += headerH;

            // Wide sections (the sample bank) use 4 stretched columns of short
            // rows; knob sections use a fixed-size cell grid.
            const int cellW = s.wideCells ? (width - 2 * pad - 3 * gap) / 4 : 74;
            const int rowH = s.wideCells ? 24 : cellH;
            const int cols =
                s.wideCells ? 4 : juce::jmax(1, (width - 2 * pad + gap) / (cellW + gap));

            int col = 0;
            int rowTop = y;
            for (auto* cell : s.cells)
            {
                const int x = pad + col * (cellW + gap);
                cell->setBounds(x, rowTop, cellW, rowH);
                if (++col >= cols)
                {
                    col = 0;
                    rowTop += rowH + gap;
                }
            }
            if (col != 0)
                rowTop += rowH + gap;
            y = rowTop + gap;
        }

        y += pad;
        if (y != getHeight())
            setSize(width, y); // grow so the Viewport can scroll to everything
    }

    // Refresh the bank slot buttons (call after any sample load).
    void refreshBank()
    {
        for (int k = 0; k < static_cast<int>(bankButtons.size()); ++k)
        {
            const auto name = proc.slotSampleName(k);
            bankButtons[static_cast<size_t>(k)]->setButtonText(
                juce::String(k + 1) + ": " + (name.isEmpty() ? juce::String("(empty)") : name));
        }
        refreshCellPads(); // a slot load changes the cell's default sample name
    }

private:
    struct Section
    {
        juce::Label* header;
        std::vector<juce::Component*> cells; // LabeledControl* (or raw buttons if wide)
        bool wideCells = false;              // stretched short rows (the sample bank)
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

    // Wrap the last-made control in a caption (+ hover tooltip) under `sec`.
    void place(Section& sec, const juce::String& name, const juce::String& tooltip = {})
    {
        auto lc = std::make_unique<LabeledControl>(name, std::move(pendingControl), tooltip);
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
    void gKnob(Section& s, const juce::String& id, const juce::String& name,
               const juce::String& tip = {})
    {
        auto* sl = makeSlider();
        globalAtt.slider.push_back(std::make_unique<APVTS::SliderAttachment>(proc.apvts, id, *sl));
        place(s, name, tip);
    }
    void gCombo(Section& s, const juce::String& id, const juce::String& name,
                const juce::String& tip = {})
    {
        auto* cb = makeCombo(id);
        globalAtt.combo.push_back(std::make_unique<APVTS::ComboBoxAttachment>(proc.apvts, id, *cb));
        place(s, name, tip);
    }
    void gToggle(Section& s, const juce::String& id, const juce::String& name,
                 const juce::String& tip = {})
    {
        auto* tg = makeToggle();
        globalAtt.button.push_back(std::make_unique<APVTS::ButtonAttachment>(proc.apvts, id, *tg));
        place(s, name, tip);
    }

    // Per-track knob: the control persists, but its attachment is (re)built for the
    // selected track in setTrack(). `base` is the un-prefixed id (e.g. "ampAttack").
    void tKnob(Section& s, const juce::String& base, const juce::String& name,
               const juce::String& tip = {})
    {
        auto* sl = makeSlider();
        perTrackKnobs.push_back({sl, base});
        place(s, name, tip);
    }

    // ---- section construction ------------------------------------------------

    void buildGlobals()
    {
        {
            auto& s = newSection("Master");
            gKnob(s, "gain", "Gain", "Output gain in dB, applied to the mix of all 8 tracks.");
            gKnob(s, "polyphony", "Poly",
                  "Voices per track. 1 = mono (Digitakt-style); higher lets notes overlap "
                  "and chords ring.");
            gKnob(s, "rootNote", "Root",
                  "MIDI note that plays a sample at its natural pitch. Other notes repitch "
                  "it varispeed-style.");
            gCombo(s, "midiAddressing", "MIDI",
                   "How MIDI picks a track. Channel: MIDI channel N plays track N. "
                   "Note Map: one channel, the note number picks the track (GM-drum style).");
            gKnob(s, "noteMapBase", "Map Base",
                  "Note Map only: this note plays track 1, the next note track 2, ... "
                  "(8 notes total). 36 = C1, the GM kick.");
        }
        buildBank();
        {
            auto& s = newSection("Amp Envelope (selected track)");
            tKnob(s, "ampDelay", "Delay", "Silence before the attack starts, in seconds (DAHDSR).");
            tKnob(s, "ampAttack", "Attack", "Fade-in time, in seconds.");
            tKnob(s, "ampHold", "Hold",
                  "Time held at full level between attack and decay, in seconds.");
            tKnob(s, "ampDecay", "Decay",
                  "Time to fall from full level to the sustain level, in seconds.");
            tKnob(s, "ampSustain", "Sustain", "Level held while the key is down (0..1).");
            tKnob(s, "ampRelease", "Release", "Fade-out time after note-off, in seconds.");
            tKnob(s, "ampAttackCurve", "Atk Crv",
                  "Attack curve shape: 0 = linear, negative = fast start that eases in "
                  "(log), positive = slow start that accelerates (exp).");
            tKnob(s, "ampDecayCurve", "Dec Crv",
                  "Decay curve shape: 0 = linear, negative = log, positive = exp.");
            tKnob(s, "ampReleaseCurve", "Rel Crv",
                  "Release curve shape: 0 = linear, negative = log, positive = exp.");
            tKnob(s, "ampVelocity", "Vel",
                  "Velocity sensitivity: 0 = every note at full level, 1 = level fully "
                  "follows how hard you hit the key.");
        }
        {
            auto& s = newSection("Mod Envelope");
            gKnob(s, "modAttack", "Attack",
                  "Second envelope that shapes the FILTER over each note. Attack time in "
                  "seconds.");
            gKnob(s, "modDecay", "Decay", "Mod envelope decay time, in seconds.");
            gKnob(s, "modSustain", "Sustain", "Mod envelope sustain level (0..1).");
            gKnob(s, "modRelease", "Release", "Mod envelope release time, in seconds.");
            gKnob(s, "modToCutoff", u8("→Cutoff"),
                  "How far the mod envelope opens the filter cutoff, in octaves at full "
                  "envelope. Negative closes it.");
            gKnob(s, "modToType", u8("→Type"),
                  "How far the mod envelope shifts the filter morph type "
                  "(LP→BP→HP→Notch) over the note.");
        }
        {
            auto& s = newSection("Filter (selected cell)");
            tKnob(s, "filterCutoff", "Cutoff", "This cell's filter cutoff frequency, in Hz.");
            tKnob(s, "filterResonance", "Reso",
                  "Filter resonance (Q): emphasis at the cutoff frequency.");
            tKnob(s, "filterType", "Type",
                  "Morphing filter character: 0 = low-pass, 0.33 = band-pass, "
                  "0.66 = high-pass, 1 = notch — everything in between blends.");
        }
        {
            auto& s = newSection("LFO");
            gCombo(s, "lfoShape", "Shape",
                   "LFO waveform. Random = stepped sample & hold; Custom = the "
                   "multi-step designer shape.");
            gKnob(s, "lfoRate", "Rate", "LFO speed in Hz (used when Sync is off).");
            gKnob(s, "lfoDepth", "Depth",
                  "LFO intensity (0..1). Scales every →destination below; 0 turns the "
                  "LFO off.");
            gToggle(s, "lfoSync", "Sync",
                    "Lock the LFO to the host tempo: the Division below replaces Rate.");
            gCombo(s, "lfoDivision", "Division",
                   "LFO cycle length in musical time (bars/beats) when Sync is on.");
            gCombo(s, "lfoTrigMode", "Trig",
                   "How notes affect the LFO. Free: runs continuously. Trig: restarts "
                   "on every note. Hold: freezes its value at each note. One-Shot: runs "
                   "a single cycle per note.");
            gKnob(s, "lfoToCutoff", u8("→Cutoff"),
                  "Assign the LFO to filter cutoff: depth in octaves. 0 = not assigned "
                  "(this is how LFO routing works here — turn a →knob instead of "
                  "picking a destination).");
            gKnob(s, "lfoToType", u8("→Type"),
                  "Assign the LFO to the filter morph type (sweeps LP→BP→HP→Notch).");
            gKnob(s, "lfoToPitch", u8("→Pitch"),
                  "Assign the LFO to pitch: depth in semitones (vibrato / sirens).");
            gKnob(s, "lfoToSampleSelect", u8("→Sample"),
                  "Assign the LFO to sample-select: at each note the LFO offsets which "
                  "bank slot plays (per-trig sample-lock). Depth = how many slots away "
                  "it can roam.");
        }
        {
            auto& s = newSection("Loop");
            gCombo(s, "loopMode", "Mode",
                   "One-Shot: play once. Forward: repeat the loop region. Ping-Pong: "
                   "bounce back and forth inside it. Loop speed follows pitch.");
            gKnob(s, "loopStart", "Start", "Loop region start, as a fraction of the sample.");
            gKnob(s, "loopEnd", "End", "Loop region end, as a fraction of the sample.");
        }
    }

    // The sample bank: 16 slot buttons showing what's loaded; click = load a file
    // into that exact slot. Layers reference these slots by number.
    void buildBank()
    {
        auto& s = newSection("Sample Bank (click a slot to load; layers reference slots)");
        s.wideCells = true;

        for (int k = 0; k < proc.numSlots(); ++k)
        {
            auto btn = std::make_unique<juce::TextButton>();
            btn->setTooltip("Load a sample into bank slot " + juce::String(k + 1) + ". Track " +
                            juce::String(k + 1) +
                            " plays this slot by default; any layer on any track can "
                            "reference it via its Slot knob.");
            auto* ptr = btn.get();
            ptr->onClick = [this, k] { chooseForSlot(k); };
            addAndMakeVisible(*ptr);
            bankButtons.push_back(ptr);
            owned.push_back(std::move(btn));
            s.cells.push_back(ptr);
        }
        refreshBank();
    }

    void chooseForSlot(int slot)
    {
        chooser =
            std::make_unique<juce::FileChooser>("Load a sample into slot " + juce::String(slot + 1),
                                                juce::File{}, "*.wav;*.aiff;*.aif;*.mp3");
        const auto flags =
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        chooser->launchAsync(flags,
                             [this, slot](const juce::FileChooser& fc)
                             {
                                 const auto file = fc.getResult();
                                 if (file == juce::File{})
                                     return;
                                 proc.loadSampleToSlot(slot, file);
                                 refreshBank();
                                 if (onBankChanged)
                                     onBankChanged();
                             });
    }

public:
    std::function<void()> onBankChanged; // editor hook: refresh the top-bar name

private:
    // Battery-style cell grid: one pad per track, click to pick the edited cell.
    // Replaces a track-selector knob; the pads drive the "track" param + setTrack().
    void buildCellGrid()
    {
        auto& s = newSection(u8("Cells — click a cell to edit its track"));
        s.wideCells = true;
        for (int t = 0; t < dsp::MultiEngine::kNumTracks; ++t)
        {
            auto btn = std::make_unique<juce::TextButton>();
            btn->setTooltip("Select cell/track " + juce::String(t + 1) +
                            " for editing below. It plays on MIDI channel " + juce::String(t + 1) +
                            " (Channel mode) or its Note Map note. Its "
                            "default sample is bank slot " +
                            juce::String(t + 1) + ".");
            auto* ptr = btn.get();
            ptr->onClick = [this, t] { selectCell(t); };
            addAndMakeVisible(*ptr);
            cellPads.push_back(ptr);
            owned.push_back(std::move(btn));
            s.cells.push_back(ptr);
        }
    }

    // Select which track/cell the panel edits: set the param, re-point the per-track
    // attachments, refresh the pad labels/highlight and the top-bar sample name.
    void selectCell(int t)
    {
        currentTrack = juce::jlimit(0, dsp::MultiEngine::kNumTracks - 1, t);
        if (auto* p = proc.apvts.getParameter("track"))
            p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(currentTrack)));
        setTrack(currentTrack);
        refreshCellPads();
        if (onBankChanged)
            onBankChanged();
    }

    void refreshCellPads()
    {
        for (int t = 0; t < static_cast<int>(cellPads.size()); ++t)
        {
            const auto name = proc.slotSampleName(t);
            cellPads[static_cast<size_t>(t)]->setButtonText(
                "T" + juce::String(t + 1) + ": " +
                (name.isEmpty() ? juce::String("(empty)") : name));
            cellPads[static_cast<size_t>(t)]->setColour(
                juce::TextButton::buttonColourId,
                t == currentTrack ? juce::Colour(0xff2b6cb0) : juce::Colour(0xff21252b));
        }
    }

    void buildTrackPanel()
    {
        auto& s = newSection("Selected cell");

        // Layer mode is per-track; its attachment is rebuilt on cell change.
        layerModeCombo = makeCombo("t0_layerMode");
        place(s, "Layer Mode",
              "Select: the track plays one sample per note (its own bank slot, or "
              "wherever LFO→Sample points). Stack: all enabled layers below sound "
              "TOGETHER on every note — layering.");

        for (int l = 0; l < kNumLayers; ++l)
        {
            auto& row = newSection("Layer " + juce::String(l + 1));
            auto& c = layerCtl[static_cast<size_t>(l)];
            c.slot = makeSlider();
            place(row, "Slot",
                  "Bank slot this layer plays (see Sample Bank above). -1 = layer off. "
                  "Stack mode only.");
            c.gain = makeSlider();
            place(row, "Gain", "Layer level (multiplies note velocity).");
            c.tune = makeSlider();
            place(row, "Tune", "Layer pitch offset in semitones (+/-24).");
            c.pan = makeSlider();
            place(row, "Pan", "Layer stereo position: -1 hard left .. +1 hard right.");
            c.flt = makeToggle();
            place(row, "Filter",
                  "Give this layer its OWN filter instead of the shared track filter "
                  "(e.g. low-pass only the boom layer).");
            c.cutoff = makeSlider();
            place(row, "Cutoff", "This layer's filter cutoff, in Hz (when Filter is on).");
            c.ftype = makeSlider();
            place(row, "F.Type",
                  "This layer's filter morph type: 0 = LP, 0.33 = BP, 0.66 = HP, 1 = notch.");
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

        // Per-track sound knobs (amp envelope, ...) re-point at the selected track.
        for (const auto& k : perTrackKnobs)
            trackAtt.slider.push_back(std::make_unique<APVTS::SliderAttachment>(
                proc.apvts, trackId(track, k.base), *k.ctl));

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

    // Per-track sound knobs whose attachment is rebuilt for the selected track.
    struct PerTrackKnob
    {
        juce::Slider* ctl;
        juce::String base; // un-prefixed id, e.g. "ampAttack"
    };
    std::vector<PerTrackKnob> perTrackKnobs;

    std::vector<juce::TextButton*> bankButtons; // owned via `owned`
    std::vector<juce::TextButton*> cellPads;    // one per track, owned via `owned`
    int currentTrack = 0;
    std::unique_ptr<juce::FileChooser> chooser; // per-slot load dialog

    Attachments globalAtt;
    Attachments trackAtt;
};

// ============================================================================

ColdAudioProcessorEditor::ColdAudioProcessorEditor(ColdAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    loadFolderButton.setButtonText(u8("Load Folder…"));
    loadFolderButton.setTooltip("Load every sample in a folder into bank slots 1..16 "
                                "in file order — the quickest way to fill the bank.");
    addAndMakeVisible(loadFolderButton);
    loadFolderButton.onClick = [this] { chooseFolder(); };

    importKitButton.setButtonText(u8("Import .kit…"));
    importKitButton.setTooltip("Import an NI Battery 3 .kit: loads its samples and applies each "
                               "cell's envelope, tune, pan and key mapping to the first 8 cells. "
                               "You pick the .kit, then the folder holding its samples.");
    addAndMakeVisible(importKitButton);
    importKitButton.onClick = [this] { chooseKit(); };

    sampleNameLabel.setJustificationType(juce::Justification::centredLeft);
    sampleNameLabel.setColour(juce::Label::textColourId, juce::Colour(0xffd6dbe4));
    addAndMakeVisible(sampleNameLabel);
    refreshSampleName();

    content = std::make_unique<Content>(processorRef);
    content->onBankChanged = [this] { refreshSampleName(); };
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
                             content->refreshBank();
                         });
}

void ColdAudioProcessorEditor::chooseKit()
{
    chooser =
        std::make_unique<juce::FileChooser>("Choose a Battery .kit", juce::File{}, "*.kit;*.kt4");
    const auto flags =
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(
        flags,
        [this](const juce::FileChooser& fc)
        {
            const auto kit = fc.getResult();
            if (kit == juce::File{})
                return;

            // Then pick the folder that holds the kit's samples (default: its own).
            chooser = std::make_unique<juce::FileChooser>(
                "Choose the folder holding the kit's samples", kit.getParentDirectory());
            const auto dirFlags = juce::FileBrowserComponent::openMode |
                                  juce::FileBrowserComponent::canSelectDirectories;
            chooser->launchAsync(
                dirFlags,
                [this, kit](const juce::FileChooser& dfc)
                {
                    auto folder = dfc.getResult();
                    if (folder == juce::File{})
                        folder = kit.getParentDirectory();

                    const int n = processorRef.importBatteryKit(kit, folder);
                    sampleNameLabel.setText(
                        n > 0 ? "Imported " + juce::String(n) + " cell(s) from " + kit.getFileName()
                              : juce::String("could not read ") + kit.getFileName(),
                        juce::dontSendNotification);
                    if (content != nullptr)
                        content->refreshBank();
                });
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
    loadFolderButton.setBounds(top.removeFromLeft(120));
    top.removeFromLeft(6);
    importKitButton.setBounds(top.removeFromLeft(120));
    top.removeFromLeft(8);
    sampleNameLabel.setBounds(top);

    area.removeFromTop(8);
    viewport.setBounds(area);
    if (content != nullptr)
        content->setSize(viewport.getMaximumVisibleWidth(), content->getHeight());
}

} // namespace cold
