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

    RenderContext ctx;
    ctx.inputChannelData = inputChannelData;
    ctx.numInputChannels = numInputChannels;
    ctx.outputChannelData = outputChannelData;
    ctx.numOutputChannels = numOutputChannels;
    ctx.numSamples = numSamples;
    ctx.rate = rate;
    ctx.isPlaying = isPlaying;
    ctx.clickOn = clickOn;

    // Pass 1: render clips into one stereo lane per track.
    renderClipsPass (ctx);
    // Pass 2: instruments, inserts, PDC and the per-block automation snapshot.
    renderInstrumentInsertPass (ctx);
    // Pass 3: native strip, fader/pan, click and master gain.
    renderStripPass (ctx);
    // Pass 4: aux returns, master rack, monitor, soft limit and metering.
    renderMasterPass (ctx);

    positionSamples.store (ctx.position);

    for (int trackIndex = 0; trackIndex < trackCount; ++trackIndex)
        tracks[static_cast<size_t> (trackIndex)].peak.store (ctx.blockPeaks[trackIndex]);

    masterPeakValue.store (ctx.masterPeak);
    masterPeakLeftValue.store (ctx.masterPeakLeft);
    masterPeakRightValue.store (ctx.masterPeakRight);
}

void AudioEngine::renderClipsPass (RenderContext& ctx)
{
    const auto numSamples = ctx.numSamples;
    const auto rate = ctx.rate;
    const auto isPlaying = ctx.isPlaying;
    const auto* const* inputChannelData = ctx.inputChannelData;
    const auto numInputChannels = ctx.numInputChannels;
    auto& trackLeftScratch = ctx.trackLeftScratch;
    auto& trackRightScratch = ctx.trackRightScratch;

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

        if (clip.asset == nullptr || assetLibrary.frameCount (*clip.asset) <= 0)
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

    auto& position = ctx.position;
    position = positionSamples.load();

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

    ctx.tempo = tempo;
    ctx.tempoMap = &tempoMap;
    ctx.beatSamples = beatSamples;
    ctx.loop = loop;
    ctx.loopStart = loopStart;
    ctx.loopEnd = loopEnd;
    ctx.anySolo = anySolo;
    ctx.blockStartSamples = blockStartSamples;
    ctx.blockStartPpq = blockStartPpq;
    ctx.mixLeft = mixLeft;
    ctx.mixRight = mixRight;
    ctx.trackBufferReady = trackBufferReady;

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
            const auto sourceFrames = assetLibrary.frameCount (*clip.asset);

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
            const auto sample0 = assetLibrary.readSourceSample (*clip.asset, sourceFrame, rate, 0);
            const auto sample1 = assetLibrary.readSourceSample (*clip.asset, sourceFrame, rate, 1);

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

}

void AudioEngine::renderInstrumentInsertPass (RenderContext& ctx)
{
    const auto numSamples = ctx.numSamples;
    const auto rate = ctx.rate;
    const auto isPlaying = ctx.isPlaying;
    const bool loop = ctx.loop;
    const auto loopStart = ctx.loopStart;
    const auto loopEnd = ctx.loopEnd;
    const auto blockStartSamples = ctx.blockStartSamples;
    const auto blockStartPpq = ctx.blockStartPpq;
    const auto trackBufferReady = ctx.trackBufferReady;
    const auto tempo = ctx.tempo;
    const auto& tempoMap = ctx.tempoMap != nullptr ? *ctx.tempoMap : fallbackTempo;
    const auto* const* inputChannelData = ctx.inputChannelData;
    const auto numInputChannels = ctx.numInputChannels;
    auto& automatedGain = ctx.automatedGain;
    auto& automatedPan = ctx.automatedPan;

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
    auto*& auxALeft = ctx.auxALeft;
    auto*& auxARight = ctx.auxARight;
    auto*& auxBLeft = ctx.auxBLeft;
    auto*& auxBRight = ctx.auxBRight;

    auxALeft = auxABuffer.getNumSamples() >= numSamples ? auxABuffer.getWritePointer (0) : nullptr;
    auxARight = auxABuffer.getNumSamples() >= numSamples ? auxABuffer.getWritePointer (1) : nullptr;
    auxBLeft = auxBBuffer.getNumSamples() >= numSamples ? auxBBuffer.getWritePointer (0) : nullptr;
    auxBRight = auxBBuffer.getNumSamples() >= numSamples ? auxBBuffer.getWritePointer (1) : nullptr;
}

void AudioEngine::renderStripPass (RenderContext& ctx)
{
    const auto numSamples = ctx.numSamples;
    const auto rate = ctx.rate;
    const auto isPlaying = ctx.isPlaying;
    const auto clickOn = ctx.clickOn;
    const auto beatSamples = ctx.beatSamples;
    const auto anySolo = ctx.anySolo;
    const auto trackBufferReady = ctx.trackBufferReady;
    const auto numOutputChannels = ctx.numOutputChannels;
    auto* const* const outputChannelData = ctx.outputChannelData;
    auto* const mixLeft = ctx.mixLeft;
    auto* const mixRight = ctx.mixRight;
    auto& automatedGain = ctx.automatedGain;
    auto& automatedPan = ctx.automatedPan;
    auto& blockPeaks = ctx.blockPeaks;
    auto* auxALeft = ctx.auxALeft;
    auto* auxARight = ctx.auxARight;
    auto* auxBLeft = ctx.auxBLeft;
    auto* auxBRight = ctx.auxBRight;

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
}

void AudioEngine::renderMasterPass (RenderContext& ctx)
{
    const auto numSamples = ctx.numSamples;
    const auto rate = ctx.rate;
    const auto isPlaying = ctx.isPlaying;
    const bool loop = ctx.loop;
    const auto loopStart = ctx.loopStart;
    const auto loopEnd = ctx.loopEnd;
    const auto blockStartSamples = ctx.blockStartSamples;
    const auto tempo = ctx.tempo;
    const auto numOutputChannels = ctx.numOutputChannels;
    auto* const* const outputChannelData = ctx.outputChannelData;
    auto* const mixLeft = ctx.mixLeft;
    auto* const mixRight = ctx.mixRight;
    const auto* auxALeft = ctx.auxALeft;
    const auto* auxBLeft = ctx.auxBLeft;
    auto& masterPeak = ctx.masterPeak;
    auto& masterPeakLeft = ctx.masterPeakLeft;
    auto& masterPeakRight = ctx.masterPeakRight;

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
}


} // namespace nc
