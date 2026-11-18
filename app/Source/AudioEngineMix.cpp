// Channel summing, aux engines, rack state serialisation and vocal roles.
// Split out of AudioEngineRender.cpp; bodies moved verbatim.

#include "AudioEngine.h"
#include "AudioEngineInternal.h"
#include "Session.h"

#include <cmath>
#include <cstdint>

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

std::uint64_t AudioEngine::sessionFingerprint() const
{
    // Not a render hash: a cheap scan of every persisted mix value, used by the
    // unsaved-work check and the autosave debounce. Quantising to 1e-6 keeps
    // float noise out while still catching a knob move.
    auto hash = std::uint64_t { 0xcbf29ce484222325ull };
    const auto mix = [&hash] (std::uint64_t value)
    {
        hash ^= value;
        hash *= 0x100000001b3ull;
    };
    const auto quant = [] (double value) { return static_cast<std::uint64_t> (std::llround (value * 1.0e6)); };

    mix (static_cast<std::uint64_t> (activeTrackCount.load()));
    mix (quant (getBpm()));
    mix (quant (getLoopStartBar()));
    mix (quant (getLoopEndBar()));
    mix (static_cast<std::uint64_t> (markers().size()));
    mix (sessionRevision.load (std::memory_order_relaxed));
    mix (quant (masterGain()));
    mix (masterDim.load() ? 1u : 0u);
    mix (masterMono.load() ? 1u : 0u);

    for (int track = 0; track < trackCount; ++track)
    {
        mix (quant (trackGain (track)));
        mix (quant (trackPan (track)));
        mix (quant (trackSendA (track)));
        mix (quant (trackSendB (track)));
        mix (quant (trackWidth (track)));
        mix (trackMuted (track) ? 1u : 0u);
        mix (trackSolo (track) ? 1u : 0u);
        mix (automationRead (track) ? 1u : 0u);
        mix (static_cast<std::uint64_t> (trackInstrumentName (track).hashCode64()));
        mix (mixRack[static_cast<size_t> (track)].params.revision.load (std::memory_order_relaxed));

        for (int slot = 0; slot < 8; ++slot)
        {
            mix (static_cast<std::uint64_t> (trackInsertName (track, slot).hashCode64()));
            mix (trackInsertIsBypassed (track, slot) ? 1u : 0u);
        }
    }

    mix (masterRack.params.revision.load (std::memory_order_relaxed));
    mix (static_cast<std::uint64_t> (masterPluginName().hashCode64()));
    return hash;
}

} // namespace nc
