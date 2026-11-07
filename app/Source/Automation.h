#pragma once

#include <vector>

namespace nc
{
/** Which fader a lane automates. Values are in the engine's own units:
    linear gain 0..1 for volume, -1..1 for pan (the same ranges the mixer and
    the phone remote already speak). */
enum class AutomationParam
{
    Volume,
    Pan
};

/**
    A breakpoint lane. Points are kept sorted by beat so evaluation is a single
    scan, and the lane is a plain value type: it can be copied, published to the
    audio thread, and compared in tests without touching the engine.
*/
class AutomationLane
{
public:
    static constexpr int maxPoints = 512;

    enum class Curve { linear, step, smooth };

    struct Point
    {
        double beats = 0.0;
        float value = 0.0f;
        Curve curve = Curve::linear;
    };

    /** Inserts a point, replacing any point already at that beat. False when the
        lane is full or the value is not finite. */
    bool addPoint (double beats, float value, Curve curve = Curve::linear);

    /** Removes a point by index. False when the index is out of range. */
    bool removePoint (int index);

    /** Replaces the lane with points in any order, sorting them. */
    void setPoints (const std::vector<Point>& points);

    void clear();

    bool empty() const { return count == 0; }
    int numPoints() const { return count; }
    Point pointAt (int index) const;

    /** Interpolates per the *previous* point's curve, held before the first
        and after the last. Returns 0 for an empty lane; callers check
        empty() when that matters. */
    float valueAt (double beats) const;

    static const char* curveName (Curve curve);
    static Curve curveFromName (const char* name);

private:
    void sortPoints();

    Point points[static_cast<size_t> (maxPoints)] {};
    int count = 0;
};
} // namespace nc
