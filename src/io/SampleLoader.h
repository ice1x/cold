#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <optional>
#include <vector>

namespace cold::io
{

// Decoded sample: deinterleaved float channels plus the source sample rate.
// Framework-agnostic to consumers (the DSP layer reads plain float vectors),
// but produced by the JUCE-backed SampleLoader.
struct SampleData
{
    // channels[ch][frame]; all inner vectors share the same length.
    std::vector<std::vector<float>> channels;
    double sampleRate = 0.0;

    [[nodiscard]] int numChannels() const { return static_cast<int>(channels.size()); }
    [[nodiscard]] int numFrames() const
    {
        return channels.empty() ? 0 : static_cast<int>(channels.front().size());
    }
    [[nodiscard]] bool isEmpty() const { return numChannels() == 0 || numFrames() == 0; }
};

// Decodes audio files (wav / aiff / mp3 and other CoreAudio formats on macOS)
// into float buffers via juce::AudioFormatManager. This is the thin JUCE layer
// that keeps file I/O out of the framework-agnostic DSP classes.
class SampleLoader
{
public:
    SampleLoader();

    // Decode a file into a SampleData. Returns std::nullopt if the file cannot
    // be opened or no registered format can read it.
    [[nodiscard]] std::optional<SampleData> loadFile(const juce::File& file);

    // Decode from an arbitrary input stream (e.g. embedded/in-memory data).
    // Takes ownership of the stream. Returns std::nullopt on failure.
    [[nodiscard]] std::optional<SampleData> loadStream(std::unique_ptr<juce::InputStream> stream);

    // Whether a file extension (e.g. ".mp3" or "mp3") is decodable by a
    // registered format. Used to advertise format support.
    [[nodiscard]] bool canLoadExtension(const juce::String& extension) const;

private:
    juce::AudioFormatManager formatManager;
};

} // namespace cold::io
