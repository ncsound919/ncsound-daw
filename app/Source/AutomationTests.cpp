#include <catch2/catch_test_macros.hpp>

#include "AudioEngine.h"
#include "Automation.h"

#include <cmath>
#include <string>
#include <vector>

using nc::AutomationLane;
using nc::AutomationParam;

TEST_CASE ("an empty lane has no automation", "[automation]")
{
    AutomationLane lane;

    REQUIRE (lane.empty());
    REQUIRE (lane.numPoints() == 0);
    REQUIRE (lane.valueAt (0.0) == 0.0f);
    REQUIRE (lane.valueAt (128.0) == 0.0f);
}

TEST_CASE ("points hold at the ends and interpolate linearly between", "[automation]")
{
    AutomationLane lane;

    REQUIRE (lane.addPoint (0.0, 0.2f));
    REQUIRE (lane.addPoint (4.0, 0.6f));

    REQUIRE (lane.numPoints() == 2);

    // Held before the first point and after the last.
    REQUIRE (lane.valueAt (-1.0) == 0.2f);
    REQUIRE (lane.valueAt (0.0) == 0.2f);
    REQUIRE (lane.valueAt (4.0) == 0.6f);
    REQUIRE (lane.valueAt (9.0) == 0.6f);

    // Linear in between.
    REQUIRE (std::abs (lane.valueAt (2.0) - 0.4f) < 1.0e-6f);
    REQUIRE (std::abs (lane.valueAt (1.0) - 0.3f) < 1.0e-6f);
    REQUIRE (std::abs (lane.valueAt (3.0) - 0.5f) < 1.0e-6f);
}

TEST_CASE ("adding a point keeps the lane sorted and replaces the same beat", "[automation]")
{
    AutomationLane lane;

    REQUIRE (lane.addPoint (8.0, 0.8f));
    REQUIRE (lane.addPoint (0.0, 0.0f));
    REQUIRE (lane.addPoint (4.0, 0.4f));

    REQUIRE (lane.numPoints() == 3);
    REQUIRE (lane.pointAt (0).beats == 0.0);
    REQUIRE (lane.pointAt (1).beats == 4.0);
    REQUIRE (lane.pointAt (2).beats == 8.0);

    // Same beat updates in place instead of stacking points.
    REQUIRE (lane.addPoint (4.0, 0.9f));
    REQUIRE (lane.numPoints() == 3);
    REQUIRE (lane.pointAt (1).value == 0.9f);
}

TEST_CASE ("a point can be removed and the lane has a hard capacity", "[automation]")
{
    AutomationLane lane;

    REQUIRE (lane.addPoint (0.0, 0.0f));
    REQUIRE (lane.addPoint (4.0, 0.5f));
    REQUIRE (lane.addPoint (8.0, 1.0f));

    REQUIRE (lane.removePoint (1));
    REQUIRE (lane.numPoints() == 2);
    REQUIRE (lane.pointAt (1).beats == 8.0);

    REQUIRE_FALSE (lane.removePoint (5));
    REQUIRE_FALSE (lane.removePoint (-1));

    // Invalid input is refused rather than stored.
    REQUIRE_FALSE (lane.addPoint (-1.0, 0.5f));
    REQUIRE_FALSE (lane.addPoint (1.0, std::nanf ("")));

    AutomationLane full;

    for (int i = 0; i < AutomationLane::maxPoints; ++i)
        REQUIRE (full.addPoint (static_cast<double> (i), 0.5f));

    REQUIRE (full.numPoints() == AutomationLane::maxPoints);
    REQUIRE_FALSE (full.addPoint (AutomationLane::maxPoints + 1.0, 0.5f));
}

TEST_CASE ("step holds the previous value and smooth eases at both ends", "[automation]")
{
    AutomationLane stepLane;
    REQUIRE (stepLane.addPoint (0.0, 0.2f, AutomationLane::Curve::step));
    REQUIRE (stepLane.addPoint (4.0, 0.6f));
    REQUIRE (stepLane.valueAt (2.0) == 0.2f);
    REQUIRE (stepLane.valueAt (3.9) == 0.2f);
    REQUIRE (stepLane.valueAt (4.0) == 0.6f);

    AutomationLane smoothLane;
    REQUIRE (smoothLane.addPoint (0.0, 0.0f, AutomationLane::Curve::smooth));
    REQUIRE (smoothLane.addPoint (4.0, 1.0f));
    REQUIRE (smoothLane.valueAt (0.0) == 0.0f);
    REQUIRE (smoothLane.valueAt (4.0) == 1.0f);
    REQUIRE (std::abs (smoothLane.valueAt (2.0) - 0.5f) < 1.0e-6f);
    // Smoothstep is slower than linear early on.
    REQUIRE (smoothLane.valueAt (1.0) < smoothLane.valueAt (2.0) - 0.1f);

    REQUIRE (AutomationLane::curveFromName ("step") == AutomationLane::Curve::step);
    REQUIRE (AutomationLane::curveFromName ("smooth") == AutomationLane::Curve::smooth);
    REQUIRE (AutomationLane::curveFromName ("bogus") == AutomationLane::Curve::linear);
    REQUIRE (AutomationLane::curveFromName (nullptr) == AutomationLane::Curve::linear);
    REQUIRE (std::string (AutomationLane::curveName (AutomationLane::Curve::step)) == "step");
}

TEST_CASE ("setPoints sorts a caller's list and drops duplicates", "[automation]")
{
    AutomationLane lane;

    lane.setPoints ({ { 8.0, 0.8f }, { 0.0, 0.1f }, { 4.0, 0.4f }, { 4.0, 0.5f } });

    REQUIRE (lane.numPoints() == 3);
    REQUIRE (lane.pointAt (0).beats == 0.0);
    REQUIRE (lane.pointAt (1).beats == 4.0);
    REQUIRE (lane.pointAt (1).value == 0.5f);   // last value at a beat wins
    REQUIRE (lane.pointAt (2).beats == 8.0);
}

TEST_CASE ("a lane published to the engine drives that fader until cleared", "[automation]")
{
    nc::AudioEngine engine;

    AutomationLane lane;
    REQUIRE (lane.addPoint (0.0, 0.25f));
    REQUIRE (lane.addPoint (8.0, 0.75f));

    engine.setTrackAutomation (0, AutomationParam::Volume, lane);
    engine.setTrackAutomation (1, AutomationParam::Pan, lane);

    REQUIRE (std::abs (engine.automationValue (0, AutomationParam::Volume, 0.0) - 0.25f) < 1.0e-6f);
    REQUIRE (std::abs (engine.automationValue (0, AutomationParam::Volume, 4.0) - 0.5f) < 1.0e-6f);
    REQUIRE (std::abs (engine.automationValue (1, AutomationParam::Pan, 8.0) - 0.75f) < 1.0e-6f);

    // Track 0 volume and track 1 pan are independent lanes.
    REQUIRE (engine.automationValue (0, AutomationParam::Pan, 4.0) == 0.0f);

    engine.clearTrackAutomation (0, AutomationParam::Volume);
    REQUIRE (engine.automationValue (0, AutomationParam::Volume, 4.0) == 0.0f);

    // Out-of-range tracks are ignored rather than crashing.
    engine.setTrackAutomation (99, AutomationParam::Volume, lane);
    REQUIRE (engine.automationValue (99, AutomationParam::Volume, 4.0) == 0.0f);
}

TEST_CASE ("republishing a lane leaves the previous value readable", "[automation]")
{
    nc::AudioEngine engine;

    AutomationLane first;
    REQUIRE (first.addPoint (0.0, 0.1f));
    engine.setTrackAutomation (0, AutomationParam::Volume, first);

    AutomationLane second;
    REQUIRE (second.addPoint (0.0, 0.9f));
    engine.setTrackAutomation (0, AutomationParam::Volume, second);

    // The newest lane wins immediately; the retired one is not freed yet.
    REQUIRE (std::abs (engine.automationValue (0, AutomationParam::Volume, 0.0) - 0.9f) < 1.0e-6f);
}
