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

// One distinct colour per layer (1..4), used for the base waveform of the selected
// layer and the "Show" comparison overlays of the others.
const std::array<juce::Colour, 4> kLayerColours{juce::Colour(0xff58b0f0), juce::Colour(0xff5fd08a),
                                                juce::Colour(0xffef8f4f), juce::Colour(0xffc77dff)};

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
        // Knobs take the whole cell; buttons and combos sit at a normal height,
        // centred, instead of stretching into tall slabs.
        if (dynamic_cast<juce::Slider*>(control.get()) != nullptr)
            control->setBounds(b.reduced(2, 0));
        else
            control->setBounds(b.withSizeKeepingCentre(b.getWidth() - 6, 24));
    }

    [[nodiscard]] juce::Component& inner() const noexcept { return *control; }

private:
    juce::Label caption;
    std::unique_ptr<juce::Component> control;
};

// A switch that says what it is: "ON" lit in the accent colour, "OFF" dim. The
// default toggle box drew as a near-invisible outline on the dark background.
class OnOffButton : public juce::TextButton
{
public:
    OnOffButton()
    {
        setClickingTogglesState(true);
        setColour(buttonColourId, juce::Colour(0xff21252b));
        setColour(buttonOnColourId, juce::Colour(0xff58b0f0));
        setColour(textColourOffId, juce::Colour(0xff8a94a3));
        setColour(textColourOnId, juce::Colour(0xff101216));
        updateText();
    }

    // Called on every toggle change, including the silent ones a parameter
    // attachment makes, so the label always matches the state.
    void buttonStateChanged() override
    {
        juce::TextButton::buttonStateChanged();
        updateText();
    }

private:
    void updateText() { setButtonText(getToggleState() ? "ON" : "OFF"); }
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

    // Fired on any press (before a marker drag), e.g. to focus the owning layer strip.
    std::function<void()> onPress;

    // What to say when there is no sample to draw.
    void setEmptyText(const juce::String& text)
    {
        emptyText = text;
        repaint();
    }

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

    // The amp-envelope contour to overlay (0..1 across the window; already scaled for
    // tune by the editor). Empty hides it.
    void setEnvelope(std::vector<float> curve)
    {
        envelope = std::move(curve);
        repaint();
    }

    // Colour of the waveform (each layer strip draws in its layer's colour).
    void setBaseColour(juce::Colour c)
    {
        if (baseColour != c)
        {
            baseColour = c;
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (onPress)
            onPress();
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
            g.drawText(emptyText, getLocalBounds(), juce::Justification::centred);
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
        g.setColour(baseColour);
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

        // Amp-envelope contour: two mirrored lines at mid +/- env*halfH, with the
        // attenuated region (outside the contour) dimmed — a live "mask" of the amp
        // shape. The env columns share the reverse mirroring so it lines up.
        if (!envelope.empty())
        {
            const int en = static_cast<int>(envelope.size());
            juce::Path top, bot, fillTop, fillBot;
            for (int x = 0; x < px; ++x)
            {
                const int src = reversed ? (px - 1 - x) : x;
                const int idx = (en <= 1) ? 0 : juce::jlimit(0, en - 1, src * en / px);
                const float e = envelope[static_cast<size_t>(idx)];
                const float fx = area.getX() + static_cast<float>(x);
                const float yTop = midY - e * halfH;
                const float yBot = midY + e * halfH;
                if (x == 0)
                {
                    top.startNewSubPath(fx, yTop);
                    bot.startNewSubPath(fx, yBot);
                }
                else
                {
                    top.lineTo(fx, yTop);
                    bot.lineTo(fx, yBot);
                }
            }
            // Dim above the top contour and below the bottom contour (the muted part).
            g.setColour(juce::Colour(0x66000000));
            juce::Path shade = top;
            shade.lineTo(area.getRight(), area.getY());
            shade.lineTo(area.getX(), area.getY());
            shade.closeSubPath();
            g.fillPath(shade);
            juce::Path shadeB = bot;
            shadeB.lineTo(area.getRight(), area.getBottom());
            shadeB.lineTo(area.getX(), area.getBottom());
            shadeB.closeSubPath();
            g.fillPath(shadeB);
            // The contour itself.
            g.setColour(juce::Colour(0xffffc861));
            g.strokePath(top, juce::PathStrokeType(1.5f));
            g.strokePath(bot, juce::PathStrokeType(1.5f));
        }
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

    juce::String emptyText{"no sample in this cell"};
    dsp::WaveformPeaks peaks;
    std::vector<float> envelope; // amp-envelope contour overlay (0..1), empty = hidden
    juce::Colour baseColour{0xff58b0f0};
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

// ---- value formatting ---------------------------------------------------------
// Knobs show musician units (ms / s, Hz / kHz, dB, %, st, oct, C/L/R) instead of
// raw parameter numbers, and never "-0.00". Typed text parses back in the same
// units. Applied AFTER a SliderAttachment is made, since the attachment installs
// the parameter's raw formatting.

// Snap tiny values to 0 so a centred / reset knob never reads as "-0.00".
double snapZero(double v, double eps)
{
    return std::abs(v) < eps ? 0.0 : v;
}

juce::String signedText(double v, int decimals, const char* unit, double eps)
{
    v = snapZero(v, eps);
    if (v == 0.0)
        return juce::String("0 ") + unit;
    return (v > 0.0 ? "+" : "") + juce::String(v, decimals) + " " + unit;
}

using TextFn = std::function<juce::String(double)>;
using ParseFn = std::function<double(const juce::String&)>;

void setFormat(juce::Slider& s, TextFn text, ParseFn parse)
{
    s.textFromValueFunction = std::move(text);
    s.valueFromTextFunction = std::move(parse);
    s.updateText();
}

// Time stored in seconds: "0 ms", "12 ms", "1.20 s".
void formatSeconds(juce::Slider& s)
{
    setFormat(
        s,
        [](double v)
        {
            if (v < 0.9995)
                return juce::String(juce::roundToInt(v * 1000.0)) + " ms";
            return juce::String(v, 2) + " s";
        },
        [](const juce::String& t)
        { return t.containsIgnoreCase("ms") ? t.getDoubleValue() / 1000.0 : t.getDoubleValue(); });
}

// Time stored in milliseconds: "250 ms", "1.20 s".
void formatMilliseconds(juce::Slider& s)
{
    setFormat(
        s,
        [](double v)
        {
            if (v < 999.5)
                return juce::String(juce::roundToInt(v)) + " ms";
            return juce::String(v / 1000.0, 2) + " s";
        },
        [](const juce::String& t)
        {
            const bool seconds = !t.containsIgnoreCase("ms") && t.containsIgnoreCase("s");
            return seconds ? t.getDoubleValue() * 1000.0 : t.getDoubleValue();
        });
}

// Frequency in Hz: "700 Hz", "1.5 kHz", "20.0 kHz".
void formatHz(juce::Slider& s)
{
    setFormat(
        s,
        [](double v)
        {
            if (v < 999.5)
                return juce::String(juce::roundToInt(v)) + " Hz";
            return juce::String(v / 1000.0, 1) + " kHz";
        },
        [](const juce::String& t)
        { return t.containsIgnoreCase("k") ? t.getDoubleValue() * 1000.0 : t.getDoubleValue(); });
}

// Value already in dB: "-12.0 dB".
void formatDb(juce::Slider& s)
{
    setFormat(
        s, [](double v) { return juce::String(snapZero(v, 0.05), 1) + " dB"; },
        [](const juce::String& t) { return t.getDoubleValue(); });
}

// 0..1 amount: "25%".
void formatPercent(juce::Slider& s)
{
    setFormat(
        s, [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; },
        [](const juce::String& t) { return t.getDoubleValue() / 100.0; });
}

// Stereo position -1..1: "C", "L40", "R100".
void formatPan(juce::Slider& s)
{
    setFormat(
        s,
        [](double v)
        {
            const int pct = juce::roundToInt(std::abs(v) * 100.0);
            if (pct == 0)
                return juce::String("C");
            return (v < 0.0 ? "L" : "R") + juce::String(pct);
        },
        [](const juce::String& t)
        {
            const auto u = t.trim().toUpperCase();
            const double amount = u.retainCharacters("0123456789.").getDoubleValue() / 100.0;
            if (u.startsWith("L"))
                return -amount;
            if (u.startsWith("R"))
                return amount;
            return 0.0; // "C" or anything unrecognised -> centre
        });
}

// Signed amount with a unit: "+7.0 st", "-2.0 oct", "0 st".
void formatSigned(juce::Slider& s, const char* unit)
{
    setFormat(
        s, [unit](double v) { return signedText(v, 1, unit, 0.05); },
        [](const juce::String& t) { return t.getDoubleValue(); });
}

// Plain signed number without negative zero: "0.00", "-0.35".
void formatPlain(juce::Slider& s)
{
    setFormat(
        s, [](double v) { return juce::String(snapZero(v, 0.005), 2); },
        [](const juce::String& t) { return t.getDoubleValue(); });
}

// Pick the format for a parameter by its id (un-prefixed for per-cell / per-layer
// controls). Unknown ids keep the parameter's own text.
void formatSlider(juce::Slider& s, const juce::String& base)
{
    static const juce::StringArray seconds{
        "ampDelay",    "ampAttack",        "ampHold",          "ampDecay",      "ampRelease",
        "modAttack",   "modDecay",         "modRelease",       "pitchEnvDecay", "compAttack",
        "compRelease", "masterCompAttack", "masterCompRelease"};
    static const juce::StringArray millis{"delayTime", "limiterRelease"};
    static const juce::StringArray hertz{"cutoff", "filterCutoff", "hpCut", "lpCut",
                                         "masterTiltPivot"};
    static const juce::StringArray decibels{"level",
                                            "gain",
                                            "compThreshold",
                                            "compMakeup",
                                            "satDrive",
                                            "masterCompThreshold",
                                            "masterCompMakeup",
                                            "limiterCeiling",
                                            "masterTilt",
                                            "humanizeLevel"};
    static const juce::StringArray percent{
        "ampSustain",  "modSustain", "delaySend",  "reverbSend",   "delayFeedback", "delayTone",
        "delayReturn", "reverbRoom", "reverbDamp", "reverbReturn", "satMix",        "lfoDepth"};
    static const juce::StringArray semitones{"tune", "pitchEnvAmount", "velToPitch", "lfoToPitch"};
    static const juce::StringArray octaves{"velToCutoff", "modToCutoff", "lfoToCutoff",
                                           "modWheelToCutoff"};
    static const juce::StringArray plain{
        "ampAttackCurve",    "ampDecayCurve", "ampReleaseCurve", "modToType", "lfoToType",
        "lfoToSampleSelect", "ampVelocity",   "filterType",      "ftype",     "filterResonance",
        "loopStart",         "loopEnd",       "masterWidth"};

    if (seconds.contains(base))
        formatSeconds(s);
    else if (millis.contains(base))
        formatMilliseconds(s);
    else if (hertz.contains(base))
        formatHz(s);
    else if (decibels.contains(base))
        formatDb(s);
    else if (percent.contains(base))
        formatPercent(s);
    else if (base == "pan")
        formatPan(s);
    else if (semitones.contains(base))
        formatSigned(s, "st");
    else if (octaves.contains(base))
        formatSigned(s, "oct");
    else if (base == "compRatio" || base == "masterCompRatio")
        setFormat(
            s, [](double v) { return juce::String(v, 1) + ":1"; },
            [](const juce::String& t) { return t.getDoubleValue(); });
    else if (base == "lfoRate")
        setFormat(
            s, [](double v) { return juce::String(v, 2) + " Hz"; },
            [](const juce::String& t) { return t.getDoubleValue(); });
    else if (base == "humanizePitch")
        setFormat(
            s, [](double v) { return juce::String(juce::roundToInt(v)) + " ct"; },
            [](const juce::String& t) { return t.getDoubleValue(); });
    else if (plain.contains(base))
        formatPlain(s);
}

// One layer of the selected cell as a self-contained strip: its sample, the most
// reached switches, a mini waveform (envelope mask + draggable start/end) and the
// core knobs with their values. All four strips are on screen at once, so a cell's
// layers read and compare at a glance; the focused strip's deeper settings (own
// envelope, filters) show in the "details" section below.
class LayerStrip : public juce::Component
{
public:
    LayerStrip(int layerIndex, juce::Colour c) : layer(layerIndex), colour(c)
    {
        setComponentID("layerStrip" + juce::String(layer));

        focusButton.setComponentID("focus");
        focusButton.setButtonText("L" + juce::String(layer + 1));
        focusButton.setTooltip("Edit this layer's details below (own envelope, filters).");
        focusButton.setColour(juce::TextButton::buttonColourId, colour);
        focusButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff101216));
        focusButton.onClick = [this] { focus(); };
        addAndMakeVisible(focusButton);

        loadButton.setComponentID("load");
        loadButton.setTooltip("Load a sample into this layer.");
        loadButton.onClick = [this]
        {
            focus();
            if (onLoad)
                onLoad();
        };
        addAndMakeVisible(loadButton);

        setupToggle(muteButton, "mute", "M", "Mute this layer.", juce::Colour(0xffd65a5a));
        setupToggle(soloButton, "solo", "S",
                    "Solo this layer: if any layer is soloed, only soloed layers sound.",
                    juce::Colour(0xffe0c341));
        setupToggle(reverseButton, "reverse", "REV", "Play this layer backwards.",
                    juce::Colour(0xff58b0f0));
        // The attachment updates the param from its listener before onClick runs, so
        // the waveform can re-read the new reverse state here.
        reverseButton.onClick = [this]
        {
            if (onReverseChanged)
                onReverseChanged();
        };

        wave.setComponentID("wave");
        wave.setBaseColour(colour);
        wave.setEmptyText("empty");
        wave.onPress = [this] { focus(); };
        addAndMakeVisible(wave);

        setupKnob(gainKnob, gainCaption, "gain", "GAIN", "Layer level.");
        setupKnob(tuneKnob, tuneCaption, "tune", "TUNE",
                  "Layer pitch in semitones. Changes playback speed, so it reshapes the "
                  "envelope mask on the waveform.");
        setupKnob(panKnob, panCaption, "pan", "PAN", "Layer stereo position: left .. right.");
    }

    std::function<void()> onFocus;          // make this the layer shown in "details"
    std::function<void()> onLoad;           // open a file chooser for this layer
    std::function<void()> onReverseChanged; // re-mirror the waveform

    [[nodiscard]] WaveformDisplay& waveform() noexcept { return wave; }

    // Point every control at (track, this layer).
    void attach(juce::AudioProcessorValueTreeState& apvts, int track)
    {
        sliderAtt.clear();
        buttonAtt.clear();
        auto slider = [&](const char* base, juce::Slider& s)
        {
            sliderAtt.push_back(
                std::make_unique<APVTS::SliderAttachment>(apvts, layerId(track, layer, base), s));
        };
        auto button = [&](const char* base, juce::Button& b)
        {
            buttonAtt.push_back(
                std::make_unique<APVTS::ButtonAttachment>(apvts, layerId(track, layer, base), b));
        };
        slider("gain", gainKnob);
        slider("tune", tuneKnob);
        slider("pan", panKnob);
        button("mute", muteButton);
        button("solo", soloButton);
        button("reverse", reverseButton);
        formatKnobs(); // after the attachments, which install the param's raw formatting
    }

    void setSampleName(const juce::String& name)
    {
        loadButton.setButtonText(name.isEmpty() ? u8("Load…") : name);
        loadButton.setColour(juce::TextButton::buttonColourId,
                             name.isEmpty() ? juce::Colour(0xff21252b) : juce::Colour(0xff262c35));
        loadButton.setColour(juce::TextButton::textColourOffId,
                             name.isEmpty() ? juce::Colour(0xff8a94a3) : juce::Colour(0xffe6ebf2));
    }

    void setFocused(bool shouldBeFocused)
    {
        if (focused != shouldBeFocused)
        {
            focused = shouldBeFocused;
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent&) override { focus(); }

    void paint(juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(juce::Colour(0xff181b21));
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(focused ? colour : juce::Colour(0xff2a2f37));
        g.drawRoundedRectangle(r, 6.0f, focused ? 2.0f : 1.0f);
    }

    void resized() override
    {
        constexpr int gap = 4;
        auto b = getLocalBounds().reduced(8, 6);

        // Left: tag + sample name, then the switches.
        auto left = b.removeFromLeft(176);
        auto row1 = left.removeFromTop(26);
        focusButton.setBounds(row1.removeFromLeft(34));
        row1.removeFromLeft(gap);
        loadButton.setBounds(row1);
        left.removeFromTop(8);
        auto row2 = left.removeFromTop(24);
        muteButton.setBounds(row2.removeFromLeft(34));
        row2.removeFromLeft(gap);
        soloButton.setBounds(row2.removeFromLeft(34));
        row2.removeFromLeft(gap);
        reverseButton.setBounds(row2.removeFromLeft(48));

        // Right: the core knobs, caption above and value below each.
        constexpr int knobW = 62;
        auto knobs = b.removeFromRight(3 * knobW + 2 * gap);
        for (auto [knob, caption] :
             {std::pair{&gainKnob, &gainCaption}, std::pair{&tuneKnob, &tuneCaption},
              std::pair{&panKnob, &panCaption}})
        {
            auto col = knobs.removeFromLeft(knobW);
            knobs.removeFromLeft(gap);
            caption->setBounds(col.removeFromTop(12));
            knob->setBounds(col);
        }

        // Middle: the waveform takes whatever is left.
        b.removeFromLeft(10);
        b.removeFromRight(10);
        wave.setBounds(b);
    }

private:
    void focus()
    {
        if (onFocus)
            onFocus();
    }

    // Musician-readable values: gain in dB, tune in signed semitones, pan as C/L/R
    // (no "-0.00"), with typed text parsed back the same way.
    void formatKnobs()
    {
        gainKnob.textFromValueFunction = [](double v)
        {
            if (v <= 1.0e-4)
                return juce::String("-inf dB");
            const double db = juce::Decibels::gainToDecibels(v);
            return juce::String(std::abs(db) < 0.05 ? 0.0 : db, 1) + " dB";
        };
        gainKnob.valueFromTextFunction = [](const juce::String& t)
        {
            if (t.containsIgnoreCase("inf"))
                return 0.0;
            return juce::Decibels::decibelsToGain(t.getDoubleValue());
        };

        gainKnob.updateText();
        formatSigned(tuneKnob, "st");
        formatPan(panKnob);
    }

    void setupToggle(juce::TextButton& b, const char* id, const char* text, const char* tip,
                     juce::Colour onColour)
    {
        b.setComponentID(id);
        b.setButtonText(text);
        b.setTooltip(tip);
        b.setClickingTogglesState(true);
        b.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff21252b));
        b.setColour(juce::TextButton::buttonOnColourId, onColour);
        b.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff8a94a3));
        b.setColour(juce::TextButton::textColourOnId, juce::Colour(0xff101216));
        addAndMakeVisible(b);
    }

    void setupKnob(juce::Slider& s, juce::Label& caption, const char* id, const char* name,
                   const char* tip)
    {
        s.setComponentID(id);
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, knobTextW, 14);
        s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        s.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xffd6dbe4));
        s.setTooltip(tip);
        addAndMakeVisible(s);

        caption.setText(name, juce::dontSendNotification);
        caption.setJustificationType(juce::Justification::centred);
        caption.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
        caption.setColour(juce::Label::textColourId, juce::Colour(0xff8a94a3));
        addAndMakeVisible(caption);
    }

    static constexpr int knobTextW = 60;

    const int layer;
    const juce::Colour colour;
    bool focused = false;

    juce::TextButton focusButton, loadButton;
    juce::TextButton muteButton, soloButton, reverseButton;
    WaveformDisplay wave;
    juce::Slider gainKnob, tuneKnob, panKnob;
    juce::Label gainCaption, tuneCaption, panCaption;

    std::vector<std::unique_ptr<APVTS::SliderAttachment>> sliderAtt;
    std::vector<std::unique_ptr<APVTS::ButtonAttachment>> buttonAtt;
};

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
        buildLayerStrips(); // the selected cell's 4 layers, pinned under the pads
        // Track panel FIRST so "Selected cell" + the focused layer's details sit at the
        // top of the Main tab (loading/editing layers is the core workflow); the
        // global sound-shaping sections follow below.
        buildTrackPanel();
        buildGlobals();
        buildTabs(); // after all sections exist

        selectCell(proc.selectedTrack()); // initial attachments + pad highlight
        startTimerHz(5);                  // poll the log ring for the Logs tab
    }

    ~Content() override
    {
        stopTimer(); // before members are destroyed, so no late callback touches them
        dsp::Logger::instance().log("editor closed");
    }

    // ---- tabs ----------------------------------------------------------------

    // The drawer below the layer strips: each tab holds one small group (at most two
    // rows of controls), so the whole editor fits on one screen with no scrolling.
    // A section's tab is set explicitly when it is built (never inferred from its
    // title). "Logs" is special: a live view of the debug log ring.
    enum Tab
    {
        kLayerTab,
        kCellEnvTab,
        kToneTab,
        kFxTab,
        kModTab,
        kSendsTab,
        kMasterTab,
        kGlobalTab,
        kLogsTab,
        kNumTabs
    };
    // Placements for sections that are always visible, outside the drawer.
    static constexpr int kPinnedGrid = -1;    // the cell pads
    static constexpr int kPinnedCellBar = -2; // the selected cell's own controls

    static constexpr const char* kTabNames[kNumTabs] = {"Layer", "Cell Env", "Tone",   "FX",  "Mod",
                                                        "Sends", "Master",   "Global", "Logs"};
    static constexpr const char* kTabIds[kNumTabs] = {"tabLayer",  "tabCellEnv", "tabTone",
                                                      "tabFX",     "tabMod",     "tabSends",
                                                      "tabMaster", "tabGlobal",  "tabLogs"};

    void buildTabs()
    {
        for (int i = 0; i < kNumTabs; ++i)
        {
            auto btn = std::make_unique<juce::TextButton>(kTabNames[i]);
            btn->setComponentID(kTabIds[i]);
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

    void refreshTabs()
    {
        for (int i = 0; i < static_cast<int>(tabButtons.size()); ++i)
            tabButtons[static_cast<size_t>(i)]->setColour(
                juce::TextButton::buttonColourId,
                i == currentTab ? juce::Colour(0xff2b6cb0) : juce::Colour(0xff21252b));
        // Refresh the log view immediately when switching onto the Logs tab.
        if (currentTab == kLogsTab)
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
        if (!isShowing())
            return;
        if (currentTab == kLogsTab)
        {
            refreshLogs();
        }
        else
        {
            // Live-update every strip's envelope mask as knobs move.
            refreshEnvOverlay();
        }
    }

    struct Section
    {
        juce::Label* header;
        std::vector<juce::Component*> cells; // LabeledControl* (or pads for the grid)
        int tab = 0;                         // drawer tab (Tab), or kPinnedGrid / kPinnedCellBar
    };

    // Drawer / cell-bar control cell size: compact so a tab never needs more than
    // two rows and the editor fits on one screen.
    static constexpr int kCellW = 72;
    static constexpr int kComboW = 96; // combos need room for "Channel" / "One-Shot"
    static constexpr int kCellH = 64;

    // A cell's width: wider for a combo box, so its choice is not cut off.
    static int cellWidth(juce::Component* cell)
    {
        if (auto* lc = dynamic_cast<LabeledControl*>(cell))
            if (dynamic_cast<juce::ComboBox*>(&lc->inner()) != nullptr)
                return kComboW;
        return kCellW;
    }

    // Flow the given sections left to right, each a small header over its controls,
    // wrapping to a new line when a section does not fit (a section wider than the
    // line wraps its own controls). Returns the y below the last line.
    static int layoutFlow(const std::vector<Section*>& secs, int x0, int y0, int lineW)
    {
        constexpr int headerH = 16;
        constexpr int gap = 6;
        constexpr int sectionGap = 14;

        int x = x0;
        int lineTop = y0;
        int bottom = y0;
        for (auto* s : secs)
        {
            int secW = -gap;
            for (auto* cell : s->cells)
                secW += cellWidth(cell) + gap;
            if (x > x0 && x + secW > x0 + lineW)
            {
                x = x0; // this section does not fit on the current line
                lineTop = bottom + gap;
            }
            s->header->setBounds(x, lineTop, juce::jmin(juce::jmax(secW, 60), x0 + lineW - x),
                                 headerH);

            int cx = x;
            int cy = lineTop + headerH;
            for (auto* cell : s->cells)
            {
                const int w = cellWidth(cell);
                if (cx > x && cx + w > x0 + lineW)
                {
                    cx = x0; // wrap within a section that is wider than the line
                    cy += kCellH + gap;
                }
                cell->setBounds(cx, cy, w, kCellH);
                cx += w + gap;
            }
            bottom = juce::jmax(bottom, cy + kCellH);
            const bool wrapped = cy > lineTop + headerH;
            if (wrapped)
            {
                x = x0; // continue below a wrapped section
                lineTop = bottom + gap;
            }
            else
            {
                x = cx - gap + sectionGap;
            }
        }
        return bottom;
    }

    void resized() override
    {
        constexpr int pad = 10;
        constexpr int gap = 6;
        constexpr int padH = 32;   // cell pad height
        constexpr int padCols = 8; // two rows of eight
        constexpr int stripH = 76;
        constexpr int tabH = 26;
        constexpr int titleW = 118; // cell-bar title column

        const int width = juce::jmax(getWidth(), 320);
        const int inner = width - 2 * pad;
        int y = pad;

        // 1. Cell pads: two rows of eight (the grid section carries no header).
        for (auto& s : sections)
        {
            if (s.tab != kPinnedGrid)
                continue;
            s.header->setVisible(false);
            const int padW = (inner - (padCols - 1) * gap) / padCols;
            for (size_t i = 0; i < s.cells.size(); ++i)
            {
                const int col = static_cast<int>(i) % padCols;
                const int row = static_cast<int>(i) / padCols;
                s.cells[i]->setBounds(pad + col * (padW + gap), y + row * (padH + gap), padW, padH);
            }
            const int rows = (static_cast<int>(s.cells.size()) + padCols - 1) / padCols;
            y += rows * (padH + gap);
        }

        // 2. Cell bar: the selected cell's own controls in one row, titled at left.
        for (auto& s : sections)
        {
            if (s.tab != kPinnedCellBar)
                continue;
            s.header->setVisible(true);
            s.header->setBounds(pad, y, titleW, kCellH);
            int x = pad + titleW;
            for (auto* cell : s.cells)
            {
                // The cell bar has room to spare: show "Round-Robin" in full.
                const int w = cellWidth(cell) == kComboW ? 124 : cellWidth(cell);
                cell->setVisible(true);
                cell->setBounds(x, y, w, kCellH);
                x += w + gap;
            }
            y += kCellH + gap;
        }

        // 3. The selected cell's four layer strips.
        for (auto& strip : strips)
        {
            strip->setBounds(pad, y, inner, stripH);
            y += stripH + gap;
        }

        // 4. Tab bar for the drawer below.
        const int nTabs = static_cast<int>(tabButtons.size());
        const int tabW = (inner - gap * (nTabs - 1)) / juce::jmax(1, nTabs);
        for (int i = 0; i < nTabs; ++i)
            tabButtons[static_cast<size_t>(i)]->setBounds(pad + i * (tabW + gap), y, tabW, tabH);
        y += tabH + gap;

        // 5. The drawer: only the active tab's sections (or the log console).
        const bool onLogs = (currentTab == kLogsTab);
        std::vector<Section*> active;
        for (auto& s : sections)
        {
            if (s.tab < 0)
                continue; // pinned, laid out above
            const bool vis = !onLogs && s.tab == currentTab;
            s.header->setVisible(vis);
            for (auto* c : s.cells)
                c->setVisible(vis);
            if (vis)
                active.push_back(&s);
        }

        logView.setVisible(onLogs);
        logClearButton.setVisible(onLogs);
        if (onLogs)
        {
            constexpr int btnH = 22;
            constexpr int logH = 136; // the same footprint as a two-row tab
            logClearButton.setBounds(width - pad - 80, y, 80, btnH);
            logView.setBounds(pad, y + btnH + gap, inner - 0, logH);
            y += btnH + gap + logH;
            refreshLogs(true);
        }
        else
        {
            y = layoutFlow(active, pad, y, inner);
        }
        y += pad;

        if (y != getHeight())
            setSize(width, y); // the Viewport only scrolls if the window is shrunk

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

    // Refresh each layer strip's sample name and the focus highlight.
    void refreshLayerButtons()
    {
        for (int l = 0; l < kNumLayers; ++l)
        {
            auto& strip = *strips[static_cast<size_t>(l)];
            strip.setSampleName(proc.slotSampleName(slotForLayer(l)));
            strip.setFocused(l == currentLayer);
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

    juce::Button* makeToggle()
    {
        auto t = std::make_unique<OnOffButton>();
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

    // A new section placed on drawer tab `tab` (or pinned: kPinnedGrid /
    // kPinnedCellBar). The tab is explicit so placement never depends on the title.
    Section& newSection(const juce::String& title, int tab)
    {
        auto h = std::make_unique<juce::Label>();
        h->setText(title, juce::dontSendNotification);
        h->setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        h->setColour(juce::Label::textColourId, juce::Colour(0xff8fd0ff));
        h->setBorderSize({});
        auto* hp = h.get();
        addAndMakeVisible(*hp);
        owned.push_back(std::move(h));
        sections.push_back({hp, {}, tab});
        return sections.back();
    }

    // Global knob / combo / toggle: attach once, permanently.
    void gKnob(Section& s, const juce::String& id, const juce::String& name,
               const juce::String& tip = {})
    {
        auto* sl = makeSlider();
        sl->setComponentID(id);
        globalAtt.slider.push_back(std::make_unique<APVTS::SliderAttachment>(proc.apvts, id, *sl));
        formatSlider(*sl, id);
        place(s, name, tip);
    }
    void gCombo(Section& s, const juce::String& id, const juce::String& name,
                const juce::String& tip = {})
    {
        auto* cb = makeCombo(id);
        cb->setComponentID(id);
        globalAtt.combo.push_back(std::make_unique<APVTS::ComboBoxAttachment>(proc.apvts, id, *cb));
        place(s, name, tip);
    }
    void gToggle(Section& s, const juce::String& id, const juce::String& name,
                 const juce::String& tip = {})
    {
        auto* tg = makeToggle();
        tg->setComponentID(id);
        globalAtt.button.push_back(std::make_unique<APVTS::ButtonAttachment>(proc.apvts, id, *tg));
        place(s, name, tip);
    }

    // Per-track knob: the control persists, but its attachment is (re)built for the
    // selected track in setTrack(). `base` is the un-prefixed id (e.g. "ampAttack").
    void tKnob(Section& s, const juce::String& base, const juce::String& name,
               const juce::String& tip = {})
    {
        auto* sl = makeSlider();
        sl->setComponentID(base);
        perTrackKnobs.push_back({sl, base});
        place(s, name, tip);
    }

    // Per-track toggle: like tKnob, its attachment is rebuilt in setTrack().
    void tToggle(Section& s, const juce::String& base, const juce::String& name,
                 const juce::String& tip = {})
    {
        auto* tg = makeToggle();
        tg->setComponentID(base);
        perTrackToggles.push_back({tg, base});
        place(s, name, tip);
    }

    // ---- section construction ------------------------------------------------

    void buildGlobals()
    {
        {
            auto& s = newSection("Output & MIDI", kGlobalTab);
            const auto nTracks = juce::String(dsp::MultiEngine::kNumTracks);
            gKnob(s, "gain", "Gain",
                  "Output gain in dB, applied to the mix of all " + nTracks + " cells.");
            gKnob(s, "polyphony", "Poly",
                  "Voices per track. 1 = mono (Digitakt-style); higher lets notes overlap "
                  "and chords ring.");
            gKnob(s, "rootNote", "Root",
                  "MIDI note that plays a sample at its natural pitch. Other notes repitch "
                  "it varispeed-style.");
            gKnob(s, "pitchBendRange", "Bend",
                  "MIDI pitch-wheel range in semitones (bend at full deflection). 0 = ignore "
                  "the wheel.");
            gKnob(s, "modWheelToCutoff", u8("Wheel→Cut"),
                  "MIDI mod wheel (CC1) -> filter cutoff: octaves of offset at full wheel. "
                  "+ opens, - closes; 0 = ignore. Needs a cell filter on.");
            gCombo(s, "midiAddressing", "MIDI",
                   "How MIDI picks a track. Channel: MIDI channel N plays track N. "
                   "Note Map: one channel, the note number picks the track (GM-drum style).");
        }
        {
            auto& s = newSection("Amp Envelope", kCellEnvTab);
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
            auto& s = newSection("Mod Envelope", kModTab);
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
            auto& s = newSection("Filter", kToneTab);
            tKnob(s, "filterCutoff", "Cutoff", "This cell's filter cutoff frequency, in Hz.");
            tKnob(s, "filterResonance", "Reso",
                  "Filter resonance (Q): emphasis at the cutoff frequency.");
            tKnob(s, "filterType", "Type",
                  "Morphing filter character: 0 = low-pass, 0.33 = band-pass, "
                  "0.66 = high-pass, 1 = notch — everything in between blends.");
        }
        {
            auto& s = newSection("Pitch Envelope", kCellEnvTab);
            tKnob(s, "pitchEnvAmount", "Amount",
                  "Drum 'blip': the pitch is offset by this many semitones at the "
                  "note start, then decays back. + sweeps down from above, - up. 0 = off.");
            tKnob(s, "pitchEnvDecay", "Decay",
                  "How long the pitch envelope takes to return to the note pitch, in "
                  "seconds. Short = a snappy click; longer = a falling 'dooo'.");
        }
        {
            auto& s = newSection("Velocity", kToneTab);
            tKnob(s, "velToPitch", u8("Vel→Pitch"),
                  "Velocity modulates pitch: this many semitones at full velocity, "
                  "scaled down for softer hits. + = harder plays higher, - = lower, "
                  "0 = off. (Velocity→volume is the Amp Envelope 'Vel' knob.)");
            tKnob(s, "velToCutoff", u8("Vel→Cutoff"),
                  "Velocity modulates the cell filter cutoff: this many octaves at full "
                  "velocity (+ = harder opens the filter). Needs the Filter on; 0 = off.");
        }
        {
            auto& s = newSection("Humanize", kToneTab);
            tKnob(s, "humanizePitch", "Pitch",
                  "Random pitch variation per hit, in cents (+/-). Adds subtle detune so "
                  "repeated hits aren't identical. 0 = off.");
            tKnob(s, "humanizeLevel", "Level",
                  "Random level variation per hit, in dB (+/-). Loosens up machine-gun "
                  "repeats. 0 = off.");
        }
        {
            auto& s = newSection("Compressor", kFxTab);
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
            auto& s = newSection("Saturation", kFxTab);
            tToggle(s, "satOn", "On", "Enable this cell's drive / saturation (warmth, grit).");
            tKnob(s, "satDrive", "Drive",
                  "How hard the signal is pushed into the tanh shaper, in dB. More = "
                  "more harmonics / grit.");
            tKnob(s, "satMix", "Mix",
                  "Dry/wet blend: 0 = clean, 1 = fully saturated. Lower for parallel "
                  "drive that keeps the transient.");
        }
        {
            auto& s = newSection("Lo-Fi", kFxTab);
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
            auto& s = newSection("LFO", kModTab);
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
            auto& s = newSection("Loop", kGlobalTab);
            gCombo(s, "loopMode", "Mode",
                   "One-Shot: play once. Forward: repeat the loop region. Ping-Pong: "
                   "bounce back and forth inside it. Loop speed follows pitch.");
            gKnob(s, "loopStart", "Start", "Loop region start, as a fraction of the sample.");
            gKnob(s, "loopEnd", "End", "Loop region end, as a fraction of the sample.");
        }
        {
            auto& s = newSection("Delay", kSendsTab);
            gKnob(s, "delayTime", "Time", "Echo time in milliseconds (used when Sync is off).");
            gToggle(s, "delaySync", "Sync",
                    "Lock the echo time to the host tempo (Division below).");
            gCombo(s, "delayDivision", "Division",
                   "Echo time as a musical division of the tempo when Sync is on.");
            gKnob(s, "delayFeedback", "Feedback",
                  "How much of each echo feeds the next (0..0.95): more = longer trail.");
            gKnob(s, "delayTone", "Tone",
                  "Feedback damping: 0 = bright repeats, 1 = darker each echo.");
            gKnob(s, "delayReturn", "Return", "Level of the delay mixed back into the master.");
        }
        {
            auto& s = newSection("Reverb", kSendsTab);
            gKnob(s, "reverbRoom", "Room", "Reverb size / tail length (0..1).");
            gKnob(s, "reverbDamp", "Damp", "High-frequency decay: higher = darker tail.");
            gKnob(s, "reverbReturn", "Return", "Level of the reverb mixed back into the master.");
        }
        {
            auto& s = newSection("Tilt EQ", kMasterTab);
            gToggle(s, "masterTiltOn", "On",
                    "Tilt tone control on the whole mix: brighter or darker in one knob.");
            gKnob(s, "masterTilt", "Tilt",
                  "Spectral tilt in dB: positive brightens (lifts highs, cuts lows), negative "
                  "darkens, zero is flat.");
            gKnob(s, "masterTiltPivot", "Pivot",
                  "The balance frequency the tilt rotates around, in Hz.");
        }
        {
            auto& s = newSection("Glue Comp", kMasterTab);
            gToggle(s, "masterCompOn", "On",
                    "Glue compressor on the whole mix (before the master gain / limiter).");
            gKnob(s, "masterCompThreshold", "Thresh",
                  "Level in dBFS above which the master compressor starts working.");
            gKnob(s, "masterCompRatio", "Ratio", "Compression ratio above the threshold.");
            gKnob(s, "masterCompAttack", "Attack", "Master compressor attack time, in seconds.");
            gKnob(s, "masterCompRelease", "Release", "Master compressor release time, in seconds.");
            gKnob(s, "masterCompMakeup", "Makeup",
                  "Make-up gain (dB) added after the master compression.");
        }
        {
            auto& s = newSection("Width", kMasterTab);
            gToggle(s, "masterWidthOn", "On", "Stereo-width control on the whole mix (mid/side).");
            gKnob(s, "masterWidth", "Width", "Stereo width: 0 = mono, 1 = unchanged, 2 = wide.");
        }
        {
            auto& s = newSection("Limiter", kMasterTab);
            gToggle(s, "limiterOn", "On",
                    "Brickwall limiter on the final output: holds the master at the ceiling "
                    "so a dense mix can't clip.");
            gKnob(s, "limiterCeiling", "Ceiling",
                  "Maximum output level in dBFS; nothing exceeds it while the limiter is on.");
            gKnob(s, "limiterRelease", "Release",
                  "How fast the limiter's gain recovers after a peak, in milliseconds.");
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
        auto& s = newSection({}, kPinnedGrid);
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
            ptr->setComponentID("cellPad" + juce::String(t));
            addAndMakeVisible(*ptr);
            cellPads.push_back(ptr);
            owned.push_back(std::move(btn));
            s.cells.push_back(ptr);
        }
    }

    // The selected cell's four layers as always-visible strips (see LayerStrip).
    void buildLayerStrips()
    {
        for (int l = 0; l < kNumLayers; ++l)
        {
            auto strip = std::make_unique<LayerStrip>(l, kLayerColours[static_cast<size_t>(l)]);
            strip->onFocus = [this, l] { selectLayer(l); };
            strip->onLoad = [this, l] { chooseForLayer(l); };
            strip->onReverseChanged = [this] { updateWaveform(); };
            strip->waveform().onWindowChanged = [this, l](float startFrac, float endFrac)
            { setLayerWindow(l, startFrac, endFrac); };
            addAndMakeVisible(*strip);
            strips[static_cast<size_t>(l)] = std::move(strip);
        }
    }

    // Dragging a strip's markers sets that layer's playable window (in frames).
    void setLayerWindow(int layer, float startFrac, float endFrac)
    {
        const int slot = slotForLayer(layer);
        const int total = proc.slotTotalFrames(slot);
        if (total <= 0)
            return;
        const int start = juce::roundToInt(startFrac * static_cast<float>(total));
        const int length =
            juce::jmax(1, juce::roundToInt((endFrac - startFrac) * static_cast<float>(total)));
        proc.setSlotStartFrames(slot, start);
        proc.setSlotLengthFrames(slot, length);
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
        if (cellBarTitle != nullptr)
            cellBarTitle->setText("Cell " + juce::String(currentTrack + 1),
                                  juce::dontSendNotification);
        refreshCellPads();
        updateWaveform();
        if (onBankChanged)
            onBankChanged();
    }

    // Pull each of the selected cell's layers into its strip's waveform (peaks at the
    // strip's pixel width, the playable window, reverse mirroring, envelope mask).
    void updateWaveform()
    {
        for (int l = 0; l < kNumLayers; ++l)
        {
            auto& wave = strips[static_cast<size_t>(l)]->waveform();
            if (auto* rev = proc.apvts.getRawParameterValue(layerId(currentTrack, l, "reverse")))
                wave.setReversed(rev->load() > 0.5f);

            const int slot = slotForLayer(l);
            const int total = proc.slotTotalFrames(slot);
            if (total <= 0)
            {
                wave.clear();
                wave.setEnvelope({});
                continue;
            }
            const int bins = juce::jmax(1, wave.getWidth());
            const float startFrac =
                static_cast<float>(proc.slotWindowStart(slot)) / static_cast<float>(total);
            const float endFrac =
                static_cast<float>(proc.slotWindowStart(slot) + proc.slotWindowLength(slot)) /
                static_cast<float>(total);
            wave.setContent(proc.slotWaveform(slot, bins), startFrac, endFrac);
        }
        refreshEnvOverlay();
    }

    // The amp-envelope contour for layer `l` of the current cell, its time axis scaled
    // by that layer's tune (higher tune plays faster, so the fixed-time envelope covers
    // a different span of the sample). Empty if the layer is unloaded.
    [[nodiscard]] std::vector<float> layerEnvCurve(int l, int cols) const
    {
        const int slot = slotForLayer(l);
        const int windowFrames = proc.slotWindowLength(slot);
        const double sr = proc.slotSampleRate(slot);
        if (windowFrames <= 0 || sr <= 0.0 || cols <= 0)
            return {};

        auto val = [this](const juce::String& id) -> float
        {
            auto* p = proc.apvts.getRawParameterValue(id);
            return p != nullptr ? p->load() : 0.0f;
        };
        const int t = currentTrack;

        // Layer's own envelope when Env On, else the cell's shared amp envelope.
        // Curves/velocity are shared (per cell) either way.
        dsp::EnvelopeShape shape;
        if (val(layerId(t, l, "ampOn")) > 0.5f)
        {
            shape.delay = val(layerId(t, l, "ampDelay"));
            shape.attack = val(layerId(t, l, "ampAttack"));
            shape.hold = val(layerId(t, l, "ampHold"));
            shape.decay = val(layerId(t, l, "ampDecay"));
            shape.sustain = val(layerId(t, l, "ampSustain"));
            shape.release = val(layerId(t, l, "ampRelease"));
        }
        else
        {
            shape.delay = val(trackId(t, "ampDelay"));
            shape.attack = val(trackId(t, "ampAttack"));
            shape.hold = val(trackId(t, "ampHold"));
            shape.decay = val(trackId(t, "ampDecay"));
            shape.sustain = val(trackId(t, "ampSustain"));
            shape.release = val(trackId(t, "ampRelease"));
        }
        shape.attackCurve = val(trackId(t, "ampAttackCurve"));
        shape.decayCurve = val(trackId(t, "ampDecayCurve"));
        shape.releaseCurve = val(trackId(t, "ampReleaseCurve"));

        const double ratio = std::pow(2.0, static_cast<double>(val(layerId(t, l, "tune"))) / 12.0);
        const double totalSeconds = static_cast<double>(windowFrames) / (sr * ratio);
        return dsp::sampleEnvelopeShape(shape, totalSeconds, cols);
    }

    // Each strip's envelope mask (its layer's own envelope, or the cell's, scaled by
    // that layer's tune).
    void refreshEnvOverlay()
    {
        for (int l = 0; l < kNumLayers; ++l)
        {
            auto& wave = strips[static_cast<size_t>(l)]->waveform();
            wave.setEnvelope(layerEnvCurve(l, juce::jmax(1, wave.getWidth())));
        }
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

            pad->setButtonText(juce::String(t + 1) + (loaded ? "  " + name : u8("  ·")) + badge);

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
        auto& s = newSection("Cell 1", kPinnedCellBar);
        cellBarTitle = s.header;
        cellBarTitle->setComponentID("cellBarTitle");
        cellBarTitle->setFont(juce::Font(juce::FontOptions(15.0f, juce::Font::bold)));
        cellBarTitle->setJustificationType(juce::Justification::centredLeft);

        // Layer mode is per-track; its attachment is rebuilt on cell change.
        layerModeCombo = makeCombo("t0_layerMode");
        layerModeCombo->setComponentID("layerMode");
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

        tKnob(s, "level", "Level", "This cell's output level into the mix, in dB.");
        tKnob(s, "pan", "Pan", "This cell's stereo position in the mix: -1 left .. +1 right.");
        tKnob(s, "delaySend", "Delay",
              "How much of this cell is sent to the shared delay (0 = dry). Set the delay "
              "itself in the FX tab.");
        tKnob(s, "reverbSend", "Reverb",
              "How much of this cell is sent to the shared reverb (0 = dry). Set the reverb "
              "itself in the FX tab.");

        // The focused layer's deeper settings (its strip above carries the core ones).
        // The controls re-attach to whichever strip is focused (selectLayer).
        auto& row = newSection("Layer 1 details", kLayerTab);
        layerDetailsHeader = row.header;
        layerDetailsHeader->setComponentID("layerDetailsHeader");
        auto& c = layerCtl;

        // Own amp envelope first: per-layer envelopes are the essence of layering.
        c.envOn = makeToggle();
        place(row, "Own Env",
              "Give this layer its OWN amp envelope instead of the cell's shared one "
              "(e.g. a fast click layer + a long tail layer). Its shape is drawn on the "
              "layer's waveform.");
        c.envDelay = makeSlider();
        place(row, "Delay", "This layer's envelope: silence before the attack (seconds).");
        c.envAttack = makeSlider();
        place(row, "Attack", "This layer's envelope attack (fade-in), in seconds.");
        c.envHold = makeSlider();
        place(row, "Hold", "This layer's envelope hold at full level, in seconds.");
        c.envDecay = makeSlider();
        place(row, "Decay", "This layer's envelope decay to the sustain level, in seconds.");
        c.envSustain = makeSlider();
        place(row, "Sustain", "This layer's envelope sustain level (0..1).");
        c.envRelease = makeSlider();
        place(row, "Release", "This layer's envelope release after note-off, in seconds.");

        c.flt = makeToggle();
        place(row, "Filter",
              "Give this layer its OWN filter instead of the shared track filter "
              "(e.g. low-pass only the boom layer).");
        c.cutoff = makeSlider();
        c.cutoff->setComponentID("detailCutoff");
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
        {
            trackAtt.slider.push_back(std::make_unique<APVTS::SliderAttachment>(
                proc.apvts, trackId(track, k.base), *k.ctl));
            formatSlider(*k.ctl, k.base);
        }

        for (const auto& tg : perTrackToggles)
            trackAtt.button.push_back(std::make_unique<APVTS::ButtonAttachment>(
                proc.apvts, trackId(track, tg.base), *tg.ctl));

        // Every layer strip follows the cell.
        for (auto& strip : strips)
            strip->attach(proc.apvts, track);

        selectLayer(currentLayer); // re-point the details at the new cell's layer
    }

    // Focus layer `layer` of the current cell: highlight its strip and point the
    // details section (own envelope, filters) at it.
    void selectLayer(int layer)
    {
        currentLayer = juce::jlimit(0, kNumLayers - 1, layer);
        const int track = currentTrack;
        const int l = currentLayer;
        auto& c = layerCtl;

        if (layerDetailsHeader != nullptr)
            layerDetailsHeader->setText("Layer " + juce::String(l + 1) +
                                            u8(" details — own envelope + filters"),
                                        juce::dontSendNotification);

        layerAtt.slider.clear();
        layerAtt.combo.clear();
        layerAtt.button.clear();

        auto attSlider = [&](const char* base, juce::Slider* s)
        {
            layerAtt.slider.push_back(
                std::make_unique<APVTS::SliderAttachment>(proc.apvts, layerId(track, l, base), *s));
            formatSlider(*s, base);
        };
        auto attButton = [&](const char* base, juce::Button* b)
        {
            layerAtt.button.push_back(
                std::make_unique<APVTS::ButtonAttachment>(proc.apvts, layerId(track, l, base), *b));
        };
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
        attButton("hpOn", c.hpOn);
        attSlider("hpCut", c.hpCut);
        attButton("lpOn", c.lpOn);
        attSlider("lpCut", c.lpCut);

        refreshLayerButtons();
        updateWaveform();
    }

    // ---- state ---------------------------------------------------------------

    static constexpr int kNumLayers = ColdAudioProcessor::kNumLayers;

    // The focused layer's details (own envelope + filters); re-attached to the
    // focused strip's layer. The core per-layer controls live on the strips.
    struct LayerCtl
    {
        juce::Button* flt = nullptr;
        juce::Slider* cutoff = nullptr;
        juce::Slider* ftype = nullptr;
        // Per-layer amp envelope (Env On + DAHDSR).
        juce::Button* envOn = nullptr;
        juce::Slider* envDelay = nullptr;
        juce::Slider* envAttack = nullptr;
        juce::Slider* envHold = nullptr;
        juce::Slider* envDecay = nullptr;
        juce::Slider* envSustain = nullptr;
        juce::Slider* envRelease = nullptr;
        juce::Button* hpOn = nullptr;
        juce::Slider* hpCut = nullptr;
        juce::Button* lpOn = nullptr;
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
    LayerCtl layerCtl{};                       // focused layer's details, re-attached per layer
    juce::Label* layerDetailsHeader = nullptr; // "Layer N details ...", owned via `owned`
    juce::Label* cellBarTitle = nullptr;       // "Cell N" over the cell bar, owned via `owned`
    int currentLayer = 0;
    // The selected cell's four layers, always on screen.
    std::array<std::unique_ptr<LayerStrip>, static_cast<size_t>(kNumLayers)> strips;

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
        juce::Button* ctl;
        juce::String base;
    };
    std::vector<PerTrackToggle> perTrackToggles;

    std::vector<juce::TextButton*> cellPads;   // one per track, owned via `owned`
    std::vector<juce::TextButton*> tabButtons; // Main/FX/Mod/Logs, owned via `owned`
    int currentTab = 0;

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

    // Fits everything on one screen: pads, cell bar, four layer strips and a two-row
    // drawer, with no scrolling.
    setSize(900, 760);
}

ColdAudioProcessorEditor::~ColdAudioProcessorEditor()
{
    setLookAndFeel(nullptr); // detach before knobLookAndFeel is destroyed
}

void ColdAudioProcessorEditor::refreshSampleName()
{
    sampleNameLabel.setText("Cell " + juce::String(processorRef.selectedTrack() + 1) + ": " +
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
