#include "AudioEngine.h"

#include "PluginIndexCache.h"
#include "Session.h"

#include <nc/engine/VoiceToMidi.hpp>

#include <algorithm>
#include <thread>
#include <utility>

namespace nc
{
AudioEngine::AudioEngine()
    : catalog ({}, {}, nc::pluginindex::defaultIndexFile())
{
    engineAlive = std::make_shared<std::atomic<bool>> (true);
    formats.registerBasicFormats();

    // Pan cache: element 0's brace initializer only seeds lane 0; every other
    // lane would start at 0 and skip the first trig, leaving its gains at zero.
    std::fill_n (cachedPanValue, trackCount, -2.0f);
    std::fill_n (cachedPanLeft, trackCount, 0.7071f);
    std::fill_n (cachedPanRight, trackCount, 0.7071f);

    // Automation reads by default. The AUTO switch is an opt-out, so a fresh
    // engine must not start with every lane disconnected.
    for (auto& flag : automationReadEnabled)
        flag.store (true, std::memory_order_relaxed);

    // Input monitoring is off until a track is armed for recording. A monitored
    // track sums its input into the mix, so with every track monitoring by
    // default a room with a microphone and speakers fed back into a drone the
    // moment the transport rolled.

    {
        const auto& specs = sessionTracks();
        for (int track = 0; track < demoTrackCount; ++track)
        {
            const auto sendA = specs[static_cast<size_t> (track)].sendA;
            const auto sendB = specs[static_cast<size_t> (track)].sendB;
            setTrackSendA (track, sendA);
            setTrackSendB (track, sendB);
            setTrackWidth (track, 0.5f);
        }
    }

    tempoSlot.owned = std::make_unique<nc::engine::TempoMap> (nc::engine::constantTempoMap (120.0));
    tempoSlot.live.store (tempoSlot.owned.get(), std::memory_order_release);
    midiSlot.owned = std::make_unique<std::vector<MidiTimelineClip>> ();
    midiSlot.live.store (midiSlot.owned.get(), std::memory_order_release);

    // Restore the stored interface (device/rate/buffer/midi). Falls back to
    // OS defaults when the stored device is gone — playback never dies.
    auto error = deviceManager.initialiseWithDefaultDevices (2, 2);

    if (error.isNotEmpty())
        error = deviceManager.initialiseWithDefaultDevices (0, 2);

    deviceInfo = error.isNotEmpty() ? "no audio device" : deviceManager.getCurrentAudioDeviceType();
    restoreAudioSettings();
    deviceManager.addAudioCallback (this);

    // The shipped project is a demo record. These assets make Play audible and
    // exercise the same path as imported files; File > Import Audio replaces
    // them with real recordings.
    createDemoAssets();

    // Demo INSTRUMENT tracks get the built-in synth so MIDI clips make
    // sound with no VST3 installed; projects can replace them.
    for (int track = 0; track < demoTrackCount; ++track)
    {
        if (sessionTracks()[static_cast<size_t> (track)].kind == "INSTRUMENT")
            setBuiltinInstrument (track, track % 3);
    }

    // Vocal tracks carry the Open Mic stage. The role comes from the session
    // model, not from a plugin name, so a renamed vocal track still gets it.
    applyVocalRoles();
}

nc::engine::ClipState AudioEngine::voiceToMidiClip (const juce::File& file, int track, double bpm,
                                                   juce::String* error)
{
    voiceError.clear();
    if (error != nullptr)
        error->clear();

    // A failed transcription still returns a MIDI clip: callers can place it
    // (empty) and show `error`, rather than special-casing a default state.
    auto failed = [track] {
        nc::engine::ClipState clip;
        clip.isMidi = true;
        clip.track = track;
        return clip;
    };
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        voiceError = "Could not open " + file.getFileName();
        if (error != nullptr)
            *error = voiceError;
        return failed();
    }
    constexpr std::size_t kMaxVoiceFrames = 4800000; // ~100 s
    const auto requestedFrames = (std::size_t) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                                            static_cast<juce::int64> (kMaxVoiceFrames));
    if (reader->lengthInSamples > static_cast<juce::int64> (kMaxVoiceFrames))
        voiceError = "Input truncated to ~100 s for analysis";
    juce::AudioBuffer<float> buf ((int) juce::jmax<juce::int64> (1, reader->numChannels),
                                  (int) requestedFrames);
    if (! reader->read (&buf, 0, (int) requestedFrames, 0, true, true))
    {
        voiceError = "Could not read the full audio file";
        if (error != nullptr)
            *error = voiceError;
        return failed();
    }
    // Mono mix.
    juce::AudioBuffer<float> mono (1, buf.getNumSamples());
    mono.clear();
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        mono.addFrom (0, 0, buf, ch, 0, buf.getNumSamples());
    if (buf.getNumChannels() > 1)
        mono.applyGain (1.0f / (float) buf.getNumChannels());

    nc::engine::V2MConfig cfg;
    cfg.sampleRate = (int) reader->sampleRate;
    cfg.bpm = bpm > 1.0 ? bpm : 120.0;
    auto notes = nc::engine::voiceToMidi (mono.getReadPointer (0), (std::size_t) mono.getNumSamples(), cfg);
    if (notes.empty())
    {
        voiceError = "No voiced notes found (too quiet, or not melodic)";
        if (error != nullptr)
            *error = voiceError;
        return failed();
    }

    nc::engine::ClipState clip;
    clip.isMidi = true;
    clip.name = "VOICE TO MIDI";
    clip.track = track;
    clip.startBar = 1.0;
    clip.lengthBars = 1.0 + (notes.back().startBeat + notes.back().lengthBeats) / nc::engine::beatsPerBarMidi;
    for (auto& n : notes)
    {
        clip.notes.push_back ({n.startBeat, n.lengthBeats, n.midiNote, n.velocity, 1});

        for (const auto& bend : n.bends)
            clip.bends.push_back ({ n.startBeat + bend.beatOffset, 1, bend.value });
    }

    nc::engine::sanitizeNotes (clip.notes, nc::engine::clipLengthBeats (clip.lengthBars));
    nc::engine::sanitizeMidiBends (clip.bends, nc::engine::clipLengthBeats (clip.lengthBars));
    return clip;
}

void AudioEngine::voiceToMidiClipAsync (const juce::File& file, int track, double bpm,
                                        std::function<void (nc::engine::ClipState, juce::String)> completion)
{
    // YIN can take seconds on long takes; keep it off the message thread.
    std::thread ([this, file, track, bpm, completion = std::move (completion)] () mutable
    {
        juce::String error;
        auto clip = voiceToMidiClip (file, track, bpm, &error);
        auto deliver = std::move (completion);
        juce::MessageManager::getInstance()->callAsync (
            [this, deliver = std::move (deliver), clip = std::move (clip), error] () mutable
            {
                voiceError = error;
                if (deliver != nullptr)
                    deliver (std::move (clip), error);
            });
    }).detach();
}


AudioEngine::~AudioEngine()
{
    saveAudioSettings();
    engineAlive->store (false, std::memory_order_release);
    deviceManager.removeAudioCallback (this);
    inserts.collectGarbage();
}


void AudioEngine::setActiveTrackCount (int count)
{
    const auto clamped = nc::limits::clampActiveTracks (count);
    const auto prepared = preparedRackCount.load();

    // Growing the session must not leave a visible track with an unprepared
    // strip. This is the message thread, so allocating here is safe.
    if (deviceOpen.load() && clamped > prepared)
    {
        for (int i = prepared; i < clamped; ++i)
            mixRack[static_cast<size_t> (i)].prepare (sampleRate.load(), juce::jmax (1, bufferSize.load()));

        preparedRackCount.store (clamped);
    }

    inserts.setActiveTrackCount (clamped);
    activeTrackCount.store (clamped);
}

void AudioEngine::setTrackGain (int track, float linearGain)
{
    if (juce::isPositiveAndBelow (track, trackCount))
        tracks[static_cast<size_t> (track)].gain.store (juce::jlimit (0.0f, 2.0f, linearGain));
}

float AudioEngine::trackGain (int track) const
{
    return juce::isPositiveAndBelow (track, trackCount) ? tracks[static_cast<size_t> (track)].gain.load() : 0.0f;
}

void AudioEngine::setTrackPan (int track, float pan)
{
    if (juce::isPositiveAndBelow (track, trackCount))
        tracks[static_cast<size_t> (track)].pan.store (juce::jlimit (-1.0f, 1.0f, pan));
}

float AudioEngine::trackPan (int track) const
{
    return juce::isPositiveAndBelow (track, trackCount) ? tracks[static_cast<size_t> (track)].pan.load() : 0.0f;
}

void AudioEngine::setTrackSendA (int track, float send)
{
    if (juce::isPositiveAndBelow (track, trackCount))
        tracks[static_cast<size_t> (track)].sendA.store (juce::jlimit (0.0f, 1.0f, send));
}

float AudioEngine::trackSendA (int track) const
{
    return juce::isPositiveAndBelow (track, trackCount) ? tracks[static_cast<size_t> (track)].sendA.load() : 0.0f;
}

void AudioEngine::setTrackSendB (int track, float send)
{
    if (juce::isPositiveAndBelow (track, trackCount))
        tracks[static_cast<size_t> (track)].sendB.store (juce::jlimit (0.0f, 1.0f, send));
}

float AudioEngine::trackSendB (int track) const
{
    return juce::isPositiveAndBelow (track, trackCount) ? tracks[static_cast<size_t> (track)].sendB.load() : 0.0f;
}

void AudioEngine::setTrackWidth (int track, float width)
{
    if (juce::isPositiveAndBelow (track, trackCount))
        tracks[static_cast<size_t> (track)].width.store (juce::jlimit (0.0f, 1.0f, width));
}

float AudioEngine::trackWidth (int track) const
{
    return juce::isPositiveAndBelow (track, trackCount) ? tracks[static_cast<size_t> (track)].width.load() : 0.5f;
}

void AudioEngine::setGlobalPitch (const nc::engine::PitchStack& stack)
{
    nc::engine::PitchStack p = stack;
    p.clamp();
    globalTranspose.store (p.transpose, std::memory_order_relaxed);
    globalPitchCents.store (p.pitchCents, std::memory_order_relaxed);
    globalTuningHz.store (p.tuningHz, std::memory_order_relaxed);
}

nc::engine::PitchStack AudioEngine::globalPitchStack() const noexcept
{
    nc::engine::PitchStack p;
    p.transpose = globalTranspose.load (std::memory_order_relaxed);
    p.pitchCents = globalPitchCents.load (std::memory_order_relaxed);
    p.tuningHz = globalTuningHz.load (std::memory_order_relaxed);
    return p;
}

void AudioEngine::setTrackPitch (int track, const nc::engine::PitchStack& stack)
{
    if (! juce::isPositiveAndBelow (track, trackCount))
        return;
    nc::engine::PitchStack p = stack;
    p.clamp();
    auto& t = tracks[static_cast<size_t> (track)];
    t.trackTranspose.store (p.transpose, std::memory_order_relaxed);
    t.trackPitchCents.store (p.pitchCents, std::memory_order_relaxed);
    t.trackTuningHz.store (p.tuningHz, std::memory_order_relaxed);
}

nc::engine::PitchStack AudioEngine::trackPitchStack (int track) const noexcept
{
    nc::engine::PitchStack p;
    if (! juce::isPositiveAndBelow (track, trackCount))
        return p;
    const auto& t = tracks[static_cast<size_t> (track)];
    p.transpose = t.trackTranspose.load (std::memory_order_relaxed);
    p.pitchCents = t.trackPitchCents.load (std::memory_order_relaxed);
    p.tuningHz = t.trackTuningHz.load (std::memory_order_relaxed);
    return p;
}

void AudioEngine::setInstrumentPitch (int track, const nc::engine::PitchStack& stack)
{
    if (! juce::isPositiveAndBelow (track, trackCount))
        return;
    nc::engine::PitchStack p = stack;
    p.clamp();
    auto& t = tracks[static_cast<size_t> (track)];
    t.instTranspose.store (p.transpose, std::memory_order_relaxed);
    t.instPitchCents.store (p.pitchCents, std::memory_order_relaxed);
    t.instTuningHz.store (p.tuningHz, std::memory_order_relaxed);
}

nc::engine::PitchStack AudioEngine::instrumentPitchStack (int track) const noexcept
{
    nc::engine::PitchStack p;
    if (! juce::isPositiveAndBelow (track, trackCount))
        return p;
    const auto& t = tracks[static_cast<size_t> (track)];
    p.transpose = t.instTranspose.load (std::memory_order_relaxed);
    p.pitchCents = t.instPitchCents.load (std::memory_order_relaxed);
    p.tuningHz = t.instTuningHz.load (std::memory_order_relaxed);
    return p;
}

void AudioEngine::setTrackMuted (int track, bool muted)
{
    if (juce::isPositiveAndBelow (track, trackCount))
        tracks[static_cast<size_t> (track)].muted.store (muted);
}

bool AudioEngine::trackMuted (int track) const
{
    return juce::isPositiveAndBelow (track, trackCount) && tracks[static_cast<size_t> (track)].muted.load();
}

void AudioEngine::setTrackSolo (int track, bool soloed)
{
    if (juce::isPositiveAndBelow (track, trackCount))
        tracks[static_cast<size_t> (track)].soloed.store (soloed);
}

bool AudioEngine::trackSolo (int track) const
{
    return juce::isPositiveAndBelow (track, trackCount) && tracks[static_cast<size_t> (track)].soloed.load();
}

void AudioEngine::setMasterGain (float linearGain) { masterGainValue.store (juce::jlimit (0.0f, 2.0f, linearGain)); }

float AudioEngine::trackPeak (int track) const
{
    return juce::isPositiveAndBelow (track, trackCount) ? tracks[static_cast<size_t> (track)].peak.load() : 0.0f;
}


int AudioEngine::demoAssetId (int track) const
{
    return juce::isPositiveAndBelow (track, trackCount)
               ? demoAssets[static_cast<size_t> (track)]
               : -1;
}


void AudioEngine::suspendAudioCallbackForTesting()
{
    deviceManager.removeAudioCallback (this);
}

void AudioEngine::processBlockForTesting (const float* const* inputChannelData,
                                          int numInputChannels,
                                          float* const* outputChannelData,
                                          int numOutputChannels,
                                          int numSamples)
{
    renderBlock (inputChannelData, numInputChannels, outputChannelData, numOutputChannels, numSamples);
}

} // namespace nc
