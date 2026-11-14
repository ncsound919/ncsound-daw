#pragma once

// Monophonic voice-to-MIDI: FFT-accelerated YIN candidates, a small Viterbi
// tracker over voiced/unvoiced states, then hysteresis-based note segmentation.
//
// The earlier implementation took the first YIN minimum below a fixed
// threshold, which turned one noisy frame into an octave error or a new note.
// This follows the useful part of pYIN (multiple YIN candidates and a path
// through them) without pulling a probabilistic-pitch library into the engine.
// Header-only, no allocation during audio callbacks: this is an offline path.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace nc::engine
{
struct V2MNote
{
    int midiNote = 60;
    double startBeat = 0.0;
    double lengthBeats = 0.25;
    int velocity = 100;
    float confidence = 0.0f;
};

struct V2MConfig
{
    int sampleRate = 48000;
    std::size_t window = 2048;
    std::size_t hop = 512;
    float threshold = 0.15f;     // YIN CMND below this is a pitch candidate
    float minConfidence = 0.35f; // discard candidates weaker than 1 - CMND
    double bpm = 120.0;

    float minFreq = 40.0f;
    float maxFreq = 2000.0f;
    int maxCandidates = 4;
    float pitchJumpCost = 0.12f; // Viterbi cost per semitone of f0 movement
    float voicingCost = 0.35f;   // cost of crossing the voiced/unvoiced state
    float rmsGate = 1e-4f;
    float minNoteSeconds = 0.04f;
    float gapBridgeSeconds = 0.035f;
    float noteHysteresis = 0.65f; // semitones before a note change is considered
};

namespace detail
{
struct YinCandidate
{
    double midi = 60.0;
    double cost = 1.0; // CMND at the candidate's period: lower is better
};

inline std::size_t nextPowerOfTwo (std::size_t n)
{
    std::size_t p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

// In-place iterative radix-2 FFT. The engine deliberately has no JUCE/DSP
// dependency, so the transform is kept here rather than linking all of JUCE
// into every consumer of SlicePlanner/VoiceToMidi.
inline void fft (std::vector<double>& re, std::vector<double>& im, bool inverse)
{
    const auto n = re.size();

    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        auto bit = n >> 1;

        for (; j & bit; bit >>= 1)
            j ^= bit;

        j ^= bit;

        if (i < j)
        {
            std::swap (re[i], re[j]);
            std::swap (im[i], im[j]);
        }
    }

    for (std::size_t len = 2; len <= n; len <<= 1)
    {
        const auto angle = (inverse ? 2.0 : -2.0) * 3.14159265358979323846
                           / static_cast<double> (len);
        const auto wRe = std::cos (angle);
        const auto wIm = std::sin (angle);

        for (std::size_t i = 0; i < n; i += len)
        {
            double curRe = 1.0;
            double curIm = 0.0;

            for (std::size_t j = 0; j < len / 2; ++j)
            {
                const auto uRe = re[i + j];
                const auto uIm = im[i + j];
                const auto vRe = re[i + j + len / 2] * curRe
                                 - im[i + j + len / 2] * curIm;
                const auto vIm = re[i + j + len / 2] * curIm
                                 + im[i + j + len / 2] * curRe;

                re[i + j] = uRe + vRe;
                im[i + j] = uIm + vIm;
                re[i + j + len / 2] = uRe - vRe;
                im[i + j + len / 2] = uIm - vIm;

                const auto nextRe = curRe * wRe - curIm * wIm;
                curIm = curRe * wIm + curIm * wRe;
                curRe = nextRe;
            }
        }
    }

    if (inverse)
        for (std::size_t i = 0; i < n; ++i)
        {
            re[i] /= static_cast<double> (n);
            im[i] /= static_cast<double> (n);
        }
}

/** Collects local CMND minima instead of committing to the first one. */
inline void yinCandidates (const float* samples,
                           std::size_t n,
                           double sampleRate,
                           const V2MConfig& cfg,
                           std::vector<YinCandidate>& out)
{
    out.clear();

    if (samples == nullptr || n < 16 || sampleRate <= 0.0)
        return;

    const auto maxTau = n / 2;

    if (maxTau < 2)
        return;

    // Remove DC: it does not change the difference function mathematically,
    // but leaving it in makes the FFT autocorrelation needlessly ill-conditioned.
    double mean = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        mean += samples[i];
    mean /= static_cast<double> (n);

    const auto fftSize = nextPowerOfTwo (n * 2);
    std::vector<double> re (fftSize, 0.0), im (fftSize, 0.0);
    std::vector<double> prefix (n + 1, 0.0);

    for (std::size_t i = 0; i < n; ++i)
    {
        const auto x = static_cast<double> (samples[i]) - mean;
        re[i] = x;
        prefix[i + 1] = prefix[i] + x * x;
    }

    fft (re, im, false);

    for (std::size_t i = 0; i < fftSize; ++i)
    {
        const auto power = re[i] * re[i] + im[i] * im[i];
        re[i] = power;
        im[i] = 0.0;
    }

    fft (re, im, true);

    // CMND (YIN steps 1-4) from the FFT autocorrelation.
    std::vector<double> cmnd (maxTau + 1, 1.0);
    double running = 0.0;

    for (std::size_t tau = 1; tau <= maxTau; ++tau)
    {
        const auto d = prefix[n - tau] + (prefix[n] - prefix[tau])
                       - 2.0 * re[tau];
        running += std::max (0.0, d);
        cmnd[tau] = running > 1e-12 ? std::max (0.0, d) * static_cast<double> (tau) / running
                                    : 1.0;
    }

    const auto threshold = std::clamp (static_cast<double> (cfg.threshold), 0.01, 0.95);
    const auto minConfidence = std::clamp (static_cast<double> (cfg.minConfidence), 0.0, 0.95);
    const auto maxCandidateCost = std::min (threshold, 1.0 - minConfidence);
    const auto minFreq = std::max (1.0, static_cast<double> (cfg.minFreq));
    const auto maxFreq = std::max (minFreq, static_cast<double> (cfg.maxFreq));

    const auto minTau = std::max<std::size_t> (2, static_cast<std::size_t> (std::floor (sampleRate / maxFreq)));
    const auto maxTauRange = std::min (maxTau - 1,
                                       static_cast<std::size_t> (std::ceil (sampleRate / minFreq)));

    if (minTau > maxTauRange)
        return;

    struct Raw
    {
        double freq = 0.0;
        double cost = 1.0;
    };

    std::vector<Raw> raw;
    bool foundFirstMinimum = false;

    for (auto tau = minTau; tau <= maxTauRange; ++tau)
    {
        if (! (cmnd[tau] < cmnd[tau - 1] && cmnd[tau] <= cmnd[tau + 1]))
            continue;

        if (cmnd[tau] > maxCandidateCost)
            continue;

        const auto a = cmnd[tau - 1];
        const auto b = cmnd[tau];
        const auto c = cmnd[tau + 1];
        const auto denom = a + c - 2.0 * b;
        const auto shift = std::abs (denom) > 1e-12 ? 0.5 * (a - c) / denom : 0.0;
        const auto period = static_cast<double> (tau) + std::clamp (shift, -0.5, 0.5);

        if (period <= 0.0)
            continue;

        const auto freq = sampleRate / period;

        if (freq >= minFreq && freq <= maxFreq)
        {
            // YIN's shortest-lag rule: the first credible period is the best
            // defence against choosing a longer subharmonic of the same tone.
            const auto prior = foundFirstMinimum ? 0.0 : 0.005;
            foundFirstMinimum = true;
            raw.push_back ({ freq, std::max (0.0, b - prior) });
        }
    }

    std::sort (raw.begin(), raw.end(), [] (const Raw& a, const Raw& b)
               {
                   if (std::abs (a.cost - b.cost) > 1e-6)
                       return a.cost < b.cost;
                   return a.freq > b.freq; // YIN's shortest-lag tie break
               });
    const auto limit = std::clamp (cfg.maxCandidates, 1, 8);

    for (const auto& candidate : raw)
    {
        const auto midi = 69.0 + 12.0 * std::log2 (candidate.freq / 440.0);
        bool duplicate = false;

        for (const auto& kept : out)
            if (std::abs (kept.midi - midi) < 0.15)
            {
                duplicate = true;
                break;
            }

        if (! duplicate)
            out.push_back ({ midi, candidate.cost });

        if (static_cast<int> (out.size()) >= limit)
            break;
    }
}

inline double median (std::vector<double> values)
{
    if (values.empty())
        return 0.0;

    const auto middle = values.size() / 2;
    std::nth_element (values.begin(), values.begin() + static_cast<std::ptrdiff_t> (middle), values.end());
    return values[middle];
}
} // namespace detail

inline std::vector<V2MNote> voiceToMidi (const float* mono, std::size_t frames, const V2MConfig& cfg)
{
    std::vector<V2MNote> out;

    if (mono == nullptr || frames == 0 || cfg.sampleRate <= 0)
        return out;

    const auto sampleRate = static_cast<double> (cfg.sampleRate);
    const auto win = std::min<std::size_t> (std::max<std::size_t> (cfg.window, 256), 2048);
    const auto hop = std::min<std::size_t> (std::max<std::size_t> (cfg.hop, 64), win);

    if (frames < win)
        return out;

    const auto bpm = cfg.bpm > 1.0 ? cfg.bpm : 120.0;
    const auto hopSeconds = static_cast<double> (hop) / sampleRate;
    const auto minNoteSeconds = std::max (0.0f, cfg.minNoteSeconds);
    const auto gapBridgeSeconds = std::max (0.0f, cfg.gapBridgeSeconds);
    const auto hysteresis = std::max (0.1f, cfg.noteHysteresis);

    std::vector<double> prefix (frames + 1, 0.0);

    for (std::size_t i = 0; i < frames; ++i)
        prefix[i + 1] = prefix[i] + static_cast<double> (mono[i]) * mono[i];

    const auto frameCount = (frames - win) / hop + 1;
    std::vector<double> times (frameCount, 0.0);
    std::vector<float> rms (frameCount, 0.0f);
    std::vector<std::vector<detail::YinCandidate>> candidates (frameCount);

    for (std::size_t f = 0; f < frameCount; ++f)
    {
        const auto start = f * hop;
        times[f] = static_cast<double> (start) / sampleRate;
        rms[f] = static_cast<float> (std::sqrt ((prefix[start + win] - prefix[start])
                                                / static_cast<double> (win)));

        if (rms[f] > cfg.rmsGate)
            detail::yinCandidates (mono + start, win, sampleRate, cfg, candidates[f]);
    }

    // Viterbi over each frame's candidate list plus one unvoiced state. The
    // observation cost is the CMND value; the transition cost keeps a noisy
    // frame from inventing a new note or jumping an octave.
    std::vector<std::vector<int>> back (frameCount);
    std::vector<std::vector<double>> dp (frameCount);

    for (std::size_t f = 0; f < frameCount; ++f)
    {
        const auto& current = candidates[f];
        const auto states = current.size() + 1;
        dp[f].assign (states, std::numeric_limits<double>::infinity());
        back[f].assign (states, -1);

        if (f == 0)
        {
            for (std::size_t s = 0; s < current.size(); ++s)
                dp[f][s] = 2.0 * current[s].cost;
            dp[f].back() = current.empty() ? 0.0 : cfg.voicingCost;
            continue;
        }

        const auto& prev = candidates[f - 1];
        const auto prevStates = prev.size() + 1;

        for (std::size_t s = 0; s < states; ++s)
        {
            const auto voiced = s < current.size();

            for (std::size_t p = 0; p < prevStates; ++p)
            {
                if (! std::isfinite (dp[f - 1][p]))
                    continue;

                const auto wasVoiced = p < prev.size();
                double transition = 0.0;

                if (voiced != wasVoiced)
                    transition = cfg.voicingCost;
                else if (voiced)
                {
                    const auto jump = std::min (24.0, std::abs (current[s].midi - prev[p].midi));
                    // Convex above an octave: a subharmonic candidate is a
                    // much worse explanation than a real melodic leap.
                    transition = cfg.pitchJumpCost * jump
                                 + (jump > 12.0 ? 0.03 * (jump - 12.0) * (jump - 12.0) : 0.0);
                }

                const auto cost = dp[f - 1][p] + transition
                                  + (voiced ? 2.0 * current[s].cost
                                            : (current.empty() ? 0.0 : cfg.voicingCost));

                if (cost < dp[f][s])
                {
                    dp[f][s] = cost;
                    back[f][s] = static_cast<int> (p);
                }
            }
        }
    }

    std::size_t best = 0;
    std::vector<int> path (frameCount, -1);

    for (std::size_t s = 1; s < dp.back().size(); ++s)
        if (dp.back()[s] < dp.back()[best])
            best = s;

    for (auto f = static_cast<std::ptrdiff_t> (frameCount) - 1; f >= 0; --f)
    {
        path[static_cast<std::size_t> (f)] = static_cast<int> (best);

        if (f > 0)
            best = static_cast<std::size_t> (std::max (0, back[static_cast<std::size_t> (f)][best]));
    }

    std::vector<double> selectedMidi (frameCount, -1.0);
    std::vector<double> selectedConfidence (frameCount, 0.0);

    for (std::size_t f = 0; f < frameCount; ++f)
    {
        const auto s = path[f];

        if (s >= 0 && s < static_cast<int> (candidates[f].size()))
        {
            const auto& candidate = candidates[f][static_cast<std::size_t> (s)];
            selectedMidi[f] = candidate.midi;
            selectedConfidence[f] = std::clamp (1.0 - candidate.cost, 0.0, 1.0);
        }
    }

    // Three-frame median smoothing removes isolated pitch spikes while
    // preserving deliberate note changes (the median cannot move a stable
    // pitch by more than one frame).
    std::vector<double> smoothed = selectedMidi;

    for (std::size_t f = 0; f < frameCount; ++f)
    {
        std::vector<double> window;

        for (auto k = static_cast<std::ptrdiff_t> (f) - 1;
             k <= static_cast<std::ptrdiff_t> (f) + 1; ++k)
            if (k >= 0 && k < static_cast<std::ptrdiff_t> (frameCount)
                && selectedMidi[static_cast<std::size_t> (k)] > 0.0)
                window.push_back (selectedMidi[static_cast<std::size_t> (k)]);

        if (window.size() >= 2)
            smoothed[f] = detail::median (std::move (window));
    }

    int currentNote = -1;
    double currentStart = 0.0;
    double lastVoiced = 0.0;
    double rmsSum = 0.0;
    int rmsCount = 0;
    int gapFrames = 0;
    int pendingNote = -1;
    int pendingCount = 0;
    double pendingStart = 0.0;
    double confidenceSum = 0.0;
    int confidenceCount = 0;

    const auto closeNote = [&] (double endSeconds)
    {
        if (currentNote < 0)
            return;

        const auto duration = endSeconds - currentStart;

        if (duration >= static_cast<double> (minNoteSeconds))
        {
            const auto meanRms = rmsCount > 0 ? rmsSum / rmsCount : 0.0;
            const auto velocity = std::clamp (static_cast<int> (std::lround (24.0 + 103.0
                                                                           * std::min (1.0, meanRms / 0.2))),
                                              1, 127);
            const auto meanConfidence = confidenceCount > 0 ? confidenceSum / confidenceCount : 0.0;

            V2MNote note;
            note.midiNote = currentNote;
            note.startBeat = currentStart * bpm / 60.0;
            note.lengthBeats = std::max (0.05, duration * bpm / 60.0);
            note.velocity = velocity;
            note.confidence = static_cast<float> (std::clamp (meanConfidence, 0.0, 1.0));
            out.push_back (note);
        }

        currentNote = -1;
        pendingNote = -1;
        pendingCount = 0;
        rmsSum = 0.0;
        rmsCount = 0;
        confidenceSum = 0.0;
        confidenceCount = 0;
        gapFrames = 0;
    };

    for (std::size_t f = 0; f < frameCount; ++f)
    {
        const auto midi = smoothed[f];

        if (midi > 0.0)
        {
            const auto note = static_cast<int> (std::lround (midi));

            if (currentNote < 0)
            {
                currentNote = note;
                currentStart = times[f];
                lastVoiced = times[f];
                rmsSum = rms[f];
                rmsCount = 1;
                confidenceSum = selectedConfidence[f];
                confidenceCount = 1;
                gapFrames = 0;
                continue;
            }

            const auto distance = std::abs (midi - static_cast<double> (currentNote));

            if (distance <= hysteresis)
            {
                pendingNote = -1;
                pendingCount = 0;
                gapFrames = 0;
                lastVoiced = times[f];
                rmsSum += rms[f];
                ++rmsCount;
                confidenceSum += selectedConfidence[f];
                ++confidenceCount;
                continue;
            }

            if (note == pendingNote)
                ++pendingCount;
            else
            {
                pendingNote = note;
                pendingCount = 1;
                pendingStart = times[f];
            }

            if (pendingCount >= 2)
            {
                const auto end = std::max (currentStart, pendingStart);
                closeNote (end);
                currentNote = note;
                currentStart = pendingStart;
                lastVoiced = times[f];
                rmsSum = rms[f];
                rmsCount = 1;
                confidenceSum = selectedConfidence[f];
                confidenceCount = 1;
                gapFrames = 0;
            }
            else
            {
                // Do not let one frame of vibrato or a transient start a note.
                lastVoiced = times[f];
                rmsSum += rms[f];
                ++rmsCount;
                confidenceSum += selectedConfidence[f];
                ++confidenceCount;
            }
        }
        else
        {
            if (currentNote < 0)
                continue;

            ++gapFrames;

            if (static_cast<double> (gapFrames) * hopSeconds > gapBridgeSeconds)
            {
                closeNote (std::min (times[f], lastVoiced + hopSeconds));
            }
        }
    }

    if (currentNote >= 0)
        closeNote (std::min (times.back() + hopSeconds, static_cast<double> (frames) / sampleRate));

    return out;
}
} // namespace nc::engine
