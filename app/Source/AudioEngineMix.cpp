// Channel summing, aux engines, rack state serialisation and vocal roles.
// Split out of AudioEngineRender.cpp; bodies moved verbatim.

#include "AudioEngine.h"
#include "AudioEngineInternal.h"
#include "Session.h"

namespace nc
{

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

} // namespace nc
