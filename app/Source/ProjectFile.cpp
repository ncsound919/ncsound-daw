#include "ProjectFile.h"

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

    if (! temporary.replaceFileIn (file))
    {
        error = "Could not replace " + file.getFullPathName();
        temporary.deleteFile();
        return false;
    }

    return true;
}
