#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "io/SampleLoader.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <memory>

using Catch::Matchers::WithinAbs;
using cold::io::SampleData;
using cold::io::SampleLoader;

namespace
{

// Deterministic test signal: channel 0 is a sine, channel 1 a linear ramp.
float expectedSample(int channel, int frame, int numFrames)
{
    if (channel == 0)
        return 0.5f * std::sin(2.0f * 3.14159265358979f * static_cast<float>(frame) / 64.0f);

    // Ramp from -1 .. +1 across the buffer (channel 1).
    return -1.0f + 2.0f * (static_cast<float>(frame) / static_cast<float>(numFrames - 1));
}

// Write a known stereo signal to `file` using the given JUCE format, so we can
// decode it back and verify a real round-trip. Returns frames written.
int writeKnownSignal(juce::AudioFormat& format, const juce::File& file, double sampleRate,
                     int numFrames)
{
    constexpr int numChannels = 2;
    juce::AudioBuffer<float> buffer(numChannels, numFrames);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int n = 0; n < numFrames; ++n)
            buffer.setSample(ch, n, expectedSample(ch, n, numFrames));

    file.deleteFile();
    auto* outStream = file.createOutputStream().release();
    REQUIRE(outStream != nullptr);

    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(outStream, sampleRate, static_cast<unsigned int>(numChannels),
                               /*bitsPerSample*/ 24, {}, 0));
    REQUIRE(writer != nullptr); // writer owns outStream from here
    REQUIRE(writer->writeFromAudioSampleBuffer(buffer, 0, numFrames));
    writer.reset(); // flush + close before reading back
    return numFrames;
}

void verifyDecoded(const SampleData& data, double sampleRate, int numFrames)
{
    REQUIRE_FALSE(data.isEmpty());
    REQUIRE(data.numChannels() == 2);
    REQUIRE(data.numFrames() == numFrames);
    REQUIRE_THAT(data.sampleRate, WithinAbs(sampleRate, 0.5));

    // 24-bit PCM round-trips to ~1e-5 of full scale; allow generous tolerance.
    for (int ch = 0; ch < 2; ++ch)
        for (int n = 0; n < numFrames; ++n)
            REQUIRE_THAT(data.channels[static_cast<size_t>(ch)][static_cast<size_t>(n)],
                         WithinAbs(expectedSample(ch, n, numFrames), 1.0e-3));
}

} // namespace

TEST_CASE("SampleLoader decodes a WAV round-trip", "[sampleloader][io]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int numFrames = 512;

    auto file = juce::File::createTempFile(".wav");
    juce::WavAudioFormat wav;
    writeKnownSignal(wav, file, sampleRate, numFrames);

    SampleLoader loader;
    auto decoded = loader.loadFile(file);
    file.deleteFile();

    REQUIRE(decoded.has_value());
    verifyDecoded(*decoded, sampleRate, numFrames);
}

TEST_CASE("SampleLoader decodes an AIFF round-trip", "[sampleloader][io]")
{
    constexpr double sampleRate = 48000.0;
    constexpr int numFrames = 480;

    auto file = juce::File::createTempFile(".aiff");
    juce::AiffAudioFormat aiff;
    writeKnownSignal(aiff, file, sampleRate, numFrames);

    SampleLoader loader;
    auto decoded = loader.loadFile(file);
    file.deleteFile();

    REQUIRE(decoded.has_value());
    verifyDecoded(*decoded, sampleRate, numFrames);
}

TEST_CASE("SampleLoader decodes from an input stream", "[sampleloader][io]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int numFrames = 256;

    auto file = juce::File::createTempFile(".wav");
    juce::WavAudioFormat wav;
    writeKnownSignal(wav, file, sampleRate, numFrames);

    SampleLoader loader;
    auto stream = std::make_unique<juce::FileInputStream>(file);
    REQUIRE(stream->openedOk());
    auto decoded = loader.loadStream(std::move(stream));
    file.deleteFile();

    REQUIRE(decoded.has_value());
    verifyDecoded(*decoded, sampleRate, numFrames);
}

TEST_CASE("SampleLoader advertises wav/aiff/mp3 support", "[sampleloader][io]")
{
    SampleLoader loader;
    REQUIRE(loader.canLoadExtension(".wav"));
    REQUIRE(loader.canLoadExtension("aiff"));
    // mp3 is decoded via CoreAudioFormat on macOS (the target platform).
    REQUIRE(loader.canLoadExtension(".mp3"));
}

TEST_CASE("SampleLoader returns nullopt for a missing file", "[sampleloader][io]")
{
    SampleLoader loader;
    auto decoded = loader.loadFile(juce::File("/nonexistent/path/to/sample.wav"));
    REQUIRE_FALSE(decoded.has_value());
}
