#include <nc/engine/SlicePlanner.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace nc
{
namespace
{
constexpr std::int64_t minSliceSamples = 32;

std::size_t nextPowerOfTwo (std::size_t n)
{
    std::size_t p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

/** In-place radix-2 FFT for the onset analysis. The engine has no JUCE
    dependency, so this stays a small local implementation rather than adding
    juce_dsp to every consumer of the slice planner. */
void fft (std::vector<double>& re, std::vector<double>& im)
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
        const auto angle = -2.0 * 3.14159265358979323846 / static_cast<double> (len);
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
}

double medianOf (std::vector<double> values)
{
    if (values.empty())
        return 0.0;

    const auto middle = values.size() / 2;
    std::nth_element (values.begin(),
                      values.begin() + static_cast<std::ptrdiff_t> (middle),
                      values.end());
    return values[middle];
}
}

std::vector<Slice> planSlices (std::int64_t lengthSamples,
                               const std::vector<std::int64_t>& transients,
                               int maxSlices)
{
    if (lengthSamples <= 0 || maxSlices <= 0)
        return {};

    const auto slicesByLength = std::max<std::int64_t> (1, lengthSamples / (minSliceSamples * 2));
    const auto cappedMax = std::min<std::int64_t> (maxSlices, 4096);
    const auto sliceCount = static_cast<int> (std::min<std::int64_t> (cappedMax, slicesByLength));

    if (sliceCount <= 1)
        return { { 0, lengthSamples } };

    const double segment = static_cast<double> (lengthSamples) / sliceCount;
    const auto tolerance = static_cast<std::int64_t> (segment * 0.4);

    std::vector<std::int64_t> bounds;
    bounds.reserve (static_cast<size_t> (sliceCount) + 1);
    bounds.push_back (0);

    std::int64_t lastBoundary = 0;

    for (int i = 1; i < sliceCount; ++i)
    {
        const auto ideal = static_cast<std::int64_t> (std::llround (segment * i));
        auto chosen = ideal;
        auto bestDistance = tolerance;

        for (const auto t : transients)
        {
            if (t <= lastBoundary + minSliceSamples * 2 || t >= lengthSamples - minSliceSamples)
                continue;

            const auto distance = std::llabs (t - ideal);

            if (distance <= bestDistance)
            {
                bestDistance = distance;
                chosen = t;
            }
        }

        chosen = std::clamp (chosen, lastBoundary + minSliceSamples, lengthSamples - minSliceSamples);

        if (chosen > lastBoundary)
        {
            bounds.push_back (chosen);
            lastBoundary = chosen;
        }
    }

    bounds.push_back (lengthSamples);

    std::vector<Slice> result;
    result.reserve (bounds.size() - 1);

    for (size_t i = 0; i + 1 < bounds.size(); ++i)
        if (bounds[i + 1] > bounds[i])
            result.push_back ({ bounds[i], bounds[i + 1] });

    return result;
}

std::vector<std::int64_t> detectTransients (const float* const* channelData,
                                            int numChannels,
                                            std::int64_t numFrames,
                                            double sensitivity)
{
    std::vector<std::int64_t> onsets;

    if (channelData == nullptr || numFrames <= 0 || numChannels <= 0)
        return onsets;

    // Spectral flux is the useful part of the standard onset detector: a
    // timbre change (new note, consonant, drum hit) raises many bins at once,
    // while a level change alone does not. A small energy-flux term keeps
    // quiet percussive hits from disappearing under a loud sustained pad.
    constexpr std::size_t analysisWindow = 1024;
    constexpr std::int64_t hop = 256;

    const auto frameCount = static_cast<std::size_t> ((numFrames + hop - 1) / hop);
    if (frameCount < 2)
        return onsets;

    const auto fftSize = nextPowerOfTwo (analysisWindow);
    const auto bins = fftSize / 2;
    std::vector<double> mixed (analysisWindow, 0.0);
    std::vector<double> re (fftSize, 0.0), im (fftSize, 0.0);
    std::vector<double> previous (bins, 0.0), current (bins, 0.0), smoothed (bins, 0.0);
    std::vector<double> spectralFlux (frameCount, 0.0);
    std::vector<double> energyFlux (frameCount, 0.0);
    std::vector<double> rms (frameCount, 0.0);

    std::size_t activeChannels = 0;
    for (int ch = 0; ch < numChannels; ++ch)
        if (channelData[ch] != nullptr)
            ++activeChannels;

    if (activeChannels == 0)
        return onsets;

    for (std::size_t f = 0; f < frameCount; ++f)
    {
        const auto start = static_cast<std::int64_t> (f) * hop;
        const auto count = std::min<std::int64_t> (analysisWindow, numFrames - start);

        std::fill (mixed.begin(), mixed.end(), 0.0);

        double energy = 0.0;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto* channel = channelData[ch];

            if (channel == nullptr)
                continue;

            for (std::int64_t i = 0; i < count; ++i)
            {
                const auto sample = static_cast<double> (channel[start + i]);
                mixed[static_cast<std::size_t> (i)] += sample;
                energy += sample * sample;
            }
        }

        const auto gain = 1.0 / static_cast<double> (activeChannels);
        rms[f] = std::sqrt (energy / (static_cast<double> (std::max<std::int64_t> (1, count)) * activeChannels));

        for (std::size_t i = 0; i < analysisWindow; ++i)
        {
            const auto window = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846
                                                       * static_cast<double> (i)
                                                       / static_cast<double> (analysisWindow - 1));
            re[i] = mixed[i] * gain * window;
            im[i] = 0.0;
        }

        fft (re, im);

        for (std::size_t k = 1; k < bins; ++k)
        {
            current[k] = std::log1p (std::sqrt (re[k] * re[k] + im[k] * im[k]));
        }

        // A narrow FM sideband (vibrato) should not look like a broadband
        // attack. Smooth across neighbouring bins before taking the flux.
        for (std::size_t k = 1; k + 1 < bins; ++k)
            smoothed[k] = 0.25 * current[k - 1] + 0.5 * current[k] + 0.25 * current[k + 1];

        double flux = 0.0;

        for (std::size_t k = 1; k < bins; ++k)
        {
            // Compare against the maximum of the neighbouring bins in the
            // previous frame (SuperFlux trajectory tracking), not just the
            // same bin. A vibrato or slow pitch glide then stops looking like
            // a stream of new onsets.
            double reference = previous[k];

            for (int offset = -3; offset <= 3; ++offset)
            {
                const auto bin = static_cast<std::ptrdiff_t> (k) + offset;

                if (bin >= 1 && bin < static_cast<std::ptrdiff_t> (bins))
                    reference = std::max (reference, previous[static_cast<std::size_t> (bin)]);
            }

            const auto rise = smoothed[k] - reference;

            if (rise > 0.0)
                flux += rise;
        }

        spectralFlux[f] = flux / static_cast<double> (bins - 1);
        energyFlux[f] = f > 0 ? std::max (0.0, rms[f] - rms[f - 1]) : 0.0;
        std::swap (previous, smoothed);
    }

    const auto maxSpectral = *std::max_element (spectralFlux.begin(), spectralFlux.end());
    const auto maxRms = *std::max_element (rms.begin(), rms.end());

    if (maxSpectral <= 1e-9 && maxRms <= 1e-9)
        return onsets;

    const auto normalizedSpectral = maxSpectral > 1e-9 ? 1.0 / maxSpectral : 0.0;
    std::vector<double> novelty (frameCount, 0.0);

    for (std::size_t f = 0; f < frameCount; ++f)
        novelty[f] = 0.7 * spectralFlux[f] * normalizedSpectral
                     + 0.3 * (maxRms > 1e-9 ? std::min (1.0, energyFlux[f] / maxRms)
                                            : 0.0);

    const auto sensitivity01 = std::clamp (sensitivity, 0.0, 1.0);
    const auto previousOnsetGap = 4;
    std::int64_t lastOnset = std::numeric_limits<std::int64_t>::min() / 2;

    // The flux peak marks the first frame whose window contains the attack, not
    // the attack itself. Refine inside that window with a short energy rise so
    // slices start at the hit instead of up to a window early.
    const auto refineOnset = [&] (std::int64_t frameStart)
    {
        constexpr std::int64_t block = 64;
        const auto end = std::min (numFrames, frameStart + static_cast<std::int64_t> (analysisWindow));
        auto best = std::min (end, frameStart + static_cast<std::int64_t> (analysisWindow / 2));
        auto bestRise = 0.0;
        auto previousRms = 0.0;

        for (auto start = frameStart; start < end; start += block)
        {
            const auto count = std::min (block, end - start);
            auto blockEnergy = 0.0;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto* channel = channelData[ch];

                if (channel == nullptr)
                    continue;

                for (std::int64_t i = 0; i < count; ++i)
                {
                    const auto sample = static_cast<double> (channel[start + i]);
                    blockEnergy += sample * sample;
                }
            }

            const auto blockRms = std::sqrt (blockEnergy
                                             / (static_cast<double> (std::max<std::int64_t> (1, count))
                                                * activeChannels));
            const auto rise = blockRms - previousRms;

            if (start == frameStart && blockRms > 1e-4)
                best = start;

            if (previousRms > 1e-6 && blockRms > previousRms * 1.8 && rise > bestRise)
            {
                bestRise = rise;
                best = start;
            }

            previousRms = blockRms;
        }

        return best;
    };

    for (std::size_t f = 0; f < frameCount; ++f)
    {
        const auto localStart = f >= 8 ? f - 8 : 0;
        const auto localEnd = std::min (frameCount, f + 9);

        // A partial final window sees the file's end as a huge spectral
        // change. It is a boundary, not an onset.
        if (static_cast<std::int64_t> (f) * hop + static_cast<std::int64_t> (analysisWindow) > numFrames)
            continue;

        std::vector<double> neighborhood (novelty.begin() + static_cast<std::ptrdiff_t> (localStart),
                                          novelty.begin() + static_cast<std::ptrdiff_t> (localEnd));
        const auto center = medianOf (neighborhood);

        for (auto& value : neighborhood)
            value = std::abs (value - center);

        const auto mad = medianOf (neighborhood);
        constexpr double noveltyFloor = 0.02;
        const auto threshold = center + (0.5 + 2.0 * (1.0 - sensitivity01))
                                         * std::max (noveltyFloor, mad * 1.4826);

        const auto isPeak = novelty[f] > threshold
                            && (f == 0 || novelty[f] >= novelty[f - 1])
                            && (f + 1 == frameCount || novelty[f] >= novelty[f + 1]);

        if (! isPeak)
            continue;

        const auto position = refineOnset (static_cast<std::int64_t> (f) * hop);

        if (position - lastOnset <= static_cast<std::int64_t> (previousOnsetGap) * hop)
            continue;

        onsets.push_back (position);
        lastOnset = position;
    }

    return onsets;
}
} // namespace nc
