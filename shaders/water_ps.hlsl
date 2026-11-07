#include "waves.hlsli"
#include "noise.hlsli"

TextureCube  environmentMap : register(t1);   // the skybox cubemap, bound once per frame by Scene
Texture2D<float4> planarReflection : register(t3);   // the scene seen from below the water (when reflectionParams.x = 1)
Texture2D<float> sceneDepth : register(t4);   // the camera's depth buffer, bound for the water pass only
Texture2D<float4> sceneColor : register(t14); // a copy of the opaque scene, for refraction (when reflectionParams.y = 1)
SamplerState linearSampler  : register(s0);
SamplerState clampSampler   : register(s2);   // edges clamp: screen-space lookups must not wrap

struct PS_IN {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 rest     : TEXCOORD1;   // xy = rest position of this surface point, z = wave height / tallest wave
};

// Hardware depth (0..1, spread non-linearly) -> distance in front of the camera, in world units.
// This is the inverse of the perspective projection's depth mapping for D3D (z in 0..1).
float LinearizeDepth(float d)
{
    const float n = screenParams.z;   // near plane
    const float f = screenParams.w;   // far plane
    return n * f / (f - d * (f - n));
}

// Small ripples on top of the big waves: two layers of noise slope, drifting with the wind. Returns the slope.
float2 RippleSlope(float2 xz, float time)
{
    const float2 wind = waterWind.xy;
    const float2 side = float2(-wind.y, wind.x);
    const float scale = waterWaveInfo.z;
    const float3 n1 = ValueNoiseD(xz * scale - wind * (time * 0.35f));
    const float3 n2 = ValueNoiseD(xz * (scale * 2.3f) + side * (time * 0.25f) - wind * (time * 0.2f) + 7.3f);
    return (n1.yz * scale + n2.yz * (scale * 2.3f * 0.4f)) * 0.3f;
}

float4 main(PS_IN input) : SV_Target
{
    const float reflectivity   = waterOptics.x;
    const float glintStrength  = waterOptics.y;
    const float glintSharpness = waterOptics.z;
    const float mirrorWobble   = waterOptics.w;
    const float3 deepColor    = waterDeepColor.rgb;
    const float3 shallowColor = waterShallowColor.rgb;
    const float baseAlpha     = waterDeepColor.a;
    const float refraction    = waterShallowColor.a;
    const float fadeDistance  = waterDepthParams.x;
    const float foamDistance  = waterDepthParams.y;
    const float foamStrength  = waterDepthParams.z;
    const float absorption    = waterDepthParams.w;
    const float time          = frameParams.x;
    const int   debugView     = (int)waterFoamInfo.w;

    const float3 toEye = cameraPos.xyz - input.worldPos;
    const float distanceToEye = length(toEye);
    const float3 V = toEye / max(distanceToEye, 1e-4f);   // surface -> eye
    const float3 L = normalize(-dirDirection.xyz);        // surface -> sun

    // ---- The surface normal: big waves (exact, from the wave table) + small ripples ----
    // Waves shorter than a few pixels fade out by themselves (that is what keeps the far sea from shimmering).
    const float pixelsPerUnit = (0.5f / screenParams.y) * proj._22 / max(distanceToEye, 0.01f);
    float3 N;
    float jacobian;
    EvaluateWaveShading(input.rest.xy, time, pixelsPerUnit, N, jacobian);

    const float detailStrength = waterWaveInfo.y;
    const float detailFade = 1.0f - smoothstep(0.35f * waterWaveInfo.w, waterWaveInfo.w, distanceToEye);
    [branch]
    if (detailStrength > 0.001f && detailFade > 0.001f) {
        const float2 slope = RippleSlope(input.rest.xy, time) * (detailStrength * detailFade);
        N = normalize(N + float3(-slope.x, 0.0f, -slope.y));
    }

    // ---- How much water lies behind this pixel? ----
    // Same screen pixel in the camera's depth buffer = the nearest SOLID thing along this ray
    // (ground, or the far plane where there is only sky). Subtract the water's own distance.
    const int2 pixel = int2(input.position.xy);
    const float waterZ = LinearizeDepth(input.position.z);
    const float groundZ = LinearizeDepth(sceneDepth.Load(int3(pixel, 0)));
    const float thickness = max(groundZ - waterZ, 0.0f);
    const float2 screenUV = input.position.xy * screenParams.xy;

    // ---- Refraction: what you see THROUGH the water is bent by the waves ----
    float3 behind = float3(0.0f, 0.0f, 0.0f);
    float seenThickness = thickness;   // how much water the (bent) line of sight crosses
    const bool refractionOn = reflectionParams.y > 0.5f && refraction > 0.001f;
    [branch]
    if (refractionOn) {
        const float shoreFade = saturate(thickness / fadeDistance);   // no bending at the shoreline: no seam
        const float2 offset = N.xz * (refraction * shoreFade / (1.0f + waterZ * 0.03f));
        const int2 size = int2(1.0f / screenParams.x + 0.5f, 1.0f / screenParams.y + 0.5f);
        int2 bentPixel = clamp(int2((screenUV + offset) / screenParams.xy), int2(0, 0), size - 1);
        float bentGround = LinearizeDepth(sceneDepth.Load(int3(bentPixel, 0)));
        if (bentGround < waterZ) {          // something stands IN FRONT of the water there: do not drag it into the water
            bentPixel = pixel;
            bentGround = groundZ;
        }
        behind = sceneColor.Load(int3(bentPixel, 0)).rgb;
        seenThickness = max(bentGround - waterZ, 0.0f);
    }

    const float depthFade = saturate(seenThickness / fadeDistance);        // 0 at the shore, 1 in deep water
    const float absorb = 1.0f - exp(-seenThickness * absorption);           // Beer-Lambert: light fades exponentially
    const float3 baseColor = lerp(shallowColor, deepColor, absorb);         // clear and bright -> dark and saturated

    // Schlick Fresnel: at a glancing angle water behaves like a mirror, looking straight
    // down you mostly see the water body. F0 = 0.02 is water's reflectance head-on.
    const float F0 = 0.02f;
    const float fresnel = (F0 + (1.0f - F0) * pow(1.0f - saturate(dot(N, V)), 5.0f)) * reflectivity;

    // Sky reflection. A reflected ray that points below the horizon would read the dark ground half of the cubemap:
    // mirror it up instead. Far away the sea is rough at pixel scale, so read a blurrier level of the sky.
    float3 R = reflect(-V, N);
    R.y = abs(R.y);
    const float skyBlur = saturate(distanceToEye / (2.0f * waterWaveInfo.w)) * 4.0f;
    float3 reflected = environmentMap.SampleLevel(linearSampler, R, skyBlur).rgb;
    [branch]
    if (reflectionParams.x > 0.5f) {
        // Where on screen is this pixel? The reflection image was drawn with the same projection,
        // so the same screen position shows the mirror image of what lies behind the surface.
        float2 mirrorUV = screenUV + N.xz * mirrorWobble;       // wave normals wobble the mirror
        // The mirrored camera renders left-right flipped (a mirror reverses handedness), so flip u back.
        mirrorUV = float2(1.0f - mirrorUV.x, mirrorUV.y);
        reflected = planarReflection.SampleLevel(clampSampler, mirrorUV, 0).rgb;
    }

    // Water body: tinted diffuse (same lighting floor as before).
    const float lighting = 0.35f + saturate(dot(N, L)) * dirDirection.w * 0.65f;
    const float3 lightColor = ambientColor.rgb + lighting * dirColor.rgb;
    const float3 body = baseColor * lightColor;

    // Sun glint: a tight Blinn-Phong highlight on the wave normals.
    const float3 H = normalize(L + V);
    const float3 glint = dirColor.rgb * dirDirection.w * glintStrength * pow(saturate(dot(N, H)), glintSharpness);

    // Light shining THROUGH a wave crest toward the eye (looking toward the sun): the glowing turquoise edge of a wave.
    const float crestHeight = saturate(input.rest.z * 0.5f + 0.5f);   // 0 in a trough, 1 on a crest
    const float3 bent = normalize(L + N * 0.3f);
    const float through = pow(saturate(dot(V, -bent)), 4.0f) * crestHeight * crestHeight * saturate(L.y * 4.0f);
    const float3 scatter = shallowColor * dirColor.rgb * (dirDirection.w * waterFoamInfo.z * through);

    const float3 waterColor = lerp(body, reflected, fresnel) + glint + scatter;

    // Opacity. Shallow water is more see-through; right at the shoreline it fades to nothing so there is
    // no hard line where the plane cuts through the ground. The glint also adds opacity so it never looks washed out.
    const float alphaBody = baseAlpha * lerp(0.35f, 1.0f, depthFade);
    const float edgeFade = saturate(thickness / (0.35f * fadeDistance));
    const float waterAlpha = saturate(lerp(alphaBody, 1.0f, fresnel) + dot(glint, float3(0.333f, 0.333f, 0.333f)));

    // ---- Foam: where the water is shallow (the shore) and on the steepest crests ----
    const float foamBand = (1.0f - saturate(thickness / max(foamDistance, 0.001f))) * foamStrength;
    // Jacobian < 1 means the surface is being squeezed together: that is where real waves break.
    const float crestFoam = saturate(((1.0f - jacobian) - waterFoamInfo.y) * 5.0f) * waterFoamInfo.x;
    float foam = 0.0f;
    [branch]
    if (foamBand > 0.001f || crestFoam > 0.001f) {   // open water without crests skips all the noise
        const float2 foamUV = input.worldPos.xz * 0.9f + float2(time * 0.25f, -time * 0.18f);
        const float noise = ValueNoise(foamUV) * 0.65f + ValueNoise(foamUV * 2.7f + 17.0f) * 0.35f;
        const float crestNoise = ValueNoise(input.rest.xy * 1.6f - waterWind.xy * (time * 0.4f) + 31.0f);
        foam = saturate(foamBand * smoothstep(0.25f, 0.7f, noise + foamBand * 0.35f) +
                        crestFoam * smoothstep(0.35f, 0.75f, crestNoise * 0.6f + noise * 0.4f));
    }
    const float3 foamColor = float3(0.9f, 0.95f, 1.0f) * lightColor;

    // ---- Debug views (editor: Water > Debug view) ----
    [branch]
    if (debugView > 0) {
        float3 debugColor = float3(0.0f, 0.0f, 0.0f);
        if (debugView == 1)      debugColor = N * 0.5f + 0.5f;
        else if (debugView == 2) debugColor = float3(1.0f - depthFade, depthFade, absorb);
        else if (debugView == 3) debugColor = float3(foam, foamBand, crestFoam);
        else if (debugView == 4) debugColor = fresnel.xxx;
        else if (debugView == 5) debugColor = crestHeight.xxx;
        return float4(debugColor, 1.0f);
    }

    // ---- Compose, with PREMULTIPLIED alpha (the blend state is ONE, INV_SRC_ALPHA) ----
    // visible = how much of the water body replaces what is behind it.
    float3 rgb;
    float alpha;
    if (refractionOn) {
        // We hold a bent copy of what is behind the water, so blend with THAT instead of the unbent pixel.
        rgb = lerp(behind, waterColor, waterAlpha) * edgeFade;
        alpha = edgeFade;
    } else {
        alpha = waterAlpha * edgeFade;
        rgb = waterColor * alpha;
    }
    // Foam goes over everything and stays solid even where the water itself is almost invisible.
    rgb = foamColor * foam + rgb * (1.0f - foam);
    alpha = foam + alpha * (1.0f - foam);
    return float4(rgb, alpha);
}
