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

bool ChopStationProcessor::loadSample (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    auto data = std::make_shared<SampleData>();
    data->sourceRate = reader->sampleRate;
    data->buffer.setSize (static_cast<int> (reader->numChannels),
                          static_cast<int> (reader->lengthInSamples));
    reader->read (&data->buffer, 0, static_cast<int> (reader->lengthInSamples), 0, true, true);

    std::vector<const float*> channels;

    for (int ch = 0; ch < data->buffer.getNumChannels(); ++ch)
        channels.push_back (data->buffer.getReadPointer (ch));

    const auto transients = detectTransients (channels.data(),
                                              data->buffer.getNumChannels(),
                                              data->buffer.getNumSamples(),
                                              0.45);

    const auto chopCount = static_cast<int> (apvts.getRawParameterValue ("chops")->load());
    data->slices = planSlices (data->buffer.getNumSamples(), transients, chopCount);

    holder.current.store (std::move (data));

    sampleFile = file;
    sampleName = file.getFileNameWithoutExtension();
    activePath = file.getFullPathName();
    return true;
}

juce::String ChopStationProcessor::getSampleName() const
{
    return sampleName;
}

int ChopStationProcessor::getSliceCount() const
{
    const auto data = holder.current.load();
    return data == nullptr ? 0 : static_cast<int> (data->slices.size());
}

void ChopStationProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("samplePath", activePath, nullptr);

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

    apvts.replaceState (tree);

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
