#pragma once
// How a directional light (the sun, the moon) is aimed, in the terms a person thinks in. No Direct3D: unit-tested.
//
//   azimuth   - compass direction of the light IN THE SKY, in degrees. 0 = +X, 90 = +Z (the same convention the wind uses).
//   elevation - height above the horizon in degrees: 0 = on the horizon, 90 = straight overhead, negative = below it.
//
// Two ways to say the same thing are kept in step by these functions:
//   the direction the light TRAVELS (what the shaders get), and the Euler rotation the editor's gizmo edits. The light
//   shines along its own forward axis (0, 0, 1) turned by the rotation, in the engine's order: about X, then Y, then Z.
#include <algorithm>
#include <cmath>

namespace LightAngles {

struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };

constexpr float kPi = 3.14159265358979f;
inline float Radians(float degrees) { return degrees * (kPi / 180.0f); }
inline float Degrees(float radians) { return radians * (180.0f / kPi); }

// The direction TOWARD the light.
inline Vec3 ToLight(float azimuthDegrees, float elevationDegrees)
{
    const float az = Radians(azimuthDegrees), el = Radians(elevationDegrees);
    return { std::cos(el) * std::cos(az), std::sin(el), std::cos(el) * std::sin(az) };
}

// The direction the light travels: the opposite of ToLight.
inline Vec3 Travel(float azimuthDegrees, float elevationDegrees)
{
    const Vec3 up = ToLight(azimuthDegrees, elevationDegrees);
    return { -up.x, -up.y, -up.z };
}

// The other way round. `travel` need not be a unit vector. azimuth comes out in 0..360.
inline void FromTravel(const Vec3& travel, float& azimuthDegrees, float& elevationDegrees)
{
    const float length = std::sqrt(travel.x * travel.x + travel.y * travel.y + travel.z * travel.z);
    if (length < 1e-6f) { azimuthDegrees = 0.0f; elevationDegrees = 90.0f; return; }
    const Vec3 toLight = { -travel.x / length, -travel.y / length, -travel.z / length };
    elevationDegrees = Degrees(std::asin(std::clamp(toLight.y, -1.0f, 1.0f)));
    // Straight up or down has no compass direction: keep whatever azimuth was there instead of snapping to 0.
    if (std::abs(toLight.x) > 1e-5f || std::abs(toLight.z) > 1e-5f) {
        azimuthDegrees = Degrees(std::atan2(toLight.z, toLight.x));
        if (azimuthDegrees < 0.0f) azimuthDegrees += 360.0f;
    }
}

// The forward axis (0,0,1) turned by the rotation, in the order X, then Y, then Z (row-vector convention, like DirectXMath).
inline Vec3 TravelFromEuler(float rx, float ry, float rz)
{
    Vec3 v = { 0.0f, 0.0f, 1.0f };
    float c = std::cos(rx), s = std::sin(rx);
    v = { v.x, v.y * c - v.z * s, v.y * s + v.z * c };
    c = std::cos(ry); s = std::sin(ry);
    v = { v.x * c + v.z * s, v.y, -v.x * s + v.z * c };
    c = std::cos(rz); s = std::sin(rz);
    v = { v.x * c - v.y * s, v.x * s + v.y * c, v.z };
    return v;
}

// A rotation (radians, roll 0) that makes the light travel along `travel`: pitch about X (down is positive), yaw about Y.
inline void EulerFromTravel(const Vec3& travel, float& pitch, float& yaw)
{
    const float length = std::sqrt(travel.x * travel.x + travel.y * travel.y + travel.z * travel.z);
    if (length < 1e-6f) { pitch = kPi * 0.5f; return; }
    pitch = std::asin(std::clamp(-travel.y / length, -1.0f, 1.0f));
    if (std::abs(travel.x) > 1e-6f || std::abs(travel.z) > 1e-6f) yaw = std::atan2(travel.x, travel.z);   // else keep the yaw we had
}

}  // namespace LightAngles
