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

TEST_CASE("BatteryKit envelope value maps to seconds/level", "[batterykit][io][unit]")
{
    // Times are milliseconds: 0 -> 0 s, 40 -> 0.040 s, huge -> clamped to the max.
    REQUIRE_THAT(batteryEnvToSeconds(0.0f), WithinAbs(0.0f, 1e-6f));
    REQUIRE_THAT(batteryEnvToSeconds(40.0f), WithinAbs(0.040f, 1e-6f));
    REQUIRE_THAT(batteryEnvToSeconds(6000.0f),
                 WithinAbs(cold::io::kBatteryTimeMaxSeconds, 1e-4f)); // clamped

    const auto kit = parseBatteryKit(kKitXml);
    // Kick decay 40 ms -> 0.040 s; sustain 0 -> level 0.
    REQUIRE_THAT(kit.cells[0].decaySeconds(), WithinAbs(0.040f, 1e-6f));
    REQUIRE_THAT(kit.cells[0].sustainLevel(), WithinAbs(0.0f, 1e-4f));
    // Snare sustain 80 -> 0.8 on the 0..100 level scale.
    REQUIRE_THAT(kit.cells[1].sustainLevel(), WithinAbs(0.8f, 1e-4f));
}

TEST_CASE("BatteryKit returns an invalid kit on junk input", "[batterykit][io][unit]")
{
    REQUIRE_FALSE(parseBatteryKit("not xml at all").valid());
    REQUIRE_FALSE(parseBatteryKit("<BatteryPatch name=\"empty\"></BatteryPatch>").valid());
}
