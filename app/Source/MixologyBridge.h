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
} // namespace nc
