#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <vector>

namespace nc::pluginEditors
{
/** Editor windows outlive the processor they display unless something closes
    them first. TrackInsertRack retires a replaced plug-in and collectGarbage()
    destroys it on the message thread; an open editor still holds that raw
    AudioProcessor, so it must be closed before the destroy. */
struct Watcher
{
    virtual ~Watcher() = default;
    virtual juce::AudioProcessor* watchedProcessor() const noexcept = 0;
    virtual void closeForProcessorGone() = 0;
};

namespace detail
{
inline std::vector<Watcher*>& watchers() noexcept
{
    static std::vector<Watcher*> list;
    return list;
}
}

inline void watch (Watcher* watcher)
{
    if (watcher != nullptr)
        detail::watchers().push_back (watcher);
}

/** Tests and diagnostics: how many windows are currently registered. */
inline std::size_t watcherCount() noexcept
{
    return detail::watchers().size();
}

inline void unwatch (Watcher* watcher) noexcept
{
    auto& list = detail::watchers();
    list.erase (std::remove (list.begin(), list.end(), watcher), list.end());
}

/** Closes every window showing `processor`. A closing window deletes itself and
    unregisters from its destructor, so the list is copied before iterating. */
inline void closeWindowsFor (const juce::AudioProcessor* processor)
{
    if (processor == nullptr)
        return;

    const auto snapshot = detail::watchers();

    for (auto* watcher : snapshot)
        if (watcher->watchedProcessor() == processor)
            watcher->closeForProcessorGone();
}
}
