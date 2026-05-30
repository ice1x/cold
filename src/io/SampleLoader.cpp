#include "SampleLoader.h"

namespace cold::io
{

SampleLoader::SampleLoader()
{
    // WAV, AIFF, FLAC, Ogg Vorbis.
    formatManager.registerBasicFormats();

#if JUCE_MAC || JUCE_IOS
    // CoreAudio adds mp3 / m4a / aac decode on Apple platforms (the target).
    formatManager.registerFormat(new juce::CoreAudioFormat(), /*makeDefault*/ false);
#endif
}

namespace
{

// Read an already-opened reader fully into deinterleaved float channels.
std::optional<SampleData> readAll(juce::AudioFormatReader& reader)
{
    const auto numFrames = static_cast<int>(reader.lengthInSamples);
    const auto numChannels = static_cast<int>(reader.numChannels);
    if (numFrames <= 0 || numChannels <= 0)
        return std::nullopt;

    juce::AudioBuffer<float> buffer(numChannels, numFrames);
    if (!reader.read(&buffer, 0, numFrames, 0, true, true))
        return std::nullopt;

    SampleData data;
    data.sampleRate = reader.sampleRate;
    data.channels.resize(static_cast<size_t>(numChannels));
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* src = buffer.getReadPointer(ch);
        data.channels[static_cast<size_t>(ch)].assign(src, src + numFrames);
    }
    return data;
}

} // namespace

std::optional<SampleData> SampleLoader::loadFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return std::nullopt;

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr)
        return std::nullopt;

    return readAll(*reader);
}

std::optional<SampleData> SampleLoader::loadStream(std::unique_ptr<juce::InputStream> stream)
{
    if (stream == nullptr)
        return std::nullopt;

    // createReaderFor takes ownership of the stream on success; on failure it
    // deletes the stream and returns nullptr, so release() is correct here.
    std::unique_ptr<juce::AudioFormatReader> reader(
        formatManager.createReaderFor(std::move(stream)));
    if (reader == nullptr)
        return std::nullopt;

    return readAll(*reader);
}

bool SampleLoader::canLoadExtension(const juce::String& extension) const
{
    return formatManager.findFormatForFileExtension(extension) != nullptr;
}

} // namespace cold::io
