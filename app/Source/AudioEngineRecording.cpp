// Track capture, input monitoring, MIDI recording and takes.
// Split out of AudioEngine.cpp (plan 07-03); bodies moved verbatim.

#include "AudioEngine.h"
#include <nc/engine/StreamWindow.hpp>

namespace nc
{
//==============================================================================
void RecordBuffer::prepare (int64_t capacityFrames)
{
    storage.setSize (2, static_cast<int> (juce::jmax<int64_t> (1, capacityFrames)), false, true, false);
    storage.clear();
    written = 0;
    overflowed = false;
}

void RecordBuffer::reset() noexcept
{
    written = 0;
    overflowed = false;
}

void RecordBuffer::push (const float* left, const float* right, int numSamples) noexcept
{
    const auto capacity = static_cast<int64_t> (storage.getNumSamples());

    if (capacity <= 0 || numSamples <= 0)
        return;

    const auto room = capacity - written;

    if (room <= 0)
    {
        overflowed = true;
        return;
    }

    const auto count = static_cast<int> (juce::jmin<int64_t> (room, numSamples));
    auto* outLeft = storage.getWritePointer (0) + written;
    auto* outRight = storage.getWritePointer (1) + written;

    if (left != nullptr)
        juce::FloatVectorOperations::copy (outLeft, left, count);
    else
        juce::FloatVectorOperations::clear (outLeft, count);

    if (right != nullptr)
        juce::FloatVectorOperations::copy (outRight, right, count);
    else
        juce::FloatVectorOperations::clear (outRight, count);

    written += count;

    if (count < numSamples)
        overflowed = true;
}

void AudioEngine::captureInputBlock (const float* const* inputChannelData,
                                     int numInputChannels,
                                     int numSamples,
                                     int64_t position)
{
    // Audio thread: no allocation, no locks, no disk. Armed tracks only, in
    // track order, so each armed track takes the next input channel (cycled when
    // there are fewer inputs than armed tracks).
    if (numInputChannels <= 0 || inputChannelData == nullptr || inputChannelData[0] == nullptr)
        numInputChannels = 0;

    if (capture.recordFramesWritten == 0)
        capture.recordStartFrame.store (position, std::memory_order_release);

    int inputIndex = 0;

    for (int track = 0; track < trackCount; ++track)
    {
        if (! capture.recordArmed[static_cast<size_t> (track)].load (std::memory_order_relaxed))
            continue;

        const auto* left = numInputChannels > 0
                               ? inputChannelData[inputIndex % numInputChannels] : nullptr;
        const auto* right = numInputChannels > 1
                                ? inputChannelData[(inputIndex + 1) % numInputChannels] : left;

        // Threaded path: push straight into the writer FIFO (no copy here;
        // the writer thread owns the disk). RAM path: preallocated buffer.
        bool pushedThreaded = false;

        for (auto& take : capture.threadedTakes)
        {
            if (take.track != track || take.writer == nullptr)
                continue;

            const auto silence = capture.recordSilence.data();
            const float* channels[2] { left != nullptr ? left : silence,
                                        right != nullptr ? right : silence };
            if (! take.writer->write (channels, numSamples))
                capture.overflowCount.fetch_add (1, std::memory_order_relaxed);
            pushedThreaded = true;
        }

        if (! pushedThreaded)
            capture.recordBuffers[static_cast<size_t> (track)].push (left, right, numSamples);

        ++inputIndex;
    }

    capture.recordFramesWritten += numSamples;
}

void AudioEngine::setRecordArmed (int track, bool armed)
{
    if (track < 0 || track >= trackCount)
        return;

    capture.recordArmed[static_cast<size_t> (track)].store (armed, std::memory_order_release);
}

bool AudioEngine::isRecordArmed (int track) const
{
    return track >= 0 && track < trackCount
           && capture.recordArmed[static_cast<size_t> (track)].load (std::memory_order_acquire);
}

void AudioEngine::setTrackMonitor (int track, bool on)
{
    if (track >= 0 && track < trackCount)
        capture.trackMonitor[static_cast<size_t> (track)].store (on, std::memory_order_release);
}

bool AudioEngine::isTrackMonitor (int track) const
{
    return track >= 0 && track < trackCount
           && capture.trackMonitor[static_cast<size_t> (track)].load (std::memory_order_acquire);
}

bool AudioEngine::consumePunchOut()
{
    return capture.punchOutPending.exchange (false, std::memory_order_acq_rel);
}

juce::Array<juce::MidiDeviceInfo> AudioEngine::availableMidiDevices()
{
    return MidiRecorder::availableDevices();
}

bool AudioEngine::openMidiDevice (const juce::String& deviceIdentifier, juce::String& error)
{
    return midiRecorder.openDevice (deviceIdentifier, error);
}

void AudioEngine::closeMidiDevice()
{
    midiRecorder.closeDevice();
}

bool AudioEngine::startMidiRecording (int track, juce::String& error)
{
    if (midiRecorder.isRecording())
    {
        error = "MIDI recording already in progress";
        return false;
    }

    if (! juce::isPositiveAndBelow (track, trackCount))
    {
        error = "MIDI record target is outside the session";
        return false;
    }

    const auto map = tempoMap();
    const auto rate = juce::jmax (1.0, sampleRate.load());
    const auto ppq = engine::ppqAtSamplesWithMap (map, clock.positionSamples(), rate);
    midiRecorder.startRecording (track, ppq, map);
    return true;
}

void AudioEngine::drainMidiRecording()
{
    midiRecorder.drain();
}

nc::engine::ClipState AudioEngine::stopMidiRecording()
{
    return midiRecorder.stopRecording();
}

int64_t AudioEngine::recordedFrames() const
{
    int64_t most = 0;

    for (int track = 0; track < trackCount; ++track)
        if (capture.recordArmed[static_cast<size_t> (track)].load (std::memory_order_relaxed))
            most = juce::jmax (most, capture.recordBuffers[static_cast<size_t> (track)].written);

    return most;
}

int AudioEngine::recordOverflows() const
{
    int count = 0;

    for (int track = 0; track < trackCount; ++track)
        if (capture.recordBuffers[static_cast<size_t> (track)].overflowed)
            ++count;

    return count;
}

bool AudioEngine::startRecording (juce::String& error)
{
    if (capture.recording.load (std::memory_order_acquire))
    {
        error = "Already recording";
        return false;
    }

    const auto rate = sampleRate.load();

    if (rate <= 0.0)
    {
        error = "No audio device is open";
        return false;
    }

    int armed = 0;

    for (int track = 0; track < trackCount; ++track)
        if (capture.recordArmed[static_cast<size_t> (track)].load (std::memory_order_relaxed))
            ++armed;

    if (armed == 0)
    {
        error = "Arm at least one track first";
        return false;
    }

    // Three minutes per armed track, allocated up front so the callback never
    // allocates. A take that runs past the buffer reports an overflow instead of
    // being silently truncated; streaming capture to disk is the next step.
    const auto capacity = static_cast<int64_t> (rate * 60.0 * 3.0);

    for (int track = 0; track < trackCount; ++track)
    {
        auto& buffer = capture.recordBuffers[static_cast<size_t> (track)];
        buffer.reset();

        if (capture.recordArmed[static_cast<size_t> (track)].load (std::memory_order_relaxed))
        {
            buffer.prepare (capacity);
            // The native UI arms tracks but has no separate monitor switch yet.
            // Monitoring follows the armed tracks while recording so the player
            // hears the input through the same chain being captured.
            capture.trackMonitor[static_cast<size_t> (track)].store (true, std::memory_order_relaxed);
        }
        else if (buffer.storage.getNumSamples() > 0)
            buffer.storage.setSize (0, 0);
    }

    capture.recordFramesWritten = 0;
    capture.recordStartFrame.store (clock.positionSamples(), std::memory_order_release);
    capture.recording.store (true, std::memory_order_release);
    error.clear();
    return true;
}

bool AudioEngine::startRecording (const juce::File& folder, juce::String& error)
{
    if (! folder.isDirectory() && ! folder.createDirectory())
    {
        error = "Cannot create the recording folder";
        return false;
    }

    if (! startRecording (error))
        return false;

    // Replace the RAM buffers with threaded writers: the callback pushes
    // blocks into a FIFO, the writer thread flushes WAV to disk.
    capture.threadedTakes.clear();
    juce::WavAudioFormat wav;
    const auto rate = sampleRate.load();
    int takeNumber = 0;

    for (int track = 0; track < trackCount; ++track)
    {
        if (! capture.recordArmed[static_cast<size_t> (track)].load (std::memory_order_relaxed))
            continue;

        const auto file = folder.getChildFile ("Track " + juce::String (track + 1)
                                               + " take " + juce::String (++takeNumber) + ".wav");
        file.deleteFile();
        std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());

        if (stream == nullptr)
        {
            error = "Cannot write " + file.getFullPathName();
            capture.threadedTakes.clear();
            capture.recording.store (false, std::memory_order_release);
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), rate, 2, 24, {}, 0));

        if (writer == nullptr)
        {
            error = "Unsupported recording format";
            capture.threadedTakes.clear();
            capture.recording.store (false, std::memory_order_release);
            return false;
        }

        ThreadedTake take;
        take.writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (
            writer.release(), capture.writerThread, 65536);
        take.file = file;
        take.track = track;
        take.startFrame = capture.recordStartFrame.load (std::memory_order_acquire);
        capture.threadedTakes.push_back (std::move (take));
    }

    error.clear();
    return true;
}

std::vector<AudioEngine::RecordedTake> AudioEngine::stopRecording (const juce::File& folder,
                                                                  int bitDepth,
                                                                  juce::String& error)
{
    std::vector<RecordedTake> takes;

    if (! capture.recording.exchange (false, std::memory_order_acq_rel))
    {
        error = "Not recording";
        return takes;
    }

    for (auto& monitor : capture.trackMonitor)
        monitor.store (false, std::memory_order_relaxed);

    // A callback that was already inside captureInputBlock when the flag
    // flipped is still using the writers it found in threadedTakes. Wait for
    // two later blocks to start (callbacks are strictly serial) before
    // touching the take list — the same retiredAtBlock + 2 rule the snapshot
    // sweeps apply. Without this, stop could free a writer mid-write.
    if (deviceOpen.load (std::memory_order_acquire))
    {
        const auto started = clock.blockCount();
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 500.0;

        while (clock.blockCount() < started + 2)
        {
            if (juce::Time::getMillisecondCounterHiRes() > deadline)
                break; // a wedged driver must not hang the UI

            juce::Thread::sleep (1);
        }
    }

    // Streaming takes are already on disk; deleting the writers flushes.
    // Note: the latency trim that the RAM path applies is not applied to
    // streamed takes (they start at the punch-in transport frame).
    if (! capture.threadedTakes.empty())
    {
        for (auto& take : capture.threadedTakes)
        {
            take.writer.reset();
            RecordedTake recorded;
            recorded.track = take.track;
            recorded.file = take.file;
            recorded.startFrame = take.startFrame;
            recorded.frames = capture.recordFramesWritten;
            takes.push_back (std::move (recorded));
        }

        capture.threadedTakes.clear();
        error.clear();
        return takes;
    }

    // Capture is closed; the buffers are no longer written from this point.
    const auto latency = static_cast<int64_t> (juce::jmax (0, capture.recordLatency.load()));
    const auto startFrame = capture.recordStartFrame.load (std::memory_order_acquire);
    const auto rate = sampleRate.load();

    if (! folder.isDirectory() && ! folder.createDirectory())
    {
        error = "Cannot create the recording folder";
        return takes;
    }

    juce::WavAudioFormat wav;
    int written = 0;

    for (int track = 0; track < trackCount; ++track)
    {
        if (! capture.recordArmed[static_cast<size_t> (track)].load (std::memory_order_relaxed))
            continue;

        auto& buffer = capture.recordBuffers[static_cast<size_t> (track)];
        const auto captured = buffer.written;
        const auto usable = static_cast<int64_t> (juce::jmax<int64_t> (0, captured - latency));

        if (usable <= 0)
            continue;

        const auto file = folder.getChildFile ("Track " + juce::String (track + 1)
                                               + " take " + juce::String (++written) + ".wav");
        file.deleteFile();

        std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());

        if (stream == nullptr)
        {
            error = "Cannot write " + file.getFullPathName();
            continue;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), rate, 2, bitDepth, {}, 0));

        if (writer == nullptr)
        {
            error = "Unsupported recording format";
            continue;
        }

        stream.release(); // the writer owns the stream from here

        // The head of the capture is the round-trip latency: dropping it aligns
        // the take with material that was already on the timeline.
        writer->writeFromAudioSampleBuffer (buffer.storage,
                                            static_cast<int> (latency),
                                            static_cast<int> (usable));
        writer.reset();

        RecordedTake take;
        take.track = track;
        take.file = file;
        take.startFrame = startFrame;
        take.frames = usable;
        takes.push_back (take);
    }

    for (auto& buffer : capture.recordBuffers)
        if (buffer.storage.getNumSamples() > 0)
            buffer.storage.setSize (0, 0);

    capture.recordFramesWritten = 0;
    return takes;
}
} // namespace nc
