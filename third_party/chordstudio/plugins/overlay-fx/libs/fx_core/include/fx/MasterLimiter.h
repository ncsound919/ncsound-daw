#pragma once

// Look-ahead true-peak maximizer.
//
//   input gain  = -threshold (Threshold -6 dB drives the limiter 6 dB harder)
//   detection   = the true peak of the driven signal: polyphase interpolation
//                 (2/4/8/16x, 24 taps per phase) plus the sample itself
//   gain        = required gain -> sliding minimum over the look-ahead window ->
//                 release -> moving average over the look-ahead (the attack ramp)
//   guarantee   = the gain reaching a sample is never above what that sample's
//                 true peak needs, so peaks land at or below the ceiling. The
//                 residual is only the interpolator's own error (tested < 0.1 dB).
//
// Character (independent designs, not emulations of any product):
//   Clean   1.0 ms look-ahead, fixed release.
//   Smooth  3.0 ms look-ahead, release stretches with sustained gain reduction.
//   Punch   0.5 ms look-ahead after a gentle soft clip of the top 1.5 dB, so
//           short peaks are shaved instead of ducking the whole mix.
// Transient emphasis lifts attacks (fast/slow envelope ratio, up to +6 dB)
// before the limiter. Stereo link blends each channel's own gain with the
// linked (smaller) gain.

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace fx
{
    class LookaheadLimiter
    {
    public:
        LookaheadLimiter() = default;
        LookaheadLimiter (const LookaheadLimiter&) = delete;              // holds a pointer into its own kernels
        LookaheadLimiter& operator= (const LookaheadLimiter&) = delete;
        enum class Mode { clean = 0, smooth, punch };
        static constexpr int maxDetectFactor = 16, detectTaps = 24, detectDelay = detectTaps / 2 - 1;

        void prepare (double sampleRate) noexcept
        {
            sr = sampleRate > 0.0 ? sampleRate : 48000.0;
            // Largest look-ahead (3 ms) plus the detector's alignment margin.
            maxLook = (int) std::ceil (0.003 * sr) + 1;
            const int delayLen = maxLook + detectDelay + 8;
            for (auto& d : delay) d.assign ((size_t) delayLen, 0.0f);
            for (auto& m : minQueue) m.assign ((size_t) (maxLook + 4) * 2 + 8, {});
            for (auto& b : box) b.assign ((size_t) maxLook + 4, 1.0);
            buildKernels();
            setDetectFactor (detectFactor, true);
            setParams (params);
            reset();
        }

        void reset() noexcept
        {
            for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
            for (auto& h : hist) h.fill (0.0f);
            for (int c = 0; c < 2; ++c)
            {
                qHead[c] = qTail[c] = 0;
                std::fill (box[(size_t) c].begin(), box[(size_t) c].end(), 1.0);
                boxSum[c] = (double) look;
                boxPos[c] = 0;
                released[c] = 1.0;
            }
            histPos = delayPos = 0;
            step = 0;
            envFast = envSlow = 0.0f;
            grAvg = 0.0;
            blockGrDb = 0.0f;
        }

        struct Params
        {
            float thresholdDb { 0.0f };   // input drive = -threshold
            float ceilingDb { -1.0f };
            float releaseMs { 120.0f };
            Mode  mode { Mode::clean };
            float link { 1.0f };          // 0 independent .. 1 fully linked
            float transient { 0.0f };     // 0..1
        };

        void setParams (const Params& p) noexcept
        {
            params = p;
            drive = (float) std::pow (10.0, -std::min (0.0f, p.thresholdDb) / 20.0);
            ceiling = (float) std::pow (10.0, std::min (0.0f, p.ceilingDb) / 20.0);
            const double lookMs = p.mode == Mode::smooth ? 3.0 : p.mode == Mode::punch ? 0.5 : 1.0;
            const int want = std::clamp ((int) std::lround (lookMs * 0.001 * sr), 1, maxLook);
            if (want != look)
            {
                look = want;
                for (int c = 0; c < 2; ++c)
                {
                    std::fill (box[(size_t) c].begin(), box[(size_t) c].end(), released[c]);
                    boxSum[c] = released[c] * look;
                    boxPos[c] = 0;
                }
            }
            relCoef = std::exp (-1.0 / (std::max (1.0f, p.releaseMs) * 0.001 * sr));
            logRel = std::log (relCoef);
            grAvgCoef = std::exp (-1.0 / (0.5 * sr));
        }

        // Switches between the 2/4/8/16x interpolators built in prepare(): no
        // allocation, safe on the audio thread.
        void setDetectFactor (int factor, bool /*force*/ = false) noexcept
        {
            factor = factor <= 2 ? 2 : factor <= 4 ? 4 : factor <= 8 ? 8 : 16;
            detectFactor = factor;
            const int idx = factor == 2 ? 0 : factor == 4 ? 1 : factor == 8 ? 2 : 3;
            kernel = kernels[(size_t) idx].empty() ? nullptr : kernels[(size_t) idx].data();
        }

        /** Samples of delay the limiter adds (report to the host). */
        int latencySamples() const noexcept { return look + detectDelay + 1; }
        /** Largest gain reduction in the last block, dB (>= 0). */
        float gainReductionDb() const noexcept { return blockGrDb; }

        void process (float* l, float* r, int n) noexcept
        {
            if (kernel == nullptr || delay[0].empty()) return;   // not prepared
            const bool smooth = params.mode == Mode::smooth, punch = params.mode == Mode::punch;
            const float clipStart = ceiling * 0.8414f;   // top 1.5 dB soft-clipped in Punch
            const float fastA = std::exp (-1.0f / (0.0005f * (float) sr)), slowA = std::exp (-1.0f / (0.010f * (float) sr));
            const float envR = std::exp (-1.0f / (0.030f * (float) sr));
            const int D = look + detectDelay + 1;
            const int dlen = (int) delay[0].size();
            float minGain = 1.0f;

            for (int i = 0; i < n; ++i)
            {
                float x[2] = { l[i] * drive, r[i] * drive };

                if (params.transient > 0.0f)
                {
                    const float a = std::max (std::abs (x[0]), std::abs (x[1]));
                    envFast = a > envFast ? a + (envFast - a) * fastA : a + (envFast - a) * envR;
                    envSlow = a > envSlow ? a + (envSlow - a) * slowA : a + (envSlow - a) * envR;
                    const float ratio = envSlow > 1.0e-6f ? envFast / envSlow : 1.0f;
                    const float g = std::min (2.0f, 1.0f + params.transient * std::max (0.0f, ratio - 1.0f));
                    x[0] *= g; x[1] *= g;
                }

                if (punch)
                    for (auto& v : x) v = softClip (v, clipStart, ceiling * 1.1885f);

                // Required gain from the true peak of each channel.
                double need[2];
                for (int c = 0; c < 2; ++c)
                {
                    auto& h = hist[(size_t) c];
                    h[(size_t) histPos] = x[c];
                    float peak = std::abs (h[(size_t) ((histPos - detectDelay + detectTaps) % detectTaps)]);
                    for (int p = 0; p < detectFactor; ++p)
                    {
                        float acc = 0.0f;
                        const float* k = kernel + p * detectTaps;
                        // Rolling index instead of (histPos - t + detectTaps)
                        // % detectTaps: the modulo sat in the innermost loop,
                        // one integer division per tap per phase per channel.
                        int idx = histPos;
                        for (int t = 0; t < detectTaps; ++t)
                        {
                            acc += h[(size_t) idx] * k[t];
                            idx = idx == 0 ? detectTaps - 1 : idx - 1;
                        }
                        peak = std::max (peak, std::abs (acc));
                    }
                    need[c] = peak > ceiling ? (double) ceiling / (double) peak : 1.0;
                }
                histPos = (histPos + 1) % detectTaps;

                const double linked = std::min (need[0], need[1]);
                double rc = relCoef;
                if (smooth)
                {
                    // Release stretches with sustained reduction (0.5 s average of the applied GR).
                    rc = std::exp (logRel / (1.0 + std::max (0.0, grAvg) / 2.0));
                }
                double g[2];
                for (int c = 0; c < 2; ++c)
                {
                    const double want = linked + (need[c] - linked) * (1.0 - (double) params.link);
                    const double h = slidingMin (c, want);
                    // Release (instant down).
                    released[c] = h < released[c] ? h : h + (released[c] - h) * rc;
                    // Attack ramp: moving average over the look-ahead.
                    auto& bx = box[(size_t) c];
                    boxSum[c] += released[c] - bx[(size_t) boxPos[c]];
                    bx[(size_t) boxPos[c]] = released[c];
                    boxPos[c] = (boxPos[c] + 1) % look;
                    g[c] = std::min (1.0, boxSum[c] / (double) look);
                }

                // Delay the (driven) audio so the gain arrives with its peak.
                for (int c = 0; c < 2; ++c) delay[(size_t) c][(size_t) delayPos] = x[c];
                const int rd = (delayPos - D + dlen) % dlen;
                delayPos = (delayPos + 1) % dlen;
                l[i] = delay[0][(size_t) rd] * (float) g[0];
                r[i] = delay[1][(size_t) rd] * (float) g[1];
                const double gmin = std::min (g[0], g[1]);
                minGain = std::min (minGain, (float) gmin);
                // The dB conversion only contributes when gain was actually
                // reduced; gmin is capped at 1.0 above, so at unity the log
                // term is zero and this is the same update without the log.
                grAvg *= grAvgCoef;
                if (gmin < 1.0)
                    grAvg += -20.0 * std::log10 (std::max (1.0e-6, gmin)) * (1.0 - grAvgCoef);
                ++step;
            }

            blockGrDb = minGain < 1.0f ? -20.0f * std::log10 (std::max (1.0e-6f, minGain)) : 0.0f;
        }

    private:
        void buildKernels()
        {
            constexpr double pi = 3.14159265358979323846;
            for (int idx = 0; idx < 4; ++idx)
            {
                const int factor = 2 << idx, len = factor * detectTaps;
                const double fc = 0.5 / factor, m = (len - 1) / 2.0;
                std::vector<double> h ((size_t) len);
                for (int n = 0; n < len; ++n)
                {
                    const double x = n - m;
                    const double sinc = x == 0.0 ? 2.0 * fc : std::sin (2.0 * pi * fc * x) / (pi * x);
                    h[(size_t) n] = sinc * 0.5 * (1.0 - std::cos (2.0 * pi * n / (len - 1)));
                }
                auto& k = kernels[(size_t) idx];
                k.assign ((size_t) len, 0.0f);
                for (int p = 0; p < factor; ++p)
                {
                    double dc = 0.0;
                    for (int t = 0; t < detectTaps; ++t) dc += h[(size_t) (p + t * factor)];
                    for (int t = 0; t < detectTaps; ++t) k[(size_t) (p * detectTaps + t)] = (float) (h[(size_t) (p + t * factor)] / dc);
                }
            }
        }

        static float softClip (float v, float start, float limit) noexcept
        {
            const float a = std::abs (v);
            if (a <= start) return v;
            const float room = limit - start;
            const float y = start + room * std::tanh ((a - start) / room);
            return std::copysign (y, v);
        }

        // Minimum of the last (look + 2) values (monotonic deque).
        double slidingMin (int c, double v) noexcept
        {
            auto& q = minQueue[(size_t) c];
            const int cap = (int) q.size();
            const long long window = look + 2;
            while (qTail[c] != qHead[c])
            {
                const int last = (qTail[c] - 1 + cap) % cap;
                if (q[(size_t) last].v >= v) qTail[c] = last; else break;
            }
            q[(size_t) qTail[c]] = { v, step };
            qTail[c] = (qTail[c] + 1) % cap;
            while (q[(size_t) qHead[c]].t <= step - window) qHead[c] = (qHead[c] + 1) % cap;
            return q[(size_t) qHead[c]].v;
        }

        struct Entry { double v { 1.0 }; long long t { 0 }; };

        double sr { 48000.0 };
        Params params {};
        float drive { 1.0f }, ceiling { 0.891f };
        double relCoef { 0.999 };
        int look { 48 }, maxLook { 145 };
        int detectFactor { 4 };
        std::array<std::vector<float>, 4> kernels;   // 2x, 4x, 8x, 16x
        const float* kernel { nullptr };
        std::array<std::array<float, detectTaps>, 2> hist {};
        int histPos { 0 };
        std::array<std::vector<float>, 2> delay;
        int delayPos { 0 };
        std::array<std::vector<Entry>, 2> minQueue;
        int qHead[2] { 0, 0 }, qTail[2] { 0, 0 };
        std::array<std::vector<double>, 2> box;
        double boxSum[2] { 48.0, 48.0 };
        int boxPos[2] { 0, 0 };
        double released[2] { 1.0, 1.0 };
        long long step { 0 };
        float envFast { 0.0f }, envSlow { 0.0f };
        float blockGrDb { 0.0f };
        double grAvg { 0.0 }, logRel { -0.001 }, grAvgCoef { 0.9999 };
    };
}
