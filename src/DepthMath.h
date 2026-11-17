#pragma once
// The depth convention of the camera passes, as plain maths (no Direct3D, no DirectXMath): tests/depth_math_test.cpp.
//
// REVERSED-Z (lesson 46). A normal perspective projection writes depth 0 at the near plane and 1 at the far plane. The
// curve between them is 1/z shaped: almost every number between 0 and 1 is spent in the first few metres, and the far
// land gets what is left. A float is also finest near 0 and coarsest near 1, so the two effects ADD UP at distance:
// with the near plane at 0.1 and the far plane at 10 km, two surfaces 4 km away must be about 10 m apart before the
// depth buffer can tell them apart. Terrain, water and distant models flicker against each other (z-fighting).
//
// Reversed-Z writes 1 at the near plane and 0 at the far plane. The 1/z curve now spends its numbers where the float is
// finest, and the two effects CANCEL: at the same 4 km the step is a fraction of a millimetre. Nothing about the picture
// changes; only the numbers in the depth buffer do. Everything that touches them follows the same rule:
//     clear to 0 (= "infinitely far")       and       "in front" means GREATER, not LESS.
// The sun's shadow map is a separate, orthographic depth buffer and keeps the plain convention (clear 1, LESS).
//
// Camera::Update gets the reversed matrix by passing the planes to the projection swapped (far where near goes).

namespace DepthMath {

constexpr float kClear = 0.0f;   // what a camera depth buffer is cleared to: the far plane

// Distance in front of the camera `z` (between nearPlane and farPlane) -> the depth the projection writes.
inline double ReversedDepth(double z, double nearPlane, double farPlane)
{
    return nearPlane * (farPlane - z) / (z * (farPlane - nearPlane));
}

// The depth a normal (not reversed) projection would write: used by the test to show what reversing buys.
inline double StandardDepth(double z, double nearPlane, double farPlane)
{
    return farPlane * (z - nearPlane) / (z * (farPlane - nearPlane));
}

// The inverse of ReversedDepth: stored depth -> distance in front of the camera. shaders/water_ps.hlsl has the same line.
inline double LinearizeReversed(double depth, double nearPlane, double farPlane)
{
    return nearPlane * farPlane / (nearPlane + depth * (farPlane - nearPlane));
}

}   // namespace DepthMath
