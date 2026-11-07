#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace nc::engine
{
/** Plugin delay compensation, host side.

    A plugin only reports a latency in samples; the host owns everything else.
    These are the rules JUCE's AudioProcessorGraph, Tracktion's PluginNode and
    Ardour's Route::update_signal_latency all implement:

      * a node's latency is the worst input latency plus its own,
      * every connection into a summing point is delayed by the difference
        between the worst input and that source,
      * graph latency is the longest terminal path, not the sum of every
        plugin in the graph,
      * a track is delayed by (worst track latency - its own latency),
      * an offline render pre-rolls by the chain latency and drops exactly
        that many output frames, otherwise the head of the render is silent.
*/
struct RenderPlan
{
    int64_t prerollFrames = 0;
    int64_t dropFrames = 0;
    int64_t tailFrames = 0;
};

inline int nodeLatency (int maxInputLatency, int pluginLatency) noexcept
{
    return std::max (0, maxInputLatency) + std::max (0, pluginLatency);
}

inline int connectionDelay (int maxInputLatency, int sourceLatency) noexcept
{
    return std::max (0, maxInputLatency - sourceLatency);
}

inline int totalLatency (const int* terminalLatencies, std::size_t count) noexcept
{
    if (terminalLatencies == nullptr)
        return 0;

    int worst = 0;

    for (std::size_t i = 0; i < count; ++i)
        worst = std::max (worst, std::max (0, terminalLatencies[i]));

    return worst;
}

inline int compensationDelay (int worstLatency, int trackLatency) noexcept
{
    return std::max (0, worstLatency - trackLatency);
}

inline RenderPlan planRender (int chainLatency, int tailLatency) noexcept
{
    RenderPlan plan;
    plan.prerollFrames = std::max (0, chainLatency);
    plan.dropFrames = plan.prerollFrames;
    plan.tailFrames = std::max (0, tailLatency);
    return plan;
}
} // namespace nc::engine
