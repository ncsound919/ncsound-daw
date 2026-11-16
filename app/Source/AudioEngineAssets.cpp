// Asset lifetime: loading, AudioSuite rendering and the forwarding API.
// The storage and the sample-read path live in AssetLibrary (plan 07-05).

#include "AudioEngine.h"
#include <nc/engine/AudioSuite.hpp>
#include <nc/engine/Resample.hpp>

namespace nc
{
int AudioEngine::loadAudioFile (const juce::File& file)
{
    assetError.clear();

    if (! file.existsAsFile())
    {
        assetError = "File not found: " + file.getFileName();
        return -1;
    }

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels == 0)
    {
        assetError = "Unsupported or empty audio file: " + file.getFileName();
        return -1;
    }

    const auto finish = [this, &file] (int id)
    {
        if (id < 0)
            assetError = "Could not import audio file: " + file.getFileName();

        return id;
    };

    // A bounded message-thread load against the central 30-minute ceiling at
    // the file's own rate. Full disk streaming replaces this RAM hold when
    // long projects matter; the cap stays explicit either way.
    const auto fileRate = reader->sampleRate > 0.0 ? reader->sampleRate : 48000.0;
    const auto sourceFrames = reader->lengthInSamples;

    auto asset = std::make_unique<AssetLibrary::Asset>();
    asset->sampleRate = fileRate;
    asset->name = file.getFileNameWithoutExtension();
    asset->file = file;

    // Long takes stream: only the read-ahead window is resident, so a long
    // import costs seconds of RAM instead of hundreds of megabytes.
    if (static_cast<double> (sourceFrames) / fileRate > nc::limits::streamThresholdSeconds)
    {
        asset->streamed = true;
        asset->totalFrames = sourceFrames;
        asset->cacheFrames = juce::jmax<int64_t> (1024, static_cast<int64_t> (fileRate * nc::limits::streamReadAheadSeconds));

        for (auto& buffer : asset->cacheA)
            buffer.assign (static_cast<size_t> (asset->cacheFrames), 0.0f);

        for (auto& buffer : asset->cacheB)
            buffer.assign (static_cast<size_t> (asset->cacheFrames), 0.0f);

        asset->reader = std::move (reader);
        return finish (assetLibrary.add (std::move (asset), sampleRate.load()));
    }

    const auto frames64 = nc::limits::clampAssetFrames (sourceFrames, fileRate);
    const auto frames = static_cast<int> (frames64);
    const auto channels = juce::jlimit (1, 2, static_cast<int> (reader->numChannels));

    asset->buffer.setSize (channels, frames);

    if (! reader->read (&asset->buffer, 0, frames, 0, true, true))
    {
        assetError = "Could not read audio file: " + file.getFileName();
        return -1;
    }

    return finish (assetLibrary.add (std::move (asset), sampleRate.load()));
}

int AudioEngine::processAsset (int sourceAssetId, const juce::String& nameSuffix,
                               const nc::engine::audiosuite::Process& process)
{
    const auto* source = assetLibrary.find (sourceAssetId);

    if (source == nullptr)
        return -1;

    auto rendered = std::make_unique<AssetLibrary::Asset>();
    rendered->sampleRate = source->sampleRate;
    rendered->name = source->name + " " + nameSuffix;

    if (source->streamed)
    {
        const auto frames = static_cast<int> (juce::jmin<int64_t> (source->totalFrames, 3600LL * 48000LL));

        if (frames <= 0 || source->reader == nullptr)
            return -1;

        rendered->buffer.setSize (2, frames);
        rendered->buffer.clear();

        const auto hasRight = source->reader->numChannels > 1;
        source->reader->read (&rendered->buffer, 0, 0, frames, true, hasRight);

        if (! hasRight)
            rendered->buffer.copyFrom (1, 0, rendered->buffer, 0, 0, frames);
    }
    else
    {
        const auto frames = source->buffer.getNumSamples();
        const auto channels = source->buffer.getNumChannels();

        if (frames <= 0 || channels <= 0)
            return -1;

        rendered->buffer.setSize (2, frames);
        rendered->buffer.copyFrom (0, 0, source->buffer, 0, 0, frames);
        rendered->buffer.copyFrom (1, 0, source->buffer, juce::jmin (1, channels - 1), 0, frames);
    }

    const nc::engine::audiosuite::RenderView view {
        rendered->buffer.getWritePointer (0),
        rendered->buffer.getWritePointer (1),
        rendered->buffer.getNumSamples(),
        rendered->sampleRate
    };

    nc::engine::audiosuite::runProcess (view, process);
    return assetLibrary.add (std::move (rendered), sampleRate.load());
}

//==============================================================================
// Forwarders. AudioEngine keeps every public method; AssetLibrary owns the data.

double AudioEngine::assetDurationSeconds (int assetId) const
{
    return assetLibrary.durationSeconds (assetId);
}

juce::File AudioEngine::assetFile (int assetId) const
{
    return assetLibrary.file (assetId);
}

juce::String AudioEngine::assetName (int assetId) const
{
    return assetLibrary.name (assetId);
}

double AudioEngine::assetSampleRate (int assetId) const
{
    return assetLibrary.sampleRate (assetId);
}

nc::engine::audiosuite::Trim AudioEngine::assetTrim (int assetId, float threshold) const
{
    return assetLibrary.trim (assetId, threshold);
}

juce::AudioBuffer<float> AudioEngine::assetSamples (int assetId, double startSeconds,
                                                    double lengthSeconds) const
{
    return assetLibrary.samples (assetId, startSeconds, lengthSeconds);
}

std::vector<float> AudioEngine::assetPeakEnvelope (int assetId, double startSeconds,
                                                   double lengthSeconds, int bins) const
{
    return assetLibrary.peakEnvelope (assetId, startSeconds, lengthSeconds, bins);
}

void AudioEngine::refreshResampleBanks()
{
    assetLibrary.refreshResampleBanks (sampleRate.load());
}

void AudioEngine::serviceStreaming()
{
    assetLibrary.serviceStreaming (sampleRate.load(), positionSeconds());
}

} // namespace nc
