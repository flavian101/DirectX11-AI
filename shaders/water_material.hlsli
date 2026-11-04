// The water's own constant block (b4). Declared HERE, by the shaders that use it, instead of
// squeezing water numbers into the generic per-object buffer. CPU side: WaterMaterialCB in Water.cpp.
#ifndef WATER_MATERIAL_HLSLI
#define WATER_MATERIAL_HLSLI

cbuffer WaterMaterial : register(b4) {
    float4 waterDeepColor;      // rgb = deep-water colour (linear), a = opacity of deep water
    float4 waterShallowColor;   // rgb = shallow-water colour (linear)
    float4 waveParams;          // x = amplitude, y = speed, z = frequency, w = reflectivity
    float4 waterLighting;       // x = glint strength, y = glint sharpness, z = mirror distortion
    float4 waterDepthParams;    // x = fade distance, y = foam distance, z = foam strength, w = absorption
};

#endif
