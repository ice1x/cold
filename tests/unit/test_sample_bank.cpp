#include <catch2/catch_test_macros.hpp>

#include "io/SampleBank.h"
#include "support/SignalFixtures.h"

#include <juce_audio_formats/juce_audio_formats.h>

using cold::io::SampleBank;
using cold::io::SampleData;
using cold::test::writeKnownSignal;

namespace
{
// Write a temp WAV of the given size and return the file (caller deletes).
juce::File tempWav(double sampleRate, int numChannels, int numFrames)
{
    auto file = juce::File::createTempFile(".wav");
    juce::WavAudioFormat wav;
    writeKnownSignal(wav, file, sampleRate, numChannels, numFrames);
    return file;
}

SampleData makeData(int numChannels, int numFrames)
{
    SampleData d;
    d.sampleRate = 44100.0;
    d.channels.assign(static_cast<size_t>(numChannels),
                      std::vector<float>(static_cast<size_t>(numFrames), 0.5f));
    return d;
}
} // namespace

TEST_CASE("SampleBank starts empty with the requested slot count", "[samplebank][io][unit]")
{
    SampleBank bank(16);
    REQUIRE(bank.numSlots() == 16);
    REQUIRE(bank.loadedCount() == 0);
    for (int i = 0; i < 16; ++i)
        REQUIRE_FALSE(bank.isSlotLoaded(i));
}

TEST_CASE("SampleBank loads a file into a slot with metadata", "[samplebank][io][unit]")
{
    constexpr double sr = 48000.0;
    constexpr int frames = 500;
    auto file = tempWav(sr, 2, frames);

    SampleBank bank(8);
    REQUIRE(bank.loadSlot(3, file));
    file.deleteFile();

    REQUIRE(bank.isSlotLoaded(3));
    REQUIRE(bank.loadedCount() == 1);

    const auto& s = bank.slot(3);
    REQUIRE(s.numFrames() == frames);
    REQUIRE(s.data.numChannels() == 2);
    REQUIRE(s.name.isNotEmpty());
    REQUIRE(s.path.isNotEmpty());
}

TEST_CASE("SampleBank replaces an existing slot on reload", "[samplebank][io][unit]")
{
    SampleBank bank(4);
    REQUIRE(bank.setSlot(1, makeData(1, 100), "first"));
    REQUIRE(bank.slot(1).numFrames() == 100);

    REQUIRE(bank.setSlot(1, makeData(2, 250), "second"));
    REQUIRE(bank.slot(1).numFrames() == 250);
    REQUIRE(bank.slot(1).name == juce::String("second"));
    REQUIRE(bank.loadedCount() == 1); // still one loaded slot, replaced not added
}

TEST_CASE("SampleBank clears a slot", "[samplebank][io][unit]")
{
    SampleBank bank(4);
    bank.setSlot(2, makeData(1, 64), "x");
    REQUIRE(bank.isSlotLoaded(2));

    bank.clearSlot(2);
    REQUIRE_FALSE(bank.isSlotLoaded(2));
    REQUIRE(bank.loadedCount() == 0);
}

TEST_CASE("SampleBank handles out-of-range indices gracefully", "[samplebank][io][unit]")
{
    SampleBank bank(4);
    REQUIRE_FALSE(bank.loadSlot(-1, juce::File{}));
    REQUIRE_FALSE(bank.setSlot(99, makeData(1, 10), "y"));
    REQUIRE_FALSE(bank.isSlotLoaded(99));
    REQUIRE_FALSE(bank.slot(99).isLoaded()); // returns a safe empty slot
    bank.clearSlot(99);                      // no crash
    bank.setSlotStart(99, 10);               // no crash
}

TEST_CASE("SampleBank rejects a file that cannot be decoded", "[samplebank][io][unit]")
{
    SampleBank bank(4);
    REQUIRE_FALSE(bank.loadSlot(0, juce::File("/nonexistent/path/sample.wav")));
    REQUIRE(bank.loadedCount() == 0);
}

TEST_CASE("SampleBank per-slot window defaults to the whole sample",
          "[samplebank][io][unit][window]")
{
    SampleBank bank(2);
    bank.setSlot(0, makeData(1, 1000), "full");

    REQUIRE(bank.slot(0).effectiveStart() == 0);
    REQUIRE(bank.slot(0).effectiveLength() == 1000); // length 0 => to the end
}

TEST_CASE("SampleBank per-slot start/length clamp to the sample", "[samplebank][io][unit][window]")
{
    SampleBank bank(2);
    bank.setSlot(0, makeData(1, 1000), "s");

    bank.setSlotStart(0, 200);
    bank.setSlotLength(0, 300);
    REQUIRE(bank.slot(0).effectiveStart() == 200);
    REQUIRE(bank.slot(0).effectiveLength() == 300);

    SECTION("length past the end is clamped to what remains")
    {
        bank.setSlotStart(0, 800);
        bank.setSlotLength(0, 500); // only 200 frames remain
        REQUIRE(bank.slot(0).effectiveLength() == 200);
    }

    SECTION("start past the end clamps and leaves no length")
    {
        bank.setSlotStart(0, 5000);
        REQUIRE(bank.slot(0).effectiveStart() == 1000);
        REQUIRE(bank.slot(0).effectiveLength() == 0);
    }
}
