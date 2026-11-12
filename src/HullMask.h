#pragma once
// Plain geometry for the "hull mask": the flat lid that hides the water inside a boat (lesson 33). No Direct3D here,
// so it can be tested without a graphics device (tests/buoyancy_test.cpp).
//
// The idea: the water is a plane that runs straight through a floating boat, so you see waves INSIDE the hull. A real
// boat keeps water out. So before the water is drawn, we write a lid into the DEPTH buffer, covering the inside of the
// hull at rim height. The water's depth test then fails behind the lid, and it is simply not drawn there.
// The lid is the boat's outline seen from above (its convex hull), shrunk a little so it stays inside the walls.
#include <algorithm>
#include <cmath>
#include <vector>

namespace HullMask {

struct Point2 {
    float x = 0.0f;
    float z = 0.0f;
};

inline float Cross(const Point2& o, const Point2& a, const Point2& b)
{
    return (a.x - o.x) * (b.z - o.z) - (a.z - o.z) * (b.x - o.x);
}

// The smallest convex polygon around the points (Andrew's monotone chain). Counter-clockwise (x to the right, z up),
// no repeated first point. Fewer than 3 distinct points gives back what there is.
inline std::vector<Point2> ConvexHull(std::vector<Point2> points)
{
    std::sort(points.begin(), points.end(), [](const Point2& a, const Point2& b) { return a.x < b.x || (a.x == b.x && a.z < b.z); });
    points.erase(std::unique(points.begin(), points.end(), [](const Point2& a, const Point2& b) { return a.x == b.x && a.z == b.z; }), points.end());
    if (points.size() < 3) return points;
    std::vector<Point2> hull(points.size() * 2);
    size_t k = 0;
    for (size_t i = 0; i < points.size(); ++i) {                      // lower chain
        while (k >= 2 && Cross(hull[k - 2], hull[k - 1], points[i]) <= 0.0f) --k;
        hull[k++] = points[i];
    }
    for (size_t i = points.size() - 1, lower = k + 1; i > 0; --i) {   // upper chain
        while (k >= lower && Cross(hull[k - 2], hull[k - 1], points[i - 1]) <= 0.0f) --k;
        hull[k++] = points[i - 1];
    }
    hull.resize(k - 1);   // the last point repeats the first
    return hull;
}

inline float Area(const std::vector<Point2>& polygon)
{
    float twice = 0.0f;
    for (size_t i = 0; i < polygon.size(); ++i) {
        const Point2& a = polygon[i];
        const Point2& b = polygon[(i + 1) % polygon.size()];
        twice += a.x * b.z - b.x * a.z;
    }
    return std::abs(twice) * 0.5f;
}

inline Point2 Centroid(const std::vector<Point2>& polygon)
{
    Point2 sum;
    for (const Point2& p : polygon) { sum.x += p.x; sum.z += p.z; }
    const float n = polygon.empty() ? 1.0f : static_cast<float>(polygon.size());
    return { sum.x / n, sum.z / n };
}

// The polygon with every edge moved `distance` towards the inside (a convex counter-clockwise polygon in, the same number
// of points out). Each new corner is where its two moved edges meet. The distance is limited so the polygon cannot turn
// inside out: when it is too large the polygon collapses towards its middle instead.
inline std::vector<Point2> InsetConvex(const std::vector<Point2>& polygon, float distance)
{
    const size_t n = polygon.size();
    if (n < 3 || distance <= 0.0f) return polygon;
    // The widest circle that fits is about (area / half the perimeter) across: never move edges further than 90% of that.
    float perimeter = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        const Point2& a = polygon[i];
        const Point2& b = polygon[(i + 1) % n];
        perimeter += std::hypot(b.x - a.x, b.z - a.z);
    }
    const float inradius = perimeter > 1e-9f ? 2.0f * Area(polygon) / perimeter : 0.0f;
    const float d = (std::min)(distance, inradius * 0.9f);

    std::vector<Point2> result(n);
    for (size_t i = 0; i < n; ++i) {
        const Point2& prev = polygon[(i + n - 1) % n];
        const Point2& cur = polygon[i];
        const Point2& next = polygon[(i + 1) % n];
        // Unit directions of the two edges meeting at `cur`, and their inward (left, for counter-clockwise) normals.
        float e0x = cur.x - prev.x, e0z = cur.z - prev.z;
        float e1x = next.x - cur.x, e1z = next.z - cur.z;
        const float l0 = std::hypot(e0x, e0z), l1 = std::hypot(e1x, e1z);
        if (l0 < 1e-9f || l1 < 1e-9f) { result[i] = cur; continue; }
        e0x /= l0; e0z /= l0; e1x /= l1; e1z /= l1;
        const float n0x = -e0z, n0z = e0x;
        const float n1x = -e1z, n1z = e1x;
        // The corner moves along the bisector: its distance to BOTH edges must grow by d. With the normals summed, that is
        // d / (1 + n0 . n1) * (n0 + n1)... derived from n0 . m = d and n1 . m = d.
        const float denominator = 1.0f + (n0x * n1x + n0z * n1z);
        if (denominator < 1e-4f) { result[i] = { cur.x + n0x * d, cur.z + n0z * d }; continue; }   // a hairpin: just push it sideways
        const float k = d / denominator;
        result[i] = { cur.x + (n0x + n1x) * k, cur.z + (n0z + n1z) * k };
    }
    return result;
}

inline bool InsideConvex(const std::vector<Point2>& polygon, const Point2& p, float tolerance = 1e-5f)
{
    if (polygon.size() < 3) return false;
    for (size_t i = 0; i < polygon.size(); ++i) {
        if (Cross(polygon[i], polygon[(i + 1) % polygon.size()], p) < -tolerance) return false;
    }
    return true;
}

} // namespace HullMask
