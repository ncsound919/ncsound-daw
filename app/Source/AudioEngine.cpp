#include "AudioEngine.h"

#include "AudioEngineInternal.h"

#include "BuiltinStudioFx.h"
#include "BuiltinSynth.h"
#include "Session.h"

#include <nc/engine/ClipTimeline.hpp>
#include <nc/engine/MidiSchedule.hpp>
#include <nc/engine/ProjectLimits.hpp>
#include <nc/engine/StreamWindow.hpp>
#include <nc/engine/VoiceToMidi.hpp>

#include <algorithm>
#include <cmath>
#include <thread>
#include <utility>

namespace nc
{
AudioEngine::AudioEngine()
{
    engineAlive = std::make_shared<std::atomic<bool>> (true);
    formats.registerBasicFormats();

    // Pan cache: element 0's brace initializer only seeds lane 0; every other
    // lane would start at 0 and skip the first trig, leaving its gains at zero.
    std::fill_n (cachedPanValue, trackCount, -2.0f);
    std::fill_n (cachedPanLeft, trackCount, 0.7071f);
    std::fill_n (cachedPanRight, trackCount, 0.7071f);

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

int AudioEngine::pollMidiLearnChanges()
{
    struct Range { float low, high; bool logarithmic = false; bool boolean = false; };
    auto rangeFor = [] (const juce::String& id, Range& out) -> bool
    {
        if (id == "stripOn" || id == "vocal" || id == "openMicOn" || id == "eqOn"
            || id == "hpfOn" || id == "lpfOn" || id == "compOn" || id == "satOn")
            { out = { 0.0f, 1.0f, false, true }; return true; }
        if (id == "openMicDrive") { out = { -12.0f, 24.0f }; return true; }
        if (id == "openMicMix") { out = { 0.0f, 1.0f }; return true; }
        if (id == "openMicTrim") { out = { -24.0f, 24.0f }; return true; }
        if (id == "hpfFreq") { out = { 20.0f, 500.0f, true }; return true; }
        if (id == "lpfFreq") { out = { 1000.0f, 20000.0f, true }; return true; }
        if (id == "lowFreq") { out = { 20.0f, 400.0f, true }; return true; }
        if (id == "lowGain") { out = { -18.0f, 18.0f }; return true; }
        if (id == "lmfFreq") { out = { 150.0f, 3000.0f, true }; return true; }
        if (id == "lmfGain") { out = { -18.0f, 18.0f }; return true; }
        if (id == "lmfQ") { out = { 0.2f, 10.0f, true }; return true; }
        if (id == "hmfFreq") { out = { 1000.0f, 12000.0f, true }; return true; }
        if (id == "hmfGain") { out = { -18.0f, 18.0f }; return true; }
        if (id == "hmfQ") { out = { 0.2f, 10.0f, true }; return true; }
        if (id == "highFreq") { out = { 5000.0f, 20000.0f, true }; return true; }
        if (id == "highGain") { out = { -18.0f, 18.0f }; return true; }
        if (id == "compThresh") { out = { -60.0f, 0.0f }; return true; }
        if (id == "compRatio") { out = { 1.0f, 20.0f, true }; return true; }
        if (id == "compAttack") { out = { 0.1f, 200.0f, true }; return true; }
        if (id == "compRelease") { out = { 10.0f, 1000.0f, true }; return true; }
        if (id == "compMakeup") { out = { -12.0f, 24.0f }; return true; }
        if (id == "satDrive") { out = { 0.0f, 24.0f }; return true; }
        if (id == "satMix") { out = { 0.0f, 1.0f }; return true; }
        if (id == "outputDb") { out = { -24.0f, 12.0f }; return true; }
        return false;
    };
    auto mapped = [] (const Range& range, float norm) -> float
    {
        if (range.boolean)
            return norm >= 0.5f ? 1.0f : 0.0f;
        if (range.logarithmic)
            return range.low * std::pow (range.high / range.low, norm);
        return range.low + norm * (range.high - range.low);
    };
    auto normalised = [] (const Range& range, float real) -> float
    {
        if (range.boolean)
            return real >= 0.5f ? 1.0f : 0.0f;
        if (range.logarithmic)
            return std::clamp (std::log (juce::jmax (range.low, real) / range.low)
                               / std::log (range.high / range.low), 0.0f, 1.0f);
        return std::clamp ((real - range.low) / (range.high - range.low), 0.0f, 1.0f);
    };

    int applied = 0;
    MidiLearnManager::Change change;
    while (midiLearn.takeChange (change))
    {
        const auto target = juce::String (change.binding.targetParamId);
        if (target == "transport.play")
        {
            setPlaying (change.rawValue > 0);
            ++applied;
            continue;
        }
        if (target == "transport.stop")
        {
            setPlaying (false);
            ++applied;
            continue;
        }
        int track = midiLearnTrack;
        juce::String parameter = target;
        if (target.startsWith ("track."))
        {
            const auto rest = target.substring (6);
            track = rest.upToFirstOccurrenceOf (".", false, false).getIntValue();
            parameter = rest.fromFirstOccurrenceOf (".", false, false);
        }
        if (! juce::isPositiveAndBelow (track, getActiveTrackCount()))
            continue;
        Range range;
        if (! rangeFor (parameter, range))
            continue;
        auto& channel = mixChannel (track);
        float nextNorm;
        if (change.binding.mode == MidiLearnManager::Mode::toggle)
            nextNorm = channel.getParam (parameter) >= 0.5f ? 0.0f : 1.0f;
        else if (change.binding.mode == MidiLearnManager::Mode::relative)
        {
            int delta = change.rawValue < 64 ? change.rawValue : change.rawValue - 128;
            nextNorm = std::clamp (normalised (range, channel.getParam (parameter)) + delta / 128.0f,
                                   0.0f, 1.0f);
        }
        else
            nextNorm = std::clamp (change.rawValue / 127.0f, 0.0f, 1.0f);
        channel.setParam (parameter, mapped (range, nextNorm));
        ++applied;
    }
    return applied;
}

AudioEngine::~AudioEngine()
{
    saveAudioSettings();
    engineAlive->store (false, std::memory_order_release);
    deviceManager.removeAudioCallback (this);
    inserts.collectGarbage();
}

bool AudioEngine::loadTrackInstrument (int track, const juce::PluginDescription& description,
                                       juce::String& error)
{
    if (! juce::isPositiveAndBelow (track, activeTrackCount.load()))
    {
        error = "Instrument target is outside the session";
        return false;
    }

    const auto rate = juce::jmax (1.0, sampleRate.load());
    const auto block = juce::jmax (1, bufferSize.load());
    auto alive = engineAlive;

    catalog.createAsync (description, rate, block,
                         [this, alive, track, rate, block, description]
                         (std::unique_ptr<juce::AudioPluginInstance> instance, juce::String creationError)
                         {
                             if (! alive->load (std::memory_order_acquire))
                                 return;
                             if (instance == nullptr)
                             {
                                 setPluginStatus ("INSTRUMENT FAILED / " + creationError);
                                 return;
                             }
                             juce::String error;
                             auto hosted = HostedInsert::create (std::move (instance), rate, block, error, true);
                             if (hosted == nullptr)
                             {
                                 setPluginStatus ("INSTRUMENT FAILED / " + error);
                                 return;
                             }
                             nc::engine::PluginSlotState state;
                             state.identifier = description.createIdentifierString().toStdString();
                             state.format = description.pluginFormatName.toStdString();
                             if (const auto xml = description.createXml())
                                 state.descriptionXml = xml->toString().toStdString();
                             if (! inserts.installInstrument (track, std::move (hosted), state, error))
                             {
                                 setPluginStatus ("INSTRUMENT FAILED / " + error);
                                 return;
                             }
                             setPluginStatus ("INSTRUMENT / " + description.name + " / TRACK " + juce::String (track + 1));
                         });
    return true;
}

bool AudioEngine::restoreTrackInstrument (int track, const nc::engine::PluginSlotState& state,
                                          juce::String& error)
{
    if (! juce::isPositiveAndBelow (track, activeTrackCount.load()))
    {
        error = "Instrument target is outside the session";
        return false;
    }
    if (state.isEmpty())
        return false;

    // Built-in synth reopens without a scan.
    if (state.identifier == builtinSynthIdentifier)
    {
        auto synth = std::make_unique<BuiltinSynth>();
        synth->setState (state);
        juce::String installError;
        return inserts.installInstrument (track, std::move (synth), state, installError);
    }

    std::optional<juce::PluginDescription> description;
    if (auto found = catalog.findDescription (juce::String (state.identifier)))
        description = *found;
    if (! description.has_value() && ! state.descriptionXml.empty())
    {
        if (const auto xml = juce::XmlDocument::parse (juce::String (state.descriptionXml)))
        {
            juce::PluginDescription parsed;
            if (parsed.loadFromXml (*xml))
                description = parsed;
        }
    }

    const auto displayName = description.has_value() ? description->name : juce::String (state.identifier);
    if (! description.has_value())
    {
        inserts.markInstrumentMissing (track, state, displayName);
        error = "Instrument not found: " + displayName;
        return false;
    }

    const auto rate = juce::jmax (1.0, sampleRate.load());
    const auto block = juce::jmax (1, bufferSize.load());
    auto instance = catalog.createSync (*description, rate, block, error);
    if (instance == nullptr)
    {
        inserts.markInstrumentMissing (track, state, displayName);
        return false;
    }
    auto hosted = HostedInsert::create (std::move (instance), rate, block, error, true);
    if (hosted == nullptr)
    {
        inserts.markInstrumentMissing (track, state, displayName);
        return false;
    }
    if (! hosted->setState (state))
    {
        inserts.markInstrumentMissing (track, state, displayName);
        error = "The saved instrument state could not be restored";
        return false;
    }
    return inserts.installInstrument (track, std::move (hosted), state, error);
}

void AudioEngine::unloadTrackInstrument (int track)
{
    inserts.removeInstrument (track);
}

void AudioEngine::setBuiltinInstrument (int track, int wave)
{
    if (! juce::isPositiveAndBelow (track, activeTrackCount.load()))
        return;
    auto synth = std::make_unique<BuiltinSynth>();
    synth->setWave (static_cast<BuiltinSynth::Wave> (juce::jlimit (0, 2, wave)));
    nc::engine::PluginSlotState state;
    state.identifier = builtinSynthIdentifier;
    state.format = "builtin";
    juce::String error;
    inserts.installInstrument (track, std::move (synth), state, error);
}

juce::String AudioEngine::trackInstrumentName (int track) const
{
    return inserts.instrumentName (track);
}

bool AudioEngine::trackInstrumentIsMissing (int track) const
{
    return inserts.isInstrumentMissing (track);
}

nc::engine::PluginSlotState AudioEngine::trackInstrumentState (int track) const
{
    return inserts.instrumentState (track);
}

juce::AudioPluginInstance* AudioEngine::trackInstrumentInstance (int track) const
{
    if (track < 0 || track >= trackCount)
        return nullptr;

    auto* owned = inserts.instrumentSlot (track).owned.get();
    if (auto* hosted = dynamic_cast<HostedInsert*> (owned))
        return hosted->instance();

    return nullptr;
}

juce::AudioPluginInstance* AudioEngine::trackInsertInstance (int track, int slot) const
{
    if (track < 0 || track >= trackCount || slot < 0 || slot >= 4)
        return nullptr;

    auto* owned = inserts.slot (track, slot).owned.get();
    if (auto* hosted = dynamic_cast<HostedInsert*> (owned))
        return hosted->instance();

    return nullptr;
}

bool AudioEngine::loadTrackInstrumentByIndex (int track, int scannedIndex, juce::String& error)
{
    const auto entries = loadableInstrumentEntries();

    if (! juce::isPositiveAndBelow (scannedIndex, static_cast<int> (entries.size())))
    {
        error = "That instrument is not in the scanned VST3 catalog";
        return false;
    }

    return loadTrackInstrument (track, entries[static_cast<size_t> (scannedIndex)].description, error);
}

bool AudioEngine::loadBuiltinInstrumentForTrack (int track, int wave, juce::String& error)
{
    if (! juce::isPositiveAndBelow (track, activeTrackCount.load()))
    {
        error = "Instrument target is outside the session";
        return false;
    }

    setBuiltinInstrument (track, wave);
    return true;
}

bool AudioEngine::loadTrackInstrumentFromId (int track, const juce::String& catalogId, juce::String& error)
{
    if (catalogId == builtinSynthIdentifier)
        return loadBuiltinInstrumentForTrack (track, 0, error);

    const auto description = catalog.resolveDescription (catalogId, error);

    if (! description.has_value())
    {
        if (error.isEmpty())
            error = "That plugin is not in the scanned VST3 catalog";

        return false;
    }

    return loadTrackInstrument (track, *description, error);
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

int AudioEngine::scanPlugins()
{
    return catalog.scanSync();
}

std::vector<CatalogEntry> AudioEngine::loadablePluginEntries() const
{
    std::vector<CatalogEntry> result;

    for (const auto& entry : catalog.snapshot())
        if (entry.external && entry.scanned && ! entry.isInstrument)
            result.push_back (entry);

    return result;
}

std::vector<CatalogEntry> AudioEngine::loadableInstrumentEntries() const
{
    std::vector<CatalogEntry> result;

    for (const auto& entry : catalog.snapshot())
        if (entry.external && entry.scanned && entry.isInstrument)
            result.push_back (entry);

    return result;
}

int AudioEngine::scannedInstrumentCount() const
{
    return static_cast<int> (loadableInstrumentEntries().size());
}

juce::String AudioEngine::scannedInstrumentName (int index) const
{
    const auto entries = loadableInstrumentEntries();

    if (! juce::isPositiveAndBelow (index, static_cast<int> (entries.size())))
        return {};

    const auto& entry = entries[static_cast<size_t> (index)];
    return entry.manufacturer.isEmpty() ? entry.name : entry.name + " / " + entry.manufacturer;
}

int AudioEngine::scannedPluginCount() const
{
    return static_cast<int> (loadablePluginEntries().size());
}

juce::String AudioEngine::scannedPluginName (int index) const
{
    const auto entries = loadablePluginEntries();

    if (! juce::isPositiveAndBelow (index, static_cast<int> (entries.size())))
        return {};

    const auto& entry = entries[static_cast<size_t> (index)];
    return entry.manufacturer.isEmpty() ? entry.name : entry.name + " / " + entry.manufacturer;
}

bool AudioEngine::loadMasterPlugin (int index)
{
    const auto entries = loadablePluginEntries();

    if (! juce::isPositiveAndBelow (index, static_cast<int> (entries.size())))
        return false;

    const auto& entry = entries[static_cast<size_t> (index)];

    juce::String error;
    return loadMasterInsert (entry.description, error);
}

bool AudioEngine::loadMasterInsert (const juce::PluginDescription& description, juce::String& error)
{
    const auto rate = juce::jmax (1.0, sampleRate.load());
    const auto block = juce::jmax (1, bufferSize.load());
    auto instance = catalog.createSync (description, rate, block, error);

    if (instance == nullptr)
        return false;

    auto hosted = HostedInsert::create (std::move (instance), rate, block, error);

    if (hosted == nullptr)
        return false;

    nc::engine::PluginSlotState state;
    state.identifier = description.createIdentifierString().toStdString();
    state.format = description.pluginFormatName.toStdString();

    if (const auto xml = description.createXml())
        state.descriptionXml = xml->toString().toStdString();

    if (! inserts.installMaster (std::move (hosted), state, error))
        return false;

    setPluginStatus ("INSERT / " + description.name + " / MASTER");
    return true;
}

void AudioEngine::removeMasterPlugin()
{
    inserts.removeMaster();
}

bool AudioEngine::hasMasterPlugin() const
{
    return inserts.masterSlot().active.load (std::memory_order_acquire) != nullptr;
}

juce::String AudioEngine::masterPluginName() const
{
    return inserts.masterName();
}

int AudioEngine::masterPluginLatency() const
{
    return inserts.masterLatency() + masterRack.latencySamples();
}

void AudioEngine::setMasterInsertBypassed (bool bypassed)
{
    inserts.setMasterBypassed (bypassed);
}

bool AudioEngine::masterInsertIsMissing() const
{
    return inserts.isMasterMissing();
}

nc::engine::PluginSlotState AudioEngine::masterInsertState() const
{
    auto* owned = inserts.masterSlot().owned.get();

    if (owned != nullptr)
    {
        if (auto* hosted = dynamic_cast<HostedInsert*> (owned))
        {
            nc::engine::PluginSlotState state;
            hosted->getState (state);
            state.descriptionXml = inserts.masterSlot().saved.descriptionXml;
            return state;
        }
    }

    return inserts.masterState();
}

juce::AudioPluginInstance* AudioEngine::masterInsertInstance() const
{
    auto* owned = inserts.masterSlot().owned.get();

    if (auto* hosted = dynamic_cast<HostedInsert*> (owned))
        return hosted->instance();

    return nullptr;
}

int AudioEngine::demoAssetId (int track) const
{
    return juce::isPositiveAndBelow (track, trackCount)
               ? demoAssets[static_cast<size_t> (track)]
               : -1;
}

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

        if (const auto* lane = volumeAutomation[static_cast<size_t> (trackIndex)].live.load (std::memory_order_acquire);
            lane != nullptr && ! lane->empty())
            automatedGain[trackIndex] = lane->valueAt (blockBeat);

        if (const auto* lane = panAutomation[static_cast<size_t> (trackIndex)].live.load (std::memory_order_acquire);
            lane != nullptr && ! lane->empty())
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

juce::String AudioEngine::masterPluginIdentifier() const
{
    return inserts.masterState().identifier;
}

juce::String AudioEngine::masterPluginStateBase64() const
{
    return masterInsertState().stateBase64;
}

bool AudioEngine::restoreMasterPlugin (const juce::String& identifier, const juce::String& stateBase64)
{
    nc::engine::PluginSlotState state;
    state.identifier = identifier.toStdString();
    if (state.identifier.empty())
        return false;
    state.format = "VST3";
    state.stateBase64 = stateBase64.toStdString();
    juce::String error;
    return restoreMasterInsert (state, error);
}

bool AudioEngine::restoreMasterInsert (const nc::engine::PluginSlotState& state, juce::String& error)
{
    if (state.isEmpty())
        return false;

    std::optional<juce::PluginDescription> description;

    if (auto found = catalog.findDescription (juce::String (state.identifier)))
        description = *found;

    if (! description.has_value() && ! state.descriptionXml.empty())
    {
        if (const auto xml = juce::XmlDocument::parse (juce::String (state.descriptionXml)))
        {
            juce::PluginDescription parsed;

            if (parsed.loadFromXml (*xml))
                description = parsed;
        }
    }

    if (! description.has_value())
    {
        // Legacy projects saved only identifier+state; try single-bundle resolve.
        juce::String resolveError;

        if (const auto resolved = catalog.resolveDescription (juce::String (state.identifier), resolveError))
            description = *resolved;
    }

    const auto displayName = description.has_value() ? description->name
                                                     : juce::String (state.identifier);

    if (! description.has_value())
    {
        inserts.markMasterMissing (state, displayName);
        error = "Plugin not found: " + displayName;
        return false;
    }

    const auto rate = juce::jmax (1.0, sampleRate.load());
    const auto block = juce::jmax (1, bufferSize.load());
    auto instance = catalog.createSync (*description, rate, block, error);

    if (instance == nullptr)
    {
        inserts.markMasterMissing (state, displayName);
        return false;
    }

    auto hosted = HostedInsert::create (std::move (instance), rate, block, error);

    if (hosted == nullptr)
    {
        inserts.markMasterMissing (state, displayName);
        return false;
    }

    if (! state.stateBase64.empty() && ! hosted->setState (state))
    {
        error = "The saved plugin state could not be restored";
        inserts.markMasterMissing (state, displayName);
        return false;
    }

    return inserts.installMaster (std::move (hosted), state, error);
}

void AudioEngine::setPluginStatus (juce::String message)
{
    if (onPluginStatus)
        onPluginStatus (std::move (message));
}

bool AudioEngine::loadTrackInsert (int track, int slot, const juce::PluginDescription& description,
                                   juce::String& error)
{
    // Eight user slots per track, matching the rack and the editor's two banks.
    // The old four-slot cap rejected every load from bank B.
    if (! juce::isPositiveAndBelow (track, activeTrackCount.load())
        || ! juce::isPositiveAndBelow (slot, kInsertSlotsPerTrack))
    {
        error = "Insert target is outside the session";
        return false;
    }

    const auto rate = juce::jmax (1.0, sampleRate.load());
    const auto block = juce::jmax (1, bufferSize.load());
    auto alive = engineAlive;

    catalog.createAsync (description, rate, block,
                         [this, alive, track, slot, description, rate, block]
                         (std::unique_ptr<juce::AudioPluginInstance> instance, juce::String creationError)
                         {
                             if (! alive->load (std::memory_order_acquire))
                                 return;

                             // The instance only exists now, on this thread. The view
                             // has to be told, or the slot keeps reading "empty" after
                             // a load that actually worked.
                             const auto announce = [this, alive]
                             {
                                 juce::MessageManager::callAsync ([this, alive]
                                 {
                                     if (alive->load (std::memory_order_acquire)
                                         && onInsertInstalled != nullptr)
                                         onInsertInstalled();
                                 });
                             };

                             if (instance == nullptr)
                             {
                                 setPluginStatus ("INSERT FAILED / " + creationError);
                                 announce();
                                 return;
                             }

                             juce::String error;
                             auto hosted = HostedInsert::create (std::move (instance), rate, block, error);

                             if (hosted == nullptr)
                             {
                                 setPluginStatus ("INSERT FAILED / " + error);
                                 announce();
                                 return;
                             }

                             nc::engine::PluginSlotState state;
                             state.identifier = description.createIdentifierString().toStdString();
                             state.format = description.pluginFormatName.toStdString();

                             if (const auto xml = description.createXml())
                                 state.descriptionXml = xml->toString().toStdString();

                             if (! inserts.install (track, slot, std::move (hosted), state, error))
                             {
                                 setPluginStatus ("INSERT FAILED / " + error);
                                 announce();
                                 return;
                             }

                             setPluginStatus ("INSERT / " + description.name + " / TRACK "
                                               + juce::String (track + 1) + " / INS " + juce::String (slot + 1));
                             announce();
                         });

    return true;
}

bool AudioEngine::setBuiltinStudioFx (int track, int slot, const juce::String& chainJson,
                                       juce::String& error)
{
    if (! juce::isPositiveAndBelow (track, activeTrackCount.load())
        || ! juce::isPositiveAndBelow (slot, kInsertSlotsPerTrack))
    {
        error = "Insert target is outside the session";
        return false;
    }

    auto fx = std::make_unique<BuiltinStudioFx>();
    if (! chainJson.isEmpty() && ! fx->setChainJson (chainJson.toStdString()))
    {
        error = "That studio-FX chain could not be read";
        return false;
    }
    nc::engine::PluginSlotState state;
    fx->getState (state);
    return inserts.install (track, slot, std::move (fx), state, error);
}

bool AudioEngine::loadTrackInsertFromId (int track, int slot, const juce::String& catalogId,
                                        juce::String& error)
{
    // The built-in chain never touches the VST3 catalog.
    if (catalogId == builtinStudioFxIdentifier)
        return setBuiltinStudioFx (track, slot, {}, error);

    const auto description = catalog.resolveDescription (catalogId, error);

    if (! description.has_value())
    {
        if (error.isEmpty())
            error = "That plugin is not in the scanned VST3 catalog";

        return false;
    }

    return loadTrackInsert (track, slot, *description, error);
}

bool AudioEngine::restoreTrackInsert (int track, int slot, const nc::engine::PluginSlotState& state,
                                      juce::String& error)
{
    if (! juce::isPositiveAndBelow (track, activeTrackCount.load())
        || ! juce::isPositiveAndBelow (slot, kInsertSlotsPerTrack))
    {
        error = "Insert target is outside the session";
        return false;
    }

    if (state.isEmpty())
        return false;

    // Built-in chain reopens without a scan; the chain JSON is the state.
    if (state.identifier == builtinStudioFxIdentifier)
    {
        auto fx = std::make_unique<BuiltinStudioFx>();
        if (! fx->setState (state))
        {
            inserts.markMissing (track, slot, state, fx->name());
            error = "The saved studio-FX chain could not be restored";
            return false;
        }
        return inserts.install (track, slot, std::move (fx), state, error);
    }

    std::optional<juce::PluginDescription> description;

    if (auto found = catalog.findDescription (juce::String (state.identifier)))
        description = *found;

    if (! description.has_value() && ! state.descriptionXml.empty())
    {
        if (const auto xml = juce::XmlDocument::parse (juce::String (state.descriptionXml)))
        {
            juce::PluginDescription parsed;

            if (parsed.loadFromXml (*xml))
                description = parsed;
        }
    }

    const auto displayName = description.has_value() ? description->name
                                                       : juce::String (state.identifier);

    if (! description.has_value())
    {
        inserts.markMissing (track, slot, state, displayName);
        error = "Plugin not found: " + displayName;
        return false;
    }

    const auto rate = juce::jmax (1.0, sampleRate.load());
    const auto block = juce::jmax (1, bufferSize.load());
    auto instance = catalog.createSync (*description, rate, block, error);

    if (instance == nullptr)
    {
        inserts.markMissing (track, slot, state, displayName);
        return false;
    }

    auto hosted = HostedInsert::create (std::move (instance), rate, block, error);

    if (hosted == nullptr)
    {
        inserts.markMissing (track, slot, state, displayName);
        return false;
    }

    if (! hosted->setState (state))
    {
        error = "The saved plugin state could not be restored";
        inserts.markMissing (track, slot, state, displayName);
        return false;
    }

    return inserts.install (track, slot, std::move (hosted), state, error);
}

void AudioEngine::unloadTrackInsert (int track, int slot)
{
    inserts.remove (track, slot);
}

bool AudioEngine::moveTrackInsert (int track, int from, int to)
{
    return inserts.moveInsert (track, from, to);
}

void AudioEngine::setTrackInsertBypassed (int track, int slot, bool bypassed)
{
    inserts.setBypassed (track, slot, bypassed);
}

juce::String AudioEngine::trackInsertName (int track, int slot) const
{
    return inserts.slotName (track, slot);
}

bool AudioEngine::trackInsertIsMissing (int track, int slot) const
{
    return inserts.isMissing (track, slot);
}

nc::engine::PluginSlotState AudioEngine::trackInsertState (int track, int slot) const
{
    return inserts.slotState (track, slot);
}

int AudioEngine::trackInsertLatency (int track, int slot) const
{
    if (! juce::isPositiveAndBelow (track, trackCount) || ! juce::isPositiveAndBelow (slot, 4))
        return 0;

    const auto& insert = inserts.slot (track, slot);
    auto* processor = insert.active.load (std::memory_order_acquire);
    return processor != nullptr ? processor->latencySamples() : 0;
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

//==============================================================================
void AudioEngine::RecordBuffer::prepare (int64_t capacityFrames)
{
    storage.setSize (2, static_cast<int> (juce::jmax<int64_t> (1, capacityFrames)), false, true, false);
    storage.clear();
    written = 0;
    overflowed = false;
}

void AudioEngine::RecordBuffer::reset() noexcept
{
    written = 0;
    overflowed = false;
}

void AudioEngine::RecordBuffer::push (const float* left, const float* right, int numSamples) noexcept
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

    if (recordFramesWritten == 0)
        recordStartFrame.store (position, std::memory_order_release);

    int inputIndex = 0;

    for (int track = 0; track < trackCount; ++track)
    {
        if (! recordArmed[static_cast<size_t> (track)].load (std::memory_order_relaxed))
            continue;

        const auto* left = numInputChannels > 0
                               ? inputChannelData[inputIndex % numInputChannels] : nullptr;
        const auto* right = numInputChannels > 1
                                ? inputChannelData[(inputIndex + 1) % numInputChannels] : left;

        // Threaded path: push straight into the writer FIFO (no copy here;
        // the writer thread owns the disk). RAM path: preallocated buffer.
        bool pushedThreaded = false;

        for (auto& take : threadedTakes)
        {
            if (take.track != track || take.writer == nullptr)
                continue;

            const auto silence = recordSilence.data();
            const float* channels[2] { left != nullptr ? left : silence,
                                        right != nullptr ? right : silence };
            if (! take.writer->write (channels, numSamples))
                overflowCount.fetch_add (1, std::memory_order_relaxed);
            pushedThreaded = true;
        }

        if (! pushedThreaded)
            recordBuffers[static_cast<size_t> (track)].push (left, right, numSamples);

        ++inputIndex;
    }

    recordFramesWritten += numSamples;
}

void AudioEngine::setRecordArmed (int track, bool armed)
{
    if (track < 0 || track >= trackCount)
        return;

    recordArmed[static_cast<size_t> (track)].store (armed, std::memory_order_release);
}

bool AudioEngine::isRecordArmed (int track) const
{
    return track >= 0 && track < trackCount
           && recordArmed[static_cast<size_t> (track)].load (std::memory_order_acquire);
}

void AudioEngine::setTrackMonitor (int track, bool on)
{
    if (track >= 0 && track < trackCount)
        trackMonitor[static_cast<size_t> (track)].store (on, std::memory_order_release);
}

bool AudioEngine::isTrackMonitor (int track) const
{
    return track >= 0 && track < trackCount
           && trackMonitor[static_cast<size_t> (track)].load (std::memory_order_acquire);
}

bool AudioEngine::consumePunchOut()
{
    return punchOutPending.exchange (false, std::memory_order_acq_rel);
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
    const auto ppq = engine::ppqAtSamplesWithMap (map, positionSamples.load(), rate);
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
        if (recordArmed[static_cast<size_t> (track)].load (std::memory_order_relaxed))
            most = juce::jmax (most, recordBuffers[static_cast<size_t> (track)].written);

    return most;
}

int AudioEngine::recordOverflows() const
{
    int count = 0;

    for (int track = 0; track < trackCount; ++track)
        if (recordBuffers[static_cast<size_t> (track)].overflowed)
            ++count;

    return count;
}

bool AudioEngine::startRecording (juce::String& error)
{
    if (recording.load (std::memory_order_acquire))
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
        if (recordArmed[static_cast<size_t> (track)].load (std::memory_order_relaxed))
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
        auto& buffer = recordBuffers[static_cast<size_t> (track)];
        buffer.reset();

        if (recordArmed[static_cast<size_t> (track)].load (std::memory_order_relaxed))
        {
            buffer.prepare (capacity);
            // The native UI arms tracks but has no separate monitor switch yet.
            // Monitoring follows the armed tracks while recording so the player
            // hears the input through the same chain being captured.
            trackMonitor[static_cast<size_t> (track)].store (true, std::memory_order_relaxed);
        }
        else if (buffer.storage.getNumSamples() > 0)
            buffer.storage.setSize (0, 0);
    }

    recordFramesWritten = 0;
    recordStartFrame.store (positionSamples.load(), std::memory_order_release);
    recording.store (true, std::memory_order_release);
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
    threadedTakes.clear();
    juce::WavAudioFormat wav;
    const auto rate = sampleRate.load();
    int takeNumber = 0;

    for (int track = 0; track < trackCount; ++track)
    {
        if (! recordArmed[static_cast<size_t> (track)].load (std::memory_order_relaxed))
            continue;

        const auto file = folder.getChildFile ("Track " + juce::String (track + 1)
                                               + " take " + juce::String (++takeNumber) + ".wav");
        file.deleteFile();
        std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());

        if (stream == nullptr)
        {
            error = "Cannot write " + file.getFullPathName();
            threadedTakes.clear();
            recording.store (false, std::memory_order_release);
            return false;
        }

        std::unique_ptr<juce::AudioFormatWriter> writer (
            wav.createWriterFor (stream.get(), rate, 2, 24, {}, 0));

        if (writer == nullptr)
        {
            error = "Unsupported recording format";
            threadedTakes.clear();
            recording.store (false, std::memory_order_release);
            return false;
        }

        ThreadedTake take;
        take.writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (
            writer.release(), writerThread, 65536);
        take.file = file;
        take.track = track;
        take.startFrame = recordStartFrame.load (std::memory_order_acquire);
        threadedTakes.push_back (std::move (take));
    }

    error.clear();
    return true;
}

std::vector<AudioEngine::RecordedTake> AudioEngine::stopRecording (const juce::File& folder,
                                                                  int bitDepth,
                                                                  juce::String& error)
{
    std::vector<RecordedTake> takes;

    if (! recording.exchange (false, std::memory_order_acq_rel))
    {
        error = "Not recording";
        return takes;
    }

    for (auto& monitor : trackMonitor)
        monitor.store (false, std::memory_order_relaxed);

    // A callback that was already inside captureInputBlock when the flag
    // flipped is still using the writers it found in threadedTakes. Wait for
    // two later blocks to start (callbacks are strictly serial) before
    // touching the take list — the same retiredAtBlock + 2 rule the snapshot
    // sweeps apply. Without this, stop could free a writer mid-write.
    if (deviceOpen.load (std::memory_order_acquire))
    {
        const auto started = audioBlockCount.load (std::memory_order_acquire);
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + 500.0;

        while (audioBlockCount.load (std::memory_order_acquire) < started + 2)
        {
            if (juce::Time::getMillisecondCounterHiRes() > deadline)
                break; // a wedged driver must not hang the UI

            juce::Thread::sleep (1);
        }
    }

    // Streaming takes are already on disk; deleting the writers flushes.
    // Note: the latency trim that the RAM path applies is not applied to
    // streamed takes (they start at the punch-in transport frame).
    if (! threadedTakes.empty())
    {
        for (auto& take : threadedTakes)
        {
            take.writer.reset();
            RecordedTake recorded;
            recorded.track = take.track;
            recorded.file = take.file;
            recorded.startFrame = take.startFrame;
            recorded.frames = recordFramesWritten;
            takes.push_back (std::move (recorded));
        }

        threadedTakes.clear();
        error.clear();
        return takes;
    }

    // Capture is closed; the buffers are no longer written from this point.
    const auto latency = static_cast<int64_t> (juce::jmax (0, recordLatency.load()));
    const auto startFrame = recordStartFrame.load (std::memory_order_acquire);
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
        if (! recordArmed[static_cast<size_t> (track)].load (std::memory_order_relaxed))
            continue;

        auto& buffer = recordBuffers[static_cast<size_t> (track)];
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

    for (auto& buffer : recordBuffers)
        if (buffer.storage.getNumSamples() > 0)
            buffer.storage.setSize (0, 0);

    recordFramesWritten = 0;
    return takes;
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
