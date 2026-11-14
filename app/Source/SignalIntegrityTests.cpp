/*
    Signal integrity: does the engine invent audio where there is none, and does
    every aux engine actually produce audio where there is some?

    A session with nothing playing must render digital silence. The strip, the
    master rack, the maximiser and both aux returns all run every block, so a
    hiss, a denormal, an uninitialised buffer or a self-oscillating feedback loop
    shows up here as a number above zero.
*/

#include <catch2/catch_test_macros.hpp>

#include "AudioEngine.h"
#include "AuxBus.h"

#include <nc/engine/Export.hpp>

#include <cmath>

namespace
{
/** Peak of both channels of a rendered file, or -1 if it cannot be read. */
float renderedPeak (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

    if (reader == nullptr)
        return -1.0f;

    const auto frames = static_cast<int> (juce::jmin<juce::int64> (reader->lengthInSamples, 48000));

    if (frames <= 0)
        return -1.0f;

    juce::AudioBuffer<float> buffer (2, frames);
    reader->read (&buffer, 0, frames, 0, true, true);

    return juce::jmax (buffer.getMagnitude (0, 0, frames),
                       buffer.getMagnitude (1, 0, frames));
}
} // namespace

TEST_CASE ("a session with nothing playing renders digital silence", "[silence]")
{
    nc::AudioEngine engine;
    engine.setBpm (120.0);

    REQUIRE (engine.setTimelineClips ({}) == 0);

    // Sends wide open, so the reverb and delay returns are in the path too.
    for (int track = 0; track < nc::AudioEngine::trackCount; ++track)
    {
        engine.setTrackSendA (track, 1.0f);
        engine.setTrackSendB (track, 1.0f);
    }

    juce::Thread::sleep (300);
    engine.suspendAudioCallbackForTesting();

    nc::engine::export_::ExportOptions options;
    options.format = nc::engine::export_::Format::wav;
    options.bitDepth = 24;
    options.sampleRate = 48000;
    options.dither = false;

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("nc-silence-render.wav");
    file.deleteFile();

    juce::String error;
    REQUIRE (engine.renderToFile (file, 1.0, 2.0, options, error));

    const auto peak = renderedPeak (file);
    INFO ("rendered peak: " << peak);

    REQUIRE (peak >= 0.0f);
    // One 24-bit LSB is 1.2e-7; dither is off, so silence must be exactly zero
    // or a single LSB, never an audible floor.
    REQUIRE (peak < 1.0e-6f);
}

TEST_CASE ("every aux engine returns finite, audible audio", "[aux-engines]")
{
    const nc::AuxBus::Engine engines[] { nc::AuxBus::Engine::overlayReverb,
                                         nc::AuxBus::Engine::nexusReverb,
                                         nc::AuxBus::Engine::overlayDelay,
                                         nc::AuxBus::Engine::temporalDelay };

    for (const auto engine : engines)
    {
        nc::AuxBus bus;
        bus.prepare (48000.0, 256, engine);

        juce::AudioBuffer<float> block (2, 256);
        double energy = 0.0;
        int nonFinite = 0;

        // 0.6 s: past the 375 ms delay so every engine has produced a return.
        for (int frameBlock = 0; frameBlock < 120; ++frameBlock)
        {
            block.clear();

            if (frameBlock == 0)
            {
                block.setSample (0, 0, 1.0f);
                block.setSample (1, 0, 1.0f);
            }

            bus.process (block, 256);

            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < 256; ++i)
                {
                    const auto value = block.getSample (channel, i);

                    if (! std::isfinite (value))
                        ++nonFinite;

                    energy += static_cast<double> (value) * value;
                }
        }

        INFO ("engine: " << bus.currentEngineName());
        REQUIRE (nonFinite == 0);
        REQUIRE (energy > 0.0);
    }
}
