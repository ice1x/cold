#include "PluginEditor.h"

#include "dsp/Logger.h"

#include <array>
#include <cstdint>
#include <iterator>
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

// Battery-style knob: a dark background track arc, a bright value arc, a solid
// knob body and a clear pointer — far more legible than JUCE's default dot. Only
// the rotary drawing is overridden; combos/toggles keep the LookAndFeel_V4 look.
class ColdKnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                          float startAngle, float endAngle, juce::Slider&) override
    {
        const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(4.0f);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const float lineW = juce::jmax(2.5f, radius * 0.16f);
        const float arcR = radius - lineW * 0.5f;
        const float toAngle = startAngle + sliderPos * (endAngle - startAngle);

        // Background track.
        juce::Path back;
        back.addCentredArc(cx, cy, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour(juce::Colour(0xff2a2f37));
        g.strokePath(back, juce::PathStrokeType(lineW, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

        // Filled value arc.
        juce::Path arc;
        arc.addCentredArc(cx, cy, arcR, arcR, 0.0f, startAngle, toAngle, true);
        g.setColour(juce::Colour(0xff58b0f0));
        g.strokePath(arc, juce::PathStrokeType(lineW, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

        // Knob body + pointer.
        const float bodyR = arcR - lineW;
        g.setColour(juce::Colour(0xff31363f));
        g.fillEllipse(cx - bodyR, cy - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour(juce::Colour(0xff3d4450));
        g.drawEllipse(cx - bodyR, cy - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

        const juce::Point<float> tip(cx + arcR * std::sin(toAngle), cy - arcR * std::cos(toAngle));
        const juce::Point<float> base(cx + bodyR * 0.35f * std::sin(toAngle),
                                      cy - bodyR * 0.35f * std::cos(toAngle));
        g.setColour(juce::Colours::white);
        g.drawLine({base, tip}, juce::jmax(2.0f, lineW * 0.6f));
    }
};

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

// A cell/track pad. Left-click selects the cell (juce::Button's normal click);
// right-click (or ctrl-click) raises a context menu — its "load sample" entry is
// the quick way to fill a cell without hunting for the per-layer Load buttons.
class CellPad : public juce::TextButton
{
public:
    std::function<void()> onRightClick;  // context menu (load a sample)
    std::function<void()> onAudition;    // left mouse-down: play the cell
    std::function<void()> onAuditionEnd; // mouse-up: release (gates looping cells)

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onRightClick)
        {
            onRightClick();
            return;
        }
        if (onAudition)
            onAudition(); // audible feedback on press, before the click selects
        juce::TextButton::mouseDown(e);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (onAuditionEnd)
            onAuditionEnd();
        juce::TextButton::mouseUp(e);
    }
};

// Battery-style waveform view of the selected cell's sample (И5c). Draws a
// min/max peak envelope (from dsp::computeWaveformPeaks, fetched by Content) and
// the playable start/end window as vertical markers with the outside dimmed.
// Purely a painter: it holds no processor reference, so Content owns the fetch.
class WaveformDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    WaveformDisplay()
    {
        setInterceptsMouseClicks(true, false);
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        setTooltip("Waveform of the selected cell. Drag the left/right edges to set "
                   "the sample start and end (the playable window).");
    }

    // Fired while the user drags a marker: the new window as fractions [0, 1] of
    // the whole sample. The editor maps these to frames and sets the slot window.
    std::function<void(float startFrac, float endFrac)> onWindowChanged;

    // Show a sample's peaks with its window as fractions [0, 1] of the whole
    // sample. `startFrac`/`endFrac` place the markers; pass 0/1 for the full clip.
    void setContent(dsp::WaveformPeaks newPeaks, float startFrac, float endFrac)
    {
        peaks = std::move(newPeaks);
        windowStart = juce::jlimit(0.0f, 1.0f, startFrac);
        windowEnd = juce::jlimit(windowStart, 1.0f, endFrac);
        hasSample = !peaks.empty();
        repaint();
    }

    void clear()
    {
        peaks = {};
        hasSample = false;
        repaint();
    }

    // Mirror the display horizontally when the layer plays in reverse, so the flipped
    // envelope is a visible cue that the sample is reversed.
    void setReversed(bool shouldReverse)
    {
        if (reversed != shouldReverse)
        {
            reversed = shouldReverse;
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (!hasSample)
            return;
        // Grab the nearer of the two markers.
        const float f = xToFrac(e.x);
        dragMarker = (std::abs(f - windowStart) <= std::abs(f - windowEnd)) ? 1 : 2;
        mouseDrag(e);
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (!hasSample || dragMarker == 0)
            return;
        const float f = xToFrac(e.x);
        if (dragMarker == 1)
            windowStart = juce::jlimit(0.0f, windowEnd, f);
        else
            windowEnd = juce::jlimit(windowStart, 1.0f, f);
        repaint();
        if (onWindowChanged)
            onWindowChanged(windowStart, windowEnd);
    }

    void mouseUp(const juce::MouseEvent&) override { dragMarker = 0; }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff15181d));
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(juce::Colour(0xff2a2f37));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

        if (!hasSample)
        {
            g.setColour(juce::Colour(0xff5a6470));
            g.drawText("no sample in this cell", getLocalBounds(), juce::Justification::centred);
            return;
        }

        const auto area = bounds.reduced(3.0f);
        const float midY = area.getCentreY();
        const float halfH = area.getHeight() * 0.5f;
        const int n = peaks.numBins();

        // Centre line.
        g.setColour(juce::Colour(0xff2a2f37));
        g.drawHorizontalLine(static_cast<int>(midY), area.getX(), area.getRight());

        // One vertical line per bin, mapped across the width (bins may differ
        // from pixel columns; nearest-bin sampling keeps it cheap and crisp).
        g.setColour(juce::Colour(0xff58b0f0));
        const int px = static_cast<int>(area.getWidth());
        for (int x = 0; x < px; ++x)
        {
            // When reversed, read bins right-to-left so the envelope is mirrored.
            const int src = reversed ? (px - 1 - x) : x;
            const int bin = (n <= 1) ? 0 : juce::jlimit(0, n - 1, src * n / px);
            const float lo = peaks.minValues[static_cast<size_t>(bin)];
            const float hi = peaks.maxValues[static_cast<size_t>(bin)];
            const float y1 = midY - hi * halfH;
            const float y2 = midY - lo * halfH;
            const float fx = area.getX() + static_cast<float>(x);
            g.drawLine(fx, y1, fx, y2, 1.0f);
        }

        // Dim the region outside the playable window and mark its edges. The window
        // fractions are in forward frames; mirror their x when reversed.
        auto toX = [&](float frac)
        { return area.getX() + (reversed ? (1.0f - frac) : frac) * area.getWidth(); };
        const float xa = toX(windowStart);
        const float xb = toX(windowEnd);
        const float left = std::min(xa, xb);
        const float right = std::max(xa, xb);
        g.setColour(juce::Colour(0x88000000));
        if (left > area.getX())
            g.fillRect(area.withRight(left));
        if (right < area.getRight())
            g.fillRect(area.withLeft(right));
        g.setColour(juce::Colour(0xffe0a53a));
        g.drawVerticalLine(static_cast<int>(left), area.getY(), area.getBottom());
        g.drawVerticalLine(static_cast<int>(right), area.getY(), area.getBottom());
    }

private:
    // Pixel x -> fraction [0, 1] within the drawn area (paint insets by 3px).
    [[nodiscard]] float xToFrac(int x) const
    {
        const auto area = getLocalBounds().toFloat().reduced(3.0f);
        if (area.getWidth() <= 0.0f)
            return 0.0f;
        const float raw =
            juce::jlimit(0.0f, 1.0f, (static_cast<float>(x) - area.getX()) / area.getWidth());
        return reversed ? 1.0f - raw : raw; // logical (forward-frame) fraction
    }

    dsp::WaveformPeaks peaks;
    float windowStart = 0.0f;
    float windowEnd = 1.0f;
    bool hasSample = false;
    bool reversed = false;
    int dragMarker = 0; // 0 none, 1 start, 2 end
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

class ColdAudioProcessorEditor::Content : public juce::Component, private juce::Timer
{
public:
    explicit Content(ColdAudioProcessor& p) : proc(p)
    {
        dsp::Logger::instance().log("editor opened");
        buildLogView();
        buildCellGrid();
        addAndMakeVisible(waveformView); // under the cell pads (positioned in resized)
        // Dragging a marker sets the selected cell's playable window (frames).
        waveformView.onWindowChanged = [this](float startFrac, float endFrac)
        {
            const int slot = slotForLayer(currentLayer); // the layer being edited
            const int total = proc.slotTotalFrames(slot);
            if (total <= 0)
                return;
            const int start = juce::roundToInt(startFrac * static_cast<float>(total));
            const int length =
                juce::jmax(1, juce::roundToInt((endFrac - startFrac) * static_cast<float>(total)));
            proc.setSlotStartFrames(slot, start);
            proc.setSlotLengthFrames(slot, length);
        };
        // Track panel FIRST so "Selected cell" + the layer rows sit at the top of the
        // Main tab, right under the waveform (loading/editing layers is the core
        // workflow); the global sound-shaping sections follow below.
        buildTrackPanel();
        buildGlobals();
        buildTabs();  // after all sections exist
        assignTabs(); // route each section to a tab

        selectCell(proc.selectedTrack()); // initial attachments + pad highlight
        startTimerHz(5);                  // poll the log ring for the Logs tab
    }

    ~Content() override
    {
        stopTimer(); // before members are destroyed, so no late callback touches them
        dsp::Logger::instance().log("editor closed");
    }

    // ---- tabs ----------------------------------------------------------------

    // The editing sections are grouped under tabs (the cell grid + waveform stay
    // pinned above). Keeps the panel short instead of one long scroll. "Logs" is a
    // special tab: a live view of the debug log ring (no sections).
    static constexpr const char* kTabNames[] = {"Main", "FX", "Mod", "Logs"};
    [[nodiscard]] int logsTabIndex() const { return static_cast<int>(std::size(kTabNames)) - 1; }

    void buildTabs()
    {
        for (int i = 0; i < static_cast<int>(std::size(kTabNames)); ++i)
        {
            auto btn = std::make_unique<juce::TextButton>(kTabNames[i]);
            btn->setClickingTogglesState(false);
            auto* ptr = btn.get();
            ptr->onClick = [this, i]
            {
                currentTab = i;
                refreshTabs();
                resized();
            };
            addAndMakeVisible(*ptr);
            tabButtons.push_back(ptr);
            owned.push_back(std::move(btn));
        }
        refreshTabs();
    }

    // Route each section to a tab by its header text (the cell grid stays pinned).
    void assignTabs()
    {
        for (size_t si = 0; si < sections.size(); ++si)
        {
            const auto title = sections[si].header->getText();
            int tab = 0; // Main
            if (si == 0)
                tab = -1; // the cell grid: always visible
            else if (title.startsWith("Compressor") || title.startsWith("Saturation") ||
                     title.startsWith("Lo-Fi"))
                tab = 1; // FX
            else if (title.startsWith("Mod Envelope") || title == "LFO")
                tab = 2; // Mod
            // "Selected cell" + the per-layer rows live on Main: loading and editing
            // a cell's layers is the core workflow, so it must not hide behind a tab.
            sections[si].tab = tab;
        }
    }

    void refreshTabs()
    {
        for (int i = 0; i < static_cast<int>(tabButtons.size()); ++i)
            tabButtons[static_cast<size_t>(i)]->setColour(
                juce::TextButton::buttonColourId,
                i == currentTab ? juce::Colour(0xff2b6cb0) : juce::Colour(0xff21252b));
        // Refresh the log view immediately when switching onto the Logs tab.
        if (currentTab == logsTabIndex())
            refreshLogs(true);
    }

    // ---- logs tab ------------------------------------------------------------

    void buildLogView()
    {
        logView.setMultiLine(true);
        logView.setReadOnly(true);
        logView.setScrollbarsShown(true);
        logView.setCaretVisible(false);
        logView.setFont(
            juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
        logView.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff15181d));
        logView.setColour(juce::TextEditor::textColourId, juce::Colour(0xffc7d0dc));
        logView.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff2a2f37));
        addChildComponent(logView); // shown only on the Logs tab

        logClearButton.setButtonText("Clear");
        logClearButton.onClick = [this]
        {
            dsp::Logger::instance().clear();
            refreshLogs(true);
        };
        addChildComponent(logClearButton);
    }

    // Rebuild the log text when new lines arrived (or forced). Keeps the view pinned
    // to the newest line.
    void refreshLogs(bool force = false)
    {
        const auto seq = dsp::Logger::instance().sequence();
        if (!force && seq == lastLogSeq)
            return;
        lastLogSeq = seq;

        juce::StringArray lines;
        for (const auto& l : dsp::Logger::instance().snapshot())
            lines.add(juce::String(l));
        logView.setText(lines.joinIntoString("\n"), false);
        logView.moveCaretToEnd();
        logView.setCaretVisible(false);
    }

    void timerCallback() override
    {
        if (currentTab == logsTabIndex() && isShowing())
            refreshLogs();
    }

    struct Section
    {
        juce::Label* header;
        std::vector<juce::Component*> cells; // LabeledControl* (or raw buttons if wide)
        bool wideCells = false;              // stretched short rows (the cell grid)
        int tab = 0;                         // which tab shows it; -1 = always visible
    };

    // Lay a section's header + cell grid starting at `y`; returns the y below it.
    // `tallPads` gives the cell grid its taller Battery-style pad rows.
    int layoutSection(Section& s, int width, int y, bool tallPads = false)
    {
        constexpr int pad = 10;
        constexpr int cellH = 78; // taller cells so the Battery-style knobs read clearly
        constexpr int headerH = 22;
        constexpr int gap = 6;

        s.header->setBounds(pad, y, width - 2 * pad, headerH);
        y += headerH;

        const int cellW = s.wideCells ? (width - 2 * pad - 3 * gap) / 4 : 84;
        const int rowH = s.wideCells ? (tallPads ? 40 : 24) : cellH;
        const int cols = s.wideCells ? 4 : juce::jmax(1, (width - 2 * pad + gap) / (cellW + gap));

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
        return rowTop + gap;
    }

    void resized() override
    {
        constexpr int pad = 10;
        constexpr int gap = 6;
        constexpr int waveH = 96;
        constexpr int tabH = 28;

        const int width = juce::jmax(getWidth(), 320);

        // Always-on header: the cell grid (section 0) + the selected cell's waveform.
        int y = pad;
        y = layoutSection(sections[0], width, y, /*tallPads*/ true);
        waveformView.setBounds(pad, y, width - 2 * pad, waveH);
        y += waveH + gap;

        // Tab bar: switches which editing sections are shown below.
        const int tabW = (width - 2 * pad - gap * (static_cast<int>(tabButtons.size()) - 1)) /
                         juce::jmax(1, static_cast<int>(tabButtons.size()));
        for (int i = 0; i < static_cast<int>(tabButtons.size()); ++i)
            tabButtons[static_cast<size_t>(i)]->setBounds(pad + i * (tabW + gap), y, tabW, tabH);
        y += tabH + gap;

        const bool onLogs = (currentTab == logsTabIndex());

        // The Logs tab: a live console filling the area below the tab bar (no sections).
        logView.setVisible(onLogs);
        logClearButton.setVisible(onLogs);
        if (onLogs)
        {
            for (size_t si = 1; si < sections.size(); ++si) // hide every section
            {
                sections[si].header->setVisible(false);
                for (auto* c : sections[si].cells)
                    c->setVisible(false);
            }
            constexpr int btnH = 24;
            logClearButton.setBounds(width - pad - 80, y, 80, btnH);
            y += btnH + gap;
            constexpr int logH = 440; // scrolls within the outer viewport
            logView.setBounds(pad, y, width - 2 * pad, logH);
            y += logH + pad;
            refreshLogs(true);
        }
        else
        {
            // Only the active tab's sections are visible / laid out.
            for (size_t si = 1; si < sections.size(); ++si)
            {
                auto& s = sections[si];
                const bool vis = (s.tab == currentTab);
                s.header->setVisible(vis);
                for (auto* c : s.cells)
                    c->setVisible(vis);
                if (vis)
                    y = layoutSection(s, width, y);
            }
            y += pad;
        }

        if (y != getHeight())
            setSize(width, y); // grow so the Viewport can scroll to everything

        updateWaveform(); // re-fetch at the new pixel width for a crisp envelope
    }

    // Refresh everything a sample load can change: the cell pads, the selected
    // cell's per-layer Load buttons, and its waveform.
    void refreshBank()
    {
        refreshCellPads();
        refreshLayerButtons();
        updateWaveform();
    }

    // Bank slot backing the selected cell's layer `layer` (its dedicated slot).
    [[nodiscard]] int slotForLayer(int layer) const
    {
        return currentTrack * ColdAudioProcessor::kNumLayers + layer;
    }

    // Refresh the layer selector (label + loaded dot + highlight for each of the 4
    // layers) and the single editor's Load button (the current layer's sample).
    void refreshLayerButtons()
    {
        for (int l = 0; l < static_cast<int>(layerSelectors.size()); ++l)
        {
            const bool hasSample = proc.slotSampleName(slotForLayer(l)).isNotEmpty();
            const bool sel = (l == currentLayer);
            auto* b = layerSelectors[static_cast<size_t>(l)];
            b->setButtonText("L" + juce::String(l + 1) + (hasSample ? u8(" ●") : juce::String()));
            b->setColour(juce::TextButton::buttonColourId, sel         ? juce::Colour(0xff2b6cb0)
                                                           : hasSample ? juce::Colour(0xff2d5f57)
                                                                       : juce::Colour(0xff21252b));
        }
        if (layerCtl.load != nullptr)
        {
            const auto name = proc.slotSampleName(slotForLayer(currentLayer));
            layerCtl.load->setButtonText(name.isEmpty() ? u8("Load…") : name);
        }
    }

private:
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

    // Per-track toggle: like tKnob, its attachment is rebuilt in setTrack().
    void tToggle(Section& s, const juce::String& base, const juce::String& name,
                 const juce::String& tip = {})
    {
        auto* tg = makeToggle();
        perTrackToggles.push_back({tg, base});
        place(s, name, tip);
    }

    // ---- section construction ------------------------------------------------

    void buildGlobals()
    {
        {
            auto& s = newSection("Master");
            const auto nTracks = juce::String(dsp::MultiEngine::kNumTracks);
            gKnob(s, "gain", "Gain",
                  "Output gain in dB, applied to the mix of all " + nTracks + " cells.");
            gKnob(s, "polyphony", "Poly",
                  "Voices per track. 1 = mono (Digitakt-style); higher lets notes overlap "
                  "and chords ring.");
            gKnob(s, "rootNote", "Root",
                  "MIDI note that plays a sample at its natural pitch. Other notes repitch "
                  "it varispeed-style.");
            gCombo(s, "midiAddressing", "MIDI",
                   "How MIDI picks a track. Channel: MIDI channel N plays track N. "
                   "Note Map: one channel, the note number picks the track (GM-drum style).");
        }
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
            auto& s = newSection("Pitch Envelope (selected cell)");
            tKnob(s, "pitchEnvAmount", "Amount",
                  "Drum 'blip': the pitch is offset by this many semitones at the "
                  "note start, then decays back. + sweeps down from above, - up. 0 = off.");
            tKnob(s, "pitchEnvDecay", "Decay",
                  "How long the pitch envelope takes to return to the note pitch, in "
                  "seconds. Short = a snappy click; longer = a falling 'dooo'.");
        }
        {
            auto& s = newSection("Velocity (selected cell)");
            tKnob(s, "velToPitch", u8("Vel→Pitch"),
                  "Velocity modulates pitch: this many semitones at full velocity, "
                  "scaled down for softer hits. + = harder plays higher, - = lower, "
                  "0 = off. (Velocity→volume is the Amp Envelope 'Vel' knob.)");
        }
        {
            auto& s = newSection("Compressor (selected cell)");
            tToggle(s, "compOn", "On", "Enable this cell's compressor (punch / glue).");
            tKnob(s, "compThreshold", "Thresh",
                  "Level (dBFS) above which the cell is compressed. Lower = more of "
                  "the hit is caught.");
            tKnob(s, "compRatio", "Ratio",
                  "How hard it clamps above the threshold (input:output). 4:1 is a "
                  "firm drum squeeze; 20:1 is near-limiting.");
            tKnob(s, "compAttack", "Attack",
                  "How fast it grabs the transient, in seconds. Fast = tames the "
                  "click; slow = lets the attack punch through.");
            tKnob(s, "compRelease", "Release",
                  "How fast it lets go after the level drops, in seconds.");
            tKnob(s, "compMakeup", "Makeup",
                  "Output gain in dB to make up for the reduction (bring the level "
                  "back up).");
        }
        {
            auto& s = newSection("Saturation (selected cell)");
            tToggle(s, "satOn", "On", "Enable this cell's drive / saturation (warmth, grit).");
            tKnob(s, "satDrive", "Drive",
                  "How hard the signal is pushed into the tanh shaper, in dB. More = "
                  "more harmonics / grit.");
            tKnob(s, "satMix", "Mix",
                  "Dry/wet blend: 0 = clean, 1 = fully saturated. Lower for parallel "
                  "drive that keeps the transient.");
        }
        {
            auto& s = newSection("Lo-Fi (selected cell)");
            tToggle(s, "crushOn", "On",
                    "Enable this cell's lo-fi: bit-depth + sample-rate reduction (vintage grit).");
            tKnob(s, "crushBits", "Bits",
                  "Bit depth (1-16). Lower = coarser quantization steps = more crunch. "
                  "16 is essentially clean.");
            tKnob(s, "crushRate", "Rate",
                  "Sample-rate divider (1-64): hold every Nth sample. Higher = darker, "
                  "grittier, more aliased (down-sampled).");
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
    // Load a file directly into the selected cell's layer `layer` (its own slot).
    void chooseForLayer(int layer) { chooseForCellLayer(currentTrack, layer); }

    // Open a file chooser and load the picked sample into `track`'s `layer`. Used by
    // the per-layer Load buttons (current cell) and the cell pad's right-click menu.
    void chooseForCellLayer(int track, int layer)
    {
        chooser = std::make_unique<juce::FileChooser>("Load a sample into cell " +
                                                          juce::String(track + 1) + " layer " +
                                                          juce::String(layer + 1),
                                                      juce::File{}, "*.wav;*.aiff;*.aif;*.mp3");
        const auto flags =
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;

        chooser->launchAsync(
            flags,
            [this, track, layer](const juce::FileChooser& fc)
            {
                const auto file = fc.getResult();
                if (file == juce::File{})
                    return;
                const bool ok = proc.loadSampleToLayer(track, layer, file);
                dsp::Logger::instance().log(std::string(ok ? "loaded '" : "FAILED to load '") +
                                            file.getFileName().toStdString() + "' -> cell " +
                                            std::to_string(track) + " layer " +
                                            std::to_string(layer));
                // Loading a 2nd+ layer while the cell is in Single (only
                // layer 1 sounds) almost always means the user wants the
                // layers to play together — switch to Stack so they do.
                if (layer > 0)
                    if (auto* m = proc.apvts.getParameter(trackId(track, "layerMode")))
                        if (m->getValue() <= 0.001f)                          // Single (index 0)
                            m->setValueNotifyingHost(m->convertTo0to1(1.0f)); // Stack
                refreshBank();
                if (onBankChanged)
                    onBankChanged();
            });
    }

    // Right-click menu on a cell pad: select the cell, then offer to load a sample
    // into its primary layer or any specific layer (why the pads had no loader
    // before: loading lived only on the per-layer buttons below).
    void showCellMenu(int track)
    {
        selectCell(track); // make the right-clicked cell the active one
        juce::PopupMenu menu;
        menu.addSectionHeader("Cell " + juce::String(track + 1));
        menu.addItem(1, u8("Load sample… (layer 1)"));
        for (int l = 1; l < ColdAudioProcessor::kNumLayers; ++l)
            menu.addItem(l + 1, "Load into layer " + juce::String(l + 1) + u8("…"));
        menu.showMenuAsync(juce::PopupMenu::Options{},
                           [this, track](int result)
                           {
                               if (result >= 1 && result <= ColdAudioProcessor::kNumLayers)
                                   chooseForCellLayer(track, result - 1);
                           });
    }

public:
    std::function<void()> onBankChanged; // editor hook: refresh the top-bar name

private:
    // Battery-style cell grid: one pad per track, click to pick the edited cell.
    // Replaces a track-selector knob; the pads drive the "track" param + setTrack().
    void buildCellGrid()
    {
        auto& s = newSection(u8("Cells — click to play/edit, right-click to load a sample"));
        s.wideCells = true;
        for (int t = 0; t < dsp::MultiEngine::kNumTracks; ++t)
        {
            auto btn = std::make_unique<CellPad>();
            btn->setTooltip("Select cell/track " + juce::String(t + 1) +
                            " for editing below. Right-click to load a sample. It plays on MIDI "
                            "channel " +
                            juce::String(t + 1) +
                            " (Channel mode) or its Note Map note. Its "
                            "default sample is bank slot " +
                            juce::String(t + 1) + ".");
            auto* ptr = btn.get();
            ptr->onClick = [this, t] { selectCell(t); };
            ptr->onRightClick = [this, t] { showCellMenu(t); };
            ptr->onAudition = [this, t] { proc.auditionCell(t); };
            ptr->onAuditionEnd = [this, t] { proc.releaseCell(t); };
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
        dsp::Logger::instance().log("selectCell(" + std::to_string(t) + ")");
        currentTrack = juce::jlimit(0, dsp::MultiEngine::kNumTracks - 1, t);
        if (auto* p = proc.apvts.getParameter("track"))
            p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(currentTrack)));
        setTrack(currentTrack);
        refreshCellPads();
        updateWaveform();
        if (onBankChanged)
            onBankChanged();
    }

    // Pull the selected cell's primary sample (layer 0) into the waveform view.
    // Fetches peaks at the view's current pixel width so bins map ~1:1 to columns.
    void updateWaveform()
    {
        // Mirror the view when this layer plays in reverse.
        if (auto* rev =
                proc.apvts.getRawParameterValue(layerId(currentTrack, currentLayer, "reverse")))
            waveformView.setReversed(rev->load() > 0.5f);

        const int slot = slotForLayer(currentLayer); // the layer being edited
        const int total = proc.slotTotalFrames(slot);
        if (total <= 0)
        {
            waveformView.clear();
            return;
        }
        const int bins = juce::jmax(1, waveformView.getWidth());
        const float startFrac =
            static_cast<float>(proc.slotWindowStart(slot)) / static_cast<float>(total);
        const float endFrac =
            static_cast<float>(proc.slotWindowStart(slot) + proc.slotWindowLength(slot)) /
            static_cast<float>(total);
        waveformView.setContent(proc.slotWaveform(slot, bins), startFrac, endFrac);
    }

    void refreshCellPads()
    {
        for (int t = 0; t < static_cast<int>(cellPads.size()); ++t)
        {
            // A cell's label is its primary sample (layer 0), plus how many of its
            // layers hold a sample so a stacked cell reads at a glance.
            const auto name = proc.slotSampleName(t * ColdAudioProcessor::kNumLayers);
            const bool loaded = name.isNotEmpty();
            const bool selected = (t == currentTrack);
            auto* pad = cellPads[static_cast<size_t>(t)];

            int layersLoaded = 0;
            for (int l = 0; l < ColdAudioProcessor::kNumLayers; ++l)
                if (proc.slotSampleName(t * ColdAudioProcessor::kNumLayers + l).isNotEmpty())
                    ++layersLoaded;
            // Only show the count once a cell is stacked (>1 layer), to keep single-
            // layer cells clean. e.g. "T1  kick.wav  3/4".
            const auto badge = layersLoaded > 1 ? "  " + juce::String(layersLoaded) + "/" +
                                                      juce::String(ColdAudioProcessor::kNumLayers)
                                                : juce::String();

            pad->setButtonText("T" + juce::String(t + 1) + (loaded ? "  " + name : u8("  ·")) +
                               badge);

            // Selected = blue; loaded = teal; empty = dark. Text brightens when there
            // is a sample, so a full kit reads at a glance.
            const auto fill = selected ? juce::Colour(0xff2b6cb0)
                              : loaded ? juce::Colour(0xff2d5f57)
                                       : juce::Colour(0xff21252b);
            pad->setColour(juce::TextButton::buttonColourId, fill);
            const auto text = selected ? juce::Colours::white
                              : loaded ? juce::Colour(0xffdfeae6)
                                       : juce::Colour(0xff6b7280);
            pad->setColour(juce::TextButton::textColourOffId, text);
        }
    }

    void buildTrackPanel()
    {
        auto& s = newSection("Selected cell");

        // Layer mode is per-track; its attachment is rebuilt on cell change.
        layerModeCombo = makeCombo("t0_layerMode");
        place(s, "Layer Mode",
              "Single: the cell plays one layer per note (layer 1, or wherever "
              "LFO→Sample points among the loaded layers). Stack: every loaded layer "
              "below sounds TOGETHER on each note — layering. Round-Robin: one loaded "
              "layer per note, cycling in order — different take on each hit. Velocity: "
              "one loaded layer chosen by how hard you play (layer 1 = softest).");

        tKnob(s, "mapNote", "Note",
              "Note Map mode: the MIDI note that triggers this cell (0-127; 36 = C1). "
              "Cells can sit on any notes — a .kit import sets each cell's own key.");

        tKnob(s, "chokeGroup", "Choke",
              "Choke / mute group (0 = off, 1-8). Cells sharing a group cut each other "
              "off — playing one silences the rest (e.g. a closed hat chokes the open hat).");

        // Layer selector: pick which of the cell's 4 layers the editor below edits.
        // (You can't show 4 layers' full envelopes at once, so layers are selected
        // like cells — one at a time.)
        {
            auto& sel = newSection(u8("Layers — click to edit a layer (loaded shown ●)"));
            sel.wideCells = true;
            for (int l = 0; l < kNumLayers; ++l)
            {
                auto btn = std::make_unique<juce::TextButton>();
                btn->setTooltip("Edit layer " + juce::String(l + 1) +
                                " of the selected cell below. Right-click a cell to load a "
                                "sample into a specific layer.");
                auto* ptr = btn.get();
                ptr->onClick = [this, l] { selectLayer(l); };
                addAndMakeVisible(*ptr);
                layerSelectors.push_back(ptr);
                owned.push_back(std::move(btn));
                sel.cells.push_back(ptr);
            }
        }

        // The single layer editor. Its controls re-attach to the selected layer.
        auto& row = newSection("Layer");
        auto& c = layerCtl;

        auto btn = std::make_unique<juce::TextButton>();
        btn->setTooltip("Load a sample into the selected layer of the selected cell.");
        c.load = btn.get();
        c.load->onClick = [this] { chooseForLayer(currentLayer); };
        pendingControl = std::move(btn);
        place(row, "Sample");

        // The most-reached per-layer switches, kept at the top so they are visible the
        // moment a layer is selected (not buried under the knobs).
        c.mute = makeToggle();
        place(row, "Mute", "Silence this layer in the stack (Stack / Round-Robin / Velocity).");
        c.solo = makeToggle();
        place(row, "Solo",
              "Solo this layer: if any layer in the cell is soloed, only soloed layers "
              "sound.");
        c.reverse = makeToggle();
        place(row, "Reverse",
              "Play THIS layer backwards (per layer — a stack can mix forward and "
              "reversed layers).");

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

        // Dedicated band filters (independent of the morph filter above): a low-cut
        // (high-pass) to remove the lows, and a high-cut (low-pass) to remove the
        // highs — band-limit a layer (e.g. HP the top layer so only the sub has bottom).
        c.hpOn = makeToggle();
        place(row, "Low Cut",
              "High-pass this layer: removes the lows below the frequency at right. Use "
              "it to keep the bottom on the sub layer only.");
        c.hpCut = makeSlider();
        place(row, "LC Freq", "Low-cut (high-pass) frequency in Hz (20-2000).");
        c.lpOn = makeToggle();
        place(row, "High Cut",
              "Low-pass this layer: removes the highs above the frequency at right.");
        c.lpCut = makeSlider();
        place(row, "HC Freq", "High-cut (low-pass) frequency in Hz (200-20000).");

        // Per-layer amp envelope — the core of layering: give this layer its own
        // contour (a snappy click layer + a long body layer on one note).
        c.envOn = makeToggle();
        place(row, "Env",
              "Give this layer its OWN amp envelope instead of the cell's shared one — "
              "the essence of layering (e.g. a fast click layer + a long tail layer).");
        c.envDelay = makeSlider();
        place(row, "E.Delay", "This layer's envelope: silence before the attack (seconds).");
        c.envAttack = makeSlider();
        place(row, "E.Attack", "This layer's envelope attack (fade-in), in seconds.");
        c.envHold = makeSlider();
        place(row, "E.Hold", "This layer's envelope hold at full level, in seconds.");
        c.envDecay = makeSlider();
        place(row, "E.Decay", "This layer's envelope decay to the sustain level, in seconds.");
        c.envSustain = makeSlider();
        place(row, "E.Sustain", "This layer's envelope sustain level (0..1).");
        c.envRelease = makeSlider();
        place(row, "E.Release", "This layer's envelope release after note-off, in seconds.");
    }

    // Rebuild the per-track attachments (layer mode + per-track knobs/toggles) to
    // point at track `t`, then re-point the single layer editor at the current layer.
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

        for (const auto& tg : perTrackToggles)
            trackAtt.button.push_back(std::make_unique<APVTS::ButtonAttachment>(
                proc.apvts, trackId(track, tg.base), *tg.ctl));

        selectLayer(currentLayer); // re-point the layer editor at the new cell's layer
    }

    // Point the single layer editor at (currentTrack, layer): rebuild its attachments
    // and refresh the selector highlight + the Load button label.
    void selectLayer(int layer)
    {
        currentLayer = juce::jlimit(0, kNumLayers - 1, layer);
        const int track = currentTrack;
        const int l = currentLayer;
        auto& c = layerCtl;

        layerAtt.slider.clear();
        layerAtt.combo.clear();
        layerAtt.button.clear();

        auto attSlider = [&](const char* base, juce::Slider* s)
        {
            layerAtt.slider.push_back(
                std::make_unique<APVTS::SliderAttachment>(proc.apvts, layerId(track, l, base), *s));
        };
        auto attButton = [&](const char* base, juce::Button* b)
        {
            layerAtt.button.push_back(
                std::make_unique<APVTS::ButtonAttachment>(proc.apvts, layerId(track, l, base), *b));
        };
        attSlider("gain", c.gain);
        attSlider("tune", c.tune);
        attSlider("pan", c.pan);
        attButton("filterOn", c.flt);
        attSlider("cutoff", c.cutoff);
        attSlider("ftype", c.ftype);
        attButton("ampOn", c.envOn);
        attSlider("ampDelay", c.envDelay);
        attSlider("ampAttack", c.envAttack);
        attSlider("ampHold", c.envHold);
        attSlider("ampDecay", c.envDecay);
        attSlider("ampSustain", c.envSustain);
        attSlider("ampRelease", c.envRelease);
        attButton("mute", c.mute);
        attButton("solo", c.solo);
        attButton("reverse", c.reverse);
        attButton("hpOn", c.hpOn);
        attSlider("hpCut", c.hpCut);
        attButton("lpOn", c.lpOn);
        attSlider("lpCut", c.lpCut);
        // Refresh the waveform when this layer's reverse is toggled (it mirrors).
        c.reverse->onClick = [this] { updateWaveform(); };

        refreshLayerButtons();
        updateWaveform(); // the waveform follows the layer being edited
    }

    // ---- state ---------------------------------------------------------------

    static constexpr int kNumLayers = ColdAudioProcessor::kNumLayers;

    // The single per-layer editor: its controls re-attach to the selected layer
    // (currentLayer) of the selected cell, so one layer's full set — including its
    // own amp envelope — is on screen at a time (a Layer 1|2|3|4 selector switches).
    struct LayerCtl
    {
        juce::TextButton* load = nullptr; // the selected layer's sample Load button
        juce::Slider* gain = nullptr;
        juce::Slider* tune = nullptr;
        juce::Slider* pan = nullptr;
        juce::ToggleButton* flt = nullptr;
        juce::Slider* cutoff = nullptr;
        juce::Slider* ftype = nullptr;
        // Per-layer amp envelope (Env On + DAHDSR).
        juce::ToggleButton* envOn = nullptr;
        juce::Slider* envDelay = nullptr;
        juce::Slider* envAttack = nullptr;
        juce::Slider* envHold = nullptr;
        juce::Slider* envDecay = nullptr;
        juce::Slider* envSustain = nullptr;
        juce::Slider* envRelease = nullptr;
        juce::ToggleButton* mute = nullptr;
        juce::ToggleButton* solo = nullptr;
        juce::ToggleButton* reverse = nullptr;
        juce::ToggleButton* hpOn = nullptr;
        juce::Slider* hpCut = nullptr;
        juce::ToggleButton* lpOn = nullptr;
        juce::Slider* lpCut = nullptr;
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
    LayerCtl layerCtl{};                           // one editor, re-attached per layer
    std::vector<juce::TextButton*> layerSelectors; // Layer 1|2|3|4, owned via `owned`
    int currentLayer = 0;

    // Per-track sound knobs whose attachment is rebuilt for the selected track.
    struct PerTrackKnob
    {
        juce::Slider* ctl;
        juce::String base; // un-prefixed id, e.g. "ampAttack"
    };
    std::vector<PerTrackKnob> perTrackKnobs;

    // Per-track toggles whose attachment is rebuilt for the selected track.
    struct PerTrackToggle
    {
        juce::ToggleButton* ctl;
        juce::String base;
    };
    std::vector<PerTrackToggle> perTrackToggles;

    std::vector<juce::TextButton*> cellPads;   // one per track, owned via `owned`
    std::vector<juce::TextButton*> tabButtons; // Main/FX/Mod/Logs, owned via `owned`
    int currentTab = 0;
    WaveformDisplay waveformView; // selected cell's sample + window

    juce::TextEditor logView; // the Logs tab's live console
    juce::TextButton logClearButton;
    std::uint64_t lastLogSeq = 0;
    int currentTrack = 0;
    std::unique_ptr<juce::FileChooser> chooser; // per-slot load dialog

    Attachments globalAtt;
    Attachments trackAtt;
    Attachments layerAtt; // the single layer editor's attachments (rebuilt per layer)
};

// ============================================================================

ColdAudioProcessorEditor::ColdAudioProcessorEditor(ColdAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    // Battery-style knobs across the whole editor (descendants inherit this).
    knobLookAndFeel = std::make_unique<ColdKnobLookAndFeel>();
    setLookAndFeel(knobLookAndFeel.get());

    loadFolderButton.setButtonText(u8("Load Folder…"));
    loadFolderButton.setTooltip("Load samples from a folder into the cells (1.." +
                                juce::String(dsp::MultiEngine::kNumTracks) +
                                ") in file order — the quickest way to fill the kit.");
    addAndMakeVisible(loadFolderButton);
    loadFolderButton.onClick = [this] { chooseFolder(); };

    importKitButton.setButtonText(u8("Import .kit…"));
    importKitButton.setTooltip("Import an NI Battery 3 .kit: loads its samples and applies each "
                               "cell's envelope, tune, pan and key mapping to the first " +
                               juce::String(dsp::MultiEngine::kNumTracks) +
                               " cells. You pick the .kit, then the folder holding its samples.");
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

ColdAudioProcessorEditor::~ColdAudioProcessorEditor()
{
    setLookAndFeel(nullptr); // detach before knobLookAndFeel is destroyed
}

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
