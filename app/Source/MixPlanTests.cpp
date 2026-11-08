#include <catch2/catch_test_macros.hpp>



#include "AudioEngine.h"

#include "MixPlan.h"



#include <cmath>



using nc::AudioEngine;

using nc::AutomationLane;

using nc::AutomationParam;

using nc::MixPlanValidation;

using nc::executeMixPlan;

using nc::parseMixPlanJson;



namespace

{

MixPlanValidation parse (const juce::String& text, int activeTracks = 16)

{

    return parseMixPlanJson (text, activeTracks);

}

} // namespace



TEST_CASE ("a well formed plan parses into lanes", "[mixplan]")

{

    const auto result = parse (R"({

        "version": 1,

        "title": "lead lift",

        "lanes": [

            { "track": 3, "param": "volume", "points": [[0, 0.72], [8, 0.72], [12, 0.98]], "why": "chorus lift" },

            { "track": 5, "param": "pan", "points": [[0, -0.2], [16, -0.35]] }

        ]

    })");



    REQUIRE (result.error.isEmpty());

    REQUIRE (result.usable());

    REQUIRE (result.rejected.empty());

    REQUIRE (result.plan.title == "lead lift");

    REQUIRE (result.plan.lanes.size() == 2);



    REQUIRE (result.plan.lanes[0].track == 3);

    REQUIRE (result.plan.lanes[0].param == AutomationParam::Volume);

    REQUIRE (result.plan.lanes[0].points.size() == 3);

    REQUIRE (result.plan.lanes[0].why == "chorus lift");

    REQUIRE (std::abs (result.plan.lanes[0].points[2].value - 0.98f) < 1.0e-6f);



    REQUIRE (result.plan.lanes[1].param == AutomationParam::Pan);

    REQUIRE (std::abs (result.plan.lanes[1].points[1].value + 0.35f) < 1.0e-6f);

}



TEST_CASE ("named points and an empty point list are accepted", "[mixplan]")

{

    const auto named = parse (R"({"lanes":[{"track":0,"param":"volume","points":[{"beat":4,"value":0.5}]}]})");



    REQUIRE (named.usable());

    REQUIRE (named.plan.lanes[0].points.size() == 1);

    REQUIRE (named.plan.lanes[0].points[0].beats == 4.0);



    const auto clears = parse (R"({"lanes":[{"track":2,"param":"volume","points":[]}]})");



    REQUIRE (clears.usable());

    REQUIRE (clears.plan.lanes[0].points.empty());

}



TEST_CASE ("a plan the session cannot use is an error, not a partial apply", "[mixplan]")

{

    const auto noLanes = parse (R"({"title":"empty"})");

    REQUIRE (noLanes.error.isNotEmpty());

    REQUIRE (! noLanes.usable());



    const auto emptyLanes = parse (R"({"lanes":[]})");

    REQUIRE (emptyLanes.error.isNotEmpty());

    REQUIRE (! emptyLanes.usable());



    const auto broken = parse ("{ lanes: [");

    REQUIRE (broken.error.isNotEmpty());

    REQUIRE (broken.error.containsIgnoreCase ("JSON"));

    REQUIRE (! broken.usable());



    const auto blank = parse ("   ");

    REQUIRE (blank.error.isNotEmpty());

    REQUIRE (! blank.usable());



    const auto notAnObject = parse ("[]");

    REQUIRE (notAnObject.error.isNotEmpty());

}



TEST_CASE ("a bad lane is rejected without taking the good ones down", "[mixplan]")

{

    const auto result = parse (R"({"lanes":[

        { "track": 20, "param": "volume", "points": [[0, 0.5]] },

        { "track": 1, "param": "gain", "points": [[0, 0.5]] },

        { "track": 1, "param": "volume", "points": [[0, 0.5]] }

    ]})");



    REQUIRE (result.usable());

    REQUIRE (result.plan.lanes.size() == 1);

    REQUIRE (result.plan.lanes[0].track == 1);

    REQUIRE (result.rejected.size() == 2);

    REQUIRE (result.rejected[0].where == "lane 1");

    REQUIRE (result.rejected[0].reason.contains ("outside"));

    REQUIRE (result.rejected[1].reason.contains ("volume"));

}



TEST_CASE ("a lane must carry a whole track and a points array", "[mixplan]")

{

    const auto fractional = parse (R"({"lanes":[{"track":2.5,"param":"volume","points":[[0,0.5]]}]})");

    REQUIRE (fractional.rejected.size() == 1);

    REQUIRE (fractional.rejected[0].reason.contains ("whole"));



    const auto missingPoints = parse (R"({"lanes":[{"track":2,"param":"volume"}]})");

    REQUIRE (missingPoints.rejected.size() == 1);

    REQUIRE (missingPoints.rejected[0].reason.contains ("points"));



    const auto badPoint = parse (R"({"lanes":[{"track":2,"param":"volume","points":[[0]]}]})");

    REQUIRE (badPoint.rejected.size() == 1);

    REQUIRE (badPoint.rejected[0].reason.contains ("[beat, value]"));

}



TEST_CASE ("overshoot is clamped and reported, negative beats are refused", "[mixplan]")

{

    const auto clamped = parse (R"({"lanes":[

        { "track": 0, "param": "volume", "points": [[0, 1.4]] },

        { "track": 1, "param": "pan", "points": [[0, -1.6]] }

    ]})");



    REQUIRE (clamped.usable());

    REQUIRE (clamped.plan.lanes[0].points[0].value == 1.0f);

    REQUIRE (clamped.plan.lanes[1].points[0].value == -1.0f);

    REQUIRE (clamped.notes.size() == 2);

    REQUIRE (clamped.notes[0].contains ("clamped"));



    const auto negative = parse (R"({"lanes":[{"track":0,"param":"volume","points":[[-1, 0.5]]}]})");

    REQUIRE (negative.rejected.size() == 1);

    REQUIRE (negative.rejected[0].reason.contains ("negative"));

}



TEST_CASE ("two lanes cannot drive the same fader", "[mixplan]")

{

    const auto result = parse (R"({"lanes":[

        { "track": 4, "param": "pan", "points": [[0, -0.5]] },

        { "track": 4, "param": "pan", "points": [[0, 0.5]] }

    ]})");



    REQUIRE (result.plan.lanes.size() == 1);

    REQUIRE (result.rejected.size() == 1);

    REQUIRE (result.rejected[0].reason.contains ("already has a pan lane"));



    // A different fader on the same track is fine.

    const auto both = parse (R"({"lanes":[

        { "track": 4, "param": "pan", "points": [[0, -0.5]] },

        { "track": 4, "param": "volume", "points": [[0, 0.8]] }

    ]})");



    REQUIRE (both.plan.lanes.size() == 2);

    REQUIRE (both.rejected.empty());

}



TEST_CASE ("a lane longer than the engine's capacity is rejected by name", "[mixplan]")

{

    juce::String points = "[";



    for (int i = 0; i < AutomationLane::maxPoints + 1; ++i)

        points << (i == 0 ? "" : ",") << "[" << i << ",0.5]";



    points << "]";



    const auto result = parse ("{\"lanes\":[{\"track\":0,\"param\":\"volume\",\"points\":" + points + "}]}");



    REQUIRE (result.rejected.size() == 1);

    REQUIRE (result.rejected[0].reason.contains ("512"));

}



TEST_CASE ("the summary names the count and the first rejection", "[mixplan]")

{

    const auto result = parse (R"({"lanes":[

        { "track": 0, "param": "volume", "points": [[0, 0.5]] },

        { "track": 40, "param": "volume", "points": [[0, 0.5]] }

    ]})");



    const auto summary = result.summary();



    REQUIRE (summary.startsWith ("1 lane"));

    REQUIRE (summary.contains ("1 rejected"));

    REQUIRE (summary.contains ("lane 2"));



    REQUIRE (parse ("{").summary().containsIgnoreCase ("JSON"));

}



TEST_CASE ("executing a plan publishes automation the engine evaluates", "[mixplan]")

{

    AudioEngine engine;

    engine.setActiveTrackCount (16);



    const auto result = parse (R"({"title":"ride","lanes":[

        { "track": 3, "param": "volume", "points": [[4, 0.4], [0, 0.2]], "why": "out of order on purpose" },

        { "track": 5, "param": "pan", "points": [[0, -0.5], [8, 0.5]] }

    ]})");



    REQUIRE (result.usable());



    const auto report = executeMixPlan (engine, result.plan);



    REQUIRE (report.applied == 2);

    REQUIRE (report.cleared == 0);

    REQUIRE (report.anyChange());

    REQUIRE (report.notes[0] == "plan: ride");



    // Held before the first point, interpolated after it: the parser sorted.

    REQUIRE (std::abs (engine.automationValue (3, AutomationParam::Volume, 0.0) - 0.2f) < 1.0e-6f);

    REQUIRE (std::abs (engine.automationValue (3, AutomationParam::Volume, 2.0) - 0.3f) < 1.0e-6f);

    REQUIRE (std::abs (engine.automationValue (3, AutomationParam::Volume, 9.0) - 0.4f) < 1.0e-6f);



    REQUIRE (std::abs (engine.automationValue (5, AutomationParam::Pan, 4.0) - 0.0f) < 1.0e-6f);

}



TEST_CASE ("an empty lane hands the fader back to manual", "[mixplan]")

{

    AudioEngine engine;

    engine.setActiveTrackCount (16);



    const auto first = parse (R"({"lanes":[{"track":2,"param":"volume","points":[[0,0.9]]}]})");

    REQUIRE (executeMixPlan (engine, first.plan).applied == 1);

    REQUIRE (std::abs (engine.automationValue (2, AutomationParam::Volume, 0.0) - 0.9f) < 1.0e-6f);



    const auto clear = parse (R"({"lanes":[{"track":2,"param":"volume","points":[]}]})");

    const auto report = executeMixPlan (engine, clear.plan);



    REQUIRE (report.cleared == 1);

    REQUIRE (report.applied == 0);

    REQUIRE (report.notes[0].contains ("cleared"));

}



TEST_CASE ("a lane whose track left the session is skipped, not folded", "[mixplan]")

{

    AudioEngine engine;

    engine.setActiveTrackCount (4);



    const auto result = parse (R"({"lanes":[

        { "track": 2, "param": "volume", "points": [[0, 0.5]] },

        { "track": 9, "param": "volume", "points": [[0, 0.5]] }

    ]})");



    REQUIRE (result.usable());

    REQUIRE (result.plan.lanes.size() == 2);



    const auto report = executeMixPlan (engine, result.plan);



    REQUIRE (report.applied == 1);

    REQUIRE (report.notes[1].contains ("no longer active"));

    REQUIRE (engine.automationValue (9, AutomationParam::Volume, 0.0) == 0.0f);

}

TEST_CASE ("the model reply is unwrapped from prose and fences", "[mixplan]")
{
    const auto fenced = juce::String (R"(Here is the plan:
```json
{"lanes":[{"track":3,"param":"volume","points":[[8,0.9]]}]}
```
Thanks)");

    const auto planJson = nc::extractMixPlanJson (fenced);
    REQUIRE (planJson.isNotEmpty());

    const auto plan = parse (planJson);
    REQUIRE (plan.usable());
    REQUIRE (plan.plan.lanes[0].track == 3);
    REQUIRE (plan.plan.lanes[0].points.size() == 1);

    REQUIRE (nc::extractMixPlanJson ("no json here").isEmpty());
    REQUIRE (nc::extractMixPlanJson ("{ broken").isEmpty());
}

TEST_CASE ("braces inside a plan string do not end the document", "[mixplan]")
{
    const auto reply = juce::String (R"(prefix {"title":"lift {chorus}","lanes":[{"track":2,"param":"volume","points":[[0,0.5]]}]} suffix)");
    const auto json = nc::extractMixPlanJson (reply);

    REQUIRE (json.isNotEmpty());
    REQUIRE (json.contains ("lift {chorus}"));

    const auto plan = parse (json);
    REQUIRE (plan.usable());
    REQUIRE (plan.plan.title == "lift {chorus}");
}
TEST_CASE ("the prompt carries the session, its units and the goal", "[mixplan]")
{
    const auto prompt = nc::buildMixPlanPrompt ("vocal-forward chorus lift", 16);

    REQUIRE (prompt.contains ("0: DRUMS"));
    REQUIRE (prompt.contains ("3: VOX"));
    REQUIRE (prompt.contains ("valid indices 0..15"));
    REQUIRE (prompt.contains ("Goal: vocal-forward chorus lift"));
    REQUIRE (prompt.contains ("linear gain 0..1"));
    REQUIRE (prompt.containsIgnoreCase ("Only JSON"));
}
