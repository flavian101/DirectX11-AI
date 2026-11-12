#pragma once
// The maths of a floating object (lesson 33), with no Direct3D in it so it can be tested on its own.
//
// A boat does not need fluid physics to look right. It needs three numbers, per frame: how high the water is under it,
// and how steeply the surface leans along and across it. We read the water height at a few points under the hull (the same
// Gerstner waves the shaders draw, so the boat sits ON the pictured surface), fit a plane through them, and let springs
// follow that plane, so the boat bobs and leans instead of snapping.
#include <algorithm>
#include <cmath>
#include <vector>

namespace Buoyancy {

struct Sample {
    float x = 0.0f;       // world position of the sample point
    float z = 0.0f;
    float height = 0.0f;  // world height of the water surface there
};

// The plane through the samples that comes closest to all of them (least squares): height = mean + slopeX * (x - cx) + slopeZ * (z - cz).
struct Plane {
    float mean = 0.0f;
    float slopeX = 0.0f;
    float slopeZ = 0.0f;
};

inline Plane FitPlane(const std::vector<Sample>& samples)
{
    Plane plane;
    if (samples.empty()) return plane;
    const float n = static_cast<float>(samples.size());
    float cx = 0.0f, cz = 0.0f, mean = 0.0f;
    for (const Sample& s : samples) { cx += s.x; cz += s.z; mean += s.height; }
    cx /= n; cz /= n; mean /= n;
    // Normal equations of the least-squares fit, on data centred at the middle: a 2x2 system.
    float sxx = 0.0f, sxz = 0.0f, szz = 0.0f, sxh = 0.0f, szh = 0.0f;
    for (const Sample& s : samples) {
        const float dx = s.x - cx, dz = s.z - cz, dh = s.height - mean;
        sxx += dx * dx; sxz += dx * dz; szz += dz * dz; sxh += dx * dh; szh += dz * dh;
    }
    const float det = sxx * szz - sxz * sxz;
    plane.mean = mean;
    if (std::abs(det) > 1e-9f) {
        plane.slopeX = (sxh * szz - szh * sxz) / det;
        plane.slopeZ = (szh * sxx - sxh * sxz) / det;
    }   // (all points on a line, or just one: no slope can be found, so the boat stays level)
    return plane;
}

// One number that chases a target like a weight on a spring with some friction. `omega` is how stiff the spring is
// (radians per second: about 1/omega seconds to catch up), `zeta` how much friction there is (1 = none left over:
// no bounce; below 1 = it overshoots and wobbles a little, which is what a boat does).
struct Spring {
    float value = 0.0f;
    float velocity = 0.0f;

    void Snap(float v) { value = v; velocity = 0.0f; }

    void Step(float target, float omega, float zeta, float dt)
    {
        // Semi-implicit Euler is stable only while omega * step is small, so a long frame is cut into short steps.
        const float maxStep = 1.0f / 120.0f;
        dt = std::clamp(dt, 0.0f, 0.1f);
        const int steps = (std::max)(1, static_cast<int>(std::ceil(dt / maxStep)));
        const float h = dt / static_cast<float>(steps);
        for (int i = 0; i < steps; ++i) {
            const float acceleration = omega * omega * (target - value) - 2.0f * zeta * omega * velocity;
            velocity += acceleration * h;   // velocity first ...
            value += velocity * h;          // ... then position from the NEW velocity: that is what keeps it stable
        }
    }
};

// The unit surface normal of a plane that leans slopeX along x and slopeZ along z, with `follow` (0..1) scaling how much of
// the lean the boat copies: 0 = always level, 1 = lies on the waves.
inline void Normal(float slopeX, float slopeZ, float follow, float out[3])
{
    const float nx = -slopeX * follow, ny = 1.0f, nz = -slopeZ * follow;
    const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
    out[0] = nx / length; out[1] = ny / length; out[2] = nz / length;
}

// The 3x3 rotation (row-vector convention, like DirectXMath: v' = v * M) that turns the up direction (0,1,0) into `normal`
// by the shortest turn. Because it turns about the axis perpendicular to both, it adds no twist: the boat's heading is
// untouched when it tilts.
inline void RotationFromUp(const float normal[3], float m[9])
{
    // axis = up x n = (nz, 0, -nx); cos = ny.
    const float ax = normal[2], az = -normal[0];
    const float c = normal[1];
    const float s2 = ax * ax + az * az;   // sin^2
    if (s2 < 1e-12f) {   // already up (or exactly upside down: not a boat's problem)
        const float identity[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
        std::copy(identity, identity + 9, m);
        return;
    }
    const float s = std::sqrt(s2);
    const float ux = ax / s, uz = az / s;   // unit axis (its y is 0)
    const float t = 1.0f - c;
    // Rodrigues' formula with the axis (ux, 0, uz), written out as the COLUMN-vector matrix R, then transposed for rows.
    const float r[9] = {
        c + ux * ux * t,   -uz * s,    ux * uz * t,
        uz * s,             c,         -ux * s,
        ux * uz * t,        ux * s,     c + uz * uz * t };
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) m[row * 3 + col] = r[col * 3 + row];
}

} // namespace Buoyancy
