#include "MidiLearnManager.h"

namespace nc
{
MidiLearnManager::MidiLearnManager() = default;
MidiLearnManager::~MidiLearnManager() = default;

juce::Array<juce::MidiDeviceInfo> MidiLearnManager::availableDevices()
{
    return juce::MidiInput::getAvailableDevices();
}

bool MidiLearnManager::openDevice (const juce::String& id, juce::String& error)
{
    // Independent: opening input must not tear down an output.
    closeInput();
    input = juce::MidiInput::openDevice (id, this);
    if (input == nullptr) { error = "MIDI input open failed"; return false; }
    deviceId = id;
    input->start();
    return true;
}

bool MidiLearnManager::openOutput (const juce::String& id, juce::String& error)
{
    // Independent: opening output must not tear down an input.
    closeOutput();
    output = juce::MidiOutput::openDevice (id);
    if (output == nullptr) { error = "MIDI output open failed"; return false; }
    outputId = id;
    return true;
}

void MidiLearnManager::closeDevices()
{
    closeInput();
    closeOutput();
}

void MidiLearnManager::closeInput()
{
    deviceId.clear();
    if (input) { input->stop(); input.reset(); }
}

void MidiLearnManager::closeOutput()
{
    outputId.clear();
    output.reset();
}

void MidiLearnManager::armLearn (const std::string& paramId, Mode mode)
{
    juce::ScopedLock scopedLock (lock);
    Binding b;
    b.targetParamId = paramId;
    b.mode = mode;
    learnTarget = b;
}

void MidiLearnManager::cancelLearn()
{
    juce::ScopedLock scopedLock (lock);
    learnTarget.reset();
}

bool MidiLearnManager::consumeNextCc (int channel, int controller, int value, bool isNote)
{
    Binding target;
    {
        juce::ScopedLock scopedLock (lock);
        if (! learnTarget.has_value())
            return false;
        target = *learnTarget;
        learnTarget.reset();
    }
    target.midiChannel = channel;
    target.controller = controller;
    target.isNote = isNote;
    {
        juce::ScopedLock scopedLock (lock);
        table[{channel, controller, isNote}] = target;
    }
    juce::ignoreUnused (value);
    return true;
}

std::optional<MidiLearnManager::Binding> MidiLearnManager::bindingFor (int channel, int controller, bool isNote) const
{
    juce::ScopedLock scopedLock (lock);
    auto it = table.find ({channel, controller, isNote});
    if (it != table.end()) return it->second;
    auto omni = table.find ({0, controller, isNote});
    return omni != table.end() ? std::optional<Binding> (omni->second) : std::nullopt;
}

void MidiLearnManager::addBinding (const Binding& binding)
{
    juce::ScopedLock scopedLock (this->lock);
    table[{binding.midiChannel, binding.controller, binding.isNote}] = binding;
}

void MidiLearnManager::removeBinding (int channel, int controller, bool isNote)
{
    juce::ScopedLock scopedLock (this->lock);
    table.erase ({channel, controller, isNote});
}

std::vector<MidiLearnManager::Binding> MidiLearnManager::bindings() const
{
    juce::ScopedLock scopedLock (lock);
    std::vector<Binding> out;
    for (auto& kv : table) out.push_back (kv.second);
    return out;
}

bool MidiLearnManager::takeChange (Change& change)
{
    juce::ScopedLock scopedLock (lock);
    if (pendingChanges.empty())
        return false;
    change = pendingChanges.front();
    pendingChanges.pop();
    return true;
}

void MidiLearnManager::saveToFile (const juce::File& file) const
{
    // Heap object: juce::var (DynamicObject*) takes ownership and deletes it.
    // Wrapping a stack object here made saveToFile delete stack memory.
    auto* root = new juce::DynamicObject();
    juce::Array<juce::var> list;
    for (auto& b : bindings())
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("channel", b.midiChannel);
        o->setProperty ("controller", b.controller);
        o->setProperty ("isNote", b.isNote);
        o->setProperty ("mode", (int) b.mode);
        o->setProperty ("target", juce::String (b.targetParamId));
        list.add (juce::var (o));
    }
    root->setProperty ("mappings", juce::var (list));
    file.replaceWithText (juce::JSON::toString (juce::var (root), true));
}

bool MidiLearnManager::loadFromFile (const juce::File& file)
{
    if (! file.existsAsFile()) return false;
    auto parsed = juce::JSON::parse (file.loadFileAsString());
    if (! parsed.isObject()) return false;
    auto* obj = parsed.getDynamicObject();
    auto arr = obj->getProperty ("mappings");
    if (! arr.isArray()) return false;
    juce::ScopedLock scopedLock (this->lock);
    table.clear();
    for (auto& varItem : *arr.getArray())
    {
        if (! varItem.isObject()) continue;
        auto* o = varItem.getDynamicObject();
        Binding b;
        b.midiChannel = (int) o->getProperty ("channel");
        b.controller = (int) o->getProperty ("controller");
        b.isNote = (bool) o->getProperty ("isNote");
        b.mode = (Mode) (int) o->getProperty ("mode");
        b.targetParamId = o->getProperty ("target").toString().toStdString();
        table[{b.midiChannel, b.controller, b.isNote}] = b;
    }
    return true;
}

void MidiLearnManager::loadBuiltInTemplate (const std::string& name)
{
    // Build into a temporary table first so an unknown name is a no-op.
    std::map<std::tuple<int,int,bool>, Binding> next;
    auto add = [&](int ch, int ctrl, const std::string& target, bool isNote = false)
    {
        Binding b; b.midiChannel = ch; b.controller = ctrl; b.isNote = isNote; b.targetParamId = target;
        next[{ch, ctrl, isNote}] = b;
    };
    if (name == "Push 3")
    {
        add (0, 74, "track.0.hpfFreq");
        add (0, 75, "track.0.hmfQ");
        add (0, 71, "track.0.volume");
    }
    else if (name == "Traktor S4")
    {
        add (0, 20, "transport.play");
        add (0, 21, "transport.stop");
    }
    else if (name == "LaunchControl XL")
    {
        for (int i = 0; i < 8; ++i) add (0, 13 + i, "track." + std::to_string (i) + ".volume");
    }
    else if (name == "DDJ-1000")
    {
        add (0, 46, "transport.play");
        add (0, 47, "transport.stop");
    }
    else
    {
        return;
    }
    juce::ScopedLock scopedLock (lock);
    table = std::move (next);
}

void MidiLearnManager::sendLed (int channel, int controller, int value)
{
    if (output != nullptr)
        output->sendMessageNow (juce::MidiMessage::controllerEvent (channel + 1, controller, value));
}

void MidiLearnManager::sendAllLeds (const std::map<int,int>& state)
{
    if (output == nullptr) return;
    for (auto& kv : state)
        output->sendMessageNow (juce::MidiMessage::controllerEvent (1, kv.first, kv.second));
}

void MidiLearnManager::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& msg)
{
    int channel = msg.getChannel() - 1;
    int controller = -1;
    int value = 0;
    bool isNote = false;
    // Note-offs are ignored: a note binding applies on the attack only, so a
    // toggle bound to a pad stays engaged and an absolute binding does not
    // drop to zero every time the key is released.
    if (msg.isController())
    {
        controller = msg.getControllerNumber();
        value = msg.getControllerValue();
    }
    else if (msg.isNoteOn())
    {
        isNote = true;
        controller = msg.getNoteNumber();
        value = msg.getVelocity();
    }
    else
    {
        return;
    }

    if (consumeNextCc (channel, controller, value, isNote))
        return;
    if (auto binding = bindingFor (channel, controller, isNote))
    {
        juce::ScopedLock scopedLock (lock);
        pendingChanges.push ({ *binding, value });
    }
}
} // namespace nc
