#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "io/BatteryKit.h"

using Catch::Matchers::WithinAbs;
using cold::io::batteryEnvToSeconds;
using cold::io::BatteryKit;
using cold::io::parseBatteryKit;

namespace
{

// A minimal Battery .kit XML: two cells with the attributes the importer reads.
const juce::String kKitXml = R"(<?xml version="1.0"?>
<BatteryPatch name="Test Kit">
  <Cells>
    <SampleSlot label="Kick" vattack="0" vhold="0" vdecay="40" vsustain="0" vrelease="20"
                pitch="1.000000" rootKey="36" pan="0.500000" volume="1.000000">
      <Sample file="Kick.wav"/>
    </SampleSlot>
    <SampleSlot label="Snare" vattack="0" vhold="10" vdecay="60" vsustain="80" vrelease="40"
                pitch="2.000000" rootKey="38" pan="0.000000" volume="0.500000">
      <Sample file="Snare.wav"/>
    </SampleSlot>
  </Cells>
</BatteryPatch>)";

} // namespace

TEST_CASE("BatteryKit parses the patch name and every cell", "[batterykit][io][unit]")
{
    const auto kit = parseBatteryKit(kKitXml);
    REQUIRE(kit.valid());
    REQUIRE(kit.name == juce::String("Test Kit"));
    REQUIRE(kit.cells.size() == 2);

    const auto& kick = kit.cells[0];
    REQUIRE(kick.label == juce::String("Kick"));
    REQUIRE(kick.file == juce::String("Kick.wav"));
    REQUIRE(kick.rootKey == 36);

    const auto& snare = kit.cells[1];
    REQUIRE(snare.label == juce::String("Snare"));
    REQUIRE(snare.file == juce::String("Snare.wav"));
    REQUIRE(snare.rootKey == 38);
}

TEST_CASE("BatteryKit maps tuning and pan exactly", "[batterykit][io][unit]")
{
    const auto kit = parseBatteryKit(kKitXml);
    REQUIRE(kit.cells.size() == 2);

    // pitch 1.0 -> 0 semitones, centred pan (0.5 -> 0).
    REQUIRE_THAT(kit.cells[0].tuneSemitones(), WithinAbs(0.0f, 1e-4f));
    REQUIRE_THAT(kit.cells[0].coldPan(), WithinAbs(0.0f, 1e-4f));

    // pitch 2.0 -> +12 semitones; pan 0.0 -> hard left (-1); volume 0.5 -> gain 0.5.
    REQUIRE_THAT(kit.cells[1].tuneSemitones(), WithinAbs(12.0f, 1e-3f));
    REQUIRE_THAT(kit.cells[1].coldPan(), WithinAbs(-1.0f, 1e-4f));
    REQUIRE_THAT(kit.cells[1].coldGain(), WithinAbs(0.5f, 1e-4f));
}

TEST_CASE("BatteryKit envelope knob maps to seconds/level", "[batterykit][io][unit]")
{
    // The time curve: 0 -> 0 s, max knob -> the documented max seconds, monotone up.
    REQUIRE_THAT(batteryEnvToSeconds(0.0f), WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(batteryEnvToSeconds(cold::io::kBatteryKnobMax),
                 WithinAbs(cold::io::kBatteryEnvMaxSeconds, 1e-4f));
    REQUIRE(batteryEnvToSeconds(64.0f) > batteryEnvToSeconds(20.0f));
    REQUIRE(batteryEnvToSeconds(20.0f) > 0.0f);

    const auto kit = parseBatteryKit(kKitXml);
    // Kick decay knob 40 -> a short positive time; sustain knob 0 -> level 0.
    REQUIRE(kit.cells[0].decaySeconds() > 0.0f);
    REQUIRE_THAT(kit.cells[0].sustainLevel(), WithinAbs(0.0f, 1e-4f));
    // Snare sustain knob 80 -> 80/127 level.
    REQUIRE_THAT(kit.cells[1].sustainLevel(), WithinAbs(80.0f / cold::io::kBatteryKnobMax, 1e-4f));
}

TEST_CASE("BatteryKit returns an invalid kit on junk input", "[batterykit][io][unit]")
{
    REQUIRE_FALSE(parseBatteryKit("not xml at all").valid());
    REQUIRE_FALSE(parseBatteryKit("<BatteryPatch name=\"empty\"></BatteryPatch>").valid());
}
