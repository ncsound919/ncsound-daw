// Asset lifetime: loading, resampling, streaming and peak envelopes.
// Split out of AudioEngine.cpp (plan 07-03); bodies moved verbatim.

#include "AudioEngine.h"
#include "AudioEngineInternal.h"
#include <nc/engine/AudioSuite.hpp>
#include <nc/engine/Resample.hpp>
#include <nc/engine/StreamWindow.hpp>
#include <algorithm>
#include <cmath>
#include <thread>

namespace nc
{

//==============================================================================
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

    auto asset = std::make_unique<AudioAsset>();
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
        return finish (addAsset (std::move (asset)));
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

    return finish (addAsset (std::move (asset)));
}

double AudioEngine::assetDurationSeconds (int assetId) const
{
    if (assetId < 0 || assetId >= static_cast<int> (assets.size()))
        return 0.0;

    const auto& asset = *assets[static_cast<size_t> (assetId)];
    return asset.sampleRate > 0.0
               ? static_cast<double> (assetFrameCount (asset)) / asset.sampleRate
               : 0.0;
}

juce::File AudioEngine::assetFile (int assetId) const
{
    return assetId >= 0 && assetId < static_cast<int> (assets.size())
               ? assets[static_cast<size_t> (assetId)]->file
               : juce::File();
}

juce::String AudioEngine::assetName (int assetId) const
{
    return assetId >= 0 && assetId < static_cast<int> (assets.size())
               ? assets[static_cast<size_t> (assetId)]->name
               : juce::String();
}

int AudioEngine::addAsset (std::unique_ptr<AudioAsset> asset)
{
    if (asset == nullptr || assetFrameCount (*asset) <= 0)
        return -1;

    assets.push_back (std::move (asset));

    auto& added = *assets.back();
    added.resample.build (added.sampleRate, juce::jmax (1.0, sampleRate.load()));
    return static_cast<int> (assets.size()) - 1;
}

int64_t AudioEngine::assetFrameCount (const AudioAsset& asset) const
{
    return asset.streamed ? asset.totalFrames
                          : static_cast<int64_t> (asset.buffer.getNumSamples());
}

double AudioEngine::assetSampleRate (int assetId) const
{
    return assetId >= 0 && assetId < static_cast<int> (assets.size())
               ? assets[static_cast<size_t> (assetId)]->sampleRate
               : 0.0;
}

nc::engine::audiosuite::Trim AudioEngine::assetTrim (int assetId, float threshold) const
{
    if (assetId < 0 || assetId >= static_cast<int> (assets.size()))
        return {};

    const auto& asset = *assets[static_cast<size_t> (assetId)];

    if (asset.streamed || asset.buffer.getNumSamples() == 0)
        return {};

    const auto* left = asset.buffer.getReadPointer (0);
    const auto* right = asset.buffer.getNumChannels() > 1 ? asset.buffer.getReadPointer (1) : left;

    return nc::engine::audiosuite::scanSilence (left, right, asset.buffer.getNumSamples(), threshold);
}

int AudioEngine::processAsset (int sourceAssetId, const juce::String& nameSuffix,
                               const nc::engine::audiosuite::Process& process)
{
    if (sourceAssetId < 0 || sourceAssetId >= static_cast<int> (assets.size()))
        return -1;

    const auto& source = *assets[static_cast<size_t> (sourceAssetId)];

    auto rendered = std::make_unique<AudioAsset>();
    rendered->sampleRate = source.sampleRate;
    rendered->name = source.name + " " + nameSuffix;

    if (source.streamed)
    {
        const auto frames = static_cast<int> (juce::jmin<int64_t> (source.totalFrames, 3600LL * 48000LL));

        if (frames <= 0 || source.reader == nullptr)
            return -1;

        rendered->buffer.setSize (2, frames);
        rendered->buffer.clear();

        const auto hasRight = source.reader->numChannels > 1;
        source.reader->read (&rendered->buffer, 0, 0, frames, true, hasRight);

        if (! hasRight)
            rendered->buffer.copyFrom (1, 0, rendered->buffer, 0, 0, frames);
    }
    else
    {
        const auto frames = source.buffer.getNumSamples();
        const auto channels = source.buffer.getNumChannels();

        if (frames <= 0 || channels <= 0)
            return -1;

        rendered->buffer.setSize (2, frames);
        rendered->buffer.copyFrom (0, 0, source.buffer, 0, 0, frames);
        rendered->buffer.copyFrom (1, 0, source.buffer, juce::jmin (1, channels - 1), 0, frames);
    }

    const nc::engine::audiosuite::RenderView view {
        rendered->buffer.getWritePointer (0),
        rendered->buffer.getWritePointer (1),
        rendered->buffer.getNumSamples(),
        rendered->sampleRate
    };

    nc::engine::audiosuite::runProcess (view, process);
    return addAsset (std::move (rendered));
}

void AudioEngine::refreshResampleBanks()
{
    const auto rate = sampleRate.load();

    for (auto& assetPtr : assets)
    {
        auto& asset = *assetPtr;

        if (! asset.resample.matches (asset.sampleRate, rate))
            asset.resample.build (asset.sampleRate, rate);
    }
}

void AudioEngine::serviceStreaming()
{
    // A session-rate change (a device swap, an export at another rate) leaves
    // the playback kernels stale; this runs on the message thread often
    // enough to rebuild them before the callback needs them again.
    refreshResampleBanks();

    const auto seconds = positionSeconds();

    for (auto& assetPtr : assets)
    {
        auto& asset = *assetPtr;

        if (! asset.streamed || asset.reader == nullptr || asset.cacheFrames <= 0)
            continue;

        const auto cursor = static_cast<int64_t> (seconds * asset.sampleRate);
        const auto active = asset.activeCache.load (std::memory_order_acquire);
        const auto start = asset.slotStart[static_cast<size_t> (active)].load (std::memory_order_acquire);
        const auto length = asset.slotLength[static_cast<size_t> (active)].load (std::memory_order_acquire);

        // Refill once the cursor has eaten more than half the resident window.
        if (engine::windowCovers ({ start, length }, cursor, asset.cacheFrames / 2))
            continue;

        const auto target = engine::planWindow (cursor, asset.totalFrames, asset.cacheFrames);
        const auto slot = static_cast<size_t> (1 - active);
        auto& left = asset.cacheA[slot];
        auto& right = asset.cacheB[slot];
        const auto count = static_cast<int> (juce::jmin<int64_t> (target.lengthFrames, asset.cacheFrames));

        std::fill (left.begin(), left.end(), 0.0f);
        std::fill (right.begin(), right.end(), 0.0f);

        if (count > 0)
        {
            // A non-owning view over the resident window: JUCE fills it from
            // the reader without touching the buffer the callback is using.
            float* channels[2] { left.data(), right.data() };
            juce::AudioBuffer<float> view (channels, 2, count);
            const auto hasRight = asset.reader->numChannels > 1;

            asset.reader->read (&view, 0, target.startFrame, count, true, hasRight);

            if (! hasRight)
                std::copy (left.begin(), left.begin() + count, right.begin());
        }

        asset.slotStart[slot].store (target.startFrame, std::memory_order_release);
        asset.slotLength[slot].store (count, std::memory_order_release);
        asset.activeCache.store (static_cast<int> (slot), std::memory_order_release);
    }
}

juce::AudioBuffer<float> AudioEngine::assetSamples (int assetId, double startSeconds,
                                                    double lengthSeconds) const
{
    juce::AudioBuffer<float> mono;

    if (! juce::isPositiveAndBelow (assetId, static_cast<int> (assets.size())))
        return mono;

    const auto& asset = *assets[static_cast<size_t> (assetId)];
    const auto frames = asset.buffer.getNumSamples();

    // A streamed import keeps only a window resident; analysing a fragment and
    // calling it the take would put invented notes in the session.
    if (asset.streamed || frames <= 0 || asset.sampleRate <= 0.0)
        return mono;

    const auto start = juce::jlimit (0, juce::jmax (0, frames - 1),
                                     static_cast<int> (std::floor (startSeconds * asset.sampleRate)));
    const auto wanted = juce::jmax (1, static_cast<int> (std::floor (lengthSeconds * asset.sampleRate)));
    const auto length = juce::jmax (1, juce::jmin (wanted, frames - start));
    const auto channels = juce::jmax (1, asset.buffer.getNumChannels());

    mono.setSize (1, length);
    mono.clear();

    for (int channel = 0; channel < channels; ++channel)
        mono.addFrom (0, 0, asset.buffer, channel, start, length, 1.0f / (float) channels);

    return mono;
}

std::vector<float> AudioEngine::assetPeakEnvelope (int assetId, double startSeconds,
                                                   double lengthSeconds, int bins) const
{
    std::vector<float> peaks;

    if (bins <= 0 || ! juce::isPositiveAndBelow (assetId, static_cast<int> (assets.size())))
        return peaks;

    const auto& asset = *assets[static_cast<size_t> (assetId)];
    const auto frames = asset.buffer.getNumSamples();

    // A streamed import keeps only a window resident; drawing half a waveform on
    // the timeline would misrepresent the clip, so it gets none.
    if (asset.streamed || frames <= 0 || asset.sampleRate <= 0.0)
        return peaks;

    const auto start = juce::jlimit (0, juce::jmax (0, frames - 1),
                                     static_cast<int> (std::floor (startSeconds * asset.sampleRate)));
    const auto wanted = juce::jmax (1, static_cast<int> (std::floor (lengthSeconds * asset.sampleRate)));
    const auto length = juce::jmax (1, juce::jmin (wanted, frames - start));
    const auto step = juce::jmax (1, length / bins);

    peaks.reserve (static_cast<size_t> (bins));

    for (int bin = 0; bin < bins; ++bin)
    {
        const auto from = start + bin * step;

        if (from >= frames)
        {
            peaks.push_back (0.0f);
            continue;
        }

        const auto count = juce::jmin (step, frames - from);
        float peak = 0.0f;

        for (int ch = 0; ch < asset.buffer.getNumChannels(); ++ch)
            peak = juce::jmax (peak, asset.buffer.getMagnitude (ch, from, count));

        peaks.push_back (juce::jlimit (0.0f, 1.0f, peak));
    }

    return peaks;
}

} // namespace nc
