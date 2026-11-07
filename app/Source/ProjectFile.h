#pragma once

#include <nc/engine/ProjectJson.hpp>

#include <juce_core/juce_core.h>

#include <optional>

/** Reads a project file, or reports why it could not be read. The result is
    already sanitised, so the caller can apply it directly. */
std::optional<nc::engine::ProjectDocument> loadProjectDocument (const juce::File& file, juce::String& error);

/** Writes through a temporary sibling and replaces the target, so a cancelled
    save cannot leave a half-written project behind. */
bool saveProjectDocument (const juce::File& file, const nc::engine::ProjectDocument& document, juce::String& error);
