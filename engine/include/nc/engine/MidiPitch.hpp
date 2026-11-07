#pragma once

#include <nc/engine/MidiSchedule.hpp>
#include <nc/engine/PitchStack.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace nc::engine
{
    // Apply a PitchStack to scheduled MIDI events in place.
    //
    // Transposes noteOn/noteOff pitches by wholeSt (transpose + rounded
    // fine/Hz cents), drops out-of-range notes, and injects / merges a
    // pitch-bend for the remainder (-100..100c mapped to ±2st bend range).
    // CC events pass through. Existing bend events have the remainder added
    // (clamped 0..16383). No allocation beyond the output vector.
    //
    // Idempotent per block: caller snapshots the PitchStack at block start
    // so a transpose change mid-note only affects the next block.
    inline void applyPitch (std::vector<MidiEvent>& events, const PitchStack& stack)
    {
        PitchStack p = stack;
        p.clamp();
        const float fineCents = p.pitchCents + PitchStack::hzToCents (p.tuningHz);
        if (p.transpose == 0 && std::abs (fineCents) < 0.5f)
            return;

        const int wholeSt = p.transpose + static_cast<int> (std::round (fineCents / 100.0f));
        const float remCents = fineCents - static_cast<float> (wholeSt - p.transpose) * 100.0f;
        const bool needBend = std::abs (remCents) >= 0.5f;
        int bendValue = 8192;
        if (needBend)
        {
            const float bendSt = remCents / 100.0f; // -1..1 semitone
            const float norm = bendSt / 2.0f; // -0.5..0.5 of +-2st range
            bendValue = 8192 + static_cast<int> (std::round (norm * 8192.0f));
            bendValue = std::clamp (bendValue, 0, 16383);
        }

        std::vector<MidiEvent> out;
        out.reserve (events.size() + 1);
        bool hasBend = false;

        for (const auto& ev : events)
        {
            if (ev.kind == MidiEvent::Kind::noteOn || ev.kind == MidiEvent::Kind::noteOff)
            {
                const int note = ev.pitch + wholeSt;
                if (note < 0 || note > 127)
                    continue; // drop out-of-range
                MidiEvent e = ev;
                e.pitch = note;
                out.push_back (e);
            }
            else if (ev.kind == MidiEvent::Kind::bend)
            {
                MidiEvent e = ev;
                e.value = std::clamp (ev.value + (bendValue - 8192), 0, 16383);
                hasBend = true;
                out.push_back (e);
            }
            else
            {
                out.push_back (ev);
            }
        }

        if (needBend && ! hasBend)
        {
            MidiEvent b;
            b.kind = MidiEvent::Kind::bend;
            b.channel = 0;
            b.value = bendValue;
            b.sampleOffset = 0;
            out.push_back (b);
            // Keep bends-first ordering at same offset as in MidiSchedule.
            std::sort (out.begin(), out.end(), [] (const MidiEvent& a, const MidiEvent& b)
            {
                if (a.sampleOffset != b.sampleOffset)
                    return a.sampleOffset < b.sampleOffset;
                return static_cast<int> (a.kind) < static_cast<int> (b.kind);
            });
        }

        events = std::move (out);
    }

    // Convenience: apply a pre-summed total cents (for clip/global paths that
    // already folded transpose into cents). Use applyPitch with a stack instead
    // when you have the structured scopes.
    inline void applyTotalCents (std::vector<MidiEvent>& events, float totalCents)
    {
        PitchStack p;
        p.transpose = 0;
        p.pitchCents = std::clamp (totalCents, -1200.0f, 1200.0f);
        p.tuningHz = PitchStack::kHzDefault;
        applyPitch (events, p);
    }
} // namespace nc::engine
