// Transport, tempo map, markers, MIDI clips, timeline commands and automation.
// Split out of AudioEngine.cpp (plan 07-03); bodies moved verbatim.

#include "AudioEngine.h"
#include <nc/engine/MidiSchedule.hpp>
#include <nc/engine/TempoMap.hpp>
#include <algorithm>
#include <utility>

namespace nc
{
void AudioEngine::setPlaying (bool shouldPlay)    { clock.setPlaying (shouldPlay); }

void AudioEngine::setMetronome (bool on)          { clock.setMetronome (on); }

void AudioEngine::setBpm (double newBpm)
{
    clock.setBpm (newBpm);
}

void AudioEngine::setTempoMap (nc::engine::TempoMap map)
{
    clock.setTempoMap (std::move (map));
}

nc::engine::TempoMap AudioEngine::tempoMap() const
{
    return clock.tempoMap();
}

void AudioEngine::setMarkers (std::vector<nc::engine::Marker> markers)
{
    clock.setMarkers (std::move (markers));
}

void AudioEngine::setMidiClips (std::vector<MidiTimelineClip> clips)
{
    // The audio callback indexes midiCursors, a fixed array of maxClips
    // entries; a snapshot longer than that would read past the array on
    // every block. The project sanitiser applies the same ceiling, so this
    // only rejects hand-built snapshots that bypass it.
    if (clips.size() > (std::size_t) nc::limits::maxClips)
        clips.resize ((std::size_t) nc::limits::maxClips);

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
                    clock.blockCount());

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
    if (static_cast<int> (clips.size()) >= maxClips)
        return -1;
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

    for (const auto& marker : clock.markers())
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

nc::engine::TransportBlock AudioEngine::transportBlock (int blockSize) const
{
    return clock.block (sampleRate.load(), blockSize);
}

double AudioEngine::positionSeconds() const
{
    return clock.positionSeconds (sampleRate.load());
}

void AudioEngine::resetPosition()
{
    clock.resetPosition();
}

void AudioEngine::setPositionSeconds (double seconds)
{
    clock.setPositionSeconds (seconds, sampleRate.load());
}

void AudioEngine::setLoopEnabled (bool on)
{
    clock.setLoopEnabled (on);
}

void AudioEngine::setLoopBars (double startBar, double endBar)
{
    clock.setLoopBars (startBar, endBar);
}

int AudioEngine::setTimelineClips (std::vector<TimelineClip> newClips)
{
    timelineError.clear();

    Command clear;
    clear.type = CommandType::clear;

    // The UI projection reads this mirror, so a refused publish must leave the
    // previous schedule visible instead of painting an empty timeline.
    if (! pushCommand (clear))
    {
        timelineError = "Timeline command queue is full";
        return static_cast<int> (newClips.size());
    }

    timelineMirror.clear();
    const auto cap = engine::truncateClipCount (static_cast<int> (newClips.size()), maxClips);
    int accepted = 0;
    double endBar = 1.0;

    for (const auto& clip : newClips)
    {
        if (accepted >= cap)
            break;

        const auto* asset = assetLibrary.find (clip.assetId);

        if (asset == nullptr)
            continue;

        if (! juce::isPositiveAndBelow (clip.track, trackCount))
            continue;

        Command add;
        add.type = CommandType::add;
        add.clip.asset = asset;
        add.clip.track = clip.track;
        add.clip.startBar = juce::jmax (1.0, clip.startBar);
        add.clip.lengthBars = juce::jmax (0.0, clip.lengthBars);
        add.clip.sourceOffsetSeconds = juce::jmax (0.0, clip.sourceOffsetSeconds);
        add.clip.gainDb = clip.gainDb;
        add.clip.fadeInBars = juce::jmax (0.0f, clip.fadeInBars);
        add.clip.fadeOutBars = juce::jmax (0.0f, clip.fadeOutBars);
        add.clip.crossfadeBars = juce::jmax (0.0f, clip.crossfadeBars);

        if (! pushCommand (add))
        {
            timelineError = "Timeline command queue filled before all clips were scheduled";
            break;
        }

        timelineMirror.push_back (clip);
        endBar = juce::jmax (endBar, add.clip.startBar + add.clip.lengthBars);
        ++accepted;
    }

    contentEnd.store (endBar);

    const auto dropped = static_cast<int> (newClips.size()) - accepted;

    if (dropped > 0 && timelineError.isEmpty())
        timelineError = "Some clips were outside the timeline limits";

    return dropped;
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

void AudioEngine::reclaimRetiredSnapshots()
{
    // Called from the message thread, by the same poll that collects retired
    // inserts. With no device open there is no callback, so every retired
    // snapshot is safe to free. Otherwise a block runs only after the previous
    // one returned, so once a later block has started, any block that loaded an
    // old pointer before the swap has finished with it.
    const bool callbackRunning = deviceOpen.load (std::memory_order_acquire);
    const auto now = clock.blockCount();

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
    sweep (midiRetired);
    clock.reclaimTempo (now, callbackRunning);
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
                    clock.blockCount());

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

    if (! automationRead (track))
        return 0.0f;

    const auto& slot = (param == AutomationParam::Pan ? panAutomation : volumeAutomation)
                           [static_cast<size_t> (track)];

    const auto* lane = slot.live.load (std::memory_order_acquire);
    return lane != nullptr ? lane->valueAt (beats) : 0.0f;
}

void AudioEngine::setAutomationRead (int track, bool enabled)
{
    if (track < 0 || track >= trackCount)
        return;

    automationReadEnabled[static_cast<size_t> (track)].store (enabled, std::memory_order_release);
}

bool AudioEngine::automationRead (int track) const
{
    return track >= 0 && track < trackCount
               ? automationReadEnabled[static_cast<size_t> (track)].load (std::memory_order_acquire)
               : true;
}

} // namespace nc
