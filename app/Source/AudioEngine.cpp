#include "AudioEngine.h"

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

namespace nc
{
namespace
{
constexpr double twoPi = juce::MathConstants<double>::twoPi;
constexpr float smoothingCoefficient = 0.0025f;

float softLimit (float value)
{
    return std::tanh (value);
}
} // namespace

AudioEngine::AudioEngine()
{
    engineAlive = std::make_shared<std::atomic<bool>> (true);
    formats.registerBasicFormats();

    for (auto& monitor : trackMonitor)
        monitor.store (true, std::memory_order_relaxed);

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

// ----------------------------------------------------------------------
// Audio interface persistence (JUCE ApplicationProperties).
// ----------------------------------------------------------------------
static juce::File audioSettingsFile()
{
    return juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
        .getChildFile ("NCSound Music Hub").getChildFile ("audio-settings.xml");
}

static juce::PropertiesFile::Options audioPropsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "NCSound Music Hub";
    o.filenameSuffix = "settings";
    o.folderName = "NCSound Music Hub";
    return o;
}

void AudioEngine::saveAudioSettings()
{
    juce::ApplicationProperties props;
    props.setStorageParameters (audioPropsOptions());
    auto xml = deviceManager.createStateXml();
    if (xml != nullptr)
    {
        xml->setAttribute ("midiInput", midiRecorder.activeDeviceId());
        props.getUserSettings()->setValue ("audioDeviceSetup", xml.get());
        props.saveIfNeeded();
    }
}

void AudioEngine::restoreAudioSettings()
{
    juce::ApplicationProperties props;
    props.setStorageParameters (audioPropsOptions());
    auto stored = props.getUserSettings()->getXmlValue ("audioDeviceSetup");
    if (stored == nullptr)
        return;
    auto error = deviceManager.initialise (2, 2, stored.get(), true);
    if (error.isNotEmpty())
        error = deviceManager.initialiseWithDefaultDevices (2, 2);
    if (error.isEmpty())
    {
        auto midiId = stored->getStringAttribute ("midiInput");
        if (midiId.isNotEmpty())
        {
            juce::String ignored;
            openMidiDevice (midiId, ignored);
        }
    }
}

void AudioEngine::clearAudioSettings()
{
    juce::ApplicationProperties props;
    props.setStorageParameters (audioPropsOptions());
    props.getUserSettings()->removeValue ("audioDeviceSetup");
    props.saveIfNeeded();
}

juce::String AudioEngine::audioSettingsSummary() const
{
    return deviceInfo + "@" + juce::String (currentSampleRate(), 0) + "Hz," + juce::String (currentBufferSize()) + "smp";
}

juce::var AudioEngine::audioStateToVar()
{
    auto* o = new juce::DynamicObject();
    const auto setup = deviceManager.getAudioDeviceSetup();
    o->setProperty ("deviceType", deviceManager.getCurrentAudioDeviceType());
    o->setProperty ("outputDevice", setup.outputDeviceName);
    o->setProperty ("sampleRate", setup.sampleRate);
    o->setProperty ("bufferSize", setup.bufferSize);
    o->setProperty ("outputChannels", setup.outputChannels.countNumberOfSetBits());
    o->setProperty ("inputChannels", setup.inputChannels.countNumberOfSetBits());
    o->setProperty ("open", isDeviceOpen());
    o->setProperty ("currentRate", currentSampleRate());
    o->setProperty ("currentBuffer", currentBufferSize());
    o->setProperty ("device", deviceDescription());
    o->setProperty ("overruns", 0.0);
    o->setProperty ("blocks", 0.0);

    juce::Array<juce::var> types;
    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        if (type == nullptr)
            continue;

        type->scanForDevices();
        auto* t = new juce::DynamicObject();
        t->setProperty ("name", type->getTypeName());
        juce::Array<juce::var> outputs;
        for (const auto& name : type->getDeviceNames (false))
            outputs.add (name);
        t->setProperty ("outputs", outputs);
        types.add (juce::var (t));
    }
    o->setProperty ("types", types);
    return juce::var (o);
}

juce::var AudioEngine::applyAudioSettings (const juce::String& deviceType,
                                           const juce::String& outputDevice,
                                           double sampleRate, int bufferSize)
{
    juce::String error;
    juce::String note;

    if (deviceType.isNotEmpty())
    {
        juce::AudioIODeviceType* chosen = nullptr;
        for (auto* type : deviceManager.getAvailableDeviceTypes())
            if (type != nullptr && type->getTypeName() == deviceType)
                chosen = type;

        if (chosen == nullptr)
        {
            error = "Unknown audio driver: " + deviceType;
        }
        else if (deviceManager.getCurrentAudioDeviceType() != deviceType)
        {
            deviceManager.setCurrentAudioDeviceType (deviceType, true);
            if (deviceManager.getCurrentAudioDeviceType() != deviceType)
                error = "Could not select " + deviceType;
        }
    }

    if (error.isEmpty())
    {
        auto setup = deviceManager.getAudioDeviceSetup();

        if (auto* type = deviceManager.getCurrentDeviceTypeObject())
        {
            type->scanForDevices();
            const auto outputs = type->getDeviceNames (false);

            if (outputDevice.isNotEmpty())
            {
                if (outputs.contains (outputDevice))
                    setup.outputDeviceName = outputDevice;
                else
                    error = "No output device named " + outputDevice;
            }
            else if (! outputs.contains (setup.outputDeviceName) && ! outputs.isEmpty())
            {
                // Switching driver leaves the previous driver's device name
                // behind; use this driver's first output rather than fail.
                setup.outputDeviceName = outputs[0];
            }
        }

        if (error.isEmpty())
        {
            if (sampleRate > 0.0)
                setup.sampleRate = sampleRate;
            if (bufferSize > 0)
                setup.bufferSize = bufferSize;

            // Let the device choose its default channel set. The saved
            // DirectSound setup asked a stereo codec for 11 channels, which is
            // the kind of request that fails silently and leaves no audio.
            setup.useDefaultOutputChannels = true;
            setup.useDefaultInputChannels = true;

            const auto err = deviceManager.setAudioDeviceSetup (setup, true);
            if (err.isNotEmpty())
                error = err;
            else if (! isDeviceOpen())
                error = "The audio device did not open.";
        }
    }

    if (error.isEmpty())
    {
        const auto actual = deviceManager.getAudioDeviceSetup();
        if (sampleRate > 0.0 && std::abs (actual.sampleRate - sampleRate) > 0.5)
            note = "Device runs at " + juce::String (actual.sampleRate, 0) + " Hz.";
        if (bufferSize > 0 && actual.bufferSize != bufferSize)
            note << (note.isEmpty() ? "" : " ")
                 << "Buffer is " << actual.bufferSize << " samples.";
        saveAudioSettings();
    }

    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", error.isEmpty());
    o->setProperty ("error", error);
    o->setProperty ("note", note);
    o->setProperty ("state", audioStateToVar());
    return juce::var (o);
}




double AudioEngine::bufferLatencyMs() const
{
    const double rate = currentSampleRate();
    return rate > 0.0 ? (double) currentBufferSize() * 1000.0 / rate : 0.0;
}

double AudioEngine::worstTrackLatencyMs() const
{
    const double rate = currentSampleRate();
    const int worst = inserts.worstTrackLatency(); // samples, capped at 16384 in TrackInsertRack
    return rate > 0.0 ? (double) worst * 1000.0 / rate : 0.0;
}

double AudioEngine::masterLatencyMs() const
{
    const double rate = currentSampleRate();
    const int lat = masterRack.latencySamples() + inserts.masterLatency();
    return rate > 0.0 ? (double) lat * 1000.0 / rate : 0.0;
}

void AudioEngine::setLowLatencyMode (bool shouldLow)
{
    lowLatency.store (shouldLow);
    if (! shouldLow)
        return;
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.bufferSize = 64;
    setup.sampleRate = 48000.0;
    deviceManager.setAudioDeviceSetup (setup, true);
    saveAudioSettings();
}

nc::engine::ClipState AudioEngine::voiceToMidiClip (const juce::File& file, int track, double bpm)
{
    voiceError.clear();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        voiceError = "Could not open " + file.getFileName();
        return {};
    }
    const auto frames = (std::size_t) juce::jmin<juce::int64> (reader->lengthInSamples, 4800000); // cap ~100s
    juce::AudioBuffer<float> buf ((int) juce::jmax<juce::int64> (1, reader->numChannels), (int) frames);
    reader->read (&buf, 0, (int) frames, 0, true, true);
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
        return {};
    }

    nc::engine::ClipState clip;
    clip.isMidi = true;
    clip.name = "VOICE TO MIDI";
    clip.track = track;
    clip.startBar = 1.0;
    clip.lengthBars = 1.0 + (notes.back().startBeat + notes.back().lengthBeats) / nc::engine::beatsPerBarMidi;
    for (auto& n : notes)
        clip.notes.push_back ({n.startBeat, n.lengthBeats, n.midiNote, n.velocity, 1});
    nc::engine::sanitizeNotes (clip.notes, nc::engine::clipLengthBeats (clip.lengthBars));
    return clip;
}

AudioEngine::~AudioEngine()
{
    saveAudioSettings();
    engineAlive->store (false, std::memory_order_release);
    deviceManager.removeAudioCallback (this);
    inserts.collectGarbage();
}

void AudioEngine::setPlaying (bool shouldPlay)    { playing.store (shouldPlay); }
void AudioEngine::setMetronome (bool on)          { metronome.store (on); }

void AudioEngine::setBpm (double newBpm)
{
    const auto clamped = nc::limits::clampBpm (newBpm);
    bpm.store (clamped);
    setTempoMap (nc::engine::constantTempoMap (clamped));
}

void AudioEngine::setTempoMap (nc::engine::TempoMap map)
{
    nc::engine::sanitizeTempoMap (map);
    auto fresh = std::make_unique<nc::engine::TempoMap> (std::move (map));

    retireSnapshot (tempoRetired, tempoSlot.owned,
                    audioBlockCount.load (std::memory_order_acquire));

    tempoSlot.owned = std::move (fresh);
    tempoSlot.live.store (tempoSlot.owned.get(), std::memory_order_release);
    bpm.store (tempoSlot.owned->points.front().bpm);
}

nc::engine::TempoMap AudioEngine::tempoMap() const
{
    if (const auto* live = tempoSlot.live.load (std::memory_order_acquire))
        return *live;

    return fallbackTempo;
}

void AudioEngine::setMarkers (std::vector<nc::engine::Marker> markers)
{
    // Project files can contain any JSON; the engine is the last boundary that
    // can reject non-finite PPQ, oversized names and absurd counts.
    nc::engine::sanitizeMarkers (markers);
    markers_ = std::move (markers);
}

void AudioEngine::setMidiClips (std::vector<MidiTimelineClip> clips)
{
    for (auto& clip : clips)
    {
        clip.track = juce::jlimit (0, trackCount - 1, clip.track);
        clip.startBar = juce::jmax (1.0, clip.startBar);
        clip.lengthBars = juce::jmax (0.25, clip.lengthBars);
        nc::engine::sanitizeNotes (clip.notes, nc::engine::clipLengthBeats (clip.lengthBars));
        nc::engine::sanitizeMidiCCs (clip.ccs, nc::engine::clipLengthBeats (clip.lengthBars));
        nc::engine::sanitizeMidiBends (clip.bends, nc::engine::clipLengthBeats (clip.lengthBars));
    }

    auto fresh = std::make_unique<std::vector<MidiTimelineClip>> (std::move (clips));

    retireSnapshot (midiRetired, midiSlot.owned,
                    audioBlockCount.load (std::memory_order_acquire));

    midiSlot.owned = std::move (fresh);
    midiSlot.live.store (midiSlot.owned.get(), std::memory_order_release);
}

std::vector<AudioEngine::MidiTimelineClip> AudioEngine::midiClips() const
{
    if (const auto* live = midiSlot.live.load (std::memory_order_acquire))
        return *live;

    return {};
}

int AudioEngine::importMidiFile (const juce::File& file, int track, double startBar)
{
    if (! file.existsAsFile())
        return -1;

    juce::MidiFile midiFile;
    std::unique_ptr<juce::FileInputStream> input (file.createInputStream());
    if (input == nullptr || ! midiFile.readFrom (*input))
        return -1;

    // getTimeFormat() is positive for ticks-per-quarter-note files; SMPTE
    // files (negative) fall back to 480.
    const auto timeFormat = midiFile.getTimeFormat();
    const auto ticksPerBeat = juce::jmax (1, timeFormat > 0 ? static_cast<int> (timeFormat) : 480);
    MidiTimelineClip clip;
    clip.track = juce::jlimit (0, trackCount - 1, track);
    clip.startBar = juce::jmax (1.0, startBar);
    clip.name = file.getFileNameWithoutExtension();

    // Pair note-ons with note-offs per (channel, pitch) across all tracks.
    struct OpenNote { double startBeat; int velocity; int channel; };
    std::vector<std::vector<OpenNote>> open (16 * 128);
    double lastBeat = 0.0;

    for (int trackIndex = 0; trackIndex < midiFile.getNumTracks(); ++trackIndex)
    {
        if (const auto* sequence = midiFile.getTrack (trackIndex))
        {
            for (int eventIndex = 0; eventIndex < sequence->getNumEvents(); ++eventIndex)
            {
                const auto* holder = sequence->getEventPointer (eventIndex);
                if (holder == nullptr)
                    continue;
                const auto beat = sequence->getEventTime (eventIndex) / ticksPerBeat;
                lastBeat = juce::jmax (lastBeat, beat);
                const auto& message = holder->message;

                if (message.isNoteOn())
                {
                    auto& slot = open[static_cast<size_t> (message.getChannel() * 128 + message.getNoteNumber())];
                    slot.push_back ({ beat, message.getVelocity(), message.getChannel() });
                }
                else if (message.isNoteOff())
                {
                    auto& slot = open[static_cast<size_t> (message.getChannel() * 128 + message.getNoteNumber())];
                    if (slot.empty())
                        continue;
                    const auto started = slot.front();
                    slot.erase (slot.begin());
                    clip.notes.push_back ({ started.startBeat, juce::jmax (engine::minMidiNoteBeats, beat - started.startBeat),
                                            message.getNoteNumber(), started.velocity, started.channel });
                }
                else if (message.isController())
                {
                    clip.ccs.push_back ({ beat, message.getChannel(), message.getControllerNumber(),
                                          message.getControllerValue() });
                }
                else if (message.isPitchWheel())
                {
                    clip.bends.push_back ({ beat, message.getChannel(), message.getPitchWheelValue() });
                }
            }
        }
    }

    clip.lengthBars = juce::jmax (0.25, (lastBeat / engine::beatsPerBarMidi) + 0.25);
    engine::sanitizeNotes (clip.notes, engine::clipLengthBeats (clip.lengthBars));

    auto clips = midiClips();
    clips.push_back (std::move (clip));
    const auto index = static_cast<int> (clips.size()) - 1;
    setMidiClips (std::move (clips));
    return index;
}

int AudioEngine::addMidiClip (nc::engine::ClipState clip)
{
    auto clips = midiClips();
    MidiTimelineClip timeline;
    timeline.track = clip.track;
    timeline.startBar = clip.startBar;
    timeline.lengthBars = clip.lengthBars;
    timeline.name = clip.name;
    timeline.notes = clip.notes;
    timeline.ccs = clip.ccs;
    timeline.bends = clip.bends;
    clips.push_back (std::move (timeline));
    const auto index = static_cast<int> (clips.size()) - 1;
    setMidiClips (std::move (clips));
    return index;
}

bool AudioEngine::exportMidiFile (const juce::File& file, int track) const
{
    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote (480);

    juce::MidiMessageSequence sequence;
    bool any = false;

    // A MIDI file has to carry the session's musical timebase. Without these
    // events, a tempo change is lost and every consumer plays the notes at
    // 120 BPM. Markers are written as standard marker meta events so sections
    // survive a round-trip through another DAW.
    const auto map = tempoMap();

    for (const auto& point : map.points)
    {
        const auto microseconds = juce::jlimit (1, 0x7fffffff,
                                                juce::roundToInt (60000000.0 /
                                                                      juce::jmax (1.0, point.bpm)));
        sequence.addEvent (juce::MidiMessage::tempoMetaEvent (microseconds),
                           juce::jmax (0.0, point.ppq) * 480.0);
    }

    sequence.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);

    for (const auto& marker : markers_)
    {
        if (! marker.name.empty())
            sequence.addEvent (juce::MidiMessage::textMetaEvent (6, juce::String (marker.name)),
                               juce::jmax (0.0, marker.ppq) * 480.0);
    }

    for (const auto& clip : midiClips())
    {
        if (clip.track != track)
            continue;

        const auto offsetBeats = engine::clipStartBeat (clip.startBar);

        for (const auto& note : clip.notes)
        {
            sequence.addEvent (juce::MidiMessage::noteOn (note.channel, note.pitch,
                                                           static_cast<juce::uint8> (note.velocity)),
                               (offsetBeats + note.startBeat) * 480);
            sequence.addEvent (juce::MidiMessage::noteOff (note.channel, note.pitch),
                               (offsetBeats + note.startBeat + note.lengthBeats) * 480);
            any = true;
        }

        for (const auto& cc : clip.ccs)
        {
            sequence.addEvent (juce::MidiMessage::controllerEvent (cc.channel, cc.controller, cc.value),
                               (offsetBeats + cc.beat) * 480);
            any = true;
        }

        for (const auto& bend : clip.bends)
        {
            sequence.addEvent (juce::MidiMessage::pitchWheel (bend.channel, bend.bend),
                               (offsetBeats + bend.beat) * 480);
            any = true;
        }
    }

    if (! any)
        return false;

    sequence.sort();
    midiFile.addTrack (sequence);
    std::unique_ptr<juce::FileOutputStream> output (file.createOutputStream());
    return output != nullptr && midiFile.writeTo (*output);
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

nc::engine::TransportBlock AudioEngine::transportBlock (int blockSize) const
{
    const auto rate = juce::jmax (1.0, sampleRate.load());
    const auto map = tempoMap();
    const auto ppq = engine::ppqAtSamplesWithMap (map, positionSamples.load(), rate);
    const auto tempo = juce::jmax (1.0, engine::bpmAt (map, ppq));
    const auto loopStart = static_cast<int64_t> (std::llround (engine::ppqToSeconds (map, engine::clipStartBeat (loopStartBar.load())) * rate));
    const auto loopEnd = static_cast<int64_t> (std::llround (engine::ppqToSeconds (map, engine::clipStartBeat (loopEndBar.load())) * rate));

    return engine::blockContext (positionSamples.load(), blockSize, tempo, rate,
                                 playing.load(), loopEnabled.load(), loopStart, loopEnd);
}

double AudioEngine::positionSeconds() const
{
    const auto rate = sampleRate.load();
    return rate > 0.0 ? static_cast<double> (positionSamples.load()) / rate : 0.0;
}

void AudioEngine::resetPosition()
{
    // The callback owns the click state; never write it from the message thread.
    resetRequested.store (true);
}

void AudioEngine::setPositionSeconds (double seconds)
{
    const auto rate = juce::jmax (1.0, sampleRate.load());
    const auto samples = static_cast<int64_t> (juce::jmax (0.0, seconds) * rate);
    positionSamples.store (samples);
    seekRequested.store (true);
}

void AudioEngine::setLoopEnabled (bool on)
{
    loopEnabled.store (on);
}

void AudioEngine::setLoopBars (double startBar, double endBar)
{
    const auto start = juce::jlimit (1.0, static_cast<double> (nc::limits::maxBars), startBar);
    const auto end = juce::jlimit (start + 1.0, static_cast<double> (nc::limits::maxBars + 1), endBar);
    loopStartBar.store (start);
    loopEndBar.store (end);
}

//==============================================================================
int AudioEngine::loadAudioFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return -1;

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels == 0)
        return -1;

    // A bounded message-thread load against the central 30-minute ceiling at
    // the file's own rate. Full disk streaming replaces this RAM hold when
    // long projects matter; the cap stays explicit either way.
    const auto fileRate = reader->sampleRate > 0.0 ? reader->sampleRate : 48000.0;
    const auto sourceFrames = reader->lengthInSamples;

    auto asset = std::make_unique<AudioAsset>();
    asset->sampleRate = fileRate;
    asset->name = file.getFileNameWithoutExtension();
    asset->file = file;

    // Long takes stream: only the read-ahead window is resident, so a long
    // import costs seconds of RAM instead of hundreds of megabytes.
    if (static_cast<double> (sourceFrames) / fileRate > nc::limits::streamThresholdSeconds)
    {
        asset->streamed = true;
        asset->totalFrames = sourceFrames;
        asset->cacheFrames = juce::jmax<int64_t> (1024, static_cast<int64_t> (fileRate * nc::limits::streamReadAheadSeconds));

        for (auto& buffer : asset->cacheA)
            buffer.assign (static_cast<size_t> (asset->cacheFrames), 0.0f);

        for (auto& buffer : asset->cacheB)
            buffer.assign (static_cast<size_t> (asset->cacheFrames), 0.0f);

        asset->reader = std::move (reader);
        return addAsset (std::move (asset));
    }

    const auto frames64 = nc::limits::clampAssetFrames (sourceFrames, fileRate);
    const auto frames = static_cast<int> (frames64);
    const auto channels = juce::jlimit (1, 2, static_cast<int> (reader->numChannels));

    asset->buffer.setSize (channels, frames);

    if (! reader->read (&asset->buffer, 0, frames, 0, true, true))
        return -1;

    return addAsset (std::move (asset));
}

double AudioEngine::assetDurationSeconds (int assetId) const
{
    if (assetId < 0 || assetId >= static_cast<int> (assets.size()))
        return 0.0;

    const auto& asset = *assets[static_cast<size_t> (assetId)];
    return asset.sampleRate > 0.0
               ? static_cast<double> (assetFrameCount (asset)) / asset.sampleRate
               : 0.0;
}

juce::File AudioEngine::assetFile (int assetId) const
{
    return assetId >= 0 && assetId < static_cast<int> (assets.size())
               ? assets[static_cast<size_t> (assetId)]->file
               : juce::File();
}

juce::String AudioEngine::assetName (int assetId) const
{
    return assetId >= 0 && assetId < static_cast<int> (assets.size())
               ? assets[static_cast<size_t> (assetId)]->name
               : juce::String();
}

int AudioEngine::demoAssetId (int track) const
{
    return juce::isPositiveAndBelow (track, trackCount)
               ? demoAssets[static_cast<size_t> (track)]
               : -1;
}

int AudioEngine::addAsset (std::unique_ptr<AudioAsset> asset)
{
    if (asset == nullptr || assetFrameCount (*asset) <= 0)
        return -1;

    assets.push_back (std::move (asset));
    return static_cast<int> (assets.size()) - 1;
}

int64_t AudioEngine::assetFrameCount (const AudioAsset& asset) const
{
    return asset.streamed ? asset.totalFrames
                          : static_cast<int64_t> (asset.buffer.getNumSamples());
}

double AudioEngine::assetSampleRate (int assetId) const
{
    return assetId >= 0 && assetId < static_cast<int> (assets.size())
               ? assets[static_cast<size_t> (assetId)]->sampleRate
               : 0.0;
}

nc::engine::audiosuite::Trim AudioEngine::assetTrim (int assetId, float threshold) const
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

int AudioEngine::processAsset (int sourceAssetId, const juce::String& nameSuffix,
                               const nc::engine::audiosuite::Process& process)
{
    if (sourceAssetId < 0 || sourceAssetId >= static_cast<int> (assets.size()))
        return -1;

    const auto& source = *assets[static_cast<size_t> (sourceAssetId)];

    auto rendered = std::make_unique<AudioAsset>();
    rendered->sampleRate = source.sampleRate;
    rendered->name = source.name + " " + nameSuffix;

    if (source.streamed)
    {
        const auto frames = static_cast<int> (juce::jmin<int64_t> (source.totalFrames, 3600LL * 48000LL));

        if (frames <= 0 || source.reader == nullptr)
            return -1;

        rendered->buffer.setSize (2, frames);
        rendered->buffer.clear();

        const auto hasRight = source.reader->numChannels > 1;
        source.reader->read (&rendered->buffer, 0, 0, frames, true, hasRight);

        if (! hasRight)
            rendered->buffer.copyFrom (1, 0, rendered->buffer, 0, 0, frames);
    }
    else
    {
        const auto frames = source.buffer.getNumSamples();
        const auto channels = source.buffer.getNumChannels();

        if (frames <= 0 || channels <= 0)
            return -1;

        rendered->buffer.setSize (2, frames);
        rendered->buffer.copyFrom (0, 0, source.buffer, 0, 0, frames);
        rendered->buffer.copyFrom (1, 0, source.buffer, juce::jmin (1, channels - 1), 0, frames);
    }

    const nc::engine::audiosuite::RenderView view {
        rendered->buffer.getWritePointer (0),
        rendered->buffer.getWritePointer (1),
        rendered->buffer.getNumSamples(),
        rendered->sampleRate
    };

    nc::engine::audiosuite::runProcess (view, process);
    return addAsset (std::move (rendered));
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

void AudioEngine::serviceStreaming()
{
    const auto seconds = positionSeconds();

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

void AudioEngine::createDemoAssets()
{
    constexpr double demoRate = 24000.0;
    constexpr double secondsPerBar = 2.0; // 120 BPM, 4/4
    constexpr int demoBars = 16;
    constexpr int frames = static_cast<int> (demoRate * secondsPerBar * demoBars);

    const char* const names[demoTrackCount] {
        "DEMO DRUMS", "DEMO BASS", "DEMO KEYS", "DEMO VOX",
        "DEMO GTR", "DEMO PERC", "DEMO VERB", "DEMO DELAY",
        "DEMO STRINGS", "DEMO PAD", "DEMO LEAD", "DEMO FX1",
        "DEMO FX2", "DEMO BUS", "DEMO AUX3", "DEMO AUX4"
    };

    for (int track = 0; track < demoTrackCount; ++track)
    {
        auto asset = std::make_unique<AudioAsset>();
        asset->sampleRate = demoRate;
        asset->name = names[track];
        asset->buffer.setSize (1, frames);

        auto* data = asset->buffer.getWritePointer (0);
        juce::Random random (0x4e430000 + track * 7919);
        auto noise = [&random] { return random.nextFloat() * 2.0f - 1.0f; };
        auto env = [] (double t, double decay) { return t >= 0.0 ? std::exp (-t / decay) : 0.0; };
        auto note = [] (double base, int semitones) { return base * std::pow (2.0, semitones / 12.0); };

        float peak = 0.0f;

        for (int frame = 0; frame < frames; ++frame)
        {
            const auto t = static_cast<double> (frame) / demoRate;
            const auto bar = static_cast<int> (t / secondsPerBar);
            const auto barTime = std::fmod (t, secondsPerBar);
            const auto beat = static_cast<int> (barTime / 0.5);
            const auto beatTime = std::fmod (barTime, 0.5);
            double sample = 0.0;

            switch (track)
            {
                case 0: // drums
                {
                    if (beat == 0 || beat == 2)
                        sample += std::sin (twoPi * (58.0 - 28.0 * beatTime) * beatTime)
                                * env (beatTime, 0.12) * 0.92;

                    if (beat == 1 || beat == 3)
                        sample += static_cast<double> (noise()) * env (beatTime, 0.075) * 0.48;

                    const auto hatTime = std::fmod (beatTime, 0.25);
                    sample += static_cast<double> (noise()) * env (hatTime, 0.018) * 0.20;
                    break;
                }

                case 1: // bass
                {
                    static const int notes[4] { 0, 0, 3, 5 };
                    const auto frequency = note (55.0, notes[bar % 4]);
                    sample = (std::sin (twoPi * frequency * t)
                            + 0.28 * std::sin (twoPi * frequency * 2.0 * t))
                           * env (beatTime, 0.22) * 0.42;
                    break;
                }

                case 2: // keys
                {
                    static const int chords[4][3] { { 0, 4, 7 }, { 5, 9, 12 }, { 7, 11, 14 }, { 3, 7, 10 } };
                    for (const auto semitone : chords[bar % 4])
                        sample += std::sin (twoPi * note (220.0, semitone) * t) * 0.10;

                    sample *= 0.45 + 0.55 * env (barTime, 0.75);
                    break;
                }

                case 3: // vocal guide
                {
                    static const int melody[4] { 7, 10, 12, 7 };
                    const auto frequency = note (220.0, melody[bar % 4]);
                    const auto vibrato = 1.0 + 0.006 * std::sin (twoPi * 5.2 * t);
                    sample = std::sin (twoPi * frequency * vibrato * t)
                           * (0.38 + 0.28 * std::sin (twoPi * 5.2 * t))
                           * env (barTime, 0.95) * 0.34;
                    break;
                }

                case 4: // guitar
                {
                    static const int riff[4] { 0, 3, 5, 7 };
                    const auto frequency = note (110.0, riff[beat % 4]);
                    sample = (std::sin (twoPi * frequency * t)
                            + 0.35 * std::sin (twoPi * frequency * 2.0 * t))
                           * env (beatTime, 0.13) * 0.32;
                    break;
                }

                case 5: // percussion
                {
                    const auto hatTime = std::fmod (beatTime, 0.25);
                    sample = static_cast<double> (noise()) * env (hatTime, 0.022) * 0.24;
                    break;
                }

                case 6: // plate return guide
                    sample = (std::sin (twoPi * 220.0 * t)
                            + std::sin (twoPi * 277.18 * t)
                            + std::sin (twoPi * 329.63 * t))
                           * 0.035 * (0.55 + 0.45 * std::sin (twoPi * t / 8.0));
                    break;

                default: // delay return guide
                    sample = static_cast<double> (noise()) * 0.018;
                    break;
            }

            const auto value = static_cast<float> (sample);
            data[frame] = value;
            peak = juce::jmax (peak, std::abs (value));
        }

        // A demo record needs a mix, not eight full-scale loops. The targets
        // leave headroom for the master and keep the return prints behind the
        // dry tracks.
        constexpr float trackPeaks[demoTrackCount] { 0.50f, 0.35f, 0.25f, 0.22f,
                                                 0.24f, 0.18f, 0.12f, 0.08f,
                                                 0.20f, 0.18f, 0.22f, 0.10f,
                                                 0.10f, 0.15f, 0.08f, 0.08f };

        if (peak > 0.0f)
            for (int frame = 0; frame < frames; ++frame)
                data[frame] = data[frame] / peak * trackPeaks[track];

        demoAssets[static_cast<size_t> (track)] = addAsset (std::move (asset));
    }
}

int AudioEngine::setTimelineClips (std::vector<TimelineClip> newClips)
{
    // The UI projection reads this mirror, so it only carries clips the engine
    // actually accepted -- a rejected asset must not appear on the timeline.
    timelineMirror.clear();

    Command clear;
    clear.type = CommandType::clear;

    if (! pushCommand (clear))
        return static_cast<int> (newClips.size());

    const auto cap = engine::truncateClipCount (static_cast<int> (newClips.size()), maxClips);
    int accepted = 0;
    double endBar = 1.0;

    for (const auto& clip : newClips)
    {
        if (accepted >= cap)
            break;

        if (clip.assetId < 0 || clip.assetId >= static_cast<int> (assets.size()))
            continue;

        if (! juce::isPositiveAndBelow (clip.track, trackCount))
            continue;

        Command add;
        add.type = CommandType::add;
        add.clip.asset = assets[static_cast<size_t> (clip.assetId)].get();
        add.clip.track = clip.track;
        add.clip.startBar = juce::jmax (1.0, clip.startBar);
        add.clip.lengthBars = juce::jmax (0.0, clip.lengthBars);
        add.clip.sourceOffsetSeconds = juce::jmax (0.0, clip.sourceOffsetSeconds);
        add.clip.gainDb = clip.gainDb;
        add.clip.fadeInBars = juce::jmax (0.0f, clip.fadeInBars);
        add.clip.fadeOutBars = juce::jmax (0.0f, clip.fadeOutBars);
        add.clip.crossfadeBars = juce::jmax (0.0f, clip.crossfadeBars);

        if (! pushCommand (add))
            break;

        timelineMirror.push_back (clip);
        endBar = juce::jmax (endBar, add.clip.startBar + add.clip.lengthBars);
        ++accepted;
    }

    contentEnd.store (endBar);

    return static_cast<int> (newClips.size()) - accepted;
}

std::vector<float> AudioEngine::assetPeakEnvelope (int assetId, double startSeconds,
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

bool AudioEngine::pushCommand (const Command& command)
{
    return commandQueue.tryPush (command);
}

void AudioEngine::processCommands()
{
    Command command;

    while (commandQueue.tryPop (command))
    {
        if (command.type == CommandType::clear)
        {
            playbackClipCount = 0;
        }
        else if (playbackClipCount < maxClips)
        {
            playbackClips[static_cast<size_t> (playbackClipCount)] = command.clip;
            ++playbackClipCount;
        }
    }
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
        auxReverb.setEngine (newEngine);
    }
    else if (aux == 1)
    {
        auxDelayEngine.store (static_cast<int> (newEngine));
        auxDelay.setEngine (newEngine);
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

void AudioEngine::reclaimRetiredSnapshots()
{
    // Called from the message thread, by the same poll that collects retired
    // inserts. With no device open there is no callback, so every retired
    // snapshot is safe to free. Otherwise a block runs only after the previous
    // one returned, so once a later block has started, any block that loaded an
    // old pointer before the swap has finished with it.
    const bool callbackRunning = deviceOpen.load (std::memory_order_acquire);
    const auto now = audioBlockCount.load (std::memory_order_acquire);

    auto sweep = [callbackRunning, now] (auto& list)
    {
        list.erase (std::remove_if (list.begin(), list.end(),
                                    [callbackRunning, now] (const auto& entry)
                                    {
                                        return ! callbackRunning
                                               || entry.retiredAtBlock + 2 <= now;
                                    }),
                    list.end());
    };

    sweep (automationRetired);
    sweep (tempoRetired);
    sweep (midiRetired);
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
}

void AudioEngine::renderBlock (const float* const* inputChannelData, int numInputChannels,
                               float* const* outputChannelData, int numOutputChannels, int numSamples)
{

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

            const auto frame1 = juce::jmin (frame0 + 1, sourceFrames - 1);
            const auto fraction = static_cast<float> (sourceFrame - static_cast<double> (frame0));

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
            // instead of stalling the callback on the disk.
            const auto start0 = sampleAt (*clip.asset, frame0, 0);
            const auto end0 = sampleAt (*clip.asset, frame1, 0);
            const auto start1 = sampleAt (*clip.asset, frame0, 1);
            const auto end1 = sampleAt (*clip.asset, frame1, 1);

            const auto sample0 = static_cast<float> (start0 + (end0 - start0) * fraction);
            const auto sample1 = static_cast<float> (start1 + (end1 - start1) * fraction);

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

                for (size_t clipIndex = 0; clipIndex < midiClipsPtr->size(); ++clipIndex)
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
    for (int sample = 0; sample < numSamples; ++sample)
    {
        float left = 0.0f;
        float right = 0.0f;
        float click = 0.0f;

        if (isPlaying)
        {
            for (int trackIndex = 0; trackIndex < trackCount; ++trackIndex)
            {
                const auto& track = tracks[static_cast<size_t> (trackIndex)];

                smoothedGain[trackIndex] += (automatedGain[trackIndex] - smoothedGain[trackIndex]) * smoothingCoefficient;
                smoothedPan[trackIndex] += (automatedPan[trackIndex] - smoothedPan[trackIndex]) * smoothingCoefficient;
                smoothedSendA[trackIndex] += (track.sendA.load() - smoothedSendA[trackIndex]) * smoothingCoefficient;
                smoothedSendB[trackIndex] += (track.sendB.load() - smoothedSendB[trackIndex]) * smoothingCoefficient;
                smoothedWidth[trackIndex] += (track.width.load() - smoothedWidth[trackIndex]) * smoothingCoefficient;

                const auto audible = ! track.muted.load() && (! anySolo || track.soloed.load());
                const auto gain = audible ? smoothedGain[trackIndex] : 0.0f;
                const auto angle = (smoothedPan[trackIndex] + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                const auto panLeft = std::cos (angle);
                const auto panRight = std::sin (angle);

                const auto sourceLeft = trackBufferReady
                                            ? trackBlock.getSample (trackIndex * 2, sample) : 0.0f;
                const auto sourceRight = trackBufferReady
                                             ? trackBlock.getSample (trackIndex * 2 + 1, sample) : 0.0f;

                auto trackLeft = sourceLeft * gain * panLeft;
                auto trackRight = sourceRight * gain * panRight;

                // The channel strip is baked into every track. It runs after
                // the inserts/PDC and before the bus sum.
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
                if (audible)
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

            if (clickOn && clickEnvelope > 0.0001)
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

        masterPeakLeft = mixed.getMagnitude (0, 0, numSamples);
        masterPeakRight = mixed.getMagnitude (1, 0, numSamples);
        masterPeak = juce::jmax (masterPeakLeft, masterPeakRight);

        for (int channel = 0; channel < juce::jmin (2, numOutputChannels); ++channel)
            if (outputChannelData[channel] != nullptr)
                juce::FloatVectorOperations::copy (outputChannelData[channel],
                                                   mixed.getReadPointer (channel), numSamples);
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
    if (! juce::isPositiveAndBelow (track, activeTrackCount.load()) || ! juce::isPositiveAndBelow (slot, 4))
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

                             if (instance == nullptr)
                             {
                                 setPluginStatus ("INSERT FAILED / " + creationError);
                                 return;
                             }

                             juce::String error;
                             auto hosted = HostedInsert::create (std::move (instance), rate, block, error);

                             if (hosted == nullptr)
                             {
                                 setPluginStatus ("INSERT FAILED / " + error);
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
                                 return;
                             }

                             setPluginStatus ("INSERT / " + description.name + " / TRACK "
                                               + juce::String (track + 1) + " / INS " + juce::String (slot + 1));
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
    if (! juce::isPositiveAndBelow (track, activeTrackCount.load()) || ! juce::isPositiveAndBelow (slot, 4))
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
    // session recorded an explicit override.
    applyVocalRoles();

    return true;
}

void AudioEngine::applyVocalRoles()
{
    const auto& tracks = sessionTracks();

    for (int track = 0; track < trackCount; ++track)
    {
        mixRack[static_cast<size_t> (track)]
            .setVocal (isVocalTrackName (tracks[static_cast<size_t> (track)].name));
        // Channel Strip is baked into every mixer track (never Aux or Master).
        // stripOn defaults to true; we re-assert it so a fresh track is never
        // silently un-stripped.
        mixRack[static_cast<size_t> (track)].setParam ("stripOn", 1.0f);
    }

    // Overlay Master is baked into the master channel (MasterRack).
    // Mixology is its default master shape (VOCAL FOCUS): eq on, one
    // subtractive low/mid cut, maximizer ceiling -1 dB.
    masterRack.setParam ("eqOn", 1.0f);
    masterRack.setParam ("lowFreq", 100.0f);
    masterRack.setParam ("lowGain", -1.5f);
    masterRack.setParam ("midFreq", 1000.0f);
    masterRack.setParam ("midGain", -1.0f);
    masterRack.setParam ("maxOn", 1.0f);
    masterRack.setParam ("ceiling", -1.0f);
}

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

void AudioEngine::setTrackAutomation (int track, AutomationParam param, const AutomationLane& lane)
{
    if (track < 0 || track >= trackCount)
        return;

    auto& slot = (param == AutomationParam::Pan ? panAutomation : volumeAutomation)
                     [static_cast<size_t> (track)];

    auto next = std::make_unique<AutomationLane> (lane);

    // Retired lanes are never freed while a block may still hold the pointer;
    // reclaimRetiredSnapshots() frees them once the callback that could have
    // loaded the old pointer has finished, or when the device closes.
    retireSnapshot (automationRetired, slot.owned,
                    audioBlockCount.load (std::memory_order_acquire));

    slot.owned = std::move (next);
    slot.live.store (slot.owned.get(), std::memory_order_release);
}

void AudioEngine::clearTrackAutomation (int track, AutomationParam param)
{
    setTrackAutomation (track, param, {});
}

AutomationLane AudioEngine::trackAutomation (int track, AutomationParam param) const
{
    if (track < 0 || track >= trackCount)
        return {};

    const auto& slot = (param == AutomationParam::Pan ? panAutomation : volumeAutomation)
                           [static_cast<size_t> (track)];
    const auto* lane = slot.live.load (std::memory_order_acquire);
    return lane != nullptr ? *lane : AutomationLane {};
}

float AudioEngine::automationValue (int track, AutomationParam param, double beats) const
{
    if (track < 0 || track >= trackCount)
        return 0.0f;

    const auto& slot = (param == AutomationParam::Pan ? panAutomation : volumeAutomation)
                           [static_cast<size_t> (track)];

    const auto* lane = slot.live.load (std::memory_order_acquire);
    return lane != nullptr ? lane->valueAt (beats) : 0.0f;
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
