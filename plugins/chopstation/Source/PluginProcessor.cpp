#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace nc
{
namespace
{
struct AnySound final : juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};
} // namespace

void ChopVoice::startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int)
{
    activeSample = holder.current.load();

    if (activeSample == nullptr || activeSample->slices.empty())
        return;

    const auto count = static_cast<int> (activeSample->slices.size());
    const auto index = ((midiNoteNumber % count) + count) % count;
    const auto& slice = activeSample->slices[static_cast<size_t> (index)];

    position = static_cast<double> (slice.start);
    sliceEnd = slice.end;
    velocityGain = velocity;

    const auto sourceRate = activeSample->sourceRate > 0.0 ? activeSample->sourceRate : 44100.0;
    increment = std::pow (2.0, (midiNoteNumber - 60) / 12.0) * sourceRate / juce::jmax (1.0, getSampleRate());
}

void ChopVoice::stopNote (float, bool)
{
    clearCurrentNote();
}

void ChopVoice::renderNextBlock (juce::AudioBuffer<float>& output, int startSample, int numSamples)
{
    if (activeSample == nullptr || ! isVoiceActive())
        return;

    const auto& buffer = activeSample->buffer;
    const auto lastSourceSample = static_cast<double> (buffer.getNumSamples() - 1);

    for (int i = 0; i < numSamples; ++i)
    {
        if (position >= static_cast<double> (sliceEnd) || position > lastSourceSample)
        {
            clearCurrentNote();
            break;
        }

        const auto index = static_cast<std::int64_t> (position);
        const auto fraction = static_cast<float> (position - static_cast<double> (index));
        const auto nextIndex = std::min<std::int64_t> (index + 1, static_cast<std::int64_t> (lastSourceSample));

        // Short fade at the slice edge so chops never click.
        const auto remaining = static_cast<float> (sliceEnd - position);
        const auto fade = juce::jlimit (0.0f, 1.0f, remaining / 64.0f);

        for (int ch = 0; ch < output.getNumChannels(); ++ch)
        {
            const auto sourceChannel = std::min (ch, buffer.getNumChannels() - 1);
            const auto a = buffer.getSample (sourceChannel, static_cast<int> (index));
            const auto b = buffer.getSample (sourceChannel, static_cast<int> (nextIndex));
            output.addSample (ch, startSample + i, (a + (b - a) * fraction) * velocityGain * fade);
        }

        position += increment;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout ChopStationProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "chops", 1 }, "Chops", 1, 32, 8));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "gain", 1 }, "Gain",
                                                                  juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "drive", 1 }, "Drive",
                                                                  juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.1f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "bits", 1 }, "Bits",
                                                                  juce::NormalisableRange<float> (4.0f, 16.0f, 1.0f), 16.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "cutoff", 1 }, "Cutoff",
                                                                  juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.3f), 18000.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "reso", 1 }, "Resonance",
                                                                  juce::NormalisableRange<float> (0.1f, 8.0f, 0.01f), 1.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "delayMix", 1 }, "Delay mix",
                                                                  juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "delayTime", 1 }, "Delay time",
                                                                  juce::NormalisableRange<float> (0.05f, 1.0f, 0.001f), 0.25f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "reverb", 1 }, "Reverb",
                                                                  juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "pan", 1 }, "Pan",
                                                                  juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f));

    return { params.begin(), params.end() };
}

ChopStationProcessor::ChopStationProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formats.registerBasicFormats();

    for (int i = 0; i < 8; ++i)
        synth.addVoice (new ChopVoice (holder));

    synth.addSound (new AnySound());
}

void ChopStationProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    synth.setCurrentPlaybackSampleRate (sampleRate);

    filters.assign (static_cast<size_t> (juce::jmax (1, getTotalNumOutputChannels())), {});

    delayBuffer.setSize (juce::jmax (1, getTotalNumOutputChannels()),
                         static_cast<int> (sampleRate * 2.0) + 4);
    delayBuffer.clear();
    delayWrite = 0;

    reverb.setSampleRate (sampleRate);
    gainSmoother.reset (sampleRate, 0.02);
    gainSmoother.setCurrentAndTargetValue (1.0f);
}

bool ChopStationProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::disabled())
        return false;

    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void ChopStationProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0)
        return;

    buffer.clear();
    synth.renderNextBlock (buffer, midiMessages, 0, numSamples);

    const auto drive = apvts.getRawParameterValue ("drive")->load();
    const auto bits = apvts.getRawParameterValue ("bits")->load();
    const auto cutoff = apvts.getRawParameterValue ("cutoff")->load();
    const auto resonance = apvts.getRawParameterValue ("reso")->load();
    const auto delayMix = apvts.getRawParameterValue ("delayMix")->load();
    const auto delayTime = apvts.getRawParameterValue ("delayTime")->load();
    const auto reverbMix = apvts.getRawParameterValue ("reverb")->load();
    const auto gainDb = apvts.getRawParameterValue ("gain")->load();
    const auto pan = apvts.getRawParameterValue ("pan")->load();

    if (filters.size() < static_cast<size_t> (numChannels))
        filters.resize (static_cast<size_t> (numChannels));

    for (int ch = 0; ch < numChannels; ++ch)
        filters[static_cast<size_t> (ch)].set (cutoff, resonance, static_cast<float> (currentSampleRate));

    const auto crushLevels = juce::jmax (1.0f, std::pow (2.0f, bits) - 1.0f);
    const auto delaySize = delayBuffer.getNumSamples();
    const auto delaySamples = juce::jlimit (1, juce::jmax (1, delaySize - 1),
                                            static_cast<int> (delayTime * static_cast<float> (currentSampleRate)));

    for (int i = 0; i < numSamples; ++i)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            auto* delay = delayBuffer.getWritePointer (juce::jmin (ch, delayBuffer.getNumChannels() - 1));

            auto x = data[i];
            x = std::tanh (x * (1.0f + drive * 6.0f));

            if (bits < 16.0f)
                x = std::round (x * crushLevels) / crushLevels;

            x = filters[static_cast<size_t> (ch)].process (x);

            const auto readIndex = (delayWrite - delaySamples + delaySize) % delaySize;
            const auto delayed = delay[readIndex];
            delay[delayWrite] = x + delayed * 0.35f;

            data[i] = x + delayed * delayMix;
        }

        delayWrite = (delayWrite + 1) % delaySize;
    }

    if (reverbMix > 0.001f && numChannels >= 2)
    {
        juce::Reverb::Parameters parameters;
        parameters.roomSize = 0.62f;
        parameters.damping = 0.42f;
        parameters.wetLevel = reverbMix;
        parameters.dryLevel = 1.0f - reverbMix * 0.6f;
        parameters.width = 1.0f;
        reverb.setParameters (parameters);
        reverb.processStereo (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
    }

    gainSmoother.setTargetValue (juce::Decibels::decibelsToGain (gainDb));
    gainSmoother.applyGain (buffer, numSamples);

    if (numChannels >= 2)
    {
        const auto left = pan <= 0.0f ? 1.0f : 1.0f - pan;
        const auto right = pan >= 0.0f ? 1.0f : 1.0f + pan;
        buffer.applyGain (0, 0, numSamples, left);
        buffer.applyGain (1, 0, numSamples, right);
    }
}

SamplePtr ChopStationProcessor::decodeSample (const juce::File& file, int chopCount)
{
    std::unique_ptr<juce::AudioFormatReader> reader;

    {
        const juce::ScopedLock formatGuard (formatLock);
        reader.reset (formats.createReaderFor (file));
    }

    if (reader == nullptr || reader->lengthInSamples <= 0)
        return {};

    // Hostile/header-bomb guard: reject before allocating. A malformed header
    // claiming gigabytes must not OOM the host.
    if (reader->lengthInSamples > maxSampleFrames)
        return {};

    if (reader->numChannels <= 0 || reader->numChannels > maxSampleChannels)
        return {};

    const auto numChannels = static_cast<int> (reader->numChannels);
    const auto numSamples = static_cast<int> (reader->lengthInSamples);

    // Re-check after downcast: the allocation happens once, here, with known bounds.
    if (numSamples <= 0)
        return {};

    auto data = std::make_shared<SampleData>();
    data->sourceRate = reader->sampleRate;
    data->buffer.setSize (numChannels, numSamples);
    reader->read (&data->buffer, 0, numSamples, 0, true, true);

    std::vector<const float*> channels;

    for (int ch = 0; ch < data->buffer.getNumChannels(); ++ch)
        channels.push_back (data->buffer.getReadPointer (ch));

    const auto transients = detectTransients (channels.data(),
                                              data->buffer.getNumChannels(),
                                              data->buffer.getNumSamples(),
                                              0.45);

    data->slices = planSlices (data->buffer.getNumSamples(), transients,
                               std::max (1, chopCount));
    return data;
}

void ChopStationProcessor::commitSample (SamplePtr data, const juce::File& file)
{
    holder.current.store (std::move (data));

    const juce::ScopedLock lock (metadataLock);
    sampleName = file.getFileNameWithoutExtension();
    activePath = file.getFullPathName();
}

int ChopStationProcessor::currentChopCount() const
{
    if (auto* param = apvts.getRawParameterValue ("chops"))
        return static_cast<int> (param->load());

    return 8;
}

void ChopStationProcessor::cancelPendingLoad() noexcept
{
    const juce::ScopedLock lock (loadStateLock);
    ++loadGeneration;
    loadingSample.store (false, std::memory_order_release);
}

bool ChopStationProcessor::loadSample (const juce::File& file)
{
    // A synchronous restore supersedes any worker that is still decoding.
    cancelPendingLoad();

    auto data = decodeSample (file, currentChopCount());

    if (data == nullptr)
    {
        lastLoadFailed.store (true, std::memory_order_release);
        return false;
    }

    commitSample (std::move (data), file);
    lastLoadFailed.store (false, std::memory_order_release);
    return true;
}

bool ChopStationProcessor::loadSampleAsync (const juce::File& file)
{
    std::uint64_t generation = 0;

    {
        const juce::ScopedLock lock (loadStateLock);

        if (loadingSample.load (std::memory_order_acquire))
            return false;

        loadingSample.store (true, std::memory_order_release);
        generation = ++loadGeneration;
    }

    lastLoadFailed.store (false, std::memory_order_release);
    const auto chopCount = currentChopCount();

    loadPool.addJob ([this, file, chopCount, generation]
    {
        auto data = decodeSample (file, chopCount);

        // A project restore or synchronous load may have superseded this job
        // while it was decoding. Only the newest generation may commit.
        const juce::ScopedLock lock (loadStateLock);

        if (generation != loadGeneration.load (std::memory_order_acquire))
            return;

        if (data != nullptr)
            commitSample (std::move (data), file);
        else
            lastLoadFailed.store (true, std::memory_order_release);

        loadingSample.store (false, std::memory_order_release);
    });

    return true;
}

juce::String ChopStationProcessor::getSampleName() const
{
    const juce::ScopedLock lock (metadataLock);
    return sampleName;
}

juce::String ChopStationProcessor::getSamplePath() const
{
    const juce::ScopedLock lock (metadataLock);
    return activePath;
}

int ChopStationProcessor::getSliceCount() const
{
    const auto data = holder.current.load();
    return data == nullptr ? 0 : static_cast<int> (data->slices.size());
}

juce::String ChopStationProcessor::presetName (int index)
{
    switch (index)
    {
        case 0: return "Clean Chop";
        case 1: return "Gritty Break";
        case 2: return "Lo-Fi";
        default: return "Default";
    }
}

void ChopStationProcessor::applyPreset (int index)
{
    // Values come from createLayout() defaults: written straight to APVTS so
    // host automation, undo, and A/B see exactly what the sliders show.
    struct Preset { float gain, drive, bits, cutoff, reso, delayMix, delayTime, reverb, pan; int chops; };
    static const Preset presets[3] = {
        { 0.0f, 0.1f, 16.0f, 18000.0f, 1.0f, 0.0f, 0.25f, 0.0f, 0.0f, 8 },     // Clean
        { 0.0f, 0.6f, 12.0f, 9000.0f, 3.0f, 0.15f, 0.35f, 0.2f, 0.0f, 16 },    // Gritty
        { -3.0f, 0.8f, 8.0f, 4000.0f, 5.0f, 0.3f, 0.5f, 0.35f, 0.0f, 8 },     // Lo-Fi
    };

    if (index < 0 || index >= numPresets())
        return;

    const auto& p = presets[index];

    auto setFloat = [this] (const char* id, float v)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (v));
    };

    setFloat ("gain", p.gain);
    setFloat ("drive", p.drive);
    setFloat ("bits", p.bits);
    setFloat ("cutoff", p.cutoff);
    setFloat ("reso", p.reso);
    setFloat ("delayMix", p.delayMix);
    setFloat ("delayTime", p.delayTime);
    setFloat ("reverb", p.reverb);
    setFloat ("pan", p.pan);

    if (auto* param = apvts.getParameter ("chops"))
        param->setValueNotifyingHost (param->convertTo0to1 (static_cast<float> (p.chops)));

    rebuildSlices();
}

void ChopStationProcessor::rebuildSlices()
{
    if (loadingSample.load (std::memory_order_acquire))
        return;

    const auto current = holder.current.load();

    if (current == nullptr || current->buffer.getNumSamples() <= 0)
        return;

    std::vector<const float*> channels;

    for (int ch = 0; ch < current->buffer.getNumChannels(); ++ch)
        channels.push_back (current->buffer.getReadPointer (ch));

    const auto transients = detectTransients (channels.data(),
                                              current->buffer.getNumChannels(),
                                              current->buffer.getNumSamples(),
                                              0.45);
    const auto chopCount = static_cast<int> (apvts.getRawParameterValue ("chops")->load());

    auto next = std::make_shared<SampleData>();
    next->buffer = current->buffer;
    next->sourceRate = current->sourceRate;
    next->slices = planSlices (next->buffer.getNumSamples(), transients, chopCount);
    holder.current.store (std::move (next));
}

void ChopStationProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::String savedPath, savedName;

    {
        const juce::ScopedLock lock (metadataLock);
        savedPath = activePath;
        savedName = sampleName;
    }

    state.setProperty ("samplePath", savedPath, nullptr);
    state.setProperty ("sampleName", savedName, nullptr);

    // Embed the decoded sample as WAV so the preset travels cross-machine.
    // A 30 s stereo cap keeps state blobs bounded; longer samples save path-only.
    if (const auto data = holder.current.load())
    {
        if (data->buffer.getNumSamples() > 0
            && data->buffer.getNumSamples() <= 30 * 44100 * 2)
        {
            juce::MemoryBlock wav;
            juce::WavAudioFormat* wavFormat = nullptr;

            {
                const juce::ScopedLock formatGuard (formatLock);
                wavFormat = dynamic_cast<juce::WavAudioFormat*> (formats.findFormatForFileExtension ("wav"));
            }

            if (wavFormat != nullptr)
            {
                // The writer takes ownership of the stream (same contract as the
                // FileOutputStream path). Heap-allocate so writer.reset() frees
                // it instead of deleting a stack object.
                auto stream = std::make_unique<juce::MemoryOutputStream> (wav, false);

                auto writer = std::unique_ptr<juce::AudioFormatWriter>
                    (wavFormat->createWriterFor (stream.get(), data->sourceRate > 0 ? data->sourceRate : 44100.0,
                                                 data->buffer.getNumChannels(), 32, {}, 0));
                stream.release(); // writer now owns it; its dtor flushes into `wav`.

                if (writer != nullptr)
                {
                    writer->writeFromAudioSampleBuffer (data->buffer, 0, data->buffer.getNumSamples());
                    writer.reset();
                    state.setProperty ("sampleData", wav.toBase64Encoding(), nullptr);
                }
            }
        }
    }

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ChopStationProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml == nullptr)
        return;

    auto tree = juce::ValueTree::fromXml (*xml);

    if (! tree.isValid())
        return;

    // State restoration is authoritative: stop an older worker from
    // committing its sample after the restored one is installed.
    cancelPendingLoad();
    apvts.replaceState (tree);

    // Embedded sample wins: decode without touching the filesystem.
    const auto embedded = tree.getProperty ("sampleData").toString();

    if (embedded.isNotEmpty())
    {
        juce::MemoryBlock wav;

        if (wav.fromBase64Encoding (embedded) && wav.getSize() > 0)
        {
            auto input = std::make_unique<juce::MemoryInputStream> (wav, false);
            juce::AudioFormatReader* newReader = nullptr;

            {
                const juce::ScopedLock formatGuard (formatLock);
                newReader = formats.createReaderFor (std::move (input));
            }

            if (auto* reader = newReader)
            {
                std::unique_ptr<juce::AudioFormatReader> ownedReader (reader);

                if (reader->lengthInSamples <= 0 || reader->lengthInSamples > maxSampleFrames)
                    return;

                auto sampleData = std::make_shared<SampleData>();
                sampleData->sourceRate = reader->sampleRate;
                const auto numCh = static_cast<int> (juce::jmin<int64_t> (reader->numChannels, maxSampleChannels));
                const auto numSamples = static_cast<int> (reader->lengthInSamples);
                sampleData->buffer.setSize (numCh, numSamples);
                reader->read (&sampleData->buffer, 0, numSamples, 0, true, true);

                std::vector<const float*> channels;

                for (int ch = 0; ch < sampleData->buffer.getNumChannels(); ++ch)
                    channels.push_back (sampleData->buffer.getReadPointer (ch));

                const auto transients = detectTransients (channels.data(),
                                                          sampleData->buffer.getNumChannels(),
                                                          sampleData->buffer.getNumSamples(),
                                                          0.45);
                const auto chopCount = static_cast<int> (apvts.getRawParameterValue ("chops")->load());
                sampleData->slices = planSlices (sampleData->buffer.getNumSamples(), transients, chopCount);
                holder.current.store (std::move (sampleData));

                {
                    const juce::ScopedLock lock (metadataLock);
                    sampleName = tree.getProperty ("sampleName").toString();
                    activePath.clear();
                }

                return;
            }
        }
    }

    // Fallback: legacy projects that stored only a path.
    const auto path = tree.getProperty ("samplePath").toString();

    if (path.isNotEmpty())
    {
        const juce::File file (path);

        if (file.existsAsFile())
            loadSample (file);
    }
}

juce::AudioProcessorEditor* ChopStationProcessor::createEditor()
{
    return new ChopStationEditor (*this);
}
} // namespace nc

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new nc::ChopStationProcessor();
}
