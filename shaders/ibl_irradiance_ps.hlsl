// Bake 1 of 3: DIFFUSE irradiance. For every direction N, the average sky light a surface facing N
// receives, weighted by the cosine of the angle (light arriving at a slant counts for less). Stored
// as E/pi, so a white diffuse surface just multiplies it by its albedo.
#include "ibl_common.hlsli"

TextureCube environmentMap : register(t0);
SamplerState environmentSampler : register(s0);

struct PS_IN { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

float4 main(PS_IN input) : SV_Target
{
    const float3 N = CubeFaceDirection((uint)bakeParams.x, input.uv);

    // Any pair of axes perpendicular to N will do: we visit the WHOLE hemisphere.
    float3 up = abs(N.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    const float3 right = normalize(cross(up, N));
    up = cross(N, right);

    // Walk the hemisphere on a regular grid of angles (midpoints of equal steps). The sky is smooth,
    // so a coarse grid is enough; reading a small mip of the sky keeps a bright speck from sparkling.
    // bakeParams.w = 1 means "fast": a sky that changes with the time of day is baked again and again, and a smooth sky needs a
    // tenth of the samples (the same grid, coarser). A still sky is baked once, with the full grid.
    const bool fast = bakeParams.w > 0.5f;
    const int phiSteps = fast ? 40 : 128;
    const int thetaSteps = fast ? 10 : 32;
    float3 sum = float3(0.0f, 0.0f, 0.0f);
    [loop] for (int i = 0; i < phiSteps; ++i) {
        const float phi = (i + 0.5f) * (2.0f * kPi / phiSteps);
        [loop] for (int j = 0; j < thetaSteps; ++j) {
            const float theta = (j + 0.5f) * (0.5f * kPi / thetaSteps);
            const float sinTheta = sin(theta);
            const float cosTheta = cos(theta);
            const float3 direction = (sinTheta * cos(phi)) * right + (sinTheta * sin(phi)) * up + cosTheta * N;
            // cos: slanted light counts less. sin: the grid is denser near the pole, so each step covers less sky.
            sum += environmentMap.SampleLevel(environmentSampler, direction, 2.0f).rgb * cosTheta * sinTheta;
        }
    }
    return float4(kPi * sum / (phiSteps * thetaSteps), 1.0f);
}
