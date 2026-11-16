// Device callbacks, the block render path, aux returns and mix rack state.
// Split out of AudioEngine.cpp (plan 07-03); bodies moved verbatim.

#include "AudioEngine.h"
#include "AudioEngineInternal.h"
#include "Session.h"
#include <nc/engine/LatencyComp.hpp>
#include <nc/engine/Resample.hpp>
#include <cmath>

namespace nc
{
void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate.store (device != nullptr ? device->getCurrentSampleRate() : 48000.0);
    bufferSize.store (device != nullptr ? device->getCurrentBufferSizeSamples() : 0);
    deviceOpen.store (device != nullptr);
    deviceInfo = device != nullptr ? device->getName() : "no audio device";
    captureAvailable.store (device != nullptr
                            && device->getActiveInputChannels().countNumberOfSetBits() > 0);
    smoothingPrimed = false;
    resetRequested.store (true);

    mixBuffer.setSize (2, juce::jmax (bufferSize.load(), 4096), false, true, false);
    instrumentBlock.setSize (2, juce::jmax (bufferSize.load(), 4096), false, true, false);
    trackBlock.setSize (2 * trackCount, juce::jmax (bufferSize.load(), 16384), false, true, false);
    auxABuffer.setSize (2, juce::jmax (bufferSize.load(), 4096), false, true, false);
    auxBBuffer.setSize (2, juce::jmax (bufferSize.load(), 4096), false, true, false);
    midiEventScratch.clear();
    midiEventScratch.reserve (4096);

    const auto maxBlock = juce::jmax (1, bufferSize.load());
    const auto active = juce::jlimit (0, trackCount, activeTrackCount.load());

    for (int i = 0; i < active; ++i)
        mixRack[static_cast<size_t> (i)].prepare (sampleRate.load(), maxBlock);

    masterRack.prepare (sampleRate.load(), maxBlock);
    preparedRackCount.store (active);
    inserts.prepare (sampleRate.load(), maxBlock, active);
    auxReverb.prepare (sampleRate.load(), maxBlock,
                       static_cast<AuxBus::Engine> (auxReverbEngine.load()));
    auxDelay.prepare (sampleRate.load(), maxBlock,
                      static_cast<AuxBus::Engine> (auxDelayEngine.load()));

    if (juce::SystemStats::getEnvironmentVariable ("NC_AUDIO_DUMP", {}).isNotEmpty())
        juce::File (juce::SystemStats::getEnvironmentVariable ("NC_AUDIO_DUMP", {}))
            .getSiblingFile ("audio-diag.txt")
            .replaceWithText ("device=" + deviceInfo
                              + "\nsampleRate=" + juce::String (sampleRate.load())
                              + "\nbufferSize=" + juce::String (bufferSize.load())
                              + "\nactiveTracks=" + juce::String (activeTrackCount.load())
                              + "\ninputsAvailable=" + juce::String (captureAvailable.load() ? 1 : 0));
}

void AudioEngine::setAuxEngine (int aux, AuxBus::Engine newEngine)
{
    if (aux == 0 && ! AuxBus::isReverb (newEngine))
        return;

    if (aux == 1 && AuxBus::isReverb (newEngine))
        return;

    if (aux == 0)
    {
        auxReverbEngine.store (static_cast<int> (newEngine));
        auxReverb.setEngine (newEngine, dspBlocksMeasured());
    }
    else if (aux == 1)
    {
        auxDelayEngine.store (static_cast<int> (newEngine));
        auxDelay.setEngine (newEngine, dspBlocksMeasured());
    }
}

AuxBus::Engine AudioEngine::auxEngine (int aux) const
{
    return static_cast<AuxBus::Engine> ((aux == 0 ? auxReverbEngine : auxDelayEngine).load());
}

juce::String AudioEngine::auxEngineName (int aux) const
{
    return AuxBus::engineName (auxEngine (aux));
}

void AudioEngine::audioDeviceStopped()
{
    automationRetired.clear();
    tempoRetired.clear();
    midiRetired.clear();
    deviceOpen.store (false);
    bufferSize.store (0);
    dspLoadValue.store (0.0f);
    masterPeakValue.store (0.0f);

    for (auto& track : tracks)
        track.peak.store (0.0f);

    for (auto& rack : mixRack)
        rack.reset();

    masterRack.reset();
    preparedRackCount.store (0);
    inserts.collectGarbage();
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                                    int numInputChannels,
                                                    float* const* outputChannelData,
                                                    int numOutputChannels,
                                                    int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    // One tick per callback. Retired snapshots are stamped with this value at
    // the swap and freed only after a later callback has started.
    audioBlockCount.fetch_add (1, std::memory_order_acq_rel);
    // An offline mixdown owns the engine. Render silence rather than run a
    // second pass into the device, and touch no shared state while doing it.
    if (offlineRender.load (std::memory_order_acquire))
    {
        for (int channel = 0; channel < numOutputChannels; ++channel)
            if (outputChannelData[channel] != nullptr)
                juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);

        // Export waits on this instead of guessing how long a block takes.
        offlineBlockDone.signal();
        return;
    }

    const auto startTicks = juce::Time::getHighResolutionTicks();

    renderBlock (inputChannelData, numInputChannels, outputChannelData,
                 numOutputChannels, numSamples);

    // Measured callback load, not a sine wave pretending to be a DSP meter.
    const auto elapsed = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTicks);
    const auto budget = static_cast<double> (numSamples) / juce::jmax (1.0, sampleRate.load());
    const auto ratio = budget > 0.0 ? static_cast<float> (elapsed / budget) : 0.0f;
    dspLoadValue.store (juce::jlimit (0.0f, 1.0f, dspLoadValue.load() * 0.82f + juce::jlimit (0.0f, 1.0f, ratio) * 0.18f));
    dspBlockCount.fetch_add (1, std::memory_order_relaxed);
    if (ratio > 1.0f)
        dspOverrunCount.fetch_add (1, std::memory_order_relaxed);
}

void AudioEngine::renderBlock (const float* const* inputChannelData, int numInputChannels,
                               float* const* outputChannelData, int numOutputChannels, int numSamples)
{
    // Flush denormals for the whole native chain. The aux reverb combs and
    // allpasses (x0.84 / x0.5 feedback per pass) and the delay's feedback
    // lowpass (x0.75 per sample) all decay exponentially towards zero, and
    // the hosted-plugin wrappers carry their own scope, so without this the
    // feedback state sits in denormal arithmetic for seconds after every
    // tail — a several-fold real-time cost on the audio thread that the
    // listener hears as crackle.
    const juce::ScopedNoDenormals noDenormals;

    for (int channel = 0; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear (outputChannelData[channel], numSamples);

    if (numOutputChannels == 0 || numSamples <= 0)
        return;

    processCommands();

    // A device starting up asks for a transport reset. An export in progress is
    // not the place for that: it keeps the request pending for the next live block.
    if (! offlineRender.load (std::memory_order_acquire) && resetRequested.exchange (false))
    {
        clickPhase = 0.0;
        clickEnvelope = 0.0;
        clickOscillator = 0.0;
        clickBeat = 0;
        positionSamples.store (0);
    }

    if (seekRequested.exchange (false))
    {
        clickPhase = 0.0;
        clickEnvelope = 0.0;
        clickOscillator = 0.0;
        clickBeat = 0;
    }

    if (! smoothingPrimed)
    {
        for (int track = 0; track < trackCount; ++track)
        {
            smoothedGain[track] = tracks[static_cast<size_t> (track)].gain.load();
            smoothedPan[track] = tracks[static_cast<size_t> (track)].pan.load();
            smoothedSendA[track] = tracks[static_cast<size_t> (track)].sendA.load();
            smoothedSendB[track] = tracks[static_cast<size_t> (track)].sendB.load();
            smoothedWidth[track] = tracks[static_cast<size_t> (track)].width.load();
        }

        smoothedMaster = masterGainValue.load();
        smoothingPrimed = true;
    }

    const auto rate = sampleRate.load();
    const auto isPlaying = playing.load();

    // Faders set while stopped are never smoothed (the per-sample loop below
    // only runs during playback), so the first block after Play would ramp from
    // the stale value -- usually unity -- down to the gain the session really
    // has. That ramp is a full-scale burst on the first samples of every play,
    // and the master maximizer's reaction to it is the metallic crack at the
    // start. Prime every smoother from the live values on the transition into
    // play so the transport starts at the gain the session actually has.
    if (isPlaying && ! transportWasPlaying)
    {
        for (int track = 0; track < trackCount; ++track)
        {
            smoothedGain[track] = tracks[static_cast<size_t> (track)].gain.load();
            smoothedPan[track] = tracks[static_cast<size_t> (track)].pan.load();
            smoothedSendA[track] = tracks[static_cast<size_t> (track)].sendA.load();
            smoothedSendB[track] = tracks[static_cast<size_t> (track)].sendB.load();
            smoothedWidth[track] = tracks[static_cast<size_t> (track)].width.load();
        }

        smoothedMaster = masterGainValue.load();
    }

    transportWasPlaying = isPlaying;
    const auto clickOn = metronome.load();

    if (rate <= 0.0)
        return;

    // Tempo map (Task 2): the live snapshot drives clip placement, loop
    // points, automation PPQ and MIDI scheduling. `tempo` is the BPM in
    // force at the block start, for the click and transport block.
    const auto* tempoMapPtr = tempoSlot.live.load (std::memory_order_acquire);
    const auto& tempoMap = tempoMapPtr != nullptr ? *tempoMapPtr : fallbackTempo;
    const auto blockStartPpq = engine::ppqAtSamplesWithMap (tempoMap, positionSamples.load(), rate);
    const auto tempo = engine::bpmAt (tempoMap, blockStartPpq);
    const auto secondsPerBar = engine::secondsPerBar (tempo);
    const auto beatSamples = rate * 60.0 / juce::jmax (1.0, tempo);

    bool anySolo = false;

    for (const auto& track : tracks)
        anySolo = anySolo || track.soloed.load();

    for (int i = 0; i < playbackClipCount; ++i)
    {
        const auto& clip = playbackClips[static_cast<size_t> (i)];
        auto& render = renderScratch[static_cast<size_t> (i)];
        render.asset = nullptr;

        if (clip.asset == nullptr || assetFrameCount (*clip.asset) <= 0)
            continue;

        const auto clipStartPpq = engine::clipStartBeat (clip.startBar);
        const auto clipEndPpq = clipStartPpq + engine::clipLengthBeats (clip.lengthBars);
        const auto startSeconds = engine::ppqToSeconds (tempoMap, clipStartPpq);
        const auto lengthSeconds = juce::jmax (0.0, engine::ppqToSeconds (tempoMap, clipEndPpq) - startSeconds);

        if (lengthSeconds <= 0.0)
            continue;
        const auto startSample = static_cast<int64_t> (std::llround (startSeconds * rate));
        const auto endSample = static_cast<int64_t> (std::llround ((startSeconds + lengthSeconds) * rate));

        if (endSample <= startSample)
            continue;
        render.asset = clip.asset;
        render.track = clip.track;
        render.startSample = startSample;
        render.endSample = endSample;
        render.sourceOffsetSeconds = clip.sourceOffsetSeconds;
        render.lengthSeconds = lengthSeconds;
        render.fadeInSeconds = juce::jlimit (0.0, lengthSeconds, clip.fadeInBars * secondsPerBar);
        render.fadeOutSeconds = juce::jlimit (0.0, lengthSeconds, clip.fadeOutBars * secondsPerBar);
        render.crossfadeSeconds = juce::jlimit (0.0, lengthSeconds, clip.crossfadeBars * secondsPerBar);
        render.gain = juce::Decibels::decibelsToGain (clip.gainDb);
    }

    const auto loopStart = static_cast<int64_t> (std::llround (
        engine::ppqToSeconds (tempoMap, engine::clipStartBeat (loopStartBar.load())) * rate));
    const auto loopEnd = static_cast<int64_t> (std::llround (
        engine::ppqToSeconds (tempoMap, engine::clipStartBeat (loopEndBar.load())) * rate));
    const auto loop = loopEnabled.load() && loopEnd > loopStart;

    auto position = positionSamples.load();

    if (isPlaying && loop && position >= loopEnd)
    {
        // Punch-out: the loop end finalises a take. The audio thread only
        // flags it; the message thread stops and places the take.
        if (recording.load (std::memory_order_acquire))
            punchOutPending.store (true, std::memory_order_release);

        position = loopStart;
        clickPhase = 0.0;
        clickEnvelope = 0.0;
        clickOscillator = 0.0;
        clickBeat = 0;
    }

    if (recording.load (std::memory_order_acquire))
        captureInputBlock (inputChannelData, numInputChannels, numSamples, position);

    float blockPeaks[trackCount] {};
    float masterPeak = 0.0f;
    float masterPeakLeft = 0.0f;
    float masterPeakRight = 0.0f;

    // The master rack and the optional external insert both need a block
    // buffer. mixBuffer is allocated once in audioDeviceAboutToStart().
    const auto blockStartSamples = position;
    const auto bufferReady = mixBuffer.getNumChannels() >= 2
                             && mixBuffer.getNumSamples() >= numSamples;
    auto* const mixLeft = bufferReady ? mixBuffer.getWritePointer (0) : nullptr;
    auto* const mixRight = bufferReady ? mixBuffer.getWritePointer (1) : nullptr;

    if (bufferReady)
        mixBuffer.clear (0, numSamples);

    const auto trackBufferReady = trackBlock.getNumChannels() >= 2 * trackCount
                                  && trackBlock.getNumSamples() >= numSamples;

    if (trackBufferReady)
        trackBlock.clear (0, numSamples);

    // Per-sample track sums live in the callback frame, not the sample loop.
    std::array<float, trackCount> trackLeftScratch {};
    std::array<float, trackCount> trackRightScratch {};

    // Pass 1: render clips into one stereo lane per track. Inserts run after
    // the whole block is rendered, so no plugin is called once per sample.
    for (int sample = 0; sample < numSamples; ++sample)
    {
        if (! isPlaying)
            continue;

        std::fill (trackLeftScratch.begin(), trackLeftScratch.end(), 0.0f);
        std::fill (trackRightScratch.begin(), trackRightScratch.end(), 0.0f);

        for (int clipIndex = 0; clipIndex < playbackClipCount; ++clipIndex)
        {
            const auto& clip = renderScratch[static_cast<size_t> (clipIndex)];

            if (clip.asset == nullptr
                || position < clip.startSample
                || position >= clip.endSample)
                continue;

            const auto localSeconds = static_cast<double> (position - clip.startSample) / rate;
            const auto sourceSeconds = clip.sourceOffsetSeconds + localSeconds;
            const auto sourceFrame = sourceSeconds * clip.asset->sampleRate;
            const auto sourceFrames = assetFrameCount (*clip.asset);

            if (sourceFrame < 0.0)
                continue;

            const auto frame0 = static_cast<int64_t> (std::floor (sourceFrame));

            if (frame0 < 0 || frame0 >= sourceFrames)
                continue;

            // Equal-power when a crossfade is set (constant loudness through
            // the overlap); linear otherwise, preserving legacy fades.
            const auto fade = clip.crossfadeSeconds > 0.0
                                  ? engine::clipGainCrossfadeAt (localSeconds,
                                                                  clip.lengthSeconds,
                                                                  juce::jmax (clip.crossfadeSeconds, clip.fadeInSeconds),
                                                                  juce::jmax (clip.crossfadeSeconds, clip.fadeOutSeconds))
                                  : engine::clipGainAt (localSeconds,
                                                         clip.lengthSeconds,
                                                         clip.fadeInSeconds,
                                                         clip.fadeOutSeconds);
            const auto gain = clip.gain * fade;

            // RAM or streamed window: one accessor serves both, so a
            // streamed clip that is not resident yet contributes silence
            // instead of stalling the callback on the disk. A source at the
            // session rate is read frame for frame; any other rate goes
            // through the band-limited kernels instead of a straight line.
            const auto sample0 = readSourceSample (*clip.asset, sourceFrame, rate, 0);
            const auto sample1 = readSourceSample (*clip.asset, sourceFrame, rate, 1);

            const auto lane = static_cast<size_t> (juce::jlimit (0, trackCount - 1, clip.track));
            trackLeftScratch[lane] += sample0 * gain;
            trackRightScratch[lane] += sample1 * gain;
        }

        if (trackBufferReady)
        {
            for (int trackIndex = 0; trackIndex < trackCount; ++trackIndex)
            {
                trackBlock.setSample (trackIndex * 2, sample,
                                      trackLeftScratch[static_cast<size_t> (trackIndex)]);
                trackBlock.setSample (trackIndex * 2 + 1, sample,
                                      trackRightScratch[static_cast<size_t> (trackIndex)]);
            }
        }

        ++position;

        if (loop && position >= loopEnd)
        {
            position = loopStart;
            clickPhase = 0.0;
            clickEnvelope = 0.0;
            clickOscillator = 0.0;
            clickBeat = 0;
        }
    }

    // Pass 2: instruments, inserts and PDC happen once per block, before
    // the native strip. MIDI is scheduled per track from the live clip
    // snapshot through the tempo map (Task 4); the instrument renders
    // into the track lane, then the insert chain processes it.
    if (trackBufferReady)
    {
        inserts.beginBlock();
        const auto insertBlock = engine::blockContext (blockStartSamples, numSamples, tempo, rate,
                                                       isPlaying, loop, loopStart, loopEnd);
        const auto blockEndPpq = engine::ppqAtSamplesWithMap (tempoMap, blockStartSamples + numSamples, rate);
        const auto* midiClipsPtr = midiSlot.live.load (std::memory_order_acquire);

        // One cursor per published clip; a new snapshot rewinds them all.
        if (midiClipsPtr != midiCursorsFor)
        {
            midiCursorsFor = midiClipsPtr;
            midiCursors.assign (midiClipsPtr != nullptr ? midiClipsPtr->size() : 0, {});
        }

        for (int trackIndex = 0; trackIndex < trackCount; ++trackIndex)
        {
            juce::AudioBuffer<float> lane (trackBlock.getArrayOfWritePointers() + trackIndex * 2,
                                           2, numSamples);

            // Input monitoring (Task 4): armed tracks hear their input
            // through the instrument/insert chain and strip, like playback.
            // The same input is captured by captureInputBlock above.
            if (isPlaying && numInputChannels > 0 && inputChannelData != nullptr
                && recordArmed[static_cast<size_t> (trackIndex)].load (std::memory_order_relaxed)
                && trackMonitor[static_cast<size_t> (trackIndex)].load (std::memory_order_relaxed))
            {
                int armedBefore = 0;
                for (int t = 0; t < trackIndex; ++t)
                    if (recordArmed[static_cast<size_t> (t)].load (std::memory_order_relaxed))
                        ++armedBefore;

                const auto* inLeft = inputChannelData[armedBefore % numInputChannels];
                const auto* inRight = numInputChannels > 1
                                          ? inputChannelData[(armedBefore + 1) % numInputChannels]
                                          : inLeft;

                if (inLeft != nullptr || inRight != nullptr)
                {
                    auto* laneLeft = lane.getWritePointer (0);
                    auto* laneRight = lane.getWritePointer (1);
                    for (int sample = 0; sample < numSamples; ++sample)
                    {
                        if (inLeft != nullptr)
                            laneLeft[sample] += inLeft[sample];
                        if (inRight != nullptr)
                            laneRight[sample] += inRight[sample];
                    }
                }
            }

            if (isPlaying && midiClipsPtr != nullptr)
            {
                juce::MidiBuffer midi;
                auto& events = midiEventScratch;

                // Defends against a snapshot that somehow bypassed the
                // setMidiClips ceiling: never index past the cursor array.
                const auto cursorClips = std::min (midiClipsPtr->size(), midiCursors.size());

                for (size_t clipIndex = 0; clipIndex < cursorClips; ++clipIndex)
                {
                    const auto& clip = (*midiClipsPtr)[clipIndex];

                    if (clip.track != trackIndex)
                        continue;

                    events.clear();
                    engine::scheduleMidiClipCursor (clip.notes, clip.ccs, clip.bends,
                                                    clip.startBar, clip.lengthBars,
                                                    blockStartPpq, blockEndPpq, tempoMap,
                                                    rate, numSamples,
                                                    midiCursors[clipIndex], events);

                    // Ported from ChordStudio: global + track + instrument pitch.
                    // Snapshot at block start so a change mid-note lands next block.
                    {
                        auto gp = globalPitchStack();
                        auto tp = trackPitchStack (trackIndex);
                        auto ip = instrumentPitchStack (trackIndex);
                        engine::PitchStack combined;
                        combined.transpose = gp.transpose + tp.transpose + ip.transpose;
                        combined.transpose = std::clamp (combined.transpose,
                                                           engine::PitchStack::kTransposeMin * 3,
                                                           engine::PitchStack::kTransposeMax * 3);
                        combined.pitchCents = gp.pitchCents + tp.pitchCents + ip.pitchCents;
                        combined.tuningHz = 440.0f;
                        // Fold Hz offsets into cents (Hz is log-additive).
                        combined.pitchCents += engine::PitchStack::hzToCents (gp.tuningHz)
                                            + engine::PitchStack::hzToCents (tp.tuningHz)
                                            + engine::PitchStack::hzToCents (ip.tuningHz);
                        // Clamp combined pitch to +-1200c for audio safety; MIDI transpose part is exact.
                        combined.pitchCents = std::clamp (combined.pitchCents, -1200.0f, 1200.0f);
                        engine::applyPitch (events, combined);
                    }

                    for (const auto& event : events)
                    {
                        switch (event.kind)
                        {
                            case engine::MidiEvent::Kind::noteOn:
                                midi.addEvent (juce::MidiMessage::noteOn (event.channel, event.pitch,
                                                                            static_cast<juce::uint8> (event.velocity)),
                                               event.sampleOffset);
                                break;
                            case engine::MidiEvent::Kind::noteOff:
                                midi.addEvent (juce::MidiMessage::noteOff (event.channel, event.pitch),
                                               event.sampleOffset);
                                break;
                            case engine::MidiEvent::Kind::cc:
                                midi.addEvent (juce::MidiMessage::controllerEvent (event.channel, event.controller,
                                                                                     event.value),
                                               event.sampleOffset);
                                break;
                            case engine::MidiEvent::Kind::bend:
                                midi.addEvent (juce::MidiMessage::pitchWheel (event.channel, event.value),
                                               event.sampleOffset);
                                break;
                        }
                    }
                }

                if (! midi.isEmpty() && instrumentBlock.getNumChannels() >= 2
                    && instrumentBlock.getNumSamples() >= numSamples)
                {
                    // Instruments overwrite their buffer in processBlock, so
                    // render into a scratch and add it to the lane: an audio
                    // clip sharing this track keeps the lane it already wrote.
                    instrumentBlock.clear (0, numSamples);
                    juce::AudioBuffer<float> scratch (instrumentBlock.getArrayOfWritePointers(), 2, numSamples);
                    inserts.processTrackInstrument (trackIndex, scratch, insertBlock, midi);
                    lane.addFrom (0, 0, instrumentBlock, 0, 0, numSamples);
                    lane.addFrom (1, 0, instrumentBlock, 1, 0, numSamples);
                }
            }

            inserts.processTrack (trackIndex, lane, insertBlock);
        }

        inserts.endBlock();
    }

    // Automation is evaluated once per block, like Ardour's per-cycle faders,
    // and the existing per-sample smoothing turns each step into a ramp.
    float automatedGain[trackCount];
    float automatedPan[trackCount];
    const auto blockBeat = engine::ppqAtSamplesWithMap (tempoMap, blockStartSamples, rate);

    for (int trackIndex = 0; trackIndex < trackCount; ++trackIndex)
    {
        automatedGain[trackIndex] = tracks[static_cast<size_t> (trackIndex)].gain.load();
        automatedPan[trackIndex] = tracks[static_cast<size_t> (trackIndex)].pan.load();

        // A track with AUTO off keeps its lane but plays the static fader, the
        // same branch an empty lane already takes.
        const auto reading = automationRead (trackIndex);

        if (const auto* lane = volumeAutomation[static_cast<size_t> (trackIndex)].live.load (std::memory_order_acquire);
            reading && lane != nullptr && ! lane->empty())
            automatedGain[trackIndex] = lane->valueAt (blockBeat);

        if (const auto* lane = panAutomation[static_cast<size_t> (trackIndex)].live.load (std::memory_order_acquire);
            reading && lane != nullptr && ! lane->empty())
            automatedPan[trackIndex] = lane->valueAt (blockBeat);
    }

    auxABuffer.clear (0, juce::jmin (numSamples, auxABuffer.getNumSamples()));
    auxBBuffer.clear (0, juce::jmin (numSamples, auxBBuffer.getNumSamples()));
    auto* auxALeft = auxABuffer.getNumSamples() >= numSamples ? auxABuffer.getWritePointer (0) : nullptr;
    auto* auxARight = auxABuffer.getNumSamples() >= numSamples ? auxABuffer.getWritePointer (1) : nullptr;
    auto* auxBLeft = auxBBuffer.getNumSamples() >= numSamples ? auxBBuffer.getWritePointer (0) : nullptr;
    auto* auxBRight = auxBBuffer.getNumSamples() >= numSamples ? auxBBuffer.getWritePointer (1) : nullptr;

    // Pass 3: native strip, fader/pan, click and master gain.
    // Loop-invariant: the probe config is read once per block, not per sample.
    const auto& probe = toneProbe();

    // Per-block snapshots of the fader-adjacent message-thread params the
    // sample loop used to read through atomics five times per track per
    // sample (~80 atomic loads per sample across the bus). These params move
    // at most once per UI event, so sampling them once per block is standard
    // practice; the per-sample smoothing still glides towards the snapshot.
    float blockSendA[trackCount] {};
    float blockSendB[trackCount] {};
    float blockWidth[trackCount] {};
    bool blockAudible[trackCount] {};

    for (int trackIndex = 0; trackIndex < trackCount; ++trackIndex)
    {
        const auto& track = tracks[static_cast<size_t> (trackIndex)];
        blockSendA[trackIndex] = track.sendA.load();
        blockSendB[trackIndex] = track.sendB.load();
        blockWidth[trackIndex] = track.width.load();
        blockAudible[trackIndex] = ! track.muted.load() && (! anySolo || track.soloed.load());
    }

    for (int sample = 0; sample < numSamples; ++sample)
    {
        float left = 0.0f;
        float right = 0.0f;
        float click = 0.0f;
        float probeTone = 0.0f;

        if (probe.on)
        {
            probeTone = probe.gain * static_cast<float> (std::sin (probePhase));
            probePhase += juce::MathConstants<double>::twoPi * probe.hz / juce::jmax (1.0, rate);

            if (probePhase >= juce::MathConstants<double>::twoPi)
                probePhase -= juce::MathConstants<double>::twoPi;
        }

        if (isPlaying)
        {
            for (int trackIndex = 0; trackIndex < trackCount; ++trackIndex)
            {
                const auto& track = tracks[static_cast<size_t> (trackIndex)];

                smoothedGain[trackIndex] += (automatedGain[trackIndex] - smoothedGain[trackIndex]) * smoothingCoefficient;
                smoothedPan[trackIndex] += (automatedPan[trackIndex] - smoothedPan[trackIndex]) * smoothingCoefficient;
                smoothedSendA[trackIndex] += (blockSendA[trackIndex] - smoothedSendA[trackIndex]) * smoothingCoefficient;
                smoothedSendB[trackIndex] += (blockSendB[trackIndex] - smoothedSendB[trackIndex]) * smoothingCoefficient;
                smoothedWidth[trackIndex] += (blockWidth[trackIndex] - smoothedWidth[trackIndex]) * smoothingCoefficient;

                const auto audible = blockAudible[trackIndex];
                const auto gain = audible ? smoothedGain[trackIndex] : 0.0f;

                // Equal-power pan needs cos/sin of the smoothed angle. While
                // the fader sits still — the normal case — the angle is
                // constant, and a trig pair per track per sample was a large
                // slice of the strip's audio budget on a slow CPU. Recompute
                // the pair only when the smoothed value has actually moved.
                auto& panL = cachedPanLeft[trackIndex];
                auto& panR = cachedPanRight[trackIndex];

                if (std::abs (smoothedPan[trackIndex] - cachedPanValue[trackIndex]) > 1.0e-5f)
                {
                    const auto angle = (smoothedPan[trackIndex] + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                    panL = std::cos (angle);
                    panR = std::sin (angle);
                    cachedPanValue[trackIndex] = smoothedPan[trackIndex];
                }

                const auto panLeft = panL;
                const auto panRight = panR;

                const auto sourceLeft = trackBufferReady
                                            ? trackBlock.getSample (trackIndex * 2, sample) : 0.0f;
                const auto sourceRight = trackBufferReady
                                             ? trackBlock.getSample (trackIndex * 2 + 1, sample) : 0.0f;

                auto trackLeft = sourceLeft * gain * panLeft;
                auto trackRight = sourceRight * gain * panRight;

                if (probe.mute)
                {
                    trackLeft = 0.0f;
                    trackRight = 0.0f;
                }

                // Ahead of the strip, so the strip itself is measured.
                if (probe.on && probe.track == trackIndex && ! probe.afterStrip)
                {
                    trackLeft += probeTone;
                    trackRight += probeTone;
                }

                // The channel strip is baked into every track. It runs after
                // the inserts/PDC and before the bus sum.
                if (! (probe.noStrip && probe.track == trackIndex))
                    mixRack[static_cast<size_t> (trackIndex)].processSample (trackLeft, trackRight);

                // Stereo width: mid/side scaling (0.5 = unity). Applied on the
                // post-strip track bus before the sum.
                {
                    const auto mid = 0.5f * (trackLeft + trackRight);
                    const auto side = 0.5f * (trackLeft - trackRight) * (smoothedWidth[trackIndex] * 2.0f);
                    trackLeft = mid + side;
                    trackRight = mid - side;
                }

                // Sends are post-fader (post-strip) taps. They do not mute with
                // the dry bus: audible already zeroes the dry path, so a muted
                // track sends silence — the user hears the dry bus, not a ghost
                // reverb. That is the intended test contract.
                if (audible && ! probe.noSend)
                {
                    if (auxALeft != nullptr && auxARight != nullptr)
                    {
                        const auto sendA = smoothedSendA[trackIndex];
                        auxALeft[sample] += trackLeft * sendA;
                        auxARight[sample] += trackRight * sendA;
                    }

                    if (auxBLeft != nullptr && auxBRight != nullptr)
                    {
                        const auto sendB = smoothedSendB[trackIndex];
                        auxBLeft[sample] += trackLeft * sendB;
                        auxBRight[sample] += trackRight * sendB;
                    }
                }

                // After the strip and its send taps, so a comparison against
                // the pre-strip injection isolates the strip and width only.
                if (probe.on && probe.track == trackIndex && probe.afterStrip)
                {
                    trackLeft += probeTone;
                    trackRight += probeTone;
                }

                left += trackLeft;
                right += trackRight;

                blockPeaks[trackIndex] = juce::jmax (blockPeaks[trackIndex],
                                                     juce::jmax (std::abs (trackLeft), std::abs (trackRight)));
            }

            if (clickPhase >= beatSamples)
            {
                clickPhase -= beatSamples;
                clickEnvelope = 0.85;
                clickOscillator = 0.0;
                ++clickBeat;
            }

            clickPhase += 1.0;

            if (clickOn && ! probe.mute && clickEnvelope > 0.0001)
            {
                const auto frequency = clickBeat % 4 == 0 ? 1600.0 : 1000.0;
                clickOscillator += twoPi * frequency / rate;
                click = static_cast<float> (std::sin (clickOscillator) * clickEnvelope * 0.3);
                clickEnvelope *= 0.9994;
            }
        }

        smoothedMaster += (masterGainValue.load() - smoothedMaster) * smoothingCoefficient;
        left = (left + click) * smoothedMaster;
        right = (right + click) * smoothedMaster;

        if (probe.on && probe.track < 0)
        {
            left += probeTone;
            right += probeTone;
        }

        if (mixLeft != nullptr)
        {
            mixLeft[sample] = left;
            mixRight[sample] = right;
        }
        else
        {
            // A block larger than the prepared ceiling is a device change we
            // have not seen yet; keep it bounded rather than writing garbage.
            if (outputChannelData[0] != nullptr)
                outputChannelData[0][sample] = juce::jlimit (-1.0f, 1.0f, left);

            if (numOutputChannels > 1 && outputChannelData[1] != nullptr)
                outputChannelData[1][sample] = juce::jlimit (-1.0f, 1.0f, right);
        }
    }

    {
        const auto auxReady = auxALeft != nullptr && auxBLeft != nullptr
                              && auxABuffer.getNumSamples() >= numSamples
                              && auxBBuffer.getNumSamples() >= numSamples;

        // Unconditional boundary so the stage indices mean the same thing on
        // every block, auxReady or not.

        if (auxReady)
        {
            juce::AudioBuffer<float> sendABuf (auxABuffer.getArrayOfWritePointers(), 2, numSamples);
            juce::AudioBuffer<float> sendBBuf (auxBBuffer.getArrayOfWritePointers(), 2, numSamples);
            auxReverb.process (sendABuf, numSamples);
            auxDelay.process (sendBBuf, numSamples);

            // Add the wet returns. The mix path and the oversized-block
            // fallback both get the return so a send is audible either way.
            if (mixLeft != nullptr)
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    mixLeft[i] += sendABuf.getSample (0, i) + sendBBuf.getSample (0, i);
                    mixRight[i] += sendABuf.getSample (1, i) + sendBBuf.getSample (1, i);
                }
            }
            else
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    if (outputChannelData[0] != nullptr)
                        outputChannelData[0][i] += juce::jlimit (-1.0f, 1.0f,
                            sendABuf.getSample (0, i) + sendBBuf.getSample (0, i));
                    if (numOutputChannels > 1 && outputChannelData[1] != nullptr)
                        outputChannelData[1][i] += juce::jlimit (-1.0f, 1.0f,
                            sendABuf.getSample (1, i) + sendBBuf.getSample (1, i));
                }
            }
        }
    }

    if (mixLeft != nullptr)
    {
        juce::AudioBuffer<float> mixed (mixBuffer.getArrayOfWritePointers(), 2, numSamples);

        dumpStage ("pre", mixed, numSamples, isPlaying);

        masterRack.process (mixed, numSamples);

        inserts.processMaster (mixed, engine::blockContext (blockStartSamples, numSamples, tempo, rate,
                                                              isPlaying, loop, loopStart, loopEnd));

        // Monitor section: mono fold and dim sit after the insert, so they
        // change what the engineer hears without touching the saved mix.
        if (masterMono.load())
        {
            auto* left = mixed.getWritePointer (0);
            auto* right = mixed.getWritePointer (1);

            for (int i = 0; i < numSamples; ++i)
            {
                const auto mid = 0.5f * (left[i] + right[i]);
                left[i] = mid;
                right[i] = mid;
            }
        }

        if (masterDim.load())
            mixed.applyGain (0, numSamples, juce::Decibels::decibelsToGain (-20.0f));

        // Nothing outside [-1, 1] may reach the device: a driver given a larger
        // float will either wrap or hard-clip it, and both are the harshest
        // distortion the chain can produce.
        for (int channel = 0; channel < 2; ++channel)
        {
            auto* samples = mixed.getWritePointer (channel);
            juce::FloatVectorOperations::clip (samples, samples, -1.0f, 1.0f, numSamples);
        }

        masterPeakLeft = mixed.getMagnitude (0, 0, numSamples);
        masterPeakRight = mixed.getMagnitude (1, 0, numSamples);
        masterPeak = juce::jmax (masterPeakLeft, masterPeakRight);

        for (int channel = 0; channel < juce::jmin (2, numOutputChannels); ++channel)
            if (outputChannelData[channel] != nullptr)
                juce::FloatVectorOperations::copy (outputChannelData[channel],
                                                   mixed.getReadPointer (channel), numSamples);

        dumpStage ("post", mixed, numSamples, isPlaying);
    }
    else
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            masterPeakLeft = juce::jmax (masterPeakLeft,
                                         std::abs (outputChannelData[0] != nullptr ? outputChannelData[0][sample] : 0.0f));
            masterPeakRight = juce::jmax (masterPeakRight,
                                          std::abs (numOutputChannels > 1 && outputChannelData[1] != nullptr
                                                        ? outputChannelData[1][sample] : 0.0f));
        }

        masterPeak = juce::jmax (masterPeakLeft, masterPeakRight);
    }

    positionSamples.store (position);

    for (int trackIndex = 0; trackIndex < trackCount; ++trackIndex)
        tracks[static_cast<size_t> (trackIndex)].peak.store (blockPeaks[trackIndex]);

    masterPeakValue.store (masterPeak);
    masterPeakLeftValue.store (masterPeakLeft);
    masterPeakRightValue.store (masterPeakRight);

}

MixRackChannel& AudioEngine::mixChannel (int track)
{
    return mixRack[static_cast<size_t> (juce::jlimit (0, trackCount - 1, track))];
}

const MixRackChannel& AudioEngine::mixChannel (int track) const
{
    return mixRack[static_cast<size_t> (juce::jlimit (0, trackCount - 1, track))];
}

juce::String AudioEngine::mixRackStateJson() const
{
    auto* root = new juce::DynamicObject();
    juce::Array<juce::var> trackArray;
    const auto active = juce::jlimit (0, trackCount, activeTrackCount.load());

    for (int i = 0; i < active; ++i)
        trackArray.add (mixRack[static_cast<size_t> (i)].toVar());

    root->setProperty ("tracks", trackArray);
    root->setProperty ("master", masterRack.toVar());
    return juce::JSON::toString (juce::var (root));
}

bool AudioEngine::restoreMixRackStateJson (const juce::String& state)
{
    const auto parsed = juce::JSON::parse (state);

    if (! parsed.isObject())
        return false;

    const auto tracksVar = parsed.getProperty ("tracks", {});
    const auto active = juce::jlimit (0, trackCount, activeTrackCount.load());

    if (tracksVar.isArray())
    {
        const auto count = juce::jmin (active, tracksVar.size());

        for (int i = 0; i < count; ++i)
            mixRack[static_cast<size_t> (i)].fromVar (tracksVar[static_cast<int> (i)]);
    }

    const auto masterVar = parsed.getProperty ("master", {});

    if (masterVar.isObject())
        masterRack.fromVar (masterVar);

    // A restored project owns its mix, but the vocal role stays a property of
    // the track itself: vocal tracks keep the Open Mic stage unless the saved
    // session recorded an explicit override. Do not overwrite saved strip or
    // master parameters with baked defaults here.
    applyVocalRoles (true);

    return true;
}

void AudioEngine::applyVocalRoles (bool restoringSavedState)
{
    const auto& tracks = sessionTracks();

    for (int track = 0; track < trackCount; ++track)
    {
        mixRack[static_cast<size_t> (track)]
            .setVocal (isVocalTrackName (tracks[static_cast<size_t> (track)].name));
        if (! restoringSavedState)
        {
            // Fresh baked default only. Channel Strip is already on track
            // channels by default and is never present on Aux or Master.
            mixRack[static_cast<size_t> (track)].setParam ("stripOn", 1.0f);
        }
    }

    if (! restoringSavedState)
    {
        // Overlay Master is baked into the master channel (MasterRack).
        // Mixology is its default master shape.
        masterRack.setParam ("eqOn", 1.0f);
        masterRack.setParam ("lowFreq", 100.0f);
        masterRack.setParam ("lowGain", -1.5f);
        masterRack.setParam ("midFreq", 1000.0f);
        masterRack.setParam ("midGain", -1.0f);
        masterRack.setParam ("maxOn", 1.0f);
        masterRack.setParam ("ceiling", -1.0f);
    }
}

float AudioEngine::sampleAt (const AudioAsset& asset, int64_t frame, int channel) const
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

float AudioEngine::readSourceSample (const AudioAsset& asset, double sourceFrame, double outputRate,
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

float AudioEngine::readBandLimited (const AudioAsset& asset, double sourceFrame, int channel) const
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
