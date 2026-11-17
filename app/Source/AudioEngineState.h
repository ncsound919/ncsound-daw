#pragma once

#include "Automation.h"
#include "TransportClock.h"

#include <nc/engine/ProjectLimits.hpp>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace nc
{
/** A published lane: the audio thread reads `live`, the message thread owns
    `owned`. Replaced lanes are retired and freed once no callback can still
    be reading them, or when the device stops. */
struct AutomationSlot
{
    std::unique_ptr<AutomationLane> owned;
    std::atomic<const AutomationLane*> live { nullptr };
};

/** Per-track automation: one published lane each for volume and pan. */
struct AudioEngineState
{
    std::array<AutomationSlot, nc::limits::maxTracks> volumeAutomation;
    std::array<AutomationSlot, nc::limits::maxTracks> panAutomation;
    /** Defaults to true; the constructor fills it, since a value-initialised
        atomic<bool> would otherwise start at false and silently mute every
        lane in the session. */
    std::array<std::atomic<bool>, nc::limits::maxTracks> automationReadEnabled {};
    std::vector<RetiredSnapshot<AutomationLane>> automationRetired;
};
} // namespace nc
