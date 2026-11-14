// The water's own constant block (b4). Declared HERE, by the shaders that use it, instead of
// squeezing water numbers into the generic per-object buffer. CPU side: WaterMaterialCB in Water.cpp.
// Every field is explained there too: change one place, change the other.
#ifndef WATER_MATERIAL_HLSLI
#define WATER_MATERIAL_HLSLI

#define WATER_MAX_WAVES 12   // must equal WaterWaves::kMaxWaves (src/WaterWaves.h)

cbuffer WaterMaterial : register(b4) {
    float4 waterDeepColor;      // rgb = deep-water colour (linear), a = opacity of deep water
    float4 waterShallowColor;   // rgb = shallow-water colour (linear), a = refraction strength (fraction of the screen)
    float4 waterOptics;         // x = reflectivity, y = glint strength, z = glint sharpness, w = mirror distortion
    float4 waterDepthParams;    // x = fade distance, y = foam distance, z = foam strength, w = absorption
    float4 waterWaveInfo;       // x = wave count, y = detail normal strength, z = detail scale, w = detail fade distance
    float4 waterFoamInfo;       // x = crest foam strength, y = crest foam threshold, z = scatter strength, w = debug view
    float4 waterMeshInfo;       // x = LOD distance, y = size of one grid cell at the finest level, z = tallest possible wave
    float4 waterWind;           // xy = unit direction the waves travel toward (XZ); for a river, the direction of the current
    float4 waterShape;          // x = 1 when this body has a mirror image bound to t3 this frame, y = shape (0 rectangle, 1 ellipse), z = ellipse edge softness
    float4 waterFlow;           // x = ripple drift multiplier (1 = wind-blown, river = rides the current), y = sideways ripple drift (1 on, 0 river), z = body type (0 ocean, 1 lake, 2 river, 3 pond), w = flow speed
    float4 waterShore;          // x = 1 when this body's depth map is bound to t15, y = shore wave strength (0 = waves identical everywhere), z = swash, w = shelter (0 = off)
    float4 waterGroupInfo;      // wave groups (WaveGroupFactor): x = strength (0 = off), y = k of the pattern, z = its speed (radians per second)
    float4 waterRiver;          // lesson 37: x = bank drag, y = how much slower the sides of the plane are, zw = unit vector ACROSS the flow in the body's own (x, z) space
    float4 waterRiver2;         // x = ripple stretch along the flow, y = rapids (foam in fast shallow water), zw unused
    float4 waterFoam2;          // x = foam bubble scale, y = seconds crest foam lingers, zw unused
    float4 waterWaveDirK[WATER_MAX_WAVES];     // xy = unit direction, z = k (radians per unit), w = amplitude
    float4 waterWaveMotion[WATER_MAX_WAVES];   // x = omega (radians per second), y = steepness Q, z = phase offset
};

// How deep the water is under each point of the body: a map baked on the CPU from the terrain (src/WaterShore.h),
// in the body's own space (u = x * 0.5 + 0.5, v = z * 0.5 + 0.5 for local x, z in -1..1). Value = water level - ground
// height, in world units; <= 0 is dry land. Read with Load + a hand-written bilinear blend so the vertex shader
// needs no sampler, and so the CPU (DepthField::Sample) can reproduce the very same number.
// Four floats per texel: x = depth, y and z = the unit direction TOWARD the nearest land (world x and z) times how sure we are
// of it (0 on a flat bottom), w = unused. Blended with the same bilinear weights as the depth, so a headland where two
// directions oppose cancels to "not sure" (WaterShore::DepthField::SampleToward is the CPU twin).
Texture2D<float4> waterDepthMap : register(t15);

float4 SampleWaterShore(float2 local)
{
    uint width, height;
    waterDepthMap.GetDimensions(width, height);
    const float2 size = float2(width, height);
    const float2 p = saturate(local * 0.5f + 0.5f) * size - 0.5f;   // texel centres sit at integer + 0.5
    const float2 base = floor(p);
    const float2 t = p - base;
    const int2 i0 = (int2)base;
    const int2 maxIndex = int2(width, height) - 1;
    const float4 d00 = waterDepthMap.Load(int3(clamp(i0, int2(0, 0), maxIndex), 0));
    const float4 d10 = waterDepthMap.Load(int3(clamp(i0 + int2(1, 0), int2(0, 0), maxIndex), 0));
    const float4 d01 = waterDepthMap.Load(int3(clamp(i0 + int2(0, 1), int2(0, 0), maxIndex), 0));
    const float4 d11 = waterDepthMap.Load(int3(clamp(i0 + int2(1, 1), int2(0, 0), maxIndex), 0));
    return lerp(lerp(d00, d10, t.x), lerp(d01, d11, t.x), t.y);
}

float SampleWaterDepth(float2 local)
{
    return SampleWaterShore(local).x;   // (the compiler drops the channels this does not use)
}

// The depth under a point of the plane, or "very deep" when this body has no map (no terrain under it).
float WaterDepthAt(float2 local)
{
    return waterShore.x > 0.5f ? SampleWaterDepth(local) : 1000.0f;
}

// Depth and the way to the land, together (x = depth, yz = toward land times how sure). No map: very deep, no direction.
float3 WaterShoreAt(float2 local)
{
    return waterShore.x > 0.5f ? SampleWaterShore(local).xyz : float3(1000.0f, 0.0f, 0.0f);
}

#endif
