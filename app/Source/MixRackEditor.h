#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "AudioEngine.h"
#include "Widgets.h"

namespace nc
{
/** A compact pop-out for the baked rack. Track mode edits the channel strip
    and Open Mic stage; master mode edits the Overlay Master chain, maximizer
    and automix target. */
class MixRackEditor final : public juce::Component
{
public:
    MixRackEditor (AudioEngine& engineToUse, int trackIndex);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void setupTrack();
    void setupMaster();
    void refresh();

    AudioEngine& engine;
    int track = 0;
    bool master = false;

    std::array<Knob, 6> knobs;
    std::array<HardwareButton, 4> toggles;

    static constexpr int knobCount = 6;
    static constexpr int toggleCount = 4;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixRackEditor)
};
} // namespace nc
