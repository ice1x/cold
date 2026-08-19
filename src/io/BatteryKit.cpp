#include "io/BatteryKit.h"

#include <algorithm>
#include <cmath>

namespace cold::io
{

float batteryEnvToSeconds(float milliseconds)
{
    return juce::jlimit(0.0f, kBatteryTimeMaxSeconds, milliseconds / 1000.0f);
}

float BatteryCell::tuneSemitones() const
{
    return pitch > 0.0f ? 12.0f * std::log2(pitch) : 0.0f;
}

float BatteryCell::coldPan() const
{
    return juce::jlimit(-1.0f, 1.0f, (pan - 0.5f) * 2.0f);
}

float BatteryCell::coldGain() const
{
    return juce::jlimit(0.0f, 1.0f, volume);
}

float BatteryCell::sustainLevel() const
{
    return juce::jlimit(0.0f, 1.0f, vsustain / kBatterySustainMax);
}

namespace
{

// Depth-first: collect every element whose tag matches `tag` (case-insensitive).
void collect(const juce::XmlElement& e, juce::StringRef tag,
             std::vector<const juce::XmlElement*>& out)
{
    if (e.hasTagName(tag))
        out.push_back(&e);
    for (auto* child : e.getChildIterator())
        collect(*child, tag, out);
}

// First descendant (or self) with the given tag, or nullptr.
const juce::XmlElement* firstWithTag(const juce::XmlElement& e, juce::StringRef tag)
{
    std::vector<const juce::XmlElement*> hits;
    collect(e, tag, hits);
    return hits.empty() ? nullptr : hits.front();
}

float attrFloat(const juce::XmlElement& e, juce::StringRef name, float fallback)
{
    return e.hasAttribute(name) ? static_cast<float>(e.getDoubleAttribute(name, fallback))
                                : fallback;
}

} // namespace

BatteryKit parseBatteryKit(const juce::String& xml)
{
    BatteryKit kit;

    auto root = juce::XmlDocument::parse(xml);
    if (root == nullptr)
        return kit;

    // The patch name lives on the <BatteryPatch> element (self or a descendant).
    if (const auto* patch = firstWithTag(*root, "BatteryPatch"))
        kit.name = patch->getStringAttribute("name");
    else if (root->hasAttribute("name"))
        kit.name = root->getStringAttribute("name");

    std::vector<const juce::XmlElement*> slots;
    collect(*root, "SampleSlot", slots);

    for (const auto* slot : slots)
    {
        BatteryCell cell;
        cell.label = slot->getStringAttribute("label");
        cell.vattack = attrFloat(*slot, "vattack", 0.0f);
        cell.vhold = attrFloat(*slot, "vhold", 0.0f);
        cell.vdecay = attrFloat(*slot, "vdecay", 0.0f);
        cell.vsustain = attrFloat(*slot, "vsustain", 0.0f);
        cell.vrelease = attrFloat(*slot, "vrelease", 0.0f);
        cell.pitch = attrFloat(*slot, "pitch", 1.0f);
        cell.pan = attrFloat(*slot, "pan", 0.5f);
        cell.volume = attrFloat(*slot, "volume", 1.0f);
        cell.rootKey = slot->hasAttribute("rootKey") ? slot->getIntAttribute("rootKey") : -1;

        // The sample filename is on a nested <Sample file="..."/>.
        if (const auto* sample = firstWithTag(*slot, "Sample"))
            cell.file = sample->getStringAttribute("file");

        kit.cells.push_back(std::move(cell));
    }

    return kit;
}

BatteryKit parseBatteryKitFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return {};
    return parseBatteryKit(file.loadFileAsString());
}

} // namespace cold::io
