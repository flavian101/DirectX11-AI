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

// ---- A river's current (lesson 37) ----
// Same numbers as src/WaterFlow.h: keep them in step.
static const float kBankDepth = 1.5f;     // WaterFlow::kBankDepth: shallower than this the bed drags the water
static const float kRiverCycle = 2.4f;    // WaterFlow::kPhaseCycle: seconds one layer of ripples lives
static const float kFoamBubbleScale = 2.6f;   // the bubbles are this many times finer than the clumps

// How fast the current runs HERE, as a fraction of the river's flow speed. It is not one speed across the whole river: the
// water drags on the bed and the banks, so it is fastest in the deep middle and creeps along at the edges. `local` is the
// position on the plane (-1..1); the shallow bed comes from the depth map.
float RiverSpeedFactor(float2 local, float mapDepth)
{
    const float across = clamp(dot(local, waterRiver.zw), -1.0f, 1.0f);
    const float middle = 1.0f - across * across;                         // 1 in the middle of the plane, 0 at its sides
    const float shallow = saturate(mapDepth / kBankDepth);               // 0 at a bank, 1 in deep water
    const float byDepth = lerp(1.0f - 0.92f * waterRiver.x, 1.0f, smoothstep(0.0f, 1.0f, shallow));
    const float byWidth = lerp(1.0f - 0.8f * waterRiver.y, 1.0f, middle);
    return max(byDepth * byWidth, 0.08f);
}

// Where one layer of a flowing pattern is in its life, and how visible it is. Two layers live half a cycle apart: one fades in
// while the other fades out, and each is reset to its start while it is invisible. So the pattern is carried downstream by the
// water, never slides forever (which is what a plain scrolling texture looks like), and the speed may differ from place to place
// (the current is faster in the middle) without the pattern tearing: this is the "flow map" technique.
void FlowLayer(float time, int layer, out float travelFraction, out float weight)
{
    travelFraction = frac(time / kRiverCycle + 0.5f * layer);
    weight = 1.0f - abs(2.0f * travelFraction - 1.0f);   // 0 at the reset, 1 half way through
}

// Ripples on a river: the same two noise layers as RippleSlope, but carried downstream at the LOCAL speed, stretched along the
// flow (water streams, it does not make round blobs), and evolving instead of sliding.
float2 RiverRippleSlope(float2 xz, float time, float2 flowDir, float speed)
{
    const float2 side = float2(-flowDir.y, flowDir.x);
    const float scale = waterWaveInfo.z;
    const float stretch = max(waterRiver2.x, 1.0f);
    float2 slope = float2(0.0f, 0.0f);
    float weightSquares = 0.0f;
    [unroll]
    for (int layer = 0; layer < 2; ++layer) {
        float fraction, weight;
        FlowLayer(time, layer, fraction, weight);
        const float travel = speed * kRiverCycle * fraction;                  // how far the water has carried this layer
        const float2 q = float2(dot(xz, side), (dot(xz, flowDir) - travel) / stretch) * scale + layer * 17.3f;
        const float3 n1 = ValueNoiseD(q);
        const float3 n2 = ValueNoiseD(q * 2.3f + 7.3f);
        const float2 g = n1.yz * scale + n2.yz * (scale * 2.3f * 0.4f);       // slope in the (across, along / stretch) frame
        slope += weight * (side * g.x + flowDir * (g.y / stretch));
        weightSquares += weight * weight;
    }
    // Blending two unrelated layers lowers the contrast half way through a cycle (the weights are 0.5 and 0.5): divide by the
    // spread the blend has, so the ripples are equally strong all the time.
    return slope * (0.3f / sqrt(max(weightSquares, 1e-4f)));
}

// ---- Foam (lesson 37) ----
// The foam pattern, 0..1 (about 0.5 on average). The caller turns it into foam by keeping only the part ABOVE a threshold that
// falls as the foam gets stronger: weak foam is a few lacy islands of bubbles, medium foam is a clumpy sheet with gaps, strong
// foam is nearly solid with a fine web. (Tuned on the CPU first: the bubbles ALONE look like confetti, so they are only the
// texture on top of organic clumps.)
float FoamTexture(float2 p, float time)
{
    // A gentle warp, so the clumps are organic rather than grid-shaped.
    const float2 warp = float2(ValueNoise(p * 0.7f + 3.1f), ValueNoise(p * 0.7f + 8.7f)) - 0.5f;
    const float2 q = p + warp;
    // Clumps: smooth noise of three sizes.
    const float clumps = ValueNoise(q * 0.5f) * 0.5f + ValueNoise(q * 1.1f + 4.0f) * 0.3f + ValueNoise(q * 2.3f + 9.0f) * 0.2f;
    // Bubbles: round in the middle of a cell (the speckle), thin walls between neighbours (the lace).
    const float2 v = Voronoi(q * kFoamBubbleScale + float2(time * 0.05f, 0.0f));
    const float bubble = 1.0f - saturate(v.x * 1.3f);
    const float lace = saturate((v.y - v.x) * 3.0f);
    return saturate(clumps * (0.6f + 0.55f * bubble) * (0.8f + 0.2f * lace) * 1.3f);
}

// The same on a river: stretched along the flow and carried by the local current (two layers, as for the ripples).
float RiverFoamTexture(float2 xz, float freq, float time, float2 flowDir, float speed)
{
    const float2 side = float2(-flowDir.y, flowDir.x);
    const float stretch = 1.0f + 0.5f * (max(waterRiver2.x, 1.0f) - 1.0f);
    float sum = 0.0f, weightSquares = 0.0f;
    [unroll]
    for (int layer = 0; layer < 2; ++layer) {
        float fraction, weight;
        FlowLayer(time, layer, fraction, weight);
        const float travel = speed * kRiverCycle * fraction;
        const float2 p = float2(dot(xz, side), (dot(xz, flowDir) - travel) / stretch) * freq + layer * 29.1f;
        sum += weight * FoamTexture(p, time);
        weightSquares += weight * weight;
    }
    const float mean = 0.5f;
    return saturate(mean + (sum - mean) / sqrt(max(weightSquares, 1e-4f)));   // restore the contrast the blend loses
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

    // A river (or a Custom body with a current): how fast the water runs at THIS pixel, and which way.
    const bool river = waterFlow.w > 0.001f;
    const float2 flowDir = waterWind.xy;
    float speedFactor = 1.0f;
    [branch]
    if (river) speedFactor = RiverSpeedFactor(input.local, mapDepth);
    const float localSpeed = waterFlow.w * speedFactor;

    const float detailStrength = waterWaveInfo.y;
    const float detailFade = 1.0f - smoothstep(0.35f * waterWaveInfo.w, waterWaveInfo.w, distanceToEye);
    [branch]
    if (detailStrength > 0.001f && detailFade > 0.001f) {
        float2 slope;
        [branch]
        if (river) slope = RiverRippleSlope(input.rest.xy, time, flowDir, localSpeed);
        else       slope = RippleSlope(input.rest.xy, time);
        slope *= detailStrength * detailFade;
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
        const float3 mirror = fogColor.rgb * fogExtra.w;   // underwater, fogExtra.w is the surface mirror's brightness (Underwater settings)
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

    // ---- Foam (lesson 37) ----
    // Three sources, each an AMOUNT from 0 to 1 (how much of the surface it covers), then ONE bubble pattern that all three share:
    //  * the shore: a band where the water is shallow (wider and stronger in the fast shallows of a river),
    //  * surf: waves tripping over the bottom,
    //  * crests: where the surface is squeezed together (Jacobian below 1), PLUS the trail the crest leaves. Because the waves
    //    are written in rest positions, "was this parcel of water squeezed 0.5 s ago?" is just the Jacobian at an earlier time.
    const float rapids = river ? waterRiver2.y * speedFactor : 0.0f;
    const float reach = foamDistance * (1.0f + 2.0f * rapids);
    const float shoreAmount = pow(1.0f - saturate(shoreDepth / max(reach, 0.001f)), 1.3f) * foamStrength;
    const float breakAmount = breaking * foamStrength * waterShore.y;
    // Jacobian < 1 means the surface is being squeezed together: that is where real waves break.
    float crestAmount = saturate(((1.0f - jacobian) - waterFoamInfo.y) * 5.0f) * waterFoamInfo.x;
    const float trailSeconds = waterFoam2.y;
    [branch]
    // Up close only (far away it is too small to see), and only where the surface is being squeezed at least a little right now:
    // a parcel that folded a moment ago is still tightened (the squeeze changes smoothly), a surface that is stretching cannot
    // be carrying a fresh trail. That skips about half of the pixels, and each skipped pixel saves three passes over the waves.
    if (waterFoamInfo.x > 0.001f && trailSeconds > 0.01f && distanceToEye < 0.7f * waterWaveInfo.w &&
        (1.0f - jacobian) > 0.2f * waterFoamInfo.y) {
        [unroll]
        for (int k = 1; k <= 3; ++k) {
            const float earlier = time - trailSeconds * (k / 3.0f);
            const float fold = WaveFold(input.rest.xy, earlier, pixelsPerUnit, shore);
            const float fading = exp(-1.9f * k / 3.0f);   // WaterFlow::TrailWeight: the older the foam, the thinner
            crestAmount = max(crestAmount, saturate(((1.0f - fold) - waterFoamInfo.y) * 5.0f) * waterFoamInfo.x * fading);
        }
    }
    // Any of the three is enough to make foam: combine them as chances (1 - none of them).
    const float amount = 1.0f - (1.0f - shoreAmount) * (1.0f - breakAmount) * (1.0f - crestAmount);

    float foam = 0.0f;
    float thick = 0.0f;   // how thick the foam is here: thin foam is see-through and bluish, thick foam is white
    [branch]
    if (amount > 0.01f) {   // open water without foam skips all the noise
        const float freq = 1.1f * waterFoam2.x;                                      // bubbles per world unit
        // The pattern is drawn on the water's REST position, so it travels with the water instead of sliding over it. A river
        // carries it downstream at the local speed.
        float tex;
        [branch]
        if (river) tex = RiverFoamTexture(input.rest.xy, freq, time, flowDir, localSpeed);
        else       tex = FoamTexture(input.rest.xy * freq, time);
        // Keep only the part of the pattern above a threshold that falls as the amount rises: the fringe of weak foam is a
        // few lacy islands (thin), the middle of strong foam is a sheet (thick).
        thick = saturate((tex - (1.0f - amount * 1.15f)) / 0.35f);
        // Bubbles smaller than about two pixels cannot be drawn (they shimmer): far away, fade to an even cover instead.
        const float detail = saturate((pixelsPerUnit / (freq * kFoamBubbleScale) - 1.5f) * 0.5f);
        thick = lerp(saturate(amount * 0.7f), thick, detail);
        foam = saturate(thick * 1.35f);
    }
    const float3 foamColor = lerp(float3(0.60f, 0.72f, 0.78f), float3(0.96f, 0.98f, 1.0f), thick) * lightColor;
    const float crestFoam = crestAmount;   // (the names the debug view uses)
    const float foamBand = shoreAmount;

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
