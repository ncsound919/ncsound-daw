#pragma once

#include <juce_core/juce_core.h>

namespace nc
{
class AudioEngine;

struct MixologyReport
{
    int applied = 0;
    int skipped = 0;
    juce::String text;

    bool ok() const { return applied > 0; }
};

juce::StringArray mixologyPresetNames();
MixologyReport applyMixologyPreset (AudioEngine& engine, int preset);

/** Result of one autonomous pass. `summary` is the evidence sentence the UI
    shows under the report; `text` is the one-line status. */
struct MixologyAutopilotReport
{
    bool ok = false;
    int applied = 0;
    int skipped = 0;
    juce::String text;
    juce::String summary;
    juce::String assessment;
};

/**
    Measure a mix with Earshot and apply a bounded Overlay Channel Strip /
    Overlay Master first pass.

    - `audioPath` may be empty, in which case the current session is rendered
      to a temporary WAV first.
    - `referencePath` is optional; when present the bands are matched to the
      reference instead of the built-in song-ready target curve.
    - Earshot is located through `EARSHOT_HOME` / `EARSHOT_PYTHON`, defaulting
      to `D:/assets/earshot-main` and falling back to `C:/assets/earshot-main`.
    - When `NC_MIXOLOGY_SERVER` is set, the Mixology server pipeline is used
      instead (Dev-Brain ranking/governance included); otherwise this native
      Earshot path is the offline fallback.

    Message thread only. The render and the Earshot process are synchronous;
    call this from an explicit user action, not a timer.
*/
MixologyAutopilotReport runMixologyAutopilot (AudioEngine& engine,
                                             const juce::String& brief,
                                             const juce::String& audioPath,
                                             const juce::String& referencePath,
                                             double depth,
                                             double targetLufs);
} // namespace nc
