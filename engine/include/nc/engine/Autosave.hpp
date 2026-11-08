#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

namespace nc::engine::autosave
{
/** Autosave policy for project files. Header-only and JUCE-free so the timer
    logic is unit-testable; the app only calls shouldAutosave() and writes the
    sibling file. The manual save stays the authoritative file — autosave never
    overwrites a project, only its `.autosave` sibling (REAPER/FL practice).

    Timing: the debounce is measured from the edit, but the interval is
    measured from max(lastAutosave, lastSave) — so the first journal fires one
    debounce after the edit (not 2 min after it), and later journals stay at
    least `intervalSeconds` apart. Callers must store the time of a successful
    autosave into lastAutosaveMillis.

    Defaults follow DAW convention: 15 s quiet period, at most one journal per
    2 min while dirty; 5 rotating backups after an explicit save. */

struct Policy
{
    bool enabled = true;
    int64_t intervalSeconds = 120;   // between autosaves while dirty
    int64_t debounceSeconds = 15;    // quiet period after an edit before firing
    int maxBackups = 5;              // rotating backups kept (0 = keep none)
};

/** True when an autosave is due: enabled, an edit has happened since the last
    save/autosave, the debounce quiet period elapsed, and at least the interval
    has passed since the last autosave/save. All times are wall-clock millis.
    Policy values are sanitised here so negative/huge inputs cannot overflow. */
inline bool shouldAutosave (int64_t nowMillis, int64_t lastSaveMillis,
                            int64_t lastEditMillis, int64_t lastAutosaveMillis,
                            const Policy& policy)
{
    if (! policy.enabled)
        return false;

    const auto intervalSeconds = std::clamp<std::int64_t> (policy.intervalSeconds, 0, 86400);
    const auto debounceSeconds = std::clamp<std::int64_t> (policy.debounceSeconds, 0, 3600);

    if (lastEditMillis <= lastSaveMillis)
        return false; // nothing new since the real save

    if (lastAutosaveMillis >= lastEditMillis)
        return false; // already journaled this edit

    if (nowMillis - lastEditMillis < debounceSeconds * 1000)
        return false; // still typing / dragging

    const auto anchor = lastAutosaveMillis > lastSaveMillis ? lastAutosaveMillis : lastSaveMillis;
    if (nowMillis - anchor < intervalSeconds * 1000)
        return false; // too soon to journal again

    return true;
}

namespace detail
{
/** Split "dir.name.ext" -> (directory prefix, stem, extension suffix).
    Only a dot after the last path separator counts as an extension dot. */
inline std::string lastSeparator (const std::string& name) noexcept
{
    const auto slash = name.find_last_of ("/\\");
    return slash == std::string::npos ? "" : name.substr (0, slash + 1);
}

inline std::size_t extensionDot (const std::string& name) noexcept
{
    const auto base = name.substr (name.find_last_of ("/\\") == std::string::npos ? 0 : name.find_last_of ("/\\") + 1);
    const auto dot = base.rfind ('.');
    if (dot == std::string::npos)
        return std::string::npos;
    const auto offset = base.size() - dot;
    return name.size() - offset;
}
} // namespace detail

/** "song.ncsproject" -> "song.autosave.ncsproject" (crash-recovery sibling). */
inline std::string autosaveFileName (const std::string& projectFileName)
{
    const auto dot = detail::extensionDot (projectFileName);

    if (dot == std::string::npos)
        return projectFileName + ".autosave";

    return projectFileName.substr (0, dot) + ".autosave" + projectFileName.substr (dot);
}

/** "song.ncsproject" -> "song.backup3.ncsproject" (post-save rotation). */
inline std::string backupFileName (const std::string& projectFileName, int index)
{
    const auto dot = detail::extensionDot (projectFileName);
    const auto stem = dot == std::string::npos ? projectFileName : projectFileName.substr (0, dot);
    const auto suffix = dot == std::string::npos ? "" : projectFileName.substr (dot);
    return stem + ".backup" + std::to_string (std::max (0, index)) + suffix;
}

inline bool isAutosaveFileName (const std::string& fileName) noexcept
{
    // "...autosave..." exactly: infix ".autosave." or suffix ".autosave".
    const auto infix = fileName.find (".autosave.");
    if (infix != std::string::npos)
        return true;
    const auto suffixPos = fileName.find (".autosave");
    return suffixPos != std::string::npos && suffixPos + 9 == fileName.size();
}

/** How many backups to delete when `existingCount` files exceed `maxBackups`. */
inline int backupsToPrune (int existingCount, int maxBackups) noexcept
{
    if (existingCount < 0)
        existingCount = 0;
    if (maxBackups < 0)
        maxBackups = 0;
    return existingCount > maxBackups ? existingCount - maxBackups : 0;
}

/** Next backup index: rotate among 0..maxBackups-1 so the newest save lands in
    the oldest slot. `lastIndex` is the index written by the previous save
    (-1 for none yet); out-of-range values are normalised. */
inline int nextBackupIndex (int lastIndex, int maxBackups) noexcept
{
    if (maxBackups <= 0)
        return -1;

    const auto normalized = ((lastIndex % maxBackups) + maxBackups) % maxBackups;
    return (normalized + 1) % maxBackups;
}

} // namespace nc::engine::autosave
