#include "waves.hlsli"
#include "noise.hlsli"

TextureCube  environmentMap : register(t1);   // the skybox cubemap, bound once per frame by Scene
TextureCube  skyIrradiance  : register(t11);  // the sky's baked diffuse light (EnvironmentLighting), bound once per frame by Scene
Texture2D<float4> planarReflection : register(t3);   // the scene seen from below the water (when waterShape.x = 1: set per water body)
Texture2D<float> sceneDepth : register(t4);   // the camera's depth buffer, bound for the water pass only
Texture2D<float4> sceneColor : register(t14); // a copy of the opaque scene, for refraction (when reflectionParams.y = 1)
SamplerState linearSampler  : register(s0);
SamplerState clampSampler   : register(s2);   // edges clamp: screen-space lookups must not wrap

struct PS_IN {
    float4 position : SV_POSITION;
    float3 worldPos : TEXCOORD0;
    float3 rest     : TEXCOORD1;   // xy = rest position of this surface point, z = wave height / tallest wave
    float2 local    : TEXCOORD2;   // position on the plane itself, -1..1 (the plane is square in its own space)
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
// On a river the same two layers ride the current instead: `drift` speeds the downstream motion up to the flow speed and
// `sideways` switches the sideways wander off (a current has no sideways). For every other body both are 1: unchanged.
float2 RippleSlope(float2 xz, float time)
{
    const float2 wind = waterWind.xy;
    const float2 side = float2(-wind.y, wind.x);
    const float scale = waterWaveInfo.z;
    const float drift = waterFlow.x;
    const float sideways = waterFlow.y;
    const float3 n1 = ValueNoiseD(xz * scale - wind * (time * 0.35f * drift));
    const float3 n2 = ValueNoiseD(xz * (scale * 2.3f) + side * (time * 0.25f * sideways) - wind * (time * 0.2f * drift) + 7.3f);
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

    // A round body is the square plane cut to its inscribed ellipse: throw away what lies outside it, and fade the
    // last few centimetres so the rim is not a hard stair-stepped line.
    float shapeFade = 1.0f;
    [branch]
    if (waterShape.y > 0.5f) {
        const float inside = 1.0f - length(input.local);
        clip(inside);
        shapeFade = saturate(inside / max(waterShape.z, 1e-4f));
    }

    const float3 toEye = cameraPos.xyz - input.worldPos;
    const float distanceToEye = length(toEye);
    const float3 V = toEye / max(distanceToEye, 1e-4f);   // surface -> eye
    const float3 L = normalize(-dirDirection.xyz);        // surface -> sun

    // ---- The surface normal: big waves (exact, from the wave table) + small ripples ----
    // Waves shorter than a few pixels fade out by themselves (that is what keeps the far sea from shimmering).
    const float pixelsPerUnit = (0.5f / screenParams.y) * proj._22 / max(distanceToEye, 0.01f);
    float3 N;
    float jacobian;
    const float3 shore = WaterShoreAt(input.local);     // depth and the way to the land under this pixel (1000 and nothing when the body has no map)
    const float mapDepth = shore.x;                     // vertical depth under this pixel
    EvaluateWaveShading(input.rest.xy, time, pixelsPerUnit, shore, N, jacobian);

    const float detailStrength = waterWaveInfo.y;
    const float detailFade = 1.0f - smoothstep(0.35f * waterWaveInfo.w, waterWaveInfo.w, distanceToEye);
    [branch]
    if (detailStrength > 0.001f && detailFade > 0.001f) {
        const float2 slope = RippleSlope(input.rest.xy, time) * (detailStrength * detailFade);
        N = normalize(N + float3(-slope.x, 0.0f, -slope.y));
    }

    // ---- Seen from BELOW (the camera is under the surface) ----
    // Looking up from under water you see the sky only through a round "window" straight above you: light from the sky bends
    // toward the vertical as it enters the water, so the whole sky is squeezed into a cone of about 97 degrees. Outside the
    // window the surface is a mirror (total internal reflection) of the water around you. Both are approximated here: the
    // window shows the refracted sky, the rest shows the water's own colour.
    [branch]
    if (cameraPos.y < input.worldPos.y) {
        const float3 downNormal = -N;                               // the surface's normal, facing the viewer below
        const float cosI = saturate(dot(V, downNormal));
        const float sinT = sqrt(saturate(1.0f - cosI * cosI)) * 1.33f;   // Snell: sin(out) = 1.33 * sin(in); >= 1 means no way out
        const float window = 1.0f - smoothstep(0.85f, 1.0f, sinT);
        const float3 bent = refract(-V, downNormal, 1.33f);
        const float3 skyDirection = dot(bent, bent) > 1e-4f ? normalize(bent) : float3(0.0f, 1.0f, 0.0f);
        const float3 skySeen = environmentMap.SampleLevel(linearSampler, SkyDirection(skyDirection), 1.0f).rgb * skyTint.rgb * skyTint.w;
        const float3 mirror = fogColor.rgb * 0.9f;
        const float3 underColor = lerp(mirror, skySeen, window);
        const float underAlpha = lerp(1.0f, 0.9f, window);   // the window lets a little of what is above the water through
        float3 underFog;
        const float underAmount = FogAt(input.worldPos, underFog);
        return float4(underColor * underAlpha * (1.0f - underAmount) + underFog * (underAmount * underAlpha), underAlpha);
    }

    // ---- How much water lies behind this pixel? ----
    // Same screen pixel in the camera's depth buffer = the nearest SOLID thing along this ray
    // (ground, or the far plane where there is only sky). Subtract the water's own distance.
    const int2 pixel = int2(input.position.xy);
    const float waterZ = LinearizeDepth(input.position.z);
    const float groundZ = LinearizeDepth(sceneDepth.Load(int3(pixel, 0)));
    const float thickness = max(groundZ - waterZ, 0.0f);
    const float2 screenUV = input.position.xy * screenParams.xy;

    // ---- How deep is the water HERE, measured straight down? ----
    // `thickness` above is the difference of two eye-space depths: it depends on where the camera looks, so a shore
    // band built from it changes width as you tilt the camera. When the body has a depth map, the vertical depth
    // from the terrain is used instead; `thickness` still counts where something other than the terrain (a rock, a
    // hull) touches the water, because the smaller of the two wins.
    // The waterline also slides with the waves: the surface itself is `waveHeight` higher or lower here, and near the
    // shore, where the waves are squeezed to nothing, `swashLift` stands in for the run-up of the first wave.
    float shoreDepth = thickness;
    float breaking = 0.0f;
    [branch]
    if (waterShore.x > 0.5f) {
        const float waveHeight = input.rest.z * waterMeshInfo.z;
        const float phase0 = waterWaveDirK[0].z * dot(waterWaveDirK[0].xy, input.rest.xy) + waterWaveMotion[0].x * time + waterWaveMotion[0].z;
        const float swashLift = waterShore.z * waterWaveDirK[0].w * sin(phase0);
        shoreDepth = max(min(mapDepth, thickness) + waveHeight + swashLift, 0.0f);
        // A crest that is tall for the water under it is about to break: wave height / depth, up to ~0.4 where it is limited.
        breaking = saturate((waveHeight / max(mapDepth, 0.05f) - 0.18f) * 8.0f) * step(0.0f, mapDepth);
    }

    // ---- Refraction: what you see THROUGH the water is bent by the waves ----
    float3 behind = float3(0.0f, 0.0f, 0.0f);
    float seenThickness = thickness;   // how much water the (bent) line of sight crosses
    const bool refractionOn = reflectionParams.y > 0.5f && refraction > 0.001f;
    [branch]
    if (refractionOn) {
        const float shoreFade = saturate(shoreDepth / fadeDistance);   // no bending at the shoreline: no seam
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
    // The sky may be turned, tinted and dimmed (Skybox settings): the mirror of it follows.
    float3 reflected = environmentMap.SampleLevel(linearSampler, SkyDirection(R), skyBlur).rgb * skyTint.rgb * skyTint.w;
    [branch]
    if (waterShape.x > 0.5f) {   // THIS body has a mirror image (other bodies in the same frame may not)
        // Where on screen is this pixel? The reflection image was drawn with the same projection,
        // so the same screen position shows the mirror image of what lies behind the surface.
        float2 mirrorUV = screenUV + N.xz * mirrorWobble;       // wave normals wobble the mirror
        // The mirrored camera renders left-right flipped (a mirror reverses handedness), so flip u back.
        mirrorUV = float2(1.0f - mirrorUV.x, mirrorUV.y);
        reflected = planarReflection.SampleLevel(clampSampler, mirrorUV, 0).rgb;
    }

    // Water body: tinted diffuse (same lighting floor as before).
    const float lighting = 0.35f + saturate(dot(N, L)) * dirDirection.w * 0.65f;
    // The light the SKY gives the water body: with sky lighting on, the baked diffuse light looking straight up
    // (so the sky sets it, not a separate flat colour); with it off, the old flat ambient colour.
    float3 skyLight = ambientColor.rgb;
    [branch]
    if (iblParams.x > 0.5f) skyLight = skyIrradiance.SampleLevel(linearSampler, SkyDirection(float3(0.0f, 1.0f, 0.0f)), 0.0f).rgb * iblParams.y * skyTint.rgb;
    const float3 lightColor = skyLight + lighting * dirColor.rgb;
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
    const float edgeFade = saturate(shoreDepth / (0.35f * fadeDistance)) * shapeFade;
    const float waterAlpha = saturate(lerp(alphaBody, 1.0f, fresnel) + dot(glint, float3(0.333f, 0.333f, 0.333f)));

    // ---- Foam: where the water is shallow (the shore) and on the steepest crests ----
    const float foamBand = (1.0f - saturate(shoreDepth / max(foamDistance, 0.001f))) * foamStrength;
    const float breakFoam = breaking * foamStrength * waterShore.y;   // surf: waves tripping over the bottom
    // Jacobian < 1 means the surface is being squeezed together: that is where real waves break.
    const float crestFoam = saturate(((1.0f - jacobian) - waterFoamInfo.y) * 5.0f) * waterFoamInfo.x;
    float foam = 0.0f;
    [branch]
    if (foamBand > 0.001f || crestFoam > 0.001f || breakFoam > 0.001f) {   // open water without crests skips all the noise
        const float2 foamUV = input.worldPos.xz * 0.9f + float2(time * 0.25f, -time * 0.18f);
        const float noise = ValueNoise(foamUV) * 0.65f + ValueNoise(foamUV * 2.7f + 17.0f) * 0.35f;
        const float crestNoise = ValueNoise(input.rest.xy * 1.6f - waterWind.xy * (time * 0.4f) + 31.0f);
        foam = saturate(foamBand * smoothstep(0.25f, 0.7f, noise + foamBand * 0.35f) +
                        crestFoam * smoothstep(0.35f, 0.75f, crestNoise * 0.6f + noise * 0.4f) +
                        breakFoam * smoothstep(0.3f, 0.7f, crestNoise * 0.5f + noise * 0.5f));
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
        else if (debugView == 6) debugColor = waterShore.x > 0.5f ? float3(step(mapDepth, 0.0f), saturate(mapDepth / 8.0f), breaking)
                                                                  : float3(1.0f, 0.0f, 1.0f);   // magenta = this body has no depth map
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
    foam *= shapeFade;
    rgb = foamColor * foam + rgb * (1.0f - foam);
    alpha = foam + alpha * (1.0f - foam);
    // Fog. The colour is premultiplied by alpha, so the fog that replaces it is too: a see-through pixel stays see-through.
    float3 fogHere;
    const float fogAmount = FogAt(input.worldPos, fogHere);
    rgb = rgb * (1.0f - fogAmount) + fogHere * (fogAmount * alpha);
    return float4(rgb, alpha);
}
