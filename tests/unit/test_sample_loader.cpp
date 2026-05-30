#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "io/SampleLoader.h"
#include "support/SignalFixtures.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstring>
#include <memory>

using Catch::Matchers::WithinAbs;
using cold::io::SampleData;
using cold::io::SampleLoader;
using cold::test::expectedSample;
using cold::test::writeKnownSignal;

namespace
{

void verifyDecoded(const SampleData& data, double sampleRate, int numChannels, int numFrames)
{
    REQUIRE_FALSE(data.isEmpty());
    REQUIRE(data.numChannels() == numChannels);
    REQUIRE(data.numFrames() == numFrames);
    REQUIRE_THAT(data.sampleRate, WithinAbs(sampleRate, 0.5));

    // 24-bit PCM round-trips to ~1e-5 of full scale; allow generous tolerance.
    for (int ch = 0; ch < numChannels; ++ch)
        for (int n = 0; n < numFrames; ++n)
            REQUIRE_THAT(data.channels[static_cast<size_t>(ch)][static_cast<size_t>(n)],
                         WithinAbs(expectedSample(ch, n, numFrames), 1.0e-3));
}

} // namespace

TEST_CASE("SampleLoader decodes a WAV round-trip", "[sampleloader][io][unit]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int numFrames = 512;

    auto file = juce::File::createTempFile(".wav");
    juce::WavAudioFormat wav;
    writeKnownSignal(wav, file, sampleRate, 2, numFrames);

    SampleLoader loader;
    auto decoded = loader.loadFile(file);
    file.deleteFile();

    REQUIRE(decoded.has_value());
    verifyDecoded(*decoded, sampleRate, 2, numFrames);
}

TEST_CASE("SampleLoader decodes an AIFF round-trip", "[sampleloader][io][unit]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int numFrames = 480;

    auto file = juce::File::createTempFile(".aiff");
    juce::AiffAudioFormat aiff;
    writeKnownSignal(aiff, file, sampleRate, 2, numFrames);

    SampleLoader loader;
    auto decoded = loader.loadFile(file);
    file.deleteFile();

    REQUIRE(decoded.has_value());
    verifyDecoded(*decoded, sampleRate, 2, numFrames);
}

TEST_CASE("SampleLoader decodes a mono file to one channel", "[sampleloader][io][unit]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int numFrames = 300;

    auto file = juce::File::createTempFile(".wav");
    juce::WavAudioFormat wav;
    writeKnownSignal(wav, file, sampleRate, 1, numFrames);

    SampleLoader loader;
    auto decoded = loader.loadFile(file);
    file.deleteFile();

    REQUIRE(decoded.has_value());
    verifyDecoded(*decoded, sampleRate, 1, numFrames);
}

TEST_CASE("SampleLoader decodes from an input stream", "[sampleloader][io][unit]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int numFrames = 256;

    auto file = juce::File::createTempFile(".wav");
    juce::WavAudioFormat wav;
    writeKnownSignal(wav, file, sampleRate, 2, numFrames);

    SampleLoader loader;
    auto stream = std::make_unique<juce::FileInputStream>(file);
    REQUIRE(stream->openedOk());
    auto decoded = loader.loadStream(std::move(stream));
    file.deleteFile();

    REQUIRE(decoded.has_value());
    verifyDecoded(*decoded, sampleRate, 2, numFrames);
}

TEST_CASE("SampleLoader advertises wav/aiff/mp3 support", "[sampleloader][io][unit]")
{
    SampleLoader loader;
    REQUIRE(loader.canLoadExtension(".wav"));
    REQUIRE(loader.canLoadExtension("aiff"));
    // mp3 is decoded via CoreAudioFormat on macOS (the target platform).
    REQUIRE(loader.canLoadExtension(".mp3"));
}

TEST_CASE("SampleLoader returns nullopt for a missing file", "[sampleloader][io][unit]")
{
    SampleLoader loader;
    auto decoded = loader.loadFile(juce::File("/nonexistent/path/to/sample.wav"));
    REQUIRE_FALSE(decoded.has_value());
}

TEST_CASE("SampleLoader returns nullopt for a non-audio stream", "[sampleloader][io][unit]")
{
    SampleLoader loader;
    const char* garbage = "this is definitely not an audio file payload";
    auto stream = std::make_unique<juce::MemoryInputStream>(garbage, std::strlen(garbage), false);
    auto decoded = loader.loadStream(std::move(stream));
    REQUIRE_FALSE(decoded.has_value());
}
