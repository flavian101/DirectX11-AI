#pragma once
#include <DirectXMath.h>
#include <cmath>

// Colours you PICK (ImGui colour pickers, values you type) are sRGB: numbers shaped for
// display. Lighting maths needs LINEAR numbers, where 0.5 really is half the light.
// Convert at the boundary: store sRGB for the user, upload linear to the GPU.
inline float SrgbToLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline DirectX::XMFLOAT3 SrgbToLinear(const DirectX::XMFLOAT3& c)
{
    return { SrgbToLinear(c.x), SrgbToLinear(c.y), SrgbToLinear(c.z) };
}
