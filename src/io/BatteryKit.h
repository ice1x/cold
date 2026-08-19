#pragma once

// Parser + parameter mapping for Native Instruments Battery 3 ".kit" files.
//
// A saved Battery kit is XML: a <BatteryPatch name="..."> holding a list of
// <SampleSlot ...> cells, each with a nested <Sample file="..."/>. Each cell
// carries its own AHDSR volume envelope (vattack/vhold/vdecay/vsustain/vrelease),
// pitch (playback ratio), key mapping (rootKey), pan and volume.
//
// This lives in the JUCE-backed io/ layer (it uses juce::XmlDocument), and maps
// each cell onto Cold's parameters so the plugin can import old kits: per-track
// amp envelope, per-layer tune/pan/gain, and a note mapping. The exact-known
// mappings (tune, pan) are exact; the Battery envelope-knob -> seconds curve is a
// documented approximation (Battery's internal time curve is proprietary).

#include <juce_core/juce_core.h>

#include <vector>

namespace cold::io
{

// Battery stores envelope times/levels as knob values on a 0..kBatteryKnobMax
// scale (assumed 127 — Battery's internal range). Times map exponentially to
// seconds so small knob values give short, drum-like envelopes; levels are
// linear. These constants are the single place to calibrate against real Battery.
inline constexpr float kBatteryKnobMax = 127.0f;
inline constexpr float kBatteryEnvMaxSeconds = 5.0f; // knob at max -> this many seconds
inline constexpr float kBatteryEnvCurve = 3.0f;      // exponent of the time curve

// Map a Battery envelope-time knob (0..kBatteryKnobMax) to seconds (approx).
[[nodiscard]] float batteryEnvToSeconds(float knob);

struct BatteryCell
{
    juce::String label;
    juce::String file; // sample filename as stored in the kit (not a full path)

    // Raw Battery values, straight from the XML.
    float vattack = 0.0f;
    float vhold = 0.0f;
    float vdecay = 0.0f;
    float vsustain = 0.0f;
    float vrelease = 0.0f;
    float pitch = 1.0f;  // playback ratio (1 = native)
    float pan = 0.5f;    // 0..1, 0.5 = centre
    float volume = 1.0f; // 0..1-ish level
    int rootKey = -1;    // MIDI note the cell is mapped to (-1 = unknown)

    // --- mapped to Cold's parameters -----------------------------------------
    // Exact: tuning is 12*log2(pitch); pan re-centres 0..1 to Cold's -1..+1.
    [[nodiscard]] float tuneSemitones() const;
    [[nodiscard]] float coldPan() const;
    [[nodiscard]] float coldGain() const; // clamp(volume, 0, 1)

    // Approximate: Battery envelope knobs -> Cold's seconds / [0,1] level.
    [[nodiscard]] float attackSeconds() const { return batteryEnvToSeconds(vattack); }
    [[nodiscard]] float holdSeconds() const { return batteryEnvToSeconds(vhold); }
    [[nodiscard]] float decaySeconds() const { return batteryEnvToSeconds(vdecay); }
    [[nodiscard]] float releaseSeconds() const { return batteryEnvToSeconds(vrelease); }
    [[nodiscard]] float sustainLevel() const; // vsustain / kBatteryKnobMax, clamped
};

struct BatteryKit
{
    juce::String name;
    std::vector<BatteryCell> cells;

    [[nodiscard]] bool valid() const { return !cells.empty(); }
};

// Parse a Battery .kit from its XML text. Returns an empty (invalid) kit if the
// text is not parseable or holds no cells.
[[nodiscard]] BatteryKit parseBatteryKit(const juce::String& xml);

// Parse a Battery .kit file from disk. Returns an empty kit on read/parse failure.
[[nodiscard]] BatteryKit parseBatteryKitFile(const juce::File& file);

} // namespace cold::io
