#include "Automation.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nc
{
namespace
{
constexpr double sameBeat = 1.0e-9;
}

const char* AutomationLane::curveName (Curve curve)
{
    switch (curve)
    {
        case Curve::step: return "step";
        case Curve::smooth: return "smooth";
        default: return "linear";
    }
}

AutomationLane::Curve AutomationLane::curveFromName (const char* name)
{
    if (name == nullptr)
        return Curve::linear;

    if (std::strcmp (name, "step") == 0)
        return Curve::step;

    if (std::strcmp (name, "smooth") == 0)
        return Curve::smooth;

    return Curve::linear;
}

bool AutomationLane::addPoint (double beats, float value, Curve curve)
{
    if (! std::isfinite (beats) || ! std::isfinite (value) || beats < 0.0)
        return false;

    for (int i = 0; i < count; ++i)
    {
        if (std::abs (points[i].beats - beats) <= sameBeat)
        {
            points[i].value = value;
            points[i].curve = curve;
            return true;
        }

        if (points[i].beats > beats)
        {
            if (count >= maxPoints)
                return false;

            for (int j = count; j > i; --j)
                points[j] = points[j - 1];

            points[i].beats = beats;
            points[i].value = value;
            points[i].curve = curve;
            ++count;
            return true;
        }
    }

    if (count >= maxPoints)
        return false;

    points[count].beats = beats;
    points[count].value = value;
    points[count].curve = curve;
    ++count;
    return true;
}

bool AutomationLane::removePoint (int index)
{
    if (index < 0 || index >= count)
        return false;

    for (int i = index; i < count - 1; ++i)
        points[i] = points[i + 1];

    --count;
    return true;
}

void AutomationLane::sortPoints()
{
    std::sort (points, points + count, [] (const Point& a, const Point& b)
    {
        return a.beats < b.beats;
    });

    // Collapse duplicates that a caller's list may have produced, keeping the
    // last value written at a beat.
    int keep = 0;

    for (int i = 0; i < count; ++i)
    {
        if (keep > 0 && std::abs (points[keep - 1].beats - points[i].beats) <= sameBeat)
        {
            points[keep - 1] = points[i];
            continue;
        }

        points[keep++] = points[i];
    }

    count = keep;
}

void AutomationLane::setPoints (const std::vector<Point>& newPoints)
{
    count = 0;

    for (const auto& point : newPoints)
    {
        if (count >= maxPoints)
            break;

        if (! std::isfinite (point.beats) || ! std::isfinite (point.value) || point.beats < 0.0)
            continue;

        points[count++] = point;
    }

    sortPoints();
}

void AutomationLane::clear()
{
    count = 0;
}

AutomationLane::Point AutomationLane::pointAt (int index) const
{
    if (index < 0 || index >= count)
        return {};

    return points[index];
}

float AutomationLane::valueAt (double beats) const
{
    if (count == 0)
        return 0.0f;

    if (beats <= points[0].beats)
        return points[0].value;

    if (beats >= points[count - 1].beats)
        return points[count - 1].value;

    for (int i = 1; i < count; ++i)
    {
        if (beats <= points[i].beats)
        {
            const auto span = points[i].beats - points[i - 1].beats;

            if (span <= sameBeat)
                return points[i].value;

            const auto position = (beats - points[i - 1].beats) / span;

            switch (points[i - 1].curve)
            {
                case Curve::step:
                    return points[i - 1].value;

                case Curve::smooth:
                    {
                        // Smoothstep (3t^2 - 2t^3): zero slope at both ends.
                        const auto t = static_cast<float> (position);
                        const auto eased = t * t * (3.0f - 2.0f * t);
                        return points[i - 1].value
                               + (points[i].value - points[i - 1].value) * eased;
                    }

                default:
                    return points[i - 1].value
                           + (points[i].value - points[i - 1].value) * static_cast<float> (position);
            }
        }
    }

    return points[count - 1].value;
}
} // namespace nc
