// Plugin catalogue, instrument and insert hosting, MIDI learn.
// Split out of AudioEngine.cpp (plan 07-03); bodies moved verbatim.

#include "AudioEngine.h"
#include "BuiltinStudioFx.h"
#include "BuiltinSynth.h"
#include <nc/engine/ProjectState.hpp>
#include <utility>

namespace nc
{
int AudioEngine::pollMidiLearnChanges()
{
    struct Range { float low, high; bool logarithmic = false; bool boolean = false; };
    auto rangeFor = [] (const juce::String& id, Range& out) -> bool
    {
        if (id == "stripOn" || id == "vocal" || id == "openMicOn" || id == "eqOn"
            || id == "hpfOn" || id == "lpfOn" || id == "compOn" || id == "satOn")
            { out = { 0.0f, 1.0f, false, true }; return true; }
        if (id == "openMicDrive") { out = { -12.0f, 24.0f }; return true; }
        if (id == "openMicMix") { out = { 0.0f, 1.0f }; return true; }
        if (id == "openMicTrim") { out = { -24.0f, 24.0f }; return true; }
        if (id == "hpfFreq") { out = { 20.0f, 500.0f, true }; return true; }
        if (id == "lpfFreq") { out = { 1000.0f, 20000.0f, true }; return true; }
        if (id == "lowFreq") { out = { 20.0f, 400.0f, true }; return true; }
        if (id == "lowGain") { out = { -18.0f, 18.0f }; return true; }
        if (id == "lmfFreq") { out = { 150.0f, 3000.0f, true }; return true; }
        if (id == "lmfGain") { out = { -18.0f, 18.0f }; return true; }
        if (id == "lmfQ") { out = { 0.2f, 10.0f, true }; return true; }
        if (id == "hmfFreq") { out = { 1000.0f, 12000.0f, true }; return true; }
        if (id == "hmfGain") { out = { -18.0f, 18.0f }; return true; }
        if (id == "hmfQ") { out = { 0.2f, 10.0f, true }; return true; }
        if (id == "highFreq") { out = { 5000.0f, 20000.0f, true }; return true; }
        if (id == "highGain") { out = { -18.0f, 18.0f }; return true; }
        if (id == "compThresh") { out = { -60.0f, 0.0f }; return true; }
        if (id == "compRatio") { out = { 1.0f, 20.0f, true }; return true; }
        if (id == "compAttack") { out = { 0.1f, 200.0f, true }; return true; }
        if (id == "compRelease") { out = { 10.0f, 1000.0f, true }; return true; }
        if (id == "compMakeup") { out = { -12.0f, 24.0f }; return true; }
        if (id == "satDrive") { out = { 0.0f, 24.0f }; return true; }
        if (id == "satMix") { out = { 0.0f, 1.0f }; return true; }
        if (id == "outputDb") { out = { -24.0f, 12.0f }; return true; }
        return false;
    };
    auto mapped = [] (const Range& range, float norm) -> float
    {
        if (range.boolean)
            return norm >= 0.5f ? 1.0f : 0.0f;
        if (range.logarithmic)
            return range.low * std::pow (range.high / range.low, norm);
        return range.low + norm * (range.high - range.low);
    };
    auto normalised = [] (const Range& range, float real) -> float
    {
        if (range.boolean)
            return real >= 0.5f ? 1.0f : 0.0f;
        if (range.logarithmic)
            return std::clamp (std::log (juce::jmax (range.low, real) / range.low)
                               / std::log (range.high / range.low), 0.0f, 1.0f);
        return std::clamp ((real - range.low) / (range.high - range.low), 0.0f, 1.0f);
    };

    int applied = 0;
    MidiLearnManager::Change change;
    while (midiLearn.takeChange (change))
    {
        const auto target = juce::String (change.binding.targetParamId);
        if (target == "transport.play")
        {
            setPlaying (change.rawValue > 0);
            ++applied;
            continue;
        }
        if (target == "transport.stop")
        {
            setPlaying (false);
            ++applied;
            continue;
        }
        int track = midiLearnTrack;
        juce::String parameter = target;
        if (target.startsWith ("track."))
        {
            const auto rest = target.substring (6);
            track = rest.upToFirstOccurrenceOf (".", false, false).getIntValue();
            parameter = rest.fromFirstOccurrenceOf (".", false, false);
        }
        if (! juce::isPositiveAndBelow (track, getActiveTrackCount()))
            continue;
        Range range;
        if (! rangeFor (parameter, range))
            continue;
        auto& channel = mixChannel (track);
        float nextNorm;
        if (change.binding.mode == MidiLearnManager::Mode::toggle)
            nextNorm = channel.getParam (parameter) >= 0.5f ? 0.0f : 1.0f;
        else if (change.binding.mode == MidiLearnManager::Mode::relative)
        {
            int delta = change.rawValue < 64 ? change.rawValue : change.rawValue - 128;
            nextNorm = std::clamp (normalised (range, channel.getParam (parameter)) + delta / 128.0f,
                                   0.0f, 1.0f);
        }
        else
            nextNorm = std::clamp (change.rawValue / 127.0f, 0.0f, 1.0f);
        channel.setParam (parameter, mapped (range, nextNorm));
        ++applied;
    }
    return applied;
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
    // Eight user slots per track, matching the rack and the editor's two banks.
    // The old four-slot cap rejected every load from bank B.
    if (! juce::isPositiveAndBelow (track, activeTrackCount.load())
        || ! juce::isPositiveAndBelow (slot, kInsertSlotsPerTrack))
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

                             // The instance only exists now, on this thread. The view
                             // has to be told, or the slot keeps reading "empty" after
                             // a load that actually worked.
                             const auto announce = [this, alive]
                             {
                                 juce::MessageManager::callAsync ([this, alive]
                                 {
                                     if (alive->load (std::memory_order_acquire)
                                         && onInsertInstalled != nullptr)
                                         onInsertInstalled();
                                 });
                             };

                             if (instance == nullptr)
                             {
                                 setPluginStatus ("INSERT FAILED / " + creationError);
                                 announce();
                                 return;
                             }

                             juce::String error;
                             auto hosted = HostedInsert::create (std::move (instance), rate, block, error);

                             if (hosted == nullptr)
                             {
                                 setPluginStatus ("INSERT FAILED / " + error);
                                 announce();
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
                                 announce();
                                 return;
                             }

                             setPluginStatus ("INSERT / " + description.name + " / TRACK "
                                               + juce::String (track + 1) + " / INS " + juce::String (slot + 1));
                             announce();
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
    if (! juce::isPositiveAndBelow (track, activeTrackCount.load())
        || ! juce::isPositiveAndBelow (slot, kInsertSlotsPerTrack))
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

bool AudioEngine::trackInsertIsBypassed (int track, int slot) const
{
    return inserts.isBypassed (track, slot);
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
} // namespace nc
