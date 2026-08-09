#pragma once

// A bank of sample slots (M4). Holds several decoded samples so the plugin can
// switch between them (and, from M5, assign them per track). JUCE-backed like
// SampleLoader (it decodes files), kept in the io layer so the DSP classes stay
// framework-agnostic.

#include "io/SampleLoader.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace cold::io
{

// One slot: the decoded audio plus metadata and a playable window (start/length
// in frames, Digitakt-style). An unloaded slot has empty data.
struct SampleSlot
{
    SampleData data;
    juce::String name;
    juce::String path;
    int start = 0;  // first playable frame
    int length = 0; // playable length in frames (0 => to end of the sample)

    [[nodiscard]] bool isLoaded() const { return !data.isEmpty(); }
    [[nodiscard]] int numFrames() const { return data.numFrames(); }

    // The playable window clamped to the sample bounds. For an unloaded slot this
    // is {0, 0}. length 0 means "to the end".
    [[nodiscard]] int effectiveStart() const;
    [[nodiscard]] int effectiveLength() const;
};

class SampleBank
{
public:
    explicit SampleBank(int numSlots = 128);

    [[nodiscard]] int numSlots() const { return static_cast<int>(slots.size()); }
    [[nodiscard]] int loadedCount() const;

    // Decode `file` into slot `index`, replacing any existing sample. Returns
    // false (leaving the slot unchanged) on a bad index or a decode failure.
    bool loadSlot(int index, const juce::File& file);

    // Inject already-decoded data (used by higher layers / tests).
    bool setSlot(int index, SampleData data, const juce::String& name,
                 const juce::String& path = {});

    void clearSlot(int index);

    [[nodiscard]] bool isSlotLoaded(int index) const;
    [[nodiscard]] const SampleSlot& slot(int index) const;

    // Per-slot playable window (M4). Clamped to the sample on read via
    // effectiveStart/Length; stored raw here. No-op on a bad index.
    void setSlotStart(int index, int startFrame);
    void setSlotLength(int index, int lengthFrames);

private:
    [[nodiscard]] bool inRange(int index) const;

    std::vector<SampleSlot> slots;
    SampleLoader loader;
    SampleSlot emptySlot; // returned for out-of-range queries
};

} // namespace cold::io
