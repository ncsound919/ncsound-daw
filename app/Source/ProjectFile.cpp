#include "ProjectFile.h"

#include <cstdio>

#if defined(_MSC_VER)
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
/** fsync the file on disk so a power loss after "Saved" does not lose it.
    JUCE FileOutputStream flushes buffers but never reaches disk; this is the
    durable close. Best-effort: failure to sync never fails the save. */
bool syncFileToDisk (const juce::File& file) noexcept
{
#if defined(_MSC_VER)
    const auto handle = ::CreateFileA (file.getFullPathName().toRawUTF8(), GENERIC_READ,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    const auto ok = (::FlushFileBuffers (handle) != 0);
    ::CloseHandle (handle);
    return ok;
#else
    const auto fd = ::open (file.getFullPathName().toRawUTF8(), O_RDONLY);
    if (fd < 0)
        return false;
    const auto ok = (::fsync (fd) == 0);
    ::close (fd);
    return ok;
#endif
}
} // namespace

std::optional<nc::engine::ProjectDocument> loadProjectDocument (const juce::File& file, juce::String& error)
{
    error.clear();

    if (! file.existsAsFile())
    {
        error = "No file at " + file.getFullPathName();
        return std::nullopt;
    }

    // Reject before loading the whole file into memory; the parser repeats this
    // check so other callers are covered too.
    if (file.getSize() > static_cast<juce::int64> (nc::limits::maxProjectJsonBytes))
    {
        error = "This project file is too large to open";
        return std::nullopt;
    }

    auto document = nc::engine::fromJson (file.loadFileAsString().toStdString());

    if (! document.has_value())
    {
        error = "This file is not a readable project";
        return std::nullopt;
    }

    if (! nc::engine::isLoadable (*document))
    {
        error = "This project was saved by a newer version of the app";
        return std::nullopt;
    }

    return nc::engine::sanitize (*document);
}

bool saveProjectDocument (const juce::File& file, const nc::engine::ProjectDocument& document, juce::String& error)
{
    error.clear();

    const auto temporary = file.getSiblingFile (file.getFileNameWithoutExtension() + ".saving");

    if (! temporary.replaceWithText (juce::String (nc::engine::toJson (document))))
    {
        error = "Could not write " + temporary.getFullPathName();
        return false;
    }

    // Sync the new contents before the rename. Afterwards the target is a
    // different file, so syncing that alone can leave the rename durable and the
    // data still in the page cache - the opposite of what syncFileToDisk is for.
    syncFileToDisk (temporary);

    if (! temporary.replaceFileIn (file))
    {
        error = "Could not replace " + file.getFullPathName();
        temporary.deleteFile();
        return false;
    }

    syncFileToDisk (file);
    return true;
}
