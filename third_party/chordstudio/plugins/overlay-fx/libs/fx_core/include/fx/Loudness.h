#pragma once

// Real-time loudness metering per ITU-R BS.1770-4 / EBU R128 (Tech 3341/3342).
//
//   K-weighting   - the two 1770 stages (pre-filter shelf + RLB high-pass),
//                   designed at the running sample rate from the analogue
//                   prototype (identical to the published 48 kHz coefficients).
//   Momentary     - 400 ms window, updated every 100 ms.
//   Short-term    - 3 s window, updated every 100 ms.
//   Integrated    - 400 ms blocks with 75 % overlap, absolute gate -70 LUFS,
//                   relative gate -10 LU. Blocks are kept in a 0.02 LU histogram
//                   with exact energy sums, so an hour of audio costs no memory.
//   LRA           - EBU Tech 3342: short-term values (10 per second), absolute
//                   gate -70, relative gate -20 LU below their power mean,
//                   P95 - P10.
//   True peak     - 4x polyphase interpolation (48-tap Hann-windowed sinc, unity
//                   DC per phase). Not the exact Annex 2 filter; it tracks the
//                   analytic inter-sample peak of test tones within 0.1 dB.
//
// Thread contract: process() on the audio thread; the getters are atomics and
// may be read from any thread. reset()/resetIntegrated() are audio-thread only
// (the processor defers a UI request to the next block).

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fx
{
    class LoudnessMeter
    {
    public:
        static constexpr float silenceLufs = -70.0f;
        static constexpr int maxChannels = 2;

        void prepare (double sampleRate, int numChannels)
        {
            sr = sampleRate > 0.0 ? sampleRate : 48000.0;
            channels = std::clamp (numChannels, 1, maxChannels);
            subBlockLen = std::max (1, (int) std::lround (0.1 * sr));
            designKWeighting();
            buildTruePeakKernel();
            reset();
        }

        void reset() noexcept
        {
            for (auto& k : kState) k = {};
            for (auto& t : tpHist) t.fill (0.0f);
            tpPos = 0;
            subSum = 0.0;
            subCount = 0;
            ring.fill (0.0);
            ringPos = 0;
            ringFilled = 0;
            momentaryLufs.store (silenceLufs);
            shortTermLufs.store (silenceLufs);
            resetIntegrated();
        }

        // Integrated, LRA and the true-peak maximum start again.
        void resetIntegrated() noexcept
        {
            intCount.fill (0);
            intEnergy.fill (0.0);
            lraCount.fill (0);
            lraEnergy.fill (0.0);
            integratedLufs.store (silenceLufs);
            lraLu.store (0.0f);
            tpMax = 0.0f;
            truePeakMaxDb.store (-120.0f);
        }

        void process (const float* const* in, int numCh, int numSamples) noexcept
        {
            if (in == nullptr || numSamples <= 0 || numCh <= 0)
                return;

            const int nc = std::min (numCh, channels);
            float blockTp = 0.0f;

            for (int i = 0; i < numSamples; ++i)
            {
                double e = 0.0;

                for (int c = 0; c < nc; ++c)
                {
                    const double x = (double) in[c][i];
                    auto& s = kState[(size_t) c];
                    // Stage 1: pre-filter (shelf).
                    const double y1 = pre.b0 * x + s.p1;   // transposed direct form II
                    s.p1 = pre.b1 * x - pre.a1 * y1 + s.p2;
                    s.p2 = pre.b2 * x - pre.a2 * y1;
                    // Stage 2: RLB high-pass.
                    const double y2 = rlb.b0 * y1 + s.r1;
                    s.r1 = rlb.b1 * y1 - rlb.a1 * y2 + s.r2;
                    s.r2 = rlb.b2 * y1 - rlb.a2 * y2;
                    e += y2 * y2;   // stereo L/R weights are 1.0 (1770-4 Table 2)

                    blockTp = std::max (blockTp, truePeakSample (c, (float) x));
                }

                tpPos = (tpPos + 1) % tpTaps;
                subSum += e;

                if (++subCount >= subBlockLen)
                    endSubBlock();
            }

            tpMax = std::max (tpMax, blockTp);
            truePeakMaxDb.store (tpMax > 1.0e-9f ? 20.0f * std::log10 (tpMax) : -120.0f);
            truePeakRecent.store (blockTp);
        }

        float momentary() const noexcept  { return momentaryLufs.load(); }
        float shortTerm() const noexcept  { return shortTermLufs.load(); }
        float integrated() const noexcept { return integratedLufs.load(); }
        float lra() const noexcept        { return lraLu.load(); }
        float truePeakMaxDbtp() const noexcept { return truePeakMaxDb.load(); }
        /** Highest interpolated sample of the last processed block (linear). */
        float truePeakLastBlock() const noexcept { return truePeakRecent.load(); }
        /** True once 3 s of audio have been measured (short-term and LRA are defined). */
        bool shortTermValid() const noexcept { return ringFilled >= kShortBlocks; }

        static float energyToLufs (double e) noexcept
        {
            return e > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (e)) : silenceLufs;
        }

        // Exposed for tests: the K-weighting section coefficients.
        struct Sos { double b0 { 1 }, b1 { 0 }, b2 { 0 }, a1 { 0 }, a2 { 0 }; };
        const Sos& prefilter() const noexcept { return pre; }
        const Sos& rlbFilter() const noexcept { return rlb; }

    private:
        static constexpr int kMomBlocks = 4, kShortBlocks = 30;
        static constexpr double histLo = -70.0, histStep = 0.02;
        static constexpr int histBins = 4000;   // -70 .. +10 LUFS
        static constexpr int tpPhases = 4, tpTaps = 12;

        struct KState { double p1 { 0 }, p2 { 0 }, r1 { 0 }, r2 { 0 }; };

        void designKWeighting() noexcept
        {
            constexpr double pi = 3.14159265358979323846;
            {   // Pre-filter: high shelf, parameters from the 1770 analogue prototype.
                const double f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196;
                const double k = std::tan (pi * f0 / sr), vh = std::pow (10.0, g / 20.0), vb = std::pow (vh, 0.4996667741545416);
                const double a0 = 1.0 + k / q + k * k;
                pre = { (vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                        2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
            }
            {   // RLB: 2nd-order high-pass (numerator 1, -2, 1 as published).
                const double f0 = 38.13547087602444, q = 0.5003270373238773;
                const double k = std::tan (pi * f0 / sr), a0 = 1.0 + k / q + k * k;
                rlb = { 1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
            }
        }

        void buildTruePeakKernel() noexcept
        {
            constexpr int len = tpPhases * tpTaps;
            constexpr double pi = 3.14159265358979323846, fc = 0.125;
            const double m = (len - 1) / 2.0;
            std::array<double, len> h {};
            for (int n = 0; n < len; ++n)
            {
                const double x = n - m;
                const double sinc = x == 0.0 ? 2.0 * fc : std::sin (2.0 * pi * fc * x) / (pi * x);
                h[(size_t) n] = sinc * 0.5 * (1.0 - std::cos (2.0 * pi * n / (len - 1)));
            }
            for (int p = 0; p < tpPhases; ++p)
            {
                double dc = 0.0;
                for (int t = 0; t < tpTaps; ++t) dc += h[(size_t) (p + t * tpPhases)];
                for (int t = 0; t < tpTaps; ++t) tpKernel[(size_t) p][(size_t) t] = (float) (h[(size_t) (p + t * tpPhases)] / dc);
            }
        }

        float truePeakSample (int c, float x) noexcept
        {
            auto& hist = tpHist[(size_t) c];
            // tpPos is shared by all channels; process() advances it once per frame.
            hist[(size_t) tpPos] = x;
            float peak = 0.0f;
            for (int p = 0; p < tpPhases; ++p)
            {
                float acc = 0.0f;
                const float* k = tpKernel[(size_t) p].data();
                // Rolling index instead of (tpPos - t + tpTaps) % tpTaps: the
                // modulo was inside the innermost loop, and on the real-time
                // budget an integer division per tap mattered (4 phases x
                // 12 taps x 2 channels per sample).
                int idx = tpPos;
                for (int t = 0; t < tpTaps; ++t)
                {
                    acc += hist[(size_t) idx] * k[(size_t) t];
                    idx = idx == 0 ? tpTaps - 1 : idx - 1;
                }
                peak = std::max (peak, std::abs (acc));
            }
            return peak;
        }

        static int binOf (float lufs) noexcept
        {
            return std::clamp ((int) std::floor ((lufs - histLo) / histStep), 0, histBins - 1);
        }

        void endSubBlock() noexcept
        {
            ring[(size_t) ringPos] = subSum / (double) subBlockLen;
            ringPos = (ringPos + 1) % kShortBlocks;
            ringFilled = std::min (ringFilled + 1, kShortBlocks);
            subSum = 0.0;
            subCount = 0;

            auto meanOfLast = [this] (int count)
            {
                double s = 0.0;
                for (int k = 1; k <= count; ++k)
                    s += ring[(size_t) ((ringPos - k + kShortBlocks) % kShortBlocks)];
                return s / (double) count;
            };

            if (ringFilled >= kMomBlocks)
            {
                const double em = meanOfLast (kMomBlocks);
                const float m = energyToLufs (em);
                momentaryLufs.store (m);

                // Every momentary value is a 400 ms gating block (75 % overlap).
                if (m >= silenceLufs)
                {
                    const int b = binOf (m);
                    ++intCount[(size_t) b];
                    intEnergy[(size_t) b] += em;
                    updateIntegrated();
                }
            }

            if (ringFilled >= kShortBlocks)
            {
                const double es = meanOfLast (kShortBlocks);
                const float s = energyToLufs (es);
                shortTermLufs.store (s);

                if (s >= silenceLufs)
                {
                    const int b = binOf (s);
                    ++lraCount[(size_t) b];
                    lraEnergy[(size_t) b] += es;
                    updateLra();
                }
            }
            else if (ringFilled > 0)
            {
                shortTermLufs.store (energyToLufs (meanOfLast (ringFilled)));   // partial window (< 3 s)
            }
        }

        void updateIntegrated() noexcept
        {
            std::uint64_t n = 0; double e = 0.0;
            for (int b = 0; b < histBins; ++b) { n += intCount[(size_t) b]; e += intEnergy[(size_t) b]; }
            if (n == 0) { integratedLufs.store (silenceLufs); return; }
            const float rel = energyToLufs (e / (double) n) - 10.0f;
            n = 0; e = 0.0;
            // Blocks in the bin that holds the gate are included when the bin's
            // own mean is at or above it (0.02 LU bins: error well below 0.01 LU).
            for (int b = binOf (rel); b < histBins; ++b)
            {
                if (intCount[(size_t) b] == 0) continue;
                if (energyToLufs (intEnergy[(size_t) b] / (double) intCount[(size_t) b]) < rel) continue;
                n += intCount[(size_t) b]; e += intEnergy[(size_t) b];
            }
            integratedLufs.store (n > 0 ? energyToLufs (e / (double) n) : silenceLufs);
        }

        void updateLra() noexcept
        {
            std::uint64_t n = 0; double e = 0.0;
            for (int b = 0; b < histBins; ++b) { n += lraCount[(size_t) b]; e += lraEnergy[(size_t) b]; }
            if (n < 2) { lraLu.store (0.0f); return; }
            const float rel = energyToLufs (e / (double) n) - 20.0f;
            const int b0 = binOf (rel);
            std::uint64_t kept = 0;
            for (int b = b0; b < histBins; ++b) kept += lraCount[(size_t) b];
            if (kept < 2) { lraLu.store (0.0f); return; }
            auto percentile = [&] (double q)
            {
                const auto target = (std::uint64_t) std::floor (q * (double) (kept - 1));
                std::uint64_t acc = 0;
                for (int b = b0; b < histBins; ++b)
                {
                    acc += lraCount[(size_t) b];
                    if (acc > target) return histLo + ((double) b + 0.5) * histStep;
                }
                return histLo + (histBins - 0.5) * histStep;
            };
            lraLu.store ((float) std::max (0.0, percentile (0.95) - percentile (0.10)));
        }

        double sr { 48000.0 };
        int channels { 2 };
        int subBlockLen { 4800 };
        Sos pre, rlb;
        std::array<KState, maxChannels> kState {};

        std::array<std::array<float, tpTaps>, tpPhases> tpKernel {};
        std::array<std::array<float, tpTaps>, maxChannels> tpHist {};
        int tpPos { 0 };
        float tpMax { 0.0f };

        double subSum { 0.0 };
        int subCount { 0 };
        std::array<double, kShortBlocks> ring {};
        int ringPos { 0 }, ringFilled { 0 };

        std::array<std::uint32_t, histBins> intCount {}, lraCount {};
        std::array<double, histBins> intEnergy {}, lraEnergy {};

        std::atomic<float> momentaryLufs { silenceLufs }, shortTermLufs { silenceLufs }, integratedLufs { silenceLufs };
        std::atomic<float> lraLu { 0.0f }, truePeakMaxDb { -120.0f }, truePeakRecent { 0.0f };
    };
}
