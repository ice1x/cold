// Integration-level tests: SampleLoader exercised across the filesystem and
// multiple formats together, as the higher layers (sample bank in M4, the
// engine in M1+) will actually use it — a step above the focused unit cases.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "io/SampleLoader.h"
#include "support/SignalFixtures.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using cold::io::SampleData;
using cold::io::SampleLoader;
using cold::test::writeKnownSignal;

namespace
{

// A scratch directory that wipes itself when the test scope exits.
struct ScopedTempDir
{
    juce::File dir{juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("cold_it_" + juce::Uuid().toString())};
    ScopedTempDir() { dir.createDirectory(); }
    ~ScopedTempDir() { dir.deleteRecursively(); }
};

bool allFinite(const SampleData& data)
{
    for (const auto& ch : data.channels)
        for (float s : ch)
            if (!std::isfinite(s))
                return false;
    return true;
}

float peak(const SampleData& data)
{
    float p = 0.0f;
    for (const auto& ch : data.channels)
        for (float s : ch)
            p = std::max(p, std::abs(s));
    return p;
}

} // namespace

TEST_CASE("Loading a folder of mixed-format samples (bank-load workflow)",
          "[sampleloader][io][integration]")
{
    ScopedTempDir tmp;

    // A heterogeneous "bank": different formats, channel counts, rates, lengths.
    struct Spec
    {
        juce::String name;
        int channels;
        double sampleRate;
        int frames;
    };
    const std::vector<Spec> specs{
        {"kick.wav", 2, 44100.0, 1024},
        {"snare.aiff", 1, 48000.0, 600},
        {"hat.wav", 2, 22050.0, 128},
        {"loop.aiff", 2, 44100.0, 22050}, // ~0.5 s
    };

    juce::WavAudioFormat wav;
    juce::AiffAudioFormat aiff;
    for (const auto& s : specs)
    {
        auto file = tmp.dir.getChildFile(s.name);
        if (s.name.endsWithIgnoreCase(".wav"))
            writeKnownSignal(wav, file, s.sampleRate, s.channels, s.frames);
        else
            writeKnownSignal(aiff, file, s.sampleRate, s.channels, s.frames);
    }

    // Iterate the folder the way a bank loader would and decode every file.
    SampleLoader loader;
    std::vector<SampleData> bank;
    for (const auto& entry :
         juce::RangedDirectoryIterator(tmp.dir, false, "*.wav;*.aiff", juce::File::findFiles))
    {
        auto decoded = loader.loadFile(entry.getFile());
        REQUIRE(decoded.has_value());
        bank.push_back(std::move(*decoded));
    }

    REQUIRE(bank.size() == specs.size());
    for (const auto& sample : bank)
    {
        REQUIRE_FALSE(sample.isEmpty());
        REQUIRE(allFinite(sample));
        REQUIRE(peak(sample) <= 1.0f + 1.0e-3f); // decoded data stays in range
        REQUIRE(sample.sampleRate > 0.0);
    }
}

TEST_CASE("Decoding is deterministic across repeated loads", "[sampleloader][io][integration]")
{
    ScopedTempDir tmp;
    auto file = tmp.dir.getChildFile("repeat.wav");
    juce::WavAudioFormat wav;
    writeKnownSignal(wav, file, 44100.0, 2, 777);

    SampleLoader loader;
    auto first = loader.loadFile(file);
    auto second = loader.loadFile(file);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());

    REQUIRE(first->numChannels() == second->numChannels());
    REQUIRE(first->numFrames() == second->numFrames());
    REQUIRE_THAT(first->sampleRate, WithinAbs(second->sampleRate, 0.0));
    for (int ch = 0; ch < first->numChannels(); ++ch)
        for (int n = 0; n < first->numFrames(); ++n)
            REQUIRE(first->channels[static_cast<size_t>(ch)][static_cast<size_t>(n)] ==
                    second->channels[static_cast<size_t>(ch)][static_cast<size_t>(n)]);
}

TEST_CASE("File decode and in-memory stream decode agree (embedded-asset workflow)",
          "[sampleloader][io][integration]")
{
    ScopedTempDir tmp;
    auto file = tmp.dir.getChildFile("asset.wav");
    juce::WavAudioFormat wav;
    writeKnownSignal(wav, file, 44100.0, 2, 333);

    SampleLoader loader;
    auto fromFile = loader.loadFile(file);
    REQUIRE(fromFile.has_value());

    // Read the same bytes into memory and decode via the stream path.
    juce::MemoryBlock bytes;
    REQUIRE(file.loadFileAsData(bytes));
    auto stream = std::make_unique<juce::MemoryInputStream>(bytes, false);
    auto fromMemory = loader.loadStream(std::move(stream));
    REQUIRE(fromMemory.has_value());

    REQUIRE(fromFile->numChannels() == fromMemory->numChannels());
    REQUIRE(fromFile->numFrames() == fromMemory->numFrames());
    for (int ch = 0; ch < fromFile->numChannels(); ++ch)
        for (int n = 0; n < fromFile->numFrames(); ++n)
            REQUIRE(fromFile->channels[static_cast<size_t>(ch)][static_cast<size_t>(n)] ==
                    fromMemory->channels[static_cast<size_t>(ch)][static_cast<size_t>(n)]);
}
