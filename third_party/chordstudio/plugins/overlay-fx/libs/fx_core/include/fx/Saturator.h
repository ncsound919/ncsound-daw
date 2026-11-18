#pragma once

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace fx
{
    // Simple, real waveshaping family. Pre-gain drives the curve, post-gain
    // keeps level sane, mix blends in parallel. DC is removed with a one-pole.
    //
    // Controls:
    //   bias       - DC offset before the nonlinearity, generating even harmonics.
    //   tone       - pre/post emphasis tilt around ~1 kHz: the band chosen by the
    //                sign of `tone` is emphasised going into the shaper (producing
    //                relatively more harmonics there) and de-emphasised after.
    //   oversample - runs the nonlinearity at 2x (drive < 12 dB) or 4x (drive >=
    //                12 dB) using juce::dsp::Oversampling to suppress aliasing.
    //   outputDb   - output trim after the shaper.
    //   harmonics  - optional 2nd/3rd order blend added after the shaper, for
    //                material where the component curve alone is not enough.
    //
    // Modes are character curves, not circuit models:
    //   tape       - tanh, symmetric, gentle odd-order.
    //   tube       - asymmetric tanh with a 2nd-order term (even harmonics).
    //   diode      - two-stage odd-order knee, hard edge.
    //   transistor - symmetric soft clip with a harder knee than tape (odd).
    //   transformer- iron: tanh plus a low-order compression term (weight).
    class Saturator
    {
    public:
        enum class Mode { tape, tube, diode, transistor, transformer };

        void prepare (double sampleRate, int maxBlockSize = 512, int numChannels = 2)
        {
            sr = sampleRate > 0.0 ? sampleRate : 48000.0;
            blockSize = std::max (1, maxBlockSize);
            channels = std::max (1, numChannels);

            dcCoeff = std::exp (-2.0f * 3.14159265f * 20.0f / (float) sr);
            tiltCoeff = std::exp (-2.0f * 3.14159265f * 1000.0f / (float) sr);

            const auto filterType = juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR;
            os2x = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) channels, 1, filterType, true);
            os4x = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) channels, 2, filterType, true);
            os2x->initProcessing ((size_t) blockSize);
            os4x->initProcessing ((size_t) blockSize);

            dry.setSize (channels, blockSize, false, false, true);

            dcState.assign ((size_t) channels, 0.0f);
            tiltPreState.assign ((size_t) channels, 0.0f);
            tiltPostState.assign ((size_t) channels, 0.0f);
            updateTiltGains();
        }

        void reset() noexcept
        {
            std::fill (dcState.begin(), dcState.end(), 0.0f);
            std::fill (tiltPreState.begin(), tiltPreState.end(), 0.0f);
            std::fill (tiltPostState.begin(), tiltPostState.end(), 0.0f);
            dry.clear();

            if (os2x != nullptr) os2x->reset();
            if (os4x != nullptr) os4x->reset();
        }

        void setDriveDb (float db) noexcept
        {
            driveDb  = db;
            preGain  = std::pow (10.0f, db / 20.0f);
            postGain = 1.0f / std::max (1.0f, std::sqrt (preGain));
        }

        void setMix (float m) noexcept { mix = std::clamp (m, 0.0f, 1.0f); }
        void setMode (Mode m) noexcept { mode = m; }

        void setBias (float b) noexcept { bias = std::clamp (b, -1.0f, 1.0f); }

        void setTone (float t) noexcept
        {
            tone = std::clamp (t, -1.0f, 1.0f);
            updateTiltGains();
        }

        void setOversample (bool on) noexcept { oversample = on; }

        void setOutputDb (float db) noexcept { outputGain = std::pow (10.0f, db / 20.0f); }

        /** Extra 2nd/3rd order content added after the component curve (0..1). */
        void setHarmonicOrders (float second, float third) noexcept
        {
            h2 = std::clamp (second, 0.0f, 1.0f);
            h3 = std::clamp (third, 0.0f, 1.0f);
        }

        // Per-sample path (no oversampling). Kept for callers that process
        // sample-by-sample and for the unit tests.
        void process (float& l, float& r) noexcept
        {
            if (mix <= 0.0f)
                return;

            activeTiltCoeff = tiltCoeff;

            const auto dl = l, dr = r;
            const auto wl = shapeChannel (0, l * preGain) * postGain * outputGain;
            const auto wr = shapeChannel (channels > 1 ? 1 : 0, r * preGain) * postGain * outputGain;

            l = dl * (1.0f - mix) + wl * mix;
            r = dr * (1.0f - mix) + wr * mix;

            blockDc (l, 0);
            blockDc (r, channels > 1 ? 1 : 0);
        }

        // Block path. Uses real oversampling around the nonlinearity when enabled.
        void processBlock (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
        {
            if (mix <= 0.0f || numSamples <= 0)
                return;

            const auto numCh = std::min (channels, buffer.getNumChannels());

            if (! oversample || os2x == nullptr || numCh <= 0)
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    float l = buffer.getSample (0, startSample + i);
                    float r = numCh > 1 ? buffer.getSample (1, startSample + i) : l;
                    process (l, r);
                    buffer.setSample (0, startSample + i, l);
                    if (numCh > 1)
                        buffer.setSample (1, startSample + i, r);
                }
                return;
            }

            auto* os = driveDb >= 12.0f ? os4x.get() : os2x.get();

            // The tilt crossover must track the oversampled rate.
            activeTiltCoeff = (float) std::exp (-2.0 * 3.14159265358979 * 1000.0
                                                / (sr * (double) os->getOversamplingFactor()));

            for (int ch = 0; ch < numCh; ++ch)
                dry.copyFrom (ch, 0, buffer, ch, startSample, numSamples);

            juce::dsp::AudioBlock<float> full (buffer);
            auto sub = full.getSubBlock ((size_t) startSample, (size_t) numSamples);

            auto up = os->processSamplesUp (sub);
            const auto upN = (int) up.getNumSamples();
            const auto upCh = std::min ((int) up.getNumChannels(), numCh);

            for (int ch = 0; ch < upCh; ++ch)
            {
                auto* p = up.getChannelPointer ((size_t) ch);

                for (int i = 0; i < upN; ++i)
                    p[i] = shapeChannel (ch, p[i] * preGain) * postGain;
            }

            os->processSamplesDown (sub);

            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* p = buffer.getWritePointer (ch) + startSample;

                for (int i = 0; i < numSamples; ++i)
                {
                    const auto wet = p[i] * outputGain;
                    p[i] = dry.getSample (ch, i) * (1.0f - mix) + wet * mix;
                    blockDc (p[i], ch);
                }
            }
        }

    private:
        void updateTiltGains() noexcept
        {
            const auto tiltDb = tone * 6.0f;
            lowTiltGain  = std::pow (10.0f, -tiltDb / 20.0f);
            highTiltGain = std::pow (10.0f,  tiltDb / 20.0f);
        }

        float shapeChannel (int ch, float x) noexcept
        {
            if (ch < 0) ch = 0;
            if (ch >= (int) tiltPreState.size()) return shape (x);

            x += bias;

            if (tone != 0.0f)
            {
                auto& lowState = tiltPreState[(size_t) ch];
                lowState += activeTiltCoeff * (x - lowState);
                const auto low = lowState;
                const auto high = x - low;
                x = low * lowTiltGain + high * highTiltGain;
            }

            auto y = shape (x);

            if (h2 > 0.0f || h3 > 0.0f)
            {
                // Harmonic generator on the bounded fundamental. x^2 carries the
                // even order (its DC offset is removed by blockDc); the cubic is
                // subtracted so it deepens the shaper's own odd-order compression
                // instead of cancelling it.
                const auto xs = std::tanh (x);
                y += h2 * 0.35f * xs * xs - h3 * 0.28f * xs * xs * xs;
            }

            if (tone != 0.0f)
            {
                auto& lowState = tiltPostState[(size_t) ch];
                lowState += activeTiltCoeff * (y - lowState);
                const auto low = lowState;
                const auto high = y - low;
                y = low / lowTiltGain + high / highTiltGain;
            }

            return y;
        }

        void blockDc (float& v, int ch) noexcept
        {
            if (ch < 0 || ch >= (int) dcState.size())
                return;

            auto& s = dcState[(size_t) ch];
            s = v * (1.0f - dcCoeff) + s * dcCoeff;
            v -= s;
        }

        float shape (float x) const noexcept
        {
            switch (mode)
            {
                case Mode::tube:
                {
                    const auto y = std::tanh (x * 0.85f + 0.12f * x * x);
                    return y - std::tanh (0.0f);
                }
                case Mode::diode:
                {
                    const auto y = std::tanh (x) + 0.5f * std::tanh (x * 0.5f);
                    return y / 1.5f;
                }
                case Mode::transistor:
                {
                    // Symmetric soft clip: odd-order, harder knee than tape.
                    return 1.35f * x / (1.0f + 0.55f * std::abs (x));
                }
                case Mode::transformer:
                {
                    // Iron: odd-order with a low-order compression term.
                    return (std::tanh (x) + 0.18f * std::tanh (0.5f * x)) / 1.18f;
                }
                case Mode::tape:
                default:
                    return std::tanh (x);
            }
        }

        double sr { 48000.0 };
        int blockSize { 512 }, channels { 2 };
        float preGain { 1.0f }, postGain { 1.0f }, mix { 0.0f };
        float driveDb { 0.0f };
        float bias { 0.0f }, tone { 0.0f };
        float h2 { 0.0f }, h3 { 0.0f };
        float lowTiltGain { 1.0f }, highTiltGain { 1.0f };
        float outputGain { 1.0f };
        bool  oversample { false };
        Mode mode { Mode::tape };

        float dcCoeff { 0.999f }, tiltCoeff { 0.87f }, activeTiltCoeff { 0.87f };
        std::vector<float> dcState, tiltPreState, tiltPostState;
        juce::AudioBuffer<float> dry;
        std::unique_ptr<juce::dsp::Oversampling<float>> os2x, os4x;
    };
}
