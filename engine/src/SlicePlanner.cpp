#include <nc/engine/SlicePlanner.hpp>

#include <algorithm>
#include <cmath>

namespace nc
{
namespace
{
constexpr std::int64_t minSliceSamples = 32;
}

std::vector<Slice> planSlices (std::int64_t lengthSamples,
                               const std::vector<std::int64_t>& transients,
                               int maxSlices)
{
    if (lengthSamples <= 0 || maxSlices <= 0)
        return {};

    const auto slicesByLength = std::max<std::int64_t> (1, lengthSamples / (minSliceSamples * 2));
    const auto sliceCount = static_cast<int> (std::min<std::int64_t> (maxSlices, slicesByLength));

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

    constexpr std::int64_t hop = 256;
    std::vector<double> rms;
    rms.reserve (static_cast<size_t> (numFrames / hop + 1));

    for (std::int64_t frame = 0; frame < numFrames; frame += hop)
    {
        const auto count = std::min (hop, numFrames - frame);
        double energy = 0.0;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto* channel = channelData[ch];

            if (channel == nullptr)
                continue;

            for (std::int64_t i = 0; i < count; ++i)
            {
                const auto v = static_cast<double> (channel[frame + i]);
                energy += v * v;
            }
        }

        const auto denominator = static_cast<double> (std::max<std::int64_t> (1, count * numChannels));
        rms.push_back (std::sqrt (energy / denominator));
    }

    if (rms.size() < 3)
        return onsets;

    double mean = 0.0;
    for (const auto v : rms)
        mean += v;
    mean /= static_cast<double> (rms.size());

    double variance = 0.0;
    for (const auto v : rms)
        variance += (v - mean) * (v - mean);
    variance /= static_cast<double> (rms.size());

    const auto threshold = mean + (1.0 - std::clamp (sensitivity, 0.0, 1.0) * 0.8) * std::sqrt (variance) * 2.0;
    std::int64_t lastOnset = -hop * 4;

    for (size_t i = 1; i < rms.size(); ++i)
    {
        if (rms[i] > threshold && rms[i] > rms[i - 1] * 1.5)
        {
            const auto position = static_cast<std::int64_t> (i) * hop;

            if (position - lastOnset > hop * 4)
            {
                onsets.push_back (position);
                lastOnset = position;
            }
        }
    }

    return onsets;
}
} // namespace nc
