#pragma once

#include "AssetLibrary.h"

#include <nc/engine/ProjectLimits.hpp>
#include <nc/engine/TempoMap.hpp>

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>

namespace nc
{
/** The audio-thread view of a timeline clip: which asset, which window of it,
    and the fades already resolved to bars. Built by the prep loop from the
    command rail; it never touches the message-thread TimelineClip. */
struct PlaybackClip
{
    const AssetLibrary::Asset* asset = nullptr;
    int track = 0;
    double startBar = 1.0;
    double lengthBars = 1.0;
    double sourceOffsetSeconds = 0.0;
    float gainDb = 0.0f;
    float fadeInBars = 0.0f;
    float fadeOutBars = 0.0f;
    float crossfadeBars = 0.0f;
};

enum class CommandType : juce::uint8 { clear, add };

struct Command
{
    CommandType type = CommandType::clear;
    PlaybackClip clip;
};

// Render scratch for the audio callback: preallocated once with the engine
// so the 1024-clip timeline never touches the callback stack or heap.
struct RenderClip
{
    const AssetLibrary::Asset* asset = nullptr;
    int track = 0;
    int64_t startSample = 0;
    int64_t endSample = 0;
    double sourceOffsetSeconds = 0.0;
    double lengthSeconds = 0.0;
    double fadeInSeconds = 0.0;
    double fadeOutSeconds = 0.0;
    double crossfadeSeconds = 0.0;
    float gain = 1.0f;
};

/** Shared per-block state handed to each render pass. Constructed once at
    the top of renderBlock; the passes read and write it instead of the
    block's locals, so no pass needs the whole argument list. */
struct RenderContext
{
    const float* const* inputChannelData = nullptr;
    int numInputChannels = 0;
    float* const* outputChannelData = nullptr;
    int numOutputChannels = 0;
    int numSamples = 0;

    double rate = 48000.0;
    bool isPlaying = false;
    bool clickOn = false;
    bool anySolo = false;

    double tempo = 120.0;
    double beatSamples = 0.0;
    const engine::TempoMap* tempoMap = nullptr;

    bool loop = false;
    int64_t loopStart = 0;
    int64_t loopEnd = 0;
    int64_t blockStartSamples = 0;
    double blockStartPpq = 0.0;
    int64_t position = 0;

    float* mixLeft = nullptr;
    float* mixRight = nullptr;
    bool trackBufferReady = false;

    float* auxALeft = nullptr;
    float* auxARight = nullptr;
    float* auxBLeft = nullptr;
    float* auxBRight = nullptr;

    std::array<float, nc::limits::maxTracks> trackLeftScratch {};
    std::array<float, nc::limits::maxTracks> trackRightScratch {};
    std::array<float, nc::limits::maxTracks> automatedGain {};
    std::array<float, nc::limits::maxTracks> automatedPan {};
    std::array<float, nc::limits::maxTracks> blockPeaks {};

    float masterPeak = 0.0f;
    float masterPeakLeft = 0.0f;
    float masterPeakRight = 0.0f;
};

/** Audio-thread only. renderScratch is reused every block: the prep loop
    invalidates each slot it skips so stale clips can never replay. */
struct RenderScratch
{
    std::array<PlaybackClip, nc::limits::maxClips> playbackClips {};
    std::array<RenderClip, nc::limits::maxClips> renderScratch {};
    int playbackClipCount = 0;

    double clickPhase = 0.0;
    double clickEnvelope = 0.0;
    double clickOscillator = 0.0;
    double probePhase = 0.0;   // diagnostic sine phase (NC_TEST_TONE_HZ)
    int clickBeat = 0;

    float smoothedGain[nc::limits::maxTracks] {};
    float smoothedPan[nc::limits::maxTracks] {};
    float smoothedSendA[nc::limits::maxTracks] {};
    float smoothedSendB[nc::limits::maxTracks] {};
    float smoothedWidth[nc::limits::maxTracks] {};
    // Equal-power pan gains, recomputed only when smoothedPan moves (audio
    // thread only; NaN-free defaults so the first block is correct either way).
    float cachedPanValue[nc::limits::maxTracks] { -2.0f };
    float cachedPanLeft[nc::limits::maxTracks] { 0.7071f };
    float cachedPanRight[nc::limits::maxTracks] { 0.7071f };
    float smoothedMaster = 0.8f;
    bool smoothingPrimed = false;
    bool transportWasPlaying = false;
};
} // namespace nc
