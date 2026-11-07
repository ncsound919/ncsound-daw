#include "MidiRecorder.h"

#include <algorithm>
#include <cmath>

namespace nc
{
MidiRecorder::MidiRecorder()
{
}

MidiRecorder::~MidiRecorder()
{
    closeDevice();
}

juce::Array<juce::MidiDeviceInfo> MidiRecorder::availableDevices()
{
    return juce::MidiInput::getAvailableDevices();
}

bool MidiRecorder::openDevice (const juce::String& deviceIdentifier, juce::String& error)
{
    closeDevice();
    input = juce::MidiInput::openDevice (deviceIdentifier, this);

    if (input == nullptr)
    {
        error = "Could not open MIDI device: " + deviceIdentifier;
        return false;
    }

    input->start();
    return true;
}

void MidiRecorder::closeDevice()
{
    if (input != nullptr)
    {
        input->stop();
        input.reset();
    }
}

void MidiRecorder::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
{
    // High-priority system thread: stamp and queue only.
    if (! recording.load (std::memory_order_relaxed))
        return;

    if (! queue.tryPush (message))
        dropped.fetch_add (1, std::memory_order_relaxed);
}

void MidiRecorder::startRecording (int trackToUse, double startPpqToUse,
                                   const nc::engine::TempoMap& tempoMapToUse)
{
    track = trackToUse;
    startPpq = startPpqToUse;
    tempoMap = tempoMapToUse;
    // MIDI timestamps are host-clock seconds, not transport seconds; keep
    // both bases so drain() can map one onto the other.
    hostBaseSeconds = juce::Time::highResolutionTicksToSeconds (
        juce::Time::getHighResolutionTicks());
    transportBaseSeconds = nc::engine::ppqToSeconds (tempoMap, startPpq);
    notes.clear();
    ccs.clear();
    bends.clear();
    openNotes.assign (16 * 128, {});
    lastBeat = 0.0;
    recording.store (true, std::memory_order_release);
}

bool MidiRecorder::drain()
{
    juce::MidiMessage message;

    while (queue.tryPop (message))
    {
        // Host timestamp (seconds) -> transport seconds -> PPQ.
        const auto transportSeconds = transportBaseSeconds
                                      + (message.getTimeStamp() - hostBaseSeconds);
        const auto ppq = nc::engine::secondsToPpq (tempoMap, transportSeconds);
        const auto beat = nc::engine::quantizeBeat (ppq - startPpq);
        lastBeat = juce::jmax (lastBeat, beat);

        if (message.isNoteOn())
        {
            auto& slot = openNotes[static_cast<size_t> (message.getChannel() * 128 + message.getNoteNumber())];
            slot.push_back ({ beat, message.getVelocity(), message.getChannel(), message.getNoteNumber() });
        }
        else if (message.isNoteOff())
        {
            auto& slot = openNotes[static_cast<size_t> (message.getChannel() * 128 + message.getNoteNumber())];
            if (slot.empty())
                continue;
            const auto started = slot.front();
            slot.erase (slot.begin());
            notes.push_back ({ started.beat,
                               juce::jmax (nc::engine::minMidiNoteBeats, beat - started.beat),
                               message.getNoteNumber(), started.velocity, started.channel });
        }
        else if (message.isController())
        {
            ccs.push_back ({ beat, message.getChannel(), message.getControllerNumber(),
                             message.getControllerValue() });
        }
        else if (message.isPitchWheel())
        {
            bends.push_back ({ beat, message.getChannel(), message.getPitchWheelValue() });
        }
    }

    return ! notes.empty() || ! ccs.empty() || ! bends.empty();
}

nc::engine::ClipState MidiRecorder::stopRecording()
{
    drain();
    recording.store (false, std::memory_order_release);

    // Hang notes still open at stop: close them at the last beat.
    for (auto& slot : openNotes)
    {
        for (const auto& started : slot)
            notes.push_back ({ started.beat,
                               juce::jmax (nc::engine::minMidiNoteBeats, lastBeat - started.beat),
                               started.pitch, started.velocity, started.channel });
        slot.clear();
    }

    nc::engine::ClipState clip;
    clip.isMidi = true;
    clip.name = "MIDI TAKE";
    clip.track = track;
    clip.startBar = 1.0 + startPpq / nc::engine::beatsPerBarMidi;
    clip.lengthBars = juce::jmax (0.25, (lastBeat / nc::engine::beatsPerBarMidi) + 0.25);
    clip.notes = notes;
    clip.ccs = ccs;
    clip.bends = bends;
    nc::engine::sanitizeNotes (clip.notes, nc::engine::clipLengthBeats (clip.lengthBars));
    nc::engine::sanitizeMidiCCs (clip.ccs, nc::engine::clipLengthBeats (clip.lengthBars));
    nc::engine::sanitizeMidiBends (clip.bends, nc::engine::clipLengthBeats (clip.lengthBars));
    return clip;
}
} // namespace nc
