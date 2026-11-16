// Asset storage and the audio-thread sample-read path.
// Split out of AudioEngine (plan 07-05); bodies moved verbatim.

#include "AssetLibrary.h"

#include <nc/engine/StreamWindow.hpp>

#include <algorithm>
#include <cmath>

namespace nc
{
int AssetLibrary::add (std::unique_ptr<Asset> asset, double outputRate)
{
    if (asset == nullptr || frameCount (*asset) <= 0)
        return -1;

    assets.push_back (std::move (asset));

    auto& added = *assets.back();
    added.resample.build (added.sampleRate, juce::jmax (1.0, outputRate));
    return static_cast<int> (assets.size()) - 1;
}

AssetLibrary::Asset* AssetLibrary::find (int assetId)
{
    return juce::isPositiveAndBelow (assetId, static_cast<int> (assets.size()))
               ? assets[static_cast<size_t> (assetId)].get()
               : nullptr;
}

const AssetLibrary::Asset* AssetLibrary::find (int assetId) const
{
    return juce::isPositiveAndBelow (assetId, static_cast<int> (assets.size()))
               ? assets[static_cast<size_t> (assetId)].get()
               : nullptr;
}

int AssetLibrary::demoAssetId (int track) const
{
    return juce::isPositiveAndBelow (track, static_cast<int> (demoAssets.size()))
               ? demoAssets[static_cast<size_t> (track)]
               : -1;
}

void AssetLibrary::setDemoAssetId (int track, int assetId)
{
    if (juce::isPositiveAndBelow (track, static_cast<int> (demoAssets.size())))
        demoAssets[static_cast<size_t> (track)] = assetId;
}

double AssetLibrary::durationSeconds (int assetId) const
{
    if (assetId < 0 || assetId >= static_cast<int> (assets.size()))
        return 0.0;

    const auto& asset = *assets[static_cast<size_t> (assetId)];
    return asset.sampleRate > 0.0
               ? static_cast<double> (frameCount (asset)) / asset.sampleRate
               : 0.0;
}

juce::File AssetLibrary::file (int assetId) const
{
    return assetId >= 0 && assetId < static_cast<int> (assets.size())
               ? assets[static_cast<size_t> (assetId)]->file
               : juce::File();
}

juce::String AssetLibrary::name (int assetId) const
{
    return assetId >= 0 && assetId < static_cast<int> (assets.size())
               ? assets[static_cast<size_t> (assetId)]->name
               : juce::String();
}

double AssetLibrary::sampleRate (int assetId) const
{
    return assetId >= 0 && assetId < static_cast<int> (assets.size())
               ? assets[static_cast<size_t> (assetId)]->sampleRate
               : 0.0;
}

int64_t AssetLibrary::frameCount (const Asset& asset) const
{
    return asset.streamed ? asset.totalFrames
                          : static_cast<int64_t> (asset.buffer.getNumSamples());
}

nc::engine::audiosuite::Trim AssetLibrary::trim (int assetId, float threshold) const
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

void AssetLibrary::refreshResampleBanks (double outputRate)
{
    const auto rate = outputRate;

    for (auto& assetPtr : assets)
    {
        auto& asset = *assetPtr;

        if (! asset.resample.matches (asset.sampleRate, rate))
            asset.resample.build (asset.sampleRate, rate);
    }
}

void AssetLibrary::serviceStreaming (double outputRate, double positionSeconds)
{
    // A session-rate change (a device swap, an export at another rate) leaves
    // the playback kernels stale; this runs on the message thread often
    // enough to rebuild them before the callback needs them again.
    refreshResampleBanks (outputRate);

    const auto seconds = positionSeconds;

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

juce::AudioBuffer<float> AssetLibrary::samples (int assetId, double startSeconds,
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

std::vector<float> AssetLibrary::peakEnvelope (int assetId, double startSeconds,
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

float AssetLibrary::sampleAt (const Asset& asset, int64_t frame, int channel) const
{
    if (! asset.streamed)
    {
        const auto channels = asset.buffer.getNumChannels();

        if (channels <= 0 || frame < 0 || frame >= asset.buffer.getNumSamples())
            return 0.0f;

        return asset.buffer.getReadPointer (juce::jlimit (0, channels - 1, channel))[frame];
    }

    const auto slot = static_cast<size_t> (asset.activeCache.load (std::memory_order_acquire));
    const auto start = asset.slotStart[slot].load (std::memory_order_acquire);
    const auto length = asset.slotLength[slot].load (std::memory_order_acquire);

    if (frame < start || frame >= start + length)
        return 0.0f; // not resident yet: silence, never a disk read here

    const auto& data = channel == 0 ? asset.cacheA[slot] : asset.cacheB[slot];
    const auto index = static_cast<size_t> (frame - start);

    return index < data.size() ? data[index] : 0.0f;
}

float AssetLibrary::readSourceSample (const Asset& asset, double sourceFrame, double outputRate,
                                      int channel) const
{
    if (std::abs (asset.sampleRate - outputRate) <= nc::engine::resample::rateEpsilon)
        return sampleAt (asset, static_cast<int64_t> (std::llround (sourceFrame)), channel);

    if (asset.resample.matches (asset.sampleRate, outputRate))
        return readBandLimited (asset, sourceFrame, channel);

    // The session rate changed and the message thread has not rebuilt the
    // kernels yet: read neighbours rather than stall the callback.
    const auto frame0 = static_cast<int64_t> (std::floor (sourceFrame));
    const auto frame1 = frame0 + 1;
    const auto fraction = static_cast<float> (sourceFrame - static_cast<double> (frame0));
    const auto start = sampleAt (asset, frame0, channel);
    const auto end = sampleAt (asset, frame1, channel);
    return static_cast<float> (start + (end - start) * fraction);
}

float AssetLibrary::readBandLimited (const Asset& asset, double sourceFrame, int channel) const
{
    const auto& weights = asset.resample;
    const auto first = static_cast<int64_t> (std::floor (sourceFrame));
    const auto* w = weights.weights (sourceFrame - static_cast<double> (first));
    const auto start = first - (nc::engine::resample::taps / 2 - 1);
    double sum = 0.0;

    if (! asset.streamed)
    {
        const auto channels = asset.buffer.getNumChannels();
        const auto frames = static_cast<int64_t> (asset.buffer.getNumSamples());

        if (channels <= 0 || frames <= 0)
            return 0.0f;

        const auto* data = asset.buffer.getReadPointer (juce::jlimit (0, channels - 1, channel));

        // Interior frames are a contiguous window: four accumulators keep the
        // taps from serialising on one dependency chain, which is what the
        // delivered Debug build of the audio path runs. Only the asset's edges
        // need the clamped read (repeating the edge keeps a clip click-free).
        static_assert (nc::engine::resample::taps % 4 == 0);

        if (start >= 0 && start + nc::engine::resample::taps <= frames)
        {
            const auto* x = data + start;
            float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;

            for (int tap = 0; tap < nc::engine::resample::taps; tap += 4)
            {
                a0 += w[tap] * x[tap];
                a1 += w[tap + 1] * x[tap + 1];
                a2 += w[tap + 2] * x[tap + 2];
                a3 += w[tap + 3] * x[tap + 3];
            }

            return (a0 + a1) + (a2 + a3);
        }

        for (int tap = 0; tap < nc::engine::resample::taps; ++tap)
            sum += w[tap] * data[juce::jlimit<int64_t> (0, frames - 1, start + tap)];

        return static_cast<float> (sum);
    }

    const auto slot = static_cast<size_t> (asset.activeCache.load (std::memory_order_acquire));
    const auto windowStart = asset.slotStart[slot].load (std::memory_order_acquire);
    const auto windowLength = asset.slotLength[slot].load (std::memory_order_acquire);

    if (windowLength <= 0)
        return 0.0f; // not resident yet: silence, never a disk read here

    // The window ends mid-kernel all the time; a read that does not reach it
    // at all is a clip whose audio is not resident and must stay silent rather
    // than repeat a distant edge sample.
    if (start + nc::engine::resample::taps <= windowStart || start >= windowStart + windowLength)
        return 0.0f;

    const auto& data = channel == 0 ? asset.cacheA[slot] : asset.cacheB[slot];

    for (int tap = 0; tap < nc::engine::resample::taps; ++tap)
    {
        const auto frame = start + tap;
        const auto index = juce::jlimit<int64_t> (0, windowLength - 1, frame - windowStart);

        if (index < static_cast<int64_t> (data.size()))
            sum += w[tap] * data[static_cast<size_t> (index)];
    }

    return static_cast<float> (sum);
}
} // namespace nc
