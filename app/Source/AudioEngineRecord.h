#pragma once

#include <nc/engine/ProjectLimits.hpp>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace nc
{
/** One armed track's capture: preallocated, appended without allocating. */
struct RecordBuffer
{
    juce::AudioBuffer<float> storage;
    int64_t written = 0;
    bool overflowed = false;

    void prepare (int64_t capacityFrames);
    void push (const float* left, const float* right, int numSamples) noexcept;
    void reset() noexcept;
};

/** Streaming takes (ThreadedWriter): when non-empty, capture pushes straight
    to disk instead of the RAM buffers. */
struct ThreadedTake
{
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    juce::File file;
    int track = -1;
    int64_t startFrame = 0;
};

/** Everything the input callback appends to, and the flags the clips read. */
struct RecordCapture
{
    std::atomic<bool> captureAvailable { false };
    std::array<RecordBuffer, nc::limits::maxTracks> recordBuffers;
    std::array<std::atomic<bool>, nc::limits::maxTracks> recordArmed {};
    std::vector<float> recordSilence { 16384, 0.0f }; // ThreadedWriter needs non-null channels

    // Message-thread ownership of the take list. The callback never walks this
    // vector: it reads one published writer per track, so start/stop can add or
    // retire takes without racing a block that is already writing.
    std::vector<ThreadedTake> threadedTakes;
    std::array<std::atomic<juce::AudioFormatWriter::ThreadedWriter*>,
               nc::limits::maxTracks> recordWriters {};
    juce::TimeSliceThread writerThread { "RecordWriter" };

    std::array<std::atomic<bool>, nc::limits::maxTracks> trackMonitor {};
    std::atomic<bool> punchOutPending { false };
    std::atomic<bool> recording { false };
    std::atomic<int> recordLatency { 0 };
    std::atomic<int> overflowCount { 0 };
    std::atomic<int64_t> recordStartFrame { 0 };
    std::atomic<int64_t> recordFramesWritten { 0 };
};
} // namespace nc
