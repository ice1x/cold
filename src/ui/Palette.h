#pragma once
// Cold's colour palette: warm graphite surfaces, warm white text and one signal
// accent. Every editor colour comes from here so the look stays consistent.
#include <juce_graphics/juce_graphics.h>

#include <array>
#include <cstdint>

namespace cold::ui::palette
{
namespace argb
{
constexpr std::uint32_t background = 0xff121113; // editor background
constexpr std::uint32_t surface = 0xff1b1a1d;    // panels, strips, menus, tooltips
constexpr std::uint32_t raised = 0xff262428;     // buttons, combos, empty pads
constexpr std::uint32_t edge = 0xff38353b;       // outlines, knob tracks
constexpr std::uint32_t knobBody = 0xff2c2a2f;
constexpr std::uint32_t knobRim = 0xff434046;
constexpr std::uint32_t text = 0xfff1ece4;      // values, names
constexpr std::uint32_t textMuted = 0xffa39d97; // captions, off states
constexpr std::uint32_t textFaint = 0xff6e6872; // empty pads, hints
constexpr std::uint32_t accent = 0xffff5b2e;    // values, selection, ON
constexpr std::uint32_t onAccent = 0xff121113;  // text on the accent
constexpr std::uint32_t loadedPad = 0xff3a2823; // a pad holding a sample
constexpr std::uint32_t header = 0xffff9a72;    // section titles
constexpr std::uint32_t marker = 0xffffc04d;    // waveform start / end markers
constexpr std::uint32_t mute = 0xffe5484d;
constexpr std::uint32_t solo = 0xfff5d90a;
constexpr std::array<std::uint32_t, 4> layers{0xffff5b2e, 0xffffc04d, 0xff9be36b, 0xffd58cff};
} // namespace argb

inline juce::Colour background()
{
    return juce::Colour(argb::background);
}
inline juce::Colour surface()
{
    return juce::Colour(argb::surface);
}
inline juce::Colour raised()
{
    return juce::Colour(argb::raised);
}
inline juce::Colour edge()
{
    return juce::Colour(argb::edge);
}
inline juce::Colour knobBody()
{
    return juce::Colour(argb::knobBody);
}
inline juce::Colour knobRim()
{
    return juce::Colour(argb::knobRim);
}
inline juce::Colour text()
{
    return juce::Colour(argb::text);
}
inline juce::Colour textMuted()
{
    return juce::Colour(argb::textMuted);
}
inline juce::Colour textFaint()
{
    return juce::Colour(argb::textFaint);
}
inline juce::Colour accent()
{
    return juce::Colour(argb::accent);
}
inline juce::Colour onAccent()
{
    return juce::Colour(argb::onAccent);
}
inline juce::Colour loadedPad()
{
    return juce::Colour(argb::loadedPad);
}
inline juce::Colour header()
{
    return juce::Colour(argb::header);
}
inline juce::Colour marker()
{
    return juce::Colour(argb::marker);
}
inline juce::Colour mute()
{
    return juce::Colour(argb::mute);
}
inline juce::Colour solo()
{
    return juce::Colour(argb::solo);
}
// Layer 1..4 tag / waveform colour (index 0..3, wraps).
inline juce::Colour layer(int index)
{
    return juce::Colour(argb::layers[static_cast<size_t>(index & 3)]);
}
} // namespace cold::ui::palette
