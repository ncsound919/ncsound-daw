// Offline render and export: mixdown, file render, stem export.
// Split out of AudioEngine.cpp (plan 07-03); bodies moved verbatim.

#include "AudioEngine.h"
#include "AudioEngineInternal.h"
#include "Session.h"
#include <nc/engine/Export.hpp>
#include <thread>

namespace nc
{

//==============================================================================
void AudioEngine::prepareForRender (double rate, int maxBlock)
{
    const auto block = juce::jmax (1, maxBlock);

    sampleRate.store (rate);
    smoothingPrimed = false;
    resetRequested.store (false);
    seekRequested.store (false);

    // Sized exactly as audioDeviceAboutToStart() does, so renderBlock() never
    // sees a block larger than the scratch it was promised.
    mixBuffer.setSize (2, juce::jmax (block, 4096), false, true, false);
    instrumentBlock.setSize (2, juce::jmax (block, 4096), false, true, false);
    trackBlock.setSize (2 * trackCount, juce::jmax (block, 16384), false, true, false);
    auxABuffer.setSize (2, juce::jmax (block, 4096), false, true, false);
    auxBBuffer.setSize (2, juce::jmax (block, 4096), false, true, false);
    midiEventScratch.clear();
    midiEventScratch.reserve (4096);

    const auto active = juce::jlimit (0, trackCount, activeTrackCount.load());

    for (int i = 0; i < active; ++i)
        mixRack[static_cast<size_t> (i)].prepare (rate, block);

    masterRack.prepare (rate, block);
    preparedRackCount.store (active);
    inserts.prepare (rate, block, active);
    auxReverb.prepare (rate, block, static_cast<AuxBus::Engine> (auxReverbEngine.load()));
    auxDelay.prepare (rate, block, static_cast<AuxBus::Engine> (auxDelayEngine.load()));

    // Both the device path and an offline render reach the mix through this
    // rate, so the kernels are rebuilt here for everything that is resident.
    refreshResampleBanks();
}

bool AudioEngine::renderToWav (const juce::File& destination,
                               double startBar,
                               double endBar,
                               int bitDepth,
                               juce::String& error)
{
    nc::engine::export_::ExportOptions options;
    options.format = nc::engine::export_::Format::wav;
    options.bitDepth = bitDepth;
    // renderToWav predates ExportOptions and is documented as writing at the
    // engine rate; ExportOptions defaults to 48 kHz, which silently resampled
    // the mixdown on a 44.1 kHz session.
    const auto sessionRate = sampleRate.load();
    options.sampleRate = sessionRate > 0.0
                             ? static_cast<int> (std::lround (sessionRate))
                             : 48000;
    // Legacy call sites wrote undithered WAV; keep that contract here and
    // let renderToFile() be the dithered/loudness path for new UI.
    options.dither = false;
    return renderToFile (destination, startBar, endBar, options, error);
}

bool AudioEngine::renderToFile (const juce::File& destination,
                                double startBar,
                                double endBar,
                                const nc::engine::export_::ExportOptions& askedOptions,
                                juce::String& error)
{
    error.clear();

    // A failed render must not leave the previous export's loudness report
    // behind; callers read lastExportLoudness() after a false return too.
    lastLoudness = {};

    if (destination == juce::File())
    {
        error = "No destination file was chosen.";
        return false;
    }

    if (! (endBar > startBar))
    {
        error = "The export range is empty.";
        return false;
    }

    const auto options = nc::engine::export_::sanitizeOptions (askedOptions);
    const auto rate = options.sampleRate > 0 ? static_cast<double> (options.sampleRate)
                                             : (sampleRate.load() > 0.0 ? sampleRate.load() : 48000.0);
    constexpr int blockSize = 512;

    // Musical range comes from the tempo map, not the current BPM: with a
    // tempo change the constant-BPM conversion writes the wrong number of
    // seconds and cuts or pads the export.
    const auto map = tempoMap();
    const auto startSeconds = engine::ppqToSeconds (map, engine::clipStartBeat (startBar));
    const auto endSeconds = engine::ppqToSeconds (map, engine::clipStartBeat (endBar));
    const auto lengthFrames = static_cast<int64_t> (std::llround ((endSeconds - startSeconds) * rate));

    if (lengthFrames <= 0)
    {
        error = "The export range is shorter than one sample.";
        return false;
    }

    if (lengthFrames > nc::limits::maxAssetFrames (rate))
    {
        error = "The export range exceeds the 30-minute render ceiling.";
        return false;
    }

    // Copy appends prepareForRender + restore verbatim so transport/device
    // behaviour is identical to the legacy WAV path.
    offlineBlockDone.reset();
    offlineRender.store (true, std::memory_order_release);

    const auto previousPosition = positionSamples.load();
    const auto previousPlaying = playing.load();
    const auto previousLoop = loopEnabled.load();
    const auto previousMetronome = metronome.load();
    const auto previousRate = sampleRate.load();
    const auto previousBufferSize = juce::jmax (1, bufferSize.load());

    // Everything below can return early (or throw while growing the render
    // buffers). Leaving offlineRender set or the borrowed export rate in place
    // would silence the device for the rest of the session, so the session is
    // put back from a scope guard instead of at every return.
    struct RestoreSession
    {
        std::function<void()> restore;
        ~RestoreSession() { restore(); }
    };

    RestoreSession restoreSession;
    restoreSession.restore = [this, previousPosition, previousPlaying, previousLoop,
                              previousMetronome, previousRate, previousBufferSize]
    {
        playing.store (previousPlaying);
        loopEnabled.store (previousLoop);
        metronome.store (previousMetronome);

        // prepareForRender() replaced the device rate with the export rate;
        // re-prepare the racks and scratch at the rate the device runs at.
        prepareForRender (previousRate > 0.0 ? previousRate : 48000.0, previousBufferSize);
        resetRequested.store (false);
        seekRequested.store (false);
        positionSamples.store (previousPosition);
        offlineRender.store (false, std::memory_order_release);
    };

    // Two callbacks have to return after the flag is set: the one that may
    // already have been inside renderBlock when it was set, and the one after
    // it, which returns immediately. Callbacks are sequential, so once the
    // second has signalled, nothing can still be holding the buffers that
    // prepareForRender() is about to resize. Waiting for that is a guarantee
    // where the old fixed sleep was a guess that a slow block could outrun.
    if (deviceOpen.load (std::memory_order_acquire))
        for (int i = 0; i < 2; ++i)
            if (! offlineBlockDone.wait (500))
                break;

    prepareForRender (rate, blockSize);

    const auto chainLatency = inserts.worstTrackLatency()
                              + juce::jmax (0, masterRack.latencySamples())
                              + inserts.masterLatency();
    const auto plan = engine::planRender (chainLatency, 0);

    const auto startFrame = static_cast<int64_t> (std::llround (startSeconds * rate));

    positionSamples.store (startFrame - plan.prerollFrames);
    playing.store (true);
    loopEnabled.store (false);
    metronome.store (false);

    juce::AudioBuffer<float> block (2, blockSize);
    float* channels[2] { block.getWritePointer (0), block.getWritePointer (1) };

    auto toDrop = plan.dropFrames;
    auto remaining = plan.prerollFrames + lengthFrames;
    auto framesWritten = static_cast<int64_t> (0);

    // Pass 1: render to memory so loudness can be measured before writing.
    std::vector<float> leftBuffer;
    std::vector<float> rightBuffer;
    leftBuffer.reserve (static_cast<size_t> (lengthFrames));
    rightBuffer.reserve (static_cast<size_t> (lengthFrames));

    while (remaining > 0)
    {
        const auto frames = static_cast<int> (juce::jmin<int64_t> (blockSize, remaining));

        renderBlock (nullptr, 0, channels, 2, frames);

        auto* left = block.getReadPointer (0);
        auto* right = block.getReadPointer (1);
        auto usable = frames;

        if (toDrop > 0)
        {
            const auto skip = static_cast<int> (juce::jmin<int64_t> (toDrop, usable));
            left += skip;
            right += skip;
            usable -= skip;
            toDrop -= skip;
        }

        if (usable > 0)
        {
            leftBuffer.insert (leftBuffer.end(), left, left + usable);
            rightBuffer.insert (rightBuffer.end(), right, right + usable);
            framesWritten += usable;
        }

        remaining -= frames;
    }

    if (framesWritten < lengthFrames)
    {
        error = "The mixdown ended early; the file may be incomplete.";
        return false;
    }

    // Measure, optionally normalise onto the LUFS target under the true-peak
    // ceiling, then dither once on bit-depth reduction.
    lastLoudness = nc::engine::export_::measureLoudness (leftBuffer.data(), rightBuffer.data(),
                                                          leftBuffer.size(), rate);

    if (options.normalizeToTarget && ! lastLoudness.silent)
    {
        auto gainDb = nc::engine::export_::gainDbForTarget (lastLoudness, options.lufsTarget);
        // The ceiling limits how far the render may be pushed up; using
        // gainDbForTruePeak() here (it never returns a positive gain) turned
        // every normalisation boost into a no-op.
        gainDb = std::min (gainDb, nc::engine::export_::maxBoostForTruePeak (
                                       lastLoudness, options.truePeakCeilingDb));
        const auto gain = static_cast<float> (juce::Decibels::decibelsToGain (static_cast<float> (gainDb)));

        for (auto& s : leftBuffer)
            s *= gain;
        for (auto& s : rightBuffer)
            s *= gain;

        lastLoudness = nc::engine::export_::measureLoudness (leftBuffer.data(), rightBuffer.data(),
                                                              leftBuffer.size(), rate);
    }

    if (options.dither && options.bitDepth < 32)
    {
        nc::engine::export_::DitherRng rng;
        rng.seed (0xC0FFEEu + static_cast<uint32_t> (lengthFrames));
        nc::engine::export_::applyTpdfDither (leftBuffer.data(), leftBuffer.size(), options.bitDepth, rng);
        nc::engine::export_::applyTpdfDither (rightBuffer.data(), rightBuffer.size(), options.bitDepth, rng);
    }

    std::unique_ptr<juce::FileOutputStream> stream (destination.createOutputStream());

    if (stream == nullptr)
    {
        lastLoudness = {};
        error = "The destination could not be opened for writing.";
        return false;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer;

    if (options.format == nc::engine::export_::Format::wav)
    {
        juce::WavAudioFormat wav;
        writer.reset (wav.createWriterFor (stream.get(), rate, 2, options.bitDepth, {}, 0));
    }
    else
    {
        // FLAC/MP3/AIFF via the registered format manager. MP3 depends on
        // the bundled LAME encoder being present; without it the writer is
        // null and the export fails with an actionable message.
        if (auto* format = formats.findFormatForFileExtension (
                juce::String (nc::engine::export_::formatExtension (options.format))))
        {
            writer.reset (format->createWriterFor (stream.get(), rate, 2, options.bitDepth, {}, 0));
        }
    }

    if (writer == nullptr)
    {
        lastLoudness = {};
        error = juce::String (nc::engine::export_::formatName (options.format))
                + " writer could not be created at that sample rate and depth. "
                  "Try WAV or FLAC.";
        return false;
    }

    stream.release();

    // Write in blocks so the writer's internal buffering matches the render.
    constexpr int writeBlock = 4096;
    auto writeOk = true;

    for (size_t offset = 0; offset < leftBuffer.size(); offset += writeBlock)
    {
        const auto count = static_cast<int> (juce::jmin<size_t> (writeBlock, leftBuffer.size() - offset));
        const float* from[2] { leftBuffer.data() + offset, rightBuffer.data() + offset };
        writeOk = writer->writeFromFloatArrays (from, 2, count) && writeOk;
    }

    writer->flush();
    writer.reset();

    if (! writeOk)
    {
        lastLoudness = {};
        error = "The file could not be written completely; check disk space and permissions.";
        return false;
    }

    return true;
}

int AudioEngine::exportStems (const juce::File& folder,
                              double startBar,
                              double endBar,
                              const nc::engine::export_::ExportOptions& askedOptions,
                              std::vector<juce::File>& written,
                              juce::String& error)
{
    written.clear();
    error.clear();

    if (folder == juce::File())
    {
        error = "No stems folder was chosen.";
        return 0;
    }

    // createOutputStream() never creates parent folders, so a fresh or
    // user-typed stems path failed for every track with "could not be opened".
    if (! folder.isDirectory() && ! folder.createDirectory())
    {
        error = "The stems folder could not be created.";
        return 0;
    }

    const auto options = nc::engine::export_::sanitizeOptions (askedOptions);
    const auto active = juce::jlimit (0, trackCount, activeTrackCount.load());
    std::vector<bool> previousMuted;
    std::vector<bool> previousSolo;
    previousMuted.reserve (static_cast<size_t> (active));
    previousSolo.reserve (static_cast<size_t> (active));

    for (int track = 0; track < active; ++track)
    {
        previousMuted.push_back (trackMuted (track));
        previousSolo.push_back (trackSolo (track));
    }

    // Mute is ignored for soloed tracks, so a solo anywhere in the session
    // would leak into every stem (and silence the stem the solo excludes).
    // Clear solo for the pass and restore it afterwards.
    for (int track = 0; track < active; ++track)
        tracks[static_cast<size_t> (track)].soloed.store (false);

    int failures = 0;

    const auto hasMaterial = [this, startBar, endBar] (int track)
    {
        const auto overlaps = [startBar, endBar] (double clipStart, double clipLen)
        {
            return clipStart < endBar && clipStart + clipLen > startBar;
        };

        const auto& clips = timelineClips();
        for (const auto& clip : clips)
        {
            if (clip.track == track && overlaps (clip.startBar, clip.lengthBars))
                return true;
        }

        for (const auto& clip : midiClips())
        {
            if (clip.track == track && overlaps (clip.startBar, clip.lengthBars))
                return true;
        }

        return false;
    };

    for (int track = 0; track < active; ++track)
    {
        for (int other = 0; other < active; ++other)
            tracks[static_cast<size_t> (other)].muted.store (other == track ? false : true);

        juce::String trackError;
        auto stem = folder.getChildFile (juce::String (nc::engine::export_::stemFileName (
            options.baseName, track, sessionTracks()[static_cast<size_t> (track)].name.toStdString(),
            options.format)));

        // A track with no clips in range is silence by definition: write it
        // directly instead of a full mix render (the 16-pass cost). Hidden
        // intent: mute/solo state must not change the fast path.
        bool fastSilence = ! hasMaterial (track);

        if (fastSilence)
        {
            const auto rate = options.sampleRate > 0 ? static_cast<double> (options.sampleRate)
                                                     : (sampleRate.load() > 0.0 ? sampleRate.load() : 48000.0);
            // The musical range must follow the tempo map exactly like the
            // audible stem path; a constant-BPM calculation writes the wrong
            // length for any project with a tempo change.
            const auto map = tempoMap();
            const auto startSeconds = engine::ppqToSeconds (map, engine::clipStartBeat (startBar));
            const auto endSeconds = engine::ppqToSeconds (map, engine::clipStartBeat (endBar));
            const auto frames = static_cast<int64_t> (std::llround ((endSeconds - startSeconds) * rate));

            if (frames <= 0)
            {
                ++failures;
                error = stem.getFileName() + ": export range is empty.";
                continue;
            }

            std::unique_ptr<juce::FileOutputStream> stream (stem.createOutputStream());

            if (stream == nullptr)
            {
                ++failures;
                error = stem.getFileName() + ": could not be opened.";
                continue;
            }

            // A silent stem is still a file the user asked to be FLAC/MP3/AIFF,
            // not WAV data behind a foreign extension. Use the same format
            // factory as the audible path.
            std::unique_ptr<juce::AudioFormatWriter> writer;

            if (options.format == nc::engine::export_::Format::wav)
            {
                juce::WavAudioFormat wav;
                writer.reset (wav.createWriterFor (stream.get(), rate, 2, options.bitDepth, {}, 0));
            }
            else if (auto* format = formats.findFormatForFileExtension (
                         juce::String (nc::engine::export_::formatExtension (options.format))))
            {
                writer.reset (format->createWriterFor (stream.get(), rate, 2, options.bitDepth, {}, 0));
            }

            if (writer == nullptr)
            {
                ++failures;
                error = stem.getFileName() + ": could not be written in "
                        + juce::String (nc::engine::export_::formatName (options.format)) + ".";
                continue;
            }

            stream.release();
            juce::AudioBuffer<float> silence (2, 4096);
            silence.clear();
            auto remaining = frames;
            bool ok = true;

            while (remaining > 0 && ok)
            {
                const auto chunk = static_cast<int> (std::min<int64_t> (remaining, silence.getNumSamples()));
                ok = writer->writeFromAudioSampleBuffer (silence, 0, chunk);
                remaining -= chunk;
            }

            writer.reset();

            if (! ok)
            {
                ++failures;
                error = stem.getFileName() + ": write failed.";
            }
            else
            {
                written.push_back (stem);
            }

            continue;
        }

        if (renderToFile (stem, startBar, endBar, options, trackError))
            written.push_back (stem);
        else
        {
            ++failures;
            error = stem.getFileName() + ": " + trackError;
        }
    }

    for (int track = 0; track < active; ++track)
    {
        tracks[static_cast<size_t> (track)].muted.store (previousMuted[static_cast<size_t> (track)]);
        tracks[static_cast<size_t> (track)].soloed.store (previousSolo[static_cast<size_t> (track)]);
    }

    return static_cast<int> (written.size());
}

} // namespace nc
