// The water's own constant block (b4). Declared HERE, by the shaders that use it, instead of
// squeezing water numbers into the generic per-object buffer. CPU side: WaterMaterialCB in Water.cpp.
// Every field is explained there too: change one place, change the other.
#ifndef WATER_MATERIAL_HLSLI
#define WATER_MATERIAL_HLSLI

#define WATER_MAX_WAVES 8

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
    float4 waterWaveDirK[WATER_MAX_WAVES];     // xy = unit direction, z = k (radians per unit), w = amplitude
    float4 waterWaveMotion[WATER_MAX_WAVES];   // x = omega (radians per second), y = steepness Q, z = phase offset
};

#endif
