#include "common.hlsli"

// Four directional sine waves. Different directions and frequencies keep the surface
// from looking like a repeating grid. Multipliers scale the three material controls:
// materialParams = (amplitude, speed, frequency, reflectivity).
#define WAVE_COUNT 4
static const float2 kWaveDir[WAVE_COUNT]   = { float2(1.0f, 0.0f), float2(0.7f, 0.7f), float2(-0.3f, 0.95f), float2(0.5f, -0.85f) };
static const float  kWaveFreq[WAVE_COUNT]  = { 1.0f, 1.7f, 2.9f, 4.3f };
static const float  kWaveAmp[WAVE_COUNT]   = { 1.0f, 0.55f, 0.3f, 0.15f };
static const float  kWaveSpeed[WAVE_COUNT] = { 1.0f, 1.3f, 1.8f, 2.4f };

struct VS_IN { float3 position : POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; };
struct VS_OUT { float4 position : SV_POSITION; float3 normal : NORMAL; float2 texCoord : TEXCOORD; float3 worldPos : TEXCOORD1; };

VS_OUT main(VS_IN input)
{
    VS_OUT output;
    const float amplitude = materialParams.x;
    const float speed     = materialParams.y;
    const float frequency = materialParams.z;
    const float time      = frameParams.x;

    // Go to WORLD space first: waves are measured in world units, so resizing the plane
    // changes how much water you see, not how big the waves are.
    // (Assumes a horizontal plane - rotating the water in the editor would need the
    // normal below rotated by the world matrix as well.)
    float3 worldPos = mul(float4(input.position, 1.0f), world).xyz;

    float  height = 0.0f;
    float2 slope  = float2(0.0f, 0.0f);   // d(height)/dx, d(height)/dz
    [unroll]
    for (int i = 0; i < WAVE_COUNT; ++i) {
        const float2 dir   = normalize(kWaveDir[i]);
        const float  k     = frequency * kWaveFreq[i];
        const float  a     = amplitude * kWaveAmp[i];
        const float  phase = dot(dir, worldPos.xz) * k + time * speed * kWaveSpeed[i];
        height += a * sin(phase);
        slope  += a * k * cos(phase) * dir;   // analytic derivative of the line above
    }
    worldPos.y += height;

    // Surface normal from the slope: a hill rising toward +x tilts the normal toward -x.
    output.normal = normalize(float3(-slope.x, 1.0f, -slope.y));
    output.position = mul(mul(float4(worldPos, 1.0f), view), proj);
    output.texCoord = input.texCoord;
    output.worldPos = worldPos;
    return output;
}
