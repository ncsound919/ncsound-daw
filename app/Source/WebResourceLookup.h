// Path handling for the embedded React bundle.
//
// juce_add_binary_data flattens every source to its basename, so the directory
// structure has to travel next to it: app/CMakeLists.txt writes
// WebResourcePaths.h from ui/dist, in the same order as the BinaryData sources.
// getWebResource() used to compare basenames alone (DAW-14), which served
// whichever file came first when two directories shared a name, and answered
// any request ending in index.html with the app shell.
#pragma once

#include <juce_core/juce_core.h>

namespace nc::web
{
/** Lower-cases, unifies separators, drops the origin of an absolute URL, any
    query string or fragment, strips the leading and trailing slashes and
    resolves the bundle root to "index.html". Returns an empty string for a path
    that climbs out of the bundle with a ".." segment, so it matches nothing. */
inline juce::String normaliseResourcePath (juce::String path)
{
    if (path.contains ("://"))
        path = path.fromFirstOccurrenceOf ("://", false, false)
                   .fromFirstOccurrenceOf ("/", false, false);

    path = path.upToFirstOccurrenceOf ("?", false, false)
               .upToFirstOccurrenceOf ("#", false, false)
               .replaceCharacter ('\\', '/')
               .toLowerCase();

    if (path.isEmpty() || path == "/")
        return "index.html";

    juce::StringArray segments;
    segments.addTokens (path, "/", "");

    juce::StringArray resolved;

    for (const auto& segment : segments)
    {
        if (segment.isEmpty() || segment == ".")
            continue;

        if (segment == "..")
            return {};

        resolved.add (segment);
    }

    if (resolved.isEmpty())
        return "index.html";

    return resolved.joinIntoString ("/");
}

/** Index of the embedded file whose whole path matches the request, or -1.
    `paths` holds the bundle-relative path of every embedded file, in the order
    juce_add_binary_data lists its resources. */
inline int findResourceIndex (const juce::String& requested,
                              const char* const* paths,
                              int count)
{
    const auto wanted = normaliseResourcePath (requested);

    if (wanted.isEmpty())
        return -1;

    for (int i = 0; i < count; ++i)
        if (paths[i] != nullptr && normaliseResourcePath (paths[i]) == wanted)
            return i;

    return -1;
}
} // namespace nc::web
