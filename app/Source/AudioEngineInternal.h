#pragma once

// Shared internals for the AudioEngine translation units (plan 07-03).
//
// These lived in an anonymous namespace inside AudioEngine.cpp. Once the class
// is split across translation units they need one definition per program, so
// they are inline here instead of copied into each file.
//
// The function-local statics are deliberate and must not become per-TU copies:
// toneProbe() reads NC_TEST_TONE_* once per process, and dumpStage() owns one
// sink per stage. Duplicated, the probe would be sampled twice and
// NC_AUDIO_DUMP would write two files per stage.

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <memory>

namespace nc
{
inline constexpr double twoPi = juce::MathConstants<double>::twoPi;
inline constexpr float smoothingCoefficient = 0.0025f;

inline float softLimit (float value)
{
    return std::tanh (value);
}

// Diagnostic only: NC_TEST_TONE_HZ=<freq> injects a pure sine at a known level
// so a distortion analyser can measure THD+N of one stage at a time instead of
// guessing from programme material. NC_TEST_TONE_TARGET=bus (default) injects at
// the mix bus (master rack only); =track0 injects ahead of that track's channel
// strip. NC_TEST_TONE_MUTE=1 silences the session content and the click, so the
// captured file is the tone and nothing else. Unset = no effect on the audio.
struct ToneProbe
{
    bool on = false;
    bool mute = false;
    bool noSend = false;     // keep the tone out of the aux sends (reverb return)
    bool afterStrip = false; // inject after the strip instead of before it
    bool noStrip = false;    // bypass that lane's strip entirely
    int track = -1;          // -1 = mix bus, else that track's lane
    double hz = 1000.0;
    float gain = 0.1f;
};

inline ToneProbe& toneProbe()
{
    static ToneProbe probe = []
    {
        ToneProbe p;
        const auto hz = juce::SystemStats::getEnvironmentVariable ("NC_TEST_TONE_HZ", {});
        p.on = hz.isNotEmpty();
        p.hz = juce::jlimit (20.0, 20000.0, hz.getDoubleValue());
        p.gain = juce::Decibels::decibelsToGain ((float) juce::SystemStats::getEnvironmentVariable ("NC_TEST_TONE_DB", "-20").getDoubleValue());
        p.mute = juce::SystemStats::getEnvironmentVariable ("NC_TEST_TONE_MUTE", {}) == "1";
        p.noSend = juce::SystemStats::getEnvironmentVariable ("NC_TEST_TONE_NOSEND", {}) == "1";
        const auto target = juce::SystemStats::getEnvironmentVariable ("NC_TEST_TONE_TARGET", "bus");
        p.track = target.startsWith ("track") ? target.substring (5).getIntValue() : -1;
        p.afterStrip = target.endsWith ("b");
        p.noStrip = juce::SystemStats::getEnvironmentVariable ("NC_TEST_TONE_NOSTRIP", {}) == "1";
        return p;
    }();
    return probe;
}

// Diagnostic only: NC_AUDIO_DUMP=<path> writes the first eight seconds of the
// playing audio at two points in the chain - before and after the master rack -
// as interleaved float32 stereo, so a fault can be attributed to a stage on disk
// instead of by ear. No effect when the variable is unset.
struct DumpSink
{
    std::unique_ptr<juce::FileOutputStream> stream;
    int64_t framesLeft = 0;
    bool initialised = false;
};

inline void dumpStage (const char* stage, const juce::AudioBuffer<float>& buffer,
                       int numSamples, bool playing)
{
    static DumpSink preSink, postSink;
    auto& sink = (stage[0] == 'p' && stage[1] == 'r') ? preSink : postSink;

    if (! sink.initialised)
    {
        sink.initialised = true;
        const auto base = juce::SystemStats::getEnvironmentVariable ("NC_AUDIO_DUMP", {});

        if (base.isNotEmpty())
        {
            const auto baseFile = juce::File (base);
            const auto file = baseFile.getSiblingFile (juce::String (stage) + "-" + baseFile.getFileName());
            sink.stream = std::make_unique<juce::FileOutputStream> (file);
            sink.framesLeft = static_cast<int64_t> (48000 * 8);
        }
    }

    // Only playing audio is captured, so the window is independent of how long
    // the window takes to load before the transport is started.
    if (! playing || sink.framesLeft <= 0 || sink.stream == nullptr || buffer.getNumChannels() < 2)
        return;

    const auto count = static_cast<int> (juce::jmin<int64_t> (numSamples, sink.framesLeft));

    for (int i = 0; i < count; ++i)
    {
        const float sample[2] { buffer.getSample (0, i), buffer.getSample (1, i) };
        sink.stream->write (sample, 8);
    }

    sink.framesLeft -= count;

    if (sink.framesLeft == 0)
        sink.stream->flush();
}

inline juce::PropertiesFile::Options audioPropsOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "NCSound Music Hub";
    o.filenameSuffix = "settings";
    o.folderName = "NCSound Music Hub";
    return o;
}
} // namespace nc
