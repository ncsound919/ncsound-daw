#pragma once

// Overlay Master modules (v2). Each module processes a whole stereo block in
// place; the processor runs them in the user's order, then the Maximizer and
// Dither. All are independent designs built from fx_core parts, not emulations
// of iZotope Ozone's algorithms.

#include "fx/Compressor.h"
#include "fx/Crossover.h"
#include "fx/DynamicEq.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <cmath>

namespace overlay::master::dsp
{
    enum Channel { stereo = 0, mid, side };

    inline fx::EqShape eqShapeFor (int type) noexcept
    {
        switch (type)
        {
            case 1:  return fx::EqShape::lowShelf;
            case 2:  return fx::EqShape::highShelf;
            case 3:  return fx::EqShape::lowCut;
            case 4:  return fx::EqShape::highCut;
            case 5:  return fx::EqShape::notch;
            default: return fx::EqShape::bell;
        }
    }

    inline bool sameSpec (const fx::EqBandSpec& a, const fx::EqBandSpec& b) noexcept
    {
        return a.shape == b.shape && a.freq == b.freq && a.gainDb == b.gainDb && a.q == b.q && a.order == b.order;
    }

    // Runs bands that are stereo / mid-only / side-only. Stereo bands are the
    // same filter on both channels, so they can run in the M/S domain too.
    template <int N>
    struct MsBandSet
    {
        std::array<fx::DynamicEqBand, N> band;
        std::array<bool, N> active {};
        std::array<int, N> ch {};
        std::array<fx::EqBandSpec, N> spec {};
        bool anyMs { false }, anyActive { false };

        void prepare (double sr)
        {
            for (auto& b : band) { b.prepare (sr, 2, 4); b.setEnabled (true); b.reset(); }
            for (auto& s : spec) s.freq = -1.0;   // force the first setSpec
        }

        void set (int k, bool on, int channel, const fx::EqBandSpec& s) noexcept
        {
            active[(size_t) k] = on;
            ch[(size_t) k] = channel;
            if (on && ! sameSpec (s, spec[(size_t) k]))
            {
                spec[(size_t) k] = s;
                band[(size_t) k].setSpec (s);
            }
        }

        void finish() noexcept
        {
            anyMs = false; anyActive = false;
            for (int k = 0; k < N; ++k)
                if (active[(size_t) k]) { anyActive = true; anyMs = anyMs || ch[(size_t) k] != stereo; }
        }

        void process (float* l, float* r, int n) noexcept
        {
            if (! anyActive) return;
            for (int i = 0; i < n; ++i)
            {
                float a = l[i], b = r[i];
                if (anyMs) { const float m = 0.5f * (a + b), s = 0.5f * (a - b); a = m; b = s; }
                for (int k = 0; k < N; ++k)
                {
                    if (! active[(size_t) k]) continue;
                    auto& bd = band[(size_t) k];
                    switch (anyMs ? ch[(size_t) k] : stereo)
                    {
                        case mid:  bd.processMono (a); break;
                        case side: bd.processMono (b); break;
                        default:   bd.process (a, b); break;
                    }
                }
                if (anyMs) { const float m = a, s = b; a = m + s; b = m - s; }
                l[i] = a; r[i] = b;
            }
        }
    };

    //==============================================================================
    // 8-band static EQ (bell, shelves, cuts, notch; stereo / mid / side per band).
    struct EqBand { bool on { false }; int type { 0 }; float freq { 1000 }, gain { 0 }, q { 0.7f }; int slope { 0 }; int ch { 0 }; };

    class EqModule
    {
    public:
        static constexpr int numBands = 8;
        void prepare (double sampleRate) { sr = sampleRate; set.prepare (sr); }
        void reset() { for (auto& b : set.band) b.reset(); }

        void setBands (const std::array<EqBand, numBands>& bands) noexcept
        {
            params = bands;
            for (int k = 0; k < numBands; ++k)
            {
                const auto& b = bands[(size_t) k];
                const auto shape = eqShapeFor (b.type);
                const bool gainShape = fx::shapeHasGain (shape);
                const bool audible = b.on && (! gainShape || std::abs (b.gain) >= 0.01f);
                fx::EqBandSpec s;
                s.shape = shape; s.freq = b.freq; s.gainDb = gainShape ? b.gain : 0.0; s.q = b.q;
                s.order = (shape == fx::EqShape::bell || shape == fx::EqShape::notch) ? 1 : 1 + std::clamp (b.slope, 0, 3);
                set.set (k, audible, b.ch, s);
            }
            set.finish();
        }

        void process (float* l, float* r, int n) noexcept { set.process (l, r, n); }

        // Static response (dB) of the bands feeding the mid path (which = 0,
        // stereo + mid bands) or the side path (which = 1, stereo + side bands).
        double responseDb (double freq, int which) const noexcept
        {
            double m = 1.0;
            for (int k = 0; k < numBands; ++k)
            {
                if (! set.active[(size_t) k]) continue;
                const int c = set.ch[(size_t) k];
                if (c == stereo || (which == 0 && c == mid) || (which == 1 && c == side))
                    m *= set.band[(size_t) k].staticMagnitude (freq);
            }
            return 20.0 * std::log10 (std::max (1.0e-9, m));
        }

        bool hasSideBands() const noexcept
        {
            for (int k = 0; k < numBands; ++k) if (set.active[(size_t) k] && set.ch[(size_t) k] != stereo) return true;
            return false;
        }

    private:
        double sr { 48000.0 };
        std::array<EqBand, numBands> params {};
        MsBandSet<numBands> set;
    };

    //==============================================================================
    // 4-band dynamic EQ. range < 0 cuts the band when it is loud, range > 0
    // lifts it; nothing happens below the threshold (static gain is 0).
    struct DynEqBand { bool on { false }; int type { 0 }; float freq { 1000 }, q { 1 }, thresh { -20 }, range { -4 }, atk { 10 }, rel { 150 }; int ch { 0 }; };

    class DynEqModule
    {
    public:
        static constexpr int numBands = 4;
        void prepare (double sampleRate) { sr = sampleRate; set.prepare (sr); }
        void reset() { for (auto& b : set.band) b.reset(); }

        void setBands (const std::array<DynEqBand, numBands>& bands) noexcept
        {
            for (int k = 0; k < numBands; ++k)
            {
                const auto& b = bands[(size_t) k];
                fx::EqBandSpec s;
                s.shape = b.type == 1 ? fx::EqShape::lowShelf : b.type == 2 ? fx::EqShape::highShelf : fx::EqShape::bell;
                s.freq = b.freq; s.gainDb = 0.0; s.q = b.q; s.order = 1;
                const bool on = b.on && std::abs (b.range) >= 0.05f;
                set.set (k, on, b.ch, s);
                if (on) set.band[(size_t) k].setDynamic (true, b.thresh, -b.range, b.atk, b.rel);
            }
            set.finish();
        }

        void process (float* l, float* r, int n) noexcept
        {
            set.process (l, r, n);
            for (int k = 0; k < numBands; ++k)
                gain[(size_t) k].store (set.active[(size_t) k] ? set.band[(size_t) k].dynamicGainDb() : 0.0f, std::memory_order_relaxed);
        }

        /** Current dynamic gain of band k in dB (negative = cutting). */
        float currentGainDb (int k) const noexcept { return gain[(size_t) k].load (std::memory_order_relaxed); }

    private:
        double sr { 48000.0 };
        MsBandSet<numBands> set;
        std::array<std::atomic<float>, numBands> gain {};
    };

    //==============================================================================
    // Multiband compressor: up to 4 bands (LR4, phase-coherent), one stereo-linked
    // compressor per band. Parallel mix blends against the crossover's own sum
    // (the all-passed dry signal), so a mix below 100 % cannot comb.
    struct DynBand { bool on { true }; float thresh { 0 }, ratio { 2 }, atk { 20 }, rel { 150 }, gain { 0 }; };

    class DynamicsModule
    {
    public:
        static constexpr int maxBands = 4;

        void prepare (double sampleRate)
        {
            sr = sampleRate;
            xo.prepare (sr);
            for (auto& c : comp) { c.prepare (sr, 2); c.reset(); }
        }

        void reset() { xo.reset(); for (auto& c : comp) c.reset(); }

        void setParams (int bands, float x1, float x2, float x3, float knee, bool rms, float mixAmt, const std::array<DynBand, maxBands>& b) noexcept
        {
            xo.setBands (bands, x1, x2, x3);
            mix = std::clamp (mixAmt, 0.0f, 1.0f);
            for (int k = 0; k < maxBands; ++k)
            {
                fx::Compressor::Params p;
                p.thresholdDb = b[(size_t) k].thresh;
                p.ratio = std::max (1.0f, b[(size_t) k].ratio);
                p.attackMs = b[(size_t) k].atk;
                p.releaseMs = b[(size_t) k].rel;
                p.kneeDb = knee;
                p.makeupDb = b[(size_t) k].gain;
                p.rms = rms;
                p.sidechainHpfHz = 1.0f;   // the band split already filters the detector
                comp[(size_t) k].setParams (p);
                bandOn[(size_t) k] = b[(size_t) k].on;
                makeup[(size_t) k] = std::pow (10.0f, b[(size_t) k].gain / 20.0f);
                ratio1[(size_t) k] = p.ratio <= 1.001f;
            }
        }

        void process (float* l, float* r, int n) noexcept
        {
            const int nb = xo.bands();
            std::array<float, maxBands> gr {};
            float bl[maxBands], br[maxBands];
            for (int i = 0; i < n; ++i)
            {
                xo.split (0, l[i], bl);
                xo.split (1, r[i], br);
                float dryL = 0, dryR = 0, wetL = 0, wetR = 0;
                for (int k = 0; k < nb; ++k)
                {
                    dryL += bl[k]; dryR += br[k];
                    if (bandOn[(size_t) k])
                    {
                        float a = bl[k], c = br[k];
                        if (ratio1[(size_t) k]) { a *= makeup[(size_t) k]; c *= makeup[(size_t) k]; }   // the compressor skips makeup at 1:1
                        else comp[(size_t) k].process (a, c);                                             // includes makeup
                        gr[(size_t) k] = std::max (gr[(size_t) k], comp[(size_t) k].gainReductionDb());
                        wetL += a; wetR += c;
                    }
                    else
                    {
                        wetL += bl[k]; wetR += br[k];
                    }
                }
                l[i] = dryL + mix * (wetL - dryL);
                r[i] = dryR + mix * (wetR - dryR);
            }
            for (int k = 0; k < maxBands; ++k)
                bandGr[(size_t) k].store (k < nb && bandOn[(size_t) k] ? gr[(size_t) k] : 0.0f, std::memory_order_relaxed);
        }

        float gainReductionDb (int k) const noexcept { return bandGr[(size_t) k].load (std::memory_order_relaxed); }
        const fx::Crossover& crossover() const noexcept { return xo; }

    private:
        double sr { 48000.0 };
        fx::Crossover xo;
        std::array<fx::Compressor, maxBands> comp;
        std::array<bool, maxBands> bandOn {};
        std::array<float, maxBands> makeup {};
        std::array<bool, maxBands> ratio1 {};
        float mix { 1.0f };
        std::array<std::atomic<float>, maxBands> bandGr {};
    };

    //==============================================================================
    // Multiband exciter: 4x linear-phase oversampling around a band split and a
    // per-band waveshaper. The shaper is normalised to unity small-signal gain
    // (shape(g x) / g), so it adds harmonics without changing the level of
    // quiet material. Mix blends dry and wet inside the oversampled domain, so
    // they are time-aligned. Adds a fixed latency (reported) while on.
    struct ExcBand { float drive { 6 }, mix { 0 }; int mode { 0 }; };

    class ExciterModule
    {
    public:
        static constexpr int maxBands = 4;

        void prepare (double sampleRate, int maxBlock)
        {
            sr = sampleRate;
            os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, false, true);
            os->initProcessing ((size_t) std::max (1, maxBlock));
            xo.prepare (sr * 4.0);
            dcCoeff = std::exp (-2.0f * 3.14159265f * 8.0f / (float) sr);
            reset();
        }

        void reset()
        {
            if (os) os->reset();
            xo.reset();
            dc[0] = dc[1] = 0.0f;
        }

        int latencySamples() const noexcept { return os ? (int) std::lround (os->getLatencyInSamples()) : 0; }

        void setParams (int bands, float x1, float x2, float x3, const std::array<ExcBand, maxBands>& b) noexcept
        {
            xo.setBands (bands, x1, x2, x3);
            for (int k = 0; k < maxBands; ++k)
            {
                band[(size_t) k] = b[(size_t) k];
                gain[(size_t) k] = std::pow (10.0f, std::clamp (b[(size_t) k].drive, 0.0f, 24.0f) / 20.0f);
            }
        }

        static float shape (float x, int mode) noexcept
        {
            switch (mode)
            {
                case 1:  return std::tanh (x * 0.85f + 0.12f * x * x) / 0.85f;      // Tube: asymmetric, even harmonics
                case 2:  return (std::tanh (x) + 0.5f * std::tanh (0.5f * x)) / 1.25f; // Diode: two knees
                default: return std::tanh (x);                                        // Tape: odd, soft
            }
        }

        void process (float* l, float* r, int n) noexcept
        {
            float* chans[2] = { l, r };
            juce::dsp::AudioBlock<float> block (chans, 2, (size_t) n);
            auto up = os->processSamplesUp (block);
            const int un = (int) up.getNumSamples();
            float* ul = up.getChannelPointer (0);
            float* ur = up.getChannelPointer (1);
            const int nb = xo.bands();
            float bl[maxBands], br[maxBands];
            for (int i = 0; i < un; ++i)
            {
                xo.split (0, ul[i], bl);
                xo.split (1, ur[i], br);
                float ol = 0, orr = 0;
                for (int k = 0; k < nb; ++k)
                {
                    const auto& b = band[(size_t) k];
                    if (b.mix <= 0.0f) { ol += bl[k]; orr += br[k]; continue; }
                    const float g = gain[(size_t) k];
                    const float wl = shape (bl[k] * g, b.mode) / g, wr = shape (br[k] * g, b.mode) / g;
                    ol += bl[k] + b.mix * (wl - bl[k]);
                    orr += br[k] + b.mix * (wr - br[k]);
                }
                ul[i] = ol; ur[i] = orr;
            }
            os->processSamplesDown (block);
            // The tube curve is asymmetric: remove the DC it makes.
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                {
                    dc[c] = chans[c][i] * (1.0f - dcCoeff) + dc[c] * dcCoeff;
                    chans[c][i] -= dc[c];
                }
        }

    private:
        double sr { 48000.0 };
        std::unique_ptr<juce::dsp::Oversampling<float>> os;
        fx::Crossover xo;
        std::array<ExcBand, maxBands> band {};
        std::array<float, maxBands> gain {};
        float dcCoeff { 0.999f }, dc[2] {};
    };

    //==============================================================================
    // Multiband imager: per-band side gain (0 % mono .. 200 %), plus "mono
    // below": a 24 dB/oct high-pass on the side signal only.
    class ImagerModule
    {
    public:
        static constexpr int maxBands = 4;

        void prepare (double sampleRate) { sr = sampleRate; xo.prepare (sr); monoHz = -1.0f; reset(); }
        void reset() { xo.reset(); for (auto& s : hp) s = {}; }

        void setParams (int bands, float x1, float x2, float x3, const std::array<float, maxBands>& width01x2, float monoBelowHz) noexcept
        {
            xo.setBands (bands, x1, x2, x3);
            width = width01x2;
            if (monoBelowHz != monoHz)
            {
                monoHz = monoBelowHz;
                if (monoHz > 1.0f)
                {
                    constexpr double pi = 3.14159265358979323846, q = 0.70710678118654752;
                    const double w = 2.0 * pi * std::min ((double) monoHz, sr * 0.45) / sr, cw = std::cos (w), alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
                    hpC = { (1 + cw) * 0.5 / a0, -(1 + cw) / a0, (1 + cw) * 0.5 / a0, -2 * cw / a0, (1 - alpha) / a0 };
                }
            }
        }

        void process (float* l, float* r, int n) noexcept
        {
            const int nb = xo.bands();
            float bl[maxBands], br[maxBands];
            for (int i = 0; i < n; ++i)
            {
                double m = 0, s = 0;
                if (nb == 1)
                {
                    m = 0.5 * (l[i] + r[i]); s = 0.5 * (l[i] - r[i]) * width[0];
                }
                else
                {
                    xo.split (0, l[i], bl);
                    xo.split (1, r[i], br);
                    for (int k = 0; k < nb; ++k) { m += 0.5 * (bl[k] + br[k]); s += 0.5 * (bl[k] - br[k]) * width[(size_t) k]; }
                }
                if (monoHz > 1.0f) s = run (hp[1], run (hp[0], s));
                l[i] = (float) (m + s); r[i] = (float) (m - s);
            }
        }

    private:
        struct C { double b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 }; };
        struct S { double z1 { 0 }, z2 { 0 }; };
        double run (S& st, double x) const noexcept
        {
            const double y = hpC.b0 * x + st.z1;
            st.z1 = hpC.b1 * x - hpC.a1 * y + st.z2;
            st.z2 = hpC.b2 * x - hpC.a2 * y;
            return y;
        }

        double sr { 48000.0 };
        fx::Crossover xo;
        std::array<float, maxBands> width { 1, 1, 1, 1 };
        float monoHz { -1.0f };
        C hpC;
        std::array<S, 2> hp {};
    };
}
