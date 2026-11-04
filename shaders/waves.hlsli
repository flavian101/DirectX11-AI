// The water surface as a sum of directional sine waves. Shared by the vertex shader
// (which needs the HEIGHT to move vertices) and the pixel shader (which needs the SLOPE to
// light every pixel). Evaluating the slope per pixel keeps lighting sharp even when the
// mesh is too coarse to represent the small waves as geometry.
//
// Wave settings come from the water's material block (waveParams); frameParams.x = time.
#ifndef WAVES_HLSLI
#define WAVES_HLSLI
#include "common.hlsli"
#include "water_material.hlsli"

#define WAVE_COUNT 4
static const float2 kWaveDir[WAVE_COUNT]   = { float2(1.0f, 0.0f), float2(0.7f, 0.7f), float2(-0.3f, 0.95f), float2(0.5f, -0.85f) };
static const float  kWaveFreq[WAVE_COUNT]  = { 1.0f, 1.7f, 2.9f, 4.3f };
static const float  kWaveAmp[WAVE_COUNT]   = { 1.0f, 0.55f, 0.3f, 0.15f };
static const float  kWaveSpeed[WAVE_COUNT] = { 1.0f, 1.3f, 1.8f, 2.4f };

// xz = WORLD-space position. slope = (d height/dx, d height/dz) - the analytic derivative.
void EvaluateWaves(float2 xz, out float height, out float2 slope)
{
    const float amplitude = waveParams.x;
    const float speed     = waveParams.y;
    const float frequency = waveParams.z;
    const float time      = frameParams.x;

    height = 0.0f;
    slope  = float2(0.0f, 0.0f);
    [unroll]
    for (int i = 0; i < WAVE_COUNT; ++i) {
        const float2 dir   = normalize(kWaveDir[i]);
        const float  k     = frequency * kWaveFreq[i];
        const float  a     = amplitude * kWaveAmp[i];
        const float  phase = dot(dir, xz) * k + time * speed * kWaveSpeed[i];
        height += a * sin(phase);
        slope  += a * k * cos(phase) * dir;
    }
}
#endif
