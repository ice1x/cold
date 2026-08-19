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

// Battery envelope calibration.
//
// Battery 3 is 32-bit and no longer runs on modern macOS, so the mapping can't be
// A/B'd against the real plugin. It is instead grounded in evidence: (a) the .kit
// stores volume/pan/pitch as normalised 0..1 floats but the envelope v* fields as
// small integers — a different scale; (b) the Battery 3 manual specifies the
// envelope times in milliseconds (its "10-30 ms hold" tip matches the stored hold
// values 25-50 seen across real kits); (c) sustain is universally 80 in those kits
// — a level on a 0..100 scale (0.8), not a time.
//
// So: envelope TIMES are milliseconds (value / 1000, clamped to Cold's range) and
// SUSTAIN is a 0..100 level (value / 100). These two constants are the single
// place to adjust if a better reference ever surfaces.
inline constexpr float kBatteryTimeMaxSeconds = 5.0f; // clamp (matches Cold's env range)
inline constexpr float kBatterySustainMax = 100.0f;   // sustain level full-scale

// Map a Battery envelope-time value (milliseconds) to seconds, clamped.
[[nodiscard]] float batteryEnvToSeconds(float milliseconds);

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
