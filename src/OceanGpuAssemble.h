#pragma once
// The last compute pass of the GPU sea, as C++ (roadmap O2, the GPU half): turn what the FFT left in textures A and B into the two textures the water
// reads. Pure: no Direct3D. Tests: tests/ocean_gpu_assemble_test.cpp. Shader: shaders/ocean_assemble_cs.hlsl (a line-by-line copy of AssembleTexel).
//
// After the FFT, per texel:
//     A = (height, displacement x, displacement z, slope x)       B = (slope z, dXdx, dZdz, dXdz)       (OceanGpuPack.h says why)
// The water wants them grouped by WHO reads them:
//     displacement map  RGBA16F   (choppiness * disp x,  height,  choppiness * disp z,  0)       the VERTEX shader moves each vertex by this
//     slope map         RGBA16F   (slope x,  slope z,  Jacobian,  0)                              the PIXEL shader: the normal, and where the sea folds (foam)
// The choppiness is applied HERE, not earlier: OceanFFT::Evolve returns the displacement unscaled, so the slider can change without a new FFT.
// The Jacobian of the surface map p -> p + choppiness * D is J = (1 + c dXdx)(1 + c dZdz) - (c dXdz)^2: 1 on a flat sea, falling toward 0 where
// crests pinch, negative where the surface folds over itself.
//
// Half floats (RGBA16F), not 32-bit: the maps get a MIP CHAIN (distant water must not alias), and 16-bit float textures can be filtered and
// generated on every Direct3D 11 card; 32-bit float filtering is optional there. Half has 11 bits of precision: about 1/2000 of a value, 4 mm for a
// 8 m wave. That is fine to DRAW; boats read the exact CPU mirror.
#include "OceanGpuPack.h"
#include <cmath>
#include <cstdint>
#include <limits>

namespace OceanGpuAssemble {

using OceanGpuPack::Float4;

// The constant buffer of the assemble pass (b0 in ocean_assemble_cs.hlsl). 16 bytes.
struct AssembleConstants {
    std::uint32_t n = 0;       // texels per side
    float choppiness = 0.0f;   // 0 .. 1
    float padding0 = 0.0f;
    float padding1 = 0.0f;
};
static_assert(sizeof(AssembleConstants) == 16, "a constant buffer must be a multiple of 16 bytes");

struct Texel {
    Float4 displacement;   // (x, height, z, 0)
    Float4 slope;          // (slope x, slope z, Jacobian, 0)
};

// shaders/ocean_assemble_cs.hlsl, one thread per texel, in float like the GPU. Keep the two in step.
inline Texel AssembleTexel(const Float4& a, const Float4& b, float choppiness)
{
    const float jxx = 1.0f + choppiness * b.y, jzz = 1.0f + choppiness * b.z, jxz = choppiness * b.w;
    Texel t;
    t.displacement = Float4{ choppiness * a.y, a.x, choppiness * a.z, 0.0f };
    t.slope = Float4{ a.w, b.x, jxx * jzz - jxz * jxz, 0.0f };
    return t;
}

// A 16-bit float (1 sign bit, 5 exponent bits, 10 fraction bits) as a float: what a staging copy of an RGBA16F texture holds.
inline float HalfToFloat(std::uint16_t h)
{
    const std::uint32_t sign = (h >> 15) & 1u, exponent = (h >> 10) & 0x1Fu, fraction = h & 0x3FFu;
    float value;
    if (exponent == 0) value = std::ldexp(static_cast<float>(fraction), -24);                  // zero and the tiny "subnormal" numbers
    else if (exponent == 31) value = fraction == 0 ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
    else value = std::ldexp(static_cast<float>(fraction | 0x400u), static_cast<int>(exponent) - 25);   // (1 + fraction / 1024) * 2^(exponent - 15)
    return sign ? -value : value;
}

}   // namespace OceanGpuAssemble
