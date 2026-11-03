// Turns the HDR scene buffer into the image that goes on screen. Three steps, in this order:
//   1. exposure     - scale the light up or down (like a camera's shutter/ISO)      lesson 7
//   2. tone mapping - squeeze unlimited brightness into 0..1 without clipping       lesson 7
//   3. sRGB encode  - linear light -> the curve monitors expect                     lesson 6
// Order matters: tone mapping works on LINEAR light, so encoding must come last.
Texture2D<float4> sceneColor : register(t0);

// b3 - CPU: PostProcess (CBPost)
cbuffer Post : register(b3) {
    float4 postParams;   // x = sRGB encode (1/0), y = exposure multiplier, z = tone mapper id
};

// ---- tone mapping operators: all take and return LINEAR colour ----

// Reinhard: x / (1 + x). Never exceeds 1, but flattens contrast.
float3 TonemapReinhard(float3 x) { return x / (1.0f + x); }

// ACES filmic curve, Krzysztof Narkowicz's fit: an S-shape (crushed toe, soft shoulder).
float3 TonemapAces(float3 x)
{
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// "Uncharted 2" filmic curve (John Hable). The partial function is the curve itself; we
// divide by its value at a chosen "white point" so that white maps to exactly 1.
float3 HablePartial(float3 x)
{
    const float A = 0.15f, B = 0.50f, C = 0.10f, D = 0.20f, E = 0.02f, F = 0.30f;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}
float3 TonemapFilmic(float3 x)
{
    const float whitePoint = 11.2f;
    const float exposureBias = 2.0f;   // the curve's authors pre-brighten the input by this much
    return saturate(HablePartial(x * exposureBias) / HablePartial(float3(whitePoint, whitePoint, whitePoint)));
}

// The exact sRGB transfer curve: a short straight segment near black, a 1/2.4 power above.
float3 LinearToSrgb(float3 c)
{
    c = saturate(c);
    const float3 low = c * 12.92f;
    const float3 high = 1.055f * pow(c, 1.0f / 2.4f) - 0.055f;
    return lerp(high, low, step(c, 0.0031308f));   // step(a,b) = 1 when b >= a: picks `low` for dark values
}

float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
    // Load() reads one exact texel (no filtering): the HDR buffer and the screen are the same size.
    float3 color = sceneColor.Load(int3(int2(position.xy), 0)).rgb;

    color *= postParams.y;                      // 1. exposure

    const int mode = (int)(postParams.z + 0.5f);   // 2. tone mapping
    if (mode == 1)      color = TonemapReinhard(color);
    else if (mode == 2) color = TonemapAces(color);
    else if (mode == 3) color = TonemapFilmic(color);
    else                color = saturate(color);   // Clamp: values above 1.0 simply turn white

    if (postParams.x > 0.5f) color = LinearToSrgb(color);   // 3. encode for the monitor
    return float4(color, 1.0f);
}
