#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <nc/engine/AudioSuite.hpp>
#include <nc/engine/ProjectLimits.hpp>
#include <nc/engine/Resample.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace nc
{
/** Owns the session's audio assets and the sample-read path the callback uses.

    Message thread: add, find, durationSeconds, name, file, sampleRate, samples,
    peakEnvelope, trim, frameCount, refreshResampleBanks, serviceStreaming,
    demoAssetId/setDemoAssetId.

    Audio thread: sampleAt, readSourceSample and readBandLimited only. They read
    a published stream window through atomics; they never touch the disk, never
    allocate, and never wait on the message thread.
*/
class AssetLibrary
{
public:
    struct Asset
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 48000.0;
        juce::String name;
        juce::File file;   // empty for generated demo assets

        // Long imports stream: the reader stays open and only a bounded
        // read-ahead window is resident, so a long take costs seconds of RAM.
        // The window is published with an atomic slot flip; the callback never
        // reads the disk and never waits on the message thread.
        bool streamed = false;
        int64_t totalFrames = 0;

        // Kernels for playing this asset at a session rate of its own. Built on
        // the message thread when the asset is added and whenever the session
        // rate changes; a clip whose rate already matches reads frames directly
        // and never touches this.
        nc::engine::resample::Bank resample;
        int64_t cacheFrames = 0;
        std::unique_ptr<juce::AudioFormatReader> reader;
        std::array<std::vector<float>, 2> cacheA;
        std::array<std::vector<float>, 2> cacheB;
        std::array<std::atomic<int64_t>, 2> slotStart {};
        std::array<std::atomic<int64_t>, 2> slotLength {};
        std::atomic<int> activeCache { 0 };
    };

    /** Appends an asset and builds its resampling kernels for `outputRate`.
        Returns the new id, or -1 when the asset has no frames. Message thread. */
    int add (std::unique_ptr<Asset> asset, double outputRate);

    int size() const { return static_cast<int> (assets.size()); }
    Asset* find (int assetId);
    const Asset* find (int assetId) const;

    /** Index of the generated demo asset for a track, or -1. */
    int demoAssetId (int track) const;
    void setDemoAssetId (int track, int assetId);

    double durationSeconds (int assetId) const;
    juce::String name (int assetId) const;
    juce::File file (int assetId) const;
    double sampleRate (int assetId) const;

    /** Mono fold of the part of an asset a clip plays. Empty for a streamed
        asset, whose frames are not all resident. */
    juce::AudioBuffer<float> samples (int assetId, double startSeconds, double lengthSeconds) const;

    /** Peak envelope for the part of an asset a clip plays, so a UI can draw a
        waveform. Empty for a streamed asset. */
    std::vector<float> peakEnvelope (int assetId, double startSeconds,
                                     double lengthSeconds, int bins) const;

    /** AudioSuite: first audible frame and length of a resident asset. */
    nc::engine::audiosuite::Trim trim (int assetId, float threshold) const;

    int64_t frameCount (const Asset& asset) const;

    /** Rebuilds the resampling kernels of every asset for the output rate.
        Message thread only. */
    void refreshResampleBanks (double outputRate);

    /** Refills the streaming read-ahead windows around the transport. Message
        thread only; `positionSeconds` is the transport position. */
    void serviceStreaming (double outputRate, double positionSeconds);

    float sampleAt (const Asset& asset, int64_t frame, int channel) const;

    /** One source frame at a fractional position, band-limited when the asset's
        rate differs from the output rate. */
    float readSourceSample (const Asset& asset, double sourceFrame, double outputRate,
                            int channel) const;
    float readBandLimited (const Asset& asset, double sourceFrame, int channel) const;

private:
    std::vector<std::unique_ptr<Asset>> assets;

    static constexpr std::array<int, nc::limits::maxTracks> emptyDemoIds()
    {
        std::array<int, nc::limits::maxTracks> ids {};

        for (auto& id : ids)
            id = -1;

        return ids;
    }

    std::array<int, nc::limits::maxTracks> demoAssets = emptyDemoIds();
};
} // namespace nc
