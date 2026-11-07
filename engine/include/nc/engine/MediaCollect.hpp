#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace nc::engine
{
/** What collecting a project's media would do to one referenced file. */
enum class CollectAction
{
    copy,           // source lives outside the project
    reuse,          // the same source was already copied earlier in this plan
    alreadyInside,  // source is already under Media/
    missing         // source no longer exists on disk
};

struct CollectPlanItem
{
    std::string source;
    std::string destination;
    std::string relativePath;   // stored in the project; never absolute
    CollectAction action = CollectAction::copy;
};

namespace detail
{
inline std::string normalisePath (std::string path)
{
    const auto separator = static_cast<char> (92);   // backslash

    for (auto& character : path)
        if (character == separator)
            character = '/';

    while (path.size() > 1 && path.back() == '/')
        path.pop_back();

    return path;
}

inline std::string fileNameOf (const std::string& path)
{
    const auto slash = path.find_last_of ('/');
    return slash == std::string::npos ? path : path.substr (slash + 1);
}

inline bool hasParentSegment (const std::string& path)
{
    size_t start = 0;
    while (start <= path.size())
    {
        const auto slash = path.find ('/', start);
        const auto length = (slash == std::string::npos) ? path.size() - start : slash - start;

        if (length == 2 && path[start] == '.' && path[start + 1] == '.')
            return true;

        if (slash == std::string::npos)
            break;

        start = slash + 1;
    }

    return false;
}

inline bool startsWithIgnoreCase (const std::string& value, const std::string& prefix)
{
    if (value.size() < prefix.size())
        return false;

    for (size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower (static_cast<unsigned char> (value[i]))
            != std::tolower (static_cast<unsigned char> (prefix[i])))
            return false;

    return true;
}
} // namespace detail

/** Plans a collect pass without touching the disk.

    Sources that live outside the project are copied into Media/ and referenced
    relatively, so the project folder can be moved or renamed. The planning is
    pure (existence comes in through fileExists) because it is where duplicate,
    collision and missing-source bugs live; the caller performs the copies.
*/
inline std::vector<CollectPlanItem> planCollect (const std::vector<std::string>& sources,
                                                 const std::vector<std::string>& existingMedia,
                                                 const std::string& projectFolder,
                                                 const std::function<bool (const std::string&)>& fileExists)
{
    std::vector<CollectPlanItem> plan;
    const auto folder = detail::normalisePath (projectFolder);
    const auto mediaPrefix = folder + "/Media/";
    auto taken = existingMedia;

    for (const auto& raw : sources)
    {
        if (raw.empty())
            continue;

        const auto source = detail::normalisePath (raw);
        CollectPlanItem item;
        item.source = source;

        if (fileExists && ! fileExists (source))
        {
            item.action = CollectAction::missing;
            plan.push_back (item);
            continue;
        }

        // Case-insensitive (Windows paths) and traversal-free: Media/../evil
        // must not be treated as already inside, or destination would keep the
        // ".." and escape the project folder (CWE-22).
        if (detail::startsWithIgnoreCase (source, mediaPrefix)
            && ! detail::hasParentSegment (source))
        {
            item.relativePath = "Media/" + detail::fileNameOf (source);
            item.destination = source;
            item.action = CollectAction::alreadyInside;
            plan.push_back (item);
            continue;
        }

        const auto previous = std::find_if (plan.begin(), plan.end(),
                                            [&source] (const CollectPlanItem& done)
                                            {
                                                return done.source == source
                                                       && done.action == CollectAction::copy;
                                            });

        if (previous != plan.end())
        {
            item.destination = previous->destination;
            item.relativePath = previous->relativePath;
            item.action = CollectAction::reuse;
            plan.push_back (item);
            continue;
        }

        auto name = detail::fileNameOf (source);

        // "." / ".." / empty names would make destination escape Media/.
        if (name.empty() || name == "." || name == "..")
        {
            item.action = CollectAction::missing;
            plan.push_back (item);
            continue;
        }

        const auto dot = name.find_last_of ('.');
        const auto stem = dot == std::string::npos ? name : name.substr (0, dot);
        const auto extension = dot == std::string::npos ? std::string() : name.substr (dot);
        auto candidate = name;
        auto suffix = 1;

        while (std::find (taken.begin(), taken.end(), candidate) != taken.end())
            candidate = stem + "-" + std::to_string (suffix++) + extension;

        taken.push_back (candidate);
        item.destination = mediaPrefix + candidate;
        item.relativePath = "Media/" + candidate;
        item.action = CollectAction::copy;
        plan.push_back (item);
    }

    return plan;
}
} // namespace nc::engine
