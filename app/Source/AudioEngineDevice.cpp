// Audio-interface settings: persistence, latency reporting, low-latency mode.
// Split out of AudioEngine.cpp (plan 07-03); bodies moved verbatim.

#include "AudioEngine.h"
#include "AudioEngineInternal.h"

namespace nc
{
void AudioEngine::saveAudioSettings()
{
    juce::ApplicationProperties props;
    props.setStorageParameters (audioPropsOptions());
    auto xml = deviceManager.createStateXml();
    // A null state means no device opened this session. That is usually a
    // transient failure at exit, not a deliberate removal, so keep the last
    // good setup: restoreAudioSettings falls back to default devices when
    // the stored one cannot be initialised.
    if (xml == nullptr)
        return;
    xml->setAttribute ("midiInput", midiRecorder.activeDeviceId());
    xml->setAttribute ("midiLearnInput", midiLearn.activeDeviceId());
    xml->setAttribute ("midiLearnOutput", midiLearn.activeOutputId());
    props.getUserSettings()->setValue ("audioDeviceSetup", xml.get());
    props.saveIfNeeded();
}

void AudioEngine::restoreAudioSettings()
{
    juce::ApplicationProperties props;
    props.setStorageParameters (audioPropsOptions());
    auto stored = props.getUserSettings()->getXmlValue ("audioDeviceSetup");
    if (stored == nullptr)
        return;
    auto error = deviceManager.initialise (2, 2, stored.get(), true);
    if (error.isNotEmpty())
        error = deviceManager.initialiseWithDefaultDevices (2, 2);
    if (error.isNotEmpty())
        error = deviceManager.initialiseWithDefaultDevices (0, 2);

    if (error.isEmpty())
    {
        auto midiId = stored->getStringAttribute ("midiInput");
        if (midiId.isNotEmpty())
        {
            juce::String ignored;
            openMidiDevice (midiId, ignored);
        }
        auto learnId = stored->getStringAttribute ("midiLearnInput");
        if (learnId.isNotEmpty())
        {
            juce::String ignored;
            midiLearn.openDevice (learnId, ignored);
        }
        auto learnOutputId = stored->getStringAttribute ("midiLearnOutput");
        if (learnOutputId.isNotEmpty())
        {
            juce::String ignored;
            midiLearn.openOutput (learnOutputId, ignored);
        }
    }
}

void AudioEngine::clearAudioSettings()
{
    juce::ApplicationProperties props;
    props.setStorageParameters (audioPropsOptions());
    props.getUserSettings()->removeValue ("audioDeviceSetup");
    props.saveIfNeeded();
}

juce::String AudioEngine::audioSettingsSummary() const
{
    return deviceInfo + "@" + juce::String (currentSampleRate(), 0) + "Hz," + juce::String (currentBufferSize()) + "smp";
}

juce::var AudioEngine::audioStateToVar()
{
    auto* o = new juce::DynamicObject();
    const auto setup = deviceManager.getAudioDeviceSetup();
    o->setProperty ("deviceType", deviceManager.getCurrentAudioDeviceType());
    o->setProperty ("outputDevice", setup.outputDeviceName);
    o->setProperty ("sampleRate", setup.sampleRate);
    o->setProperty ("bufferSize", setup.bufferSize);
    o->setProperty ("outputChannels", setup.outputChannels.countNumberOfSetBits());
    o->setProperty ("inputChannels", setup.inputChannels.countNumberOfSetBits());
    o->setProperty ("open", isDeviceOpen());
    o->setProperty ("currentRate", currentSampleRate());
    o->setProperty ("currentBuffer", currentBufferSize());
    o->setProperty ("device", deviceDescription());
    o->setProperty ("overruns", 0.0);
    o->setProperty ("blocks", 0.0);

    juce::Array<juce::var> types;
    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        if (type == nullptr)
            continue;

        type->scanForDevices();
        auto* t = new juce::DynamicObject();
        t->setProperty ("name", type->getTypeName());
        juce::Array<juce::var> outputs;
        for (const auto& name : type->getDeviceNames (false))
            outputs.add (name);
        t->setProperty ("outputs", outputs);
        types.add (juce::var (t));
    }
    o->setProperty ("types", types);
    return juce::var (o);
}

juce::var AudioEngine::applyAudioSettings (const juce::String& deviceType,
                                           const juce::String& outputDevice,
                                           double sampleRate, int bufferSize)
{
    juce::String error;
    juce::String note;

    if (deviceType.isNotEmpty())
    {
        juce::AudioIODeviceType* chosen = nullptr;
        for (auto* type : deviceManager.getAvailableDeviceTypes())
            if (type != nullptr && type->getTypeName() == deviceType)
                chosen = type;

        if (chosen == nullptr)
        {
            error = "Unknown audio driver: " + deviceType;
        }
        else if (deviceManager.getCurrentAudioDeviceType() != deviceType)
        {
            deviceManager.setCurrentAudioDeviceType (deviceType, true);
            if (deviceManager.getCurrentAudioDeviceType() != deviceType)
                error = "Could not select " + deviceType;
        }
    }

    if (error.isEmpty())
    {
        auto setup = deviceManager.getAudioDeviceSetup();

        if (auto* type = deviceManager.getCurrentDeviceTypeObject())
        {
            type->scanForDevices();
            const auto outputs = type->getDeviceNames (false);

            if (outputDevice.isNotEmpty())
            {
                if (outputs.contains (outputDevice))
                    setup.outputDeviceName = outputDevice;
                else
                    error = "No output device named " + outputDevice;
            }
            else if (! outputs.contains (setup.outputDeviceName) && ! outputs.isEmpty())
            {
                // Switching driver leaves the previous driver's device name
                // behind; use this driver's first output rather than fail.
                setup.outputDeviceName = outputs[0];
            }
        }

        if (error.isEmpty())
        {
            if (sampleRate > 0.0)
                setup.sampleRate = sampleRate;
            if (bufferSize > 0)
                setup.bufferSize = bufferSize;

            // Let the device choose its default channel set. The saved
            // DirectSound setup asked a stereo codec for 11 channels, which is
            // the kind of request that fails silently and leaves no audio.
            setup.useDefaultOutputChannels = true;
            setup.useDefaultInputChannels = true;

            const auto err = deviceManager.setAudioDeviceSetup (setup, true);
            if (err.isNotEmpty())
                error = err;
            else if (! isDeviceOpen())
                error = "The audio device did not open.";
        }
    }

    if (error.isEmpty())
    {
        const auto actual = deviceManager.getAudioDeviceSetup();
        if (sampleRate > 0.0 && std::abs (actual.sampleRate - sampleRate) > 0.5)
            note = "Device runs at " + juce::String (actual.sampleRate, 0) + " Hz.";
        if (bufferSize > 0 && actual.bufferSize != bufferSize)
            note << (note.isEmpty() ? "" : " ")
                 << "Buffer is " << actual.bufferSize << " samples.";
        saveAudioSettings();
    }

    auto* o = new juce::DynamicObject();
    o->setProperty ("ok", error.isEmpty());
    o->setProperty ("error", error);
    o->setProperty ("note", note);
    o->setProperty ("state", audioStateToVar());
    return juce::var (o);
}

double AudioEngine::bufferLatencyMs() const
{
    const double rate = currentSampleRate();
    if (rate <= 0.0)
        return 0.0;
    int samples = currentBufferSize();
    if (auto* device = deviceManager.getCurrentAudioDevice())
        samples = juce::jmax (samples, device->getOutputLatencyInSamples());
    return (double) samples * 1000.0 / rate;
}

double AudioEngine::worstTrackLatencyMs() const
{
    const double rate = currentSampleRate();
    const int worst = inserts.worstTrackLatency(); // samples, capped at 16384 in TrackInsertRack
    return rate > 0.0 ? (double) worst * 1000.0 / rate : 0.0;
}

double AudioEngine::masterLatencyMs() const
{
    const double rate = currentSampleRate();
    const int lat = masterRack.latencySamples() + inserts.masterLatency();
    return rate > 0.0 ? (double) lat * 1000.0 / rate : 0.0;
}

void AudioEngine::setLowLatencyMode (bool shouldLow)
{
    auto setup = deviceManager.getAudioDeviceSetup();
    if (shouldLow)
    {
        if (lowLatency.load())
            return;
        previousBufferSize.store (setup.bufferSize);
        previousSampleRate.store (setup.sampleRate);
        setup.bufferSize = juce::jmin (setup.bufferSize, 64);
    }
    else
    {
        if (! lowLatency.load())
            return;
        if (previousBufferSize.load() > 0)
            setup.bufferSize = previousBufferSize.load();
        if (previousSampleRate.load() > 0.0)
            setup.sampleRate = previousSampleRate.load();
    }
    const auto error = deviceManager.setAudioDeviceSetup (setup, true);
    lowLatency.store (error.isEmpty());
    if (error.isEmpty())
        saveAudioSettings();
}

} // namespace nc
