#pragma once

// Shared test fixtures for the JUCE-linked suites (unit / integration / e2e).
// Generates deterministic audio and writes it to real files via JUCE format
// writers so the SampleLoader (and, later, the engine) can be exercised against
// genuine decoded data rather than checked-in binary blobs.

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <memory>

namespace cold::test
{

// Deterministic, format-independent sample value in [-1, 1] for a given
// (channel, frame). Channel 0 is a sine; other channels are a linear ramp,
// so multi-channel files have per-channel-distinct, verifiable content.
inline float expectedSample(int channel, int frame, int numFrames)
{
    if (channel == 0)
        return 0.5f * std::sin(2.0f * 3.14159265358979f * static_cast<float>(frame) / 64.0f);

    if (numFrames <= 1)
        return 0.0f;

    return -1.0f + 2.0f * (static_cast<float>(frame) / static_cast<float>(numFrames - 1));
}

// An in-memory buffer filled with the deterministic signal above.
inline juce::AudioBuffer<float> makeKnownBuffer(int numChannels, int numFrames)
{
    juce::AudioBuffer<float> buffer(numChannels, numFrames);
    for (int ch = 0; ch < numChannels; ++ch)
        for (int n = 0; n < numFrames; ++n)
            buffer.setSample(ch, n, expectedSample(ch, n, numFrames));
    return buffer;
}

// Write the deterministic signal to `file` using the given JUCE format so the
// loader can decode a real round-trip. REQUIREs (fails the test) on any I/O
// error rather than returning a status.
inline void writeKnownSignal(juce::AudioFormat& format, const juce::File& file, double sampleRate,
                             int numChannels, int numFrames, int bitsPerSample = 24)
{
    const auto buffer = makeKnownBuffer(numChannels, numFrames);

    file.deleteFile();
    auto* outStream = file.createOutputStream().release();
    REQUIRE(outStream != nullptr);

    std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(
        outStream, sampleRate, static_cast<unsigned int>(numChannels), bitsPerSample, {}, 0));
    REQUIRE(writer != nullptr); // writer owns outStream from here
    REQUIRE(writer->writeFromAudioSampleBuffer(buffer, 0, numFrames));
    writer.reset(); // flush + close before reading back
}

} // namespace cold::test
