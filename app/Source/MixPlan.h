#pragma once

#include "Automation.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace nc
{
class AudioEngine;

/**
    A mix plan is the only thing an agent may hand the session.

    The local model writes JSON, never code and never a direct engine call:

        {
          "version": 1,
          "title": "lead lift into the chorus",
          "lanes": [
            { "track": 3, "param": "volume",
              "points": [[0, 0.72], [8, 0.72], [12, 0.98]],
              "why": "ride the lead up as the chorus opens" },
            { "track": 5, "param": "pan", "points": [[0, -0.2], [16, -0.35]] },
            { "track": 9, "param": "volume", "points": [] }
          ]
        }

    Values are the engine's own units, so the model never speaks dB or
    percentages: volume is linear gain 0..1 and pan is -1..1. Beats are quarter
    notes from bar 1, matching the lanes the engine already evaluates. An empty
    point list is a clear instruction and hands the fader back to manual.

    A plan is untrusted input. Every lane is checked here, and a rejected lane
    never takes the lanes around it down with it.
*/

/** One fader the plan wants to drive. */
struct MixPlanLane
{
    int track = 0;
    AutomationParam param = AutomationParam::Volume;
    std::vector<AutomationLane::Point> points; // sorted and clamped by the parser
    juce::String why;                          // the model's reason, shown in reports
};

struct MixPlan
{
    juce::String title;
    std::vector<MixPlanLane> lanes;
};

/** A lane the parser refused, kept so the report can say what and why. */
struct MixPlanProblem
{
    juce::String where;
    juce::String reason;
};

struct MixPlanValidation
{
    MixPlan plan;
    std::vector<MixPlanProblem> rejected;
    std::vector<juce::String> notes; // clamps applied along the way
    juce::String error;              // document-level: no lane can apply

    /** True when at least one lane survived and the document was readable. */
    bool usable() const { return error.isEmpty() && ! plan.lanes.empty(); }

    /** One line for the status bar: what will apply, or why nothing will. */
    juce::String summary() const;
};

struct MixPlanReport
{
    int applied = 0;
    int cleared = 0;
    juce::StringArray notes;

    bool anyChange() const { return applied + cleared > 0; }
};

/** Validates an already-parsed plan tree against the live session shape. */
MixPlanValidation parseMixPlan (const juce::var& root, int activeTracks);

/** Parses and validates plan text, reporting malformed JSON as an error. */
MixPlanValidation parseMixPlanJson (const juce::String& text, int activeTracks);

/** Applies a validated plan: lanes with points become automation, empty lanes
    clear it. Message thread only; lanes are published through the engine's
    atomic slot, so this is safe while the transport runs. */
MixPlanReport executeMixPlan (AudioEngine& engine, const MixPlan& plan);

/** Result of asking a local model to author a plan. `validation` is usable
    whenever `error` is empty; `error` holds the transport/parse problem. */
struct MixPlanFetchResult
{
    MixPlanValidation validation;
    juce::String error;
};

/** Extract the first complete JSON object from a model reply, ignoring any
    surrounding fence or prose. Returns an empty string when none exists. */
juce::String extractMixPlanJson (const juce::String& reply);

/** Builds the prompt for the local model: every active track with its role and
    instrument, the engine units, and the user's goal. Message thread only. */
juce::String buildMixPlanPrompt (const juce::String& brief, int activeTracks);

/** Blocking HTTP call to a local OpenAI-compatible server. The prompt and track
    count are snapshotted by the caller, so this is safe off the UI thread even
    if the session closes mid-request. */
MixPlanFetchResult requestMixPlanFromQwen (const juce::String& serverUrl,
                                          const juce::String& prompt,
                                          int activeTracks);
} // namespace nc
