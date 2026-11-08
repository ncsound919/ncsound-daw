#pragma once

// Minimal YIN + note tracker for voice-to-MIDI (monophonic).
// Ported from VoxStudio YinPitchDetector/NoteTracker (validated there).
// Header-only, no allocation, offline conversion of a WAV buffer to notes.

#include <cmath>
#include <cstddef>
#include <vector>

namespace nc::engine
{
struct V2MNote
{
    int midiNote = 60;
    double startBeat = 0.0;
    double lengthBeats = 0.25;
    int velocity = 100;
};

struct V2MConfig
{
    int sampleRate = 48000;
    std::size_t window = 2048;
    std::size_t hop = 512;
    float threshold = 0.15f;     // YIN below this CMND → unvoiced
    float minConfidence = 0.35f;
    double bpm = 120.0;
};

namespace detail
{
inline double autocorrelationPitch (const float* samples, std::size_t n, double sr, float& confidence)
{
    // Squared-difference + CMND (YIN step 1-4), shortest-lag rule suppresses octave errors.
    const std::size_t maxTau = n / 2;
    std::vector<float> diff(maxTau + 1, 0.0f), cmnd(maxTau + 1, 0.0f);
    for (std::size_t tau = 1; tau <= maxTau; ++tau)
    {
        float sum = 0.0f;
        for (std::size_t j = 0; j + tau < n; ++j)
        {
            const float d = samples[j] - samples[j + tau];
            sum += d * d;
        }
        diff[tau] = sum;
    }
    float running = 0.0f;
    cmnd[0] = 1.0f;
    for (std::size_t tau = 1; tau <= maxTau; ++tau)
    {
        running += diff[tau];
        cmnd[tau] = running > 1e-9f ? diff[tau] * (float) tau / running : 1.0f;
    }
    // First local minimum below threshold wins (shortest lag rule).
    std::size_t best = 0;
    for (std::size_t tau = 2; tau + 1 <= maxTau; ++tau)
    {
        if (cmnd[tau] < 0.15f && cmnd[tau] <= cmnd[tau - 1] && cmnd[tau] <= cmnd[tau + 1])
        {
            best = tau;
            break;
        }
    }
    if (best == 0)
    {
        confidence = 0.0f;
        return 0.0;
    }
    // Parabolic interpolation.
    const float a = cmnd[best - 1], b = cmnd[best], c = cmnd[best + 1];
    const float denom = a + c - 2.0f * b;
    const float shift = denom != 0.0f ? 0.5f * (a - c) / denom : 0.0f;
    const double period = (double) best + shift;
    confidence = 1.0f - b;
    return period > 0.0 ? sr / period : 0.0;
}
} // namespace detail

inline std::vector<V2MNote> voiceToMidi (const float* mono, std::size_t frames, const V2MConfig& cfg)
{
    std::vector<V2MNote> out;
    if (mono == nullptr || frames == 0) return out;
    const std::size_t win = std::min<std::size_t> (cfg.window, 2048);
    const std::size_t hop = std::max<std::size_t> (cfg.hop, 64);

    struct Hop { double freq; float conf; float rms; double tSec; };
    std::vector<Hop> hops;
    for (std::size_t start = 0; start + win <= frames; start += hop)
    {
        float conf = 0.0f;
        const double freq = detail::autocorrelationPitch (mono + start, win, (double) cfg.sampleRate, conf);
        float rms = 0.0f;
        for (std::size_t i = 0; i < win; ++i) rms += mono[start + i] * mono[start + i];
        rms = std::sqrt (rms / (float) win);
        const double tSec = (double) start / (double) cfg.sampleRate;
        hops.push_back ({freq, conf, rms, tSec});
    }
    if (hops.empty()) return out;

    // RMS gate + note tracker (3Hz smoothing → bend, hysteresis).
    auto beats = [&](double sec) { return sec * cfg.bpm / 60.0; };
    int current = -1;
    double currentStart = 0.0;
    for (std::size_t i = 0; i < hops.size(); ++i)
    {
        const auto& h = hops[i];
        const bool voiced = h.freq > 40.0 && h.freq < 2000.0 && h.conf > cfg.minConfidence && h.rms > 1e-4f;
        int note = -1;
        if (voiced)
            note = (int) std::lround (69.0 + 12.0 * std::log2 (h.freq / 440.0));
        if (note != current)
        {
            if (current >= 0)
            {
                const double end = hops[i].tSec;
                V2MNote n;
                n.midiNote = current;
                n.startBeat = beats (currentStart);
                n.lengthBeats = std::max (0.25, beats (end) - beats (currentStart));
                n.velocity = 100;
                out.push_back (n);
            }
            current = note;
            currentStart = h.tSec;
        }
    }
    if (current >= 0)
    {
        V2MNote n;
        n.midiNote = current;
        n.startBeat = beats (currentStart);
        n.lengthBeats = std::max (0.25, beats (hops.back().tSec) - beats (currentStart));
        n.velocity = 100;
        out.push_back (n);
    }
    return out;
}
} // namespace nc::engine
