#include "io/SampleBank.h"

#include <algorithm>
#include <utility>

namespace cold::io
{

int SampleSlot::effectiveStart() const
{
    if (!isLoaded())
        return 0;
    return std::clamp(start, 0, numFrames());
}

int SampleSlot::effectiveLength() const
{
    if (!isLoaded())
        return 0;
    const int begin = effectiveStart();
    const int available = numFrames() - begin;
    if (length <= 0)
        return available; // 0 => to the end
    return std::min(length, available);
}

SampleBank::SampleBank(int numSlots)
{
    slots.resize(static_cast<size_t>(std::max(1, numSlots)));
}

bool SampleBank::inRange(int index) const
{
    return index >= 0 && index < numSlots();
}

int SampleBank::loadedCount() const
{
    int count = 0;
    for (const auto& s : slots)
        if (s.isLoaded())
            ++count;
    return count;
}

bool SampleBank::loadSlot(int index, const juce::File& file)
{
    if (!inRange(index))
        return false;

    auto decoded = loader.loadFile(file);
    if (!decoded.has_value() || decoded->isEmpty())
        return false;

    return setSlot(index, std::move(*decoded), file.getFileNameWithoutExtension(),
                   file.getFullPathName());
}

bool SampleBank::setSlot(int index, SampleData data, const juce::String& name,
                         const juce::String& path)
{
    if (!inRange(index) || data.isEmpty())
        return false;

    auto& s = slots[static_cast<size_t>(index)];
    s.data = std::move(data);
    s.name = name;
    s.path = path;
    s.start = 0;
    s.length = 0; // default: whole sample
    return true;
}

void SampleBank::clearSlot(int index)
{
    if (inRange(index))
        slots[static_cast<size_t>(index)] = SampleSlot{};
}

bool SampleBank::isSlotLoaded(int index) const
{
    return inRange(index) && slots[static_cast<size_t>(index)].isLoaded();
}

const SampleSlot& SampleBank::slot(int index) const
{
    if (!inRange(index))
        return emptySlot;
    return slots[static_cast<size_t>(index)];
}

void SampleBank::setSlotStart(int index, int startFrame)
{
    if (inRange(index))
        slots[static_cast<size_t>(index)].start = std::max(0, startFrame);
}

void SampleBank::setSlotLength(int index, int lengthFrames)
{
    if (inRange(index))
        slots[static_cast<size_t>(index)].length = std::max(0, lengthFrames);
}

} // namespace cold::io
