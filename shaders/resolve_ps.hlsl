// Turns the HDR scene buffer into the image that goes on screen.
// The buffer holds LINEAR light. A monitor expects values on the sRGB curve, so the last
// step is to encode. (Lesson 6. Tone mapping joins this shader in lesson 7.)
Texture2D<float4> sceneColor : register(t0);

// b3 - CPU: PostProcess (CBPost)
cbuffer Post : register(b3) {
    float4 postParams;   // x = encode to sRGB (1 = yes, 0 = leave linear, for comparison)
};

// The exact sRGB transfer curve: a short straight segment near black, a 1/2.4 power above.
float3 LinearToSrgb(float3 c)
{
    c = saturate(c);
    const float3 low = c * 12.92f;
    const float3 high = 1.055f * pow(c, 1.0f / 2.4f) - 0.055f;
    return lerp(high, low, step(c, 0.0031308f));   // step(a,b) = 1 when b >= a, so this picks `low` for dark values
}

float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
    // Load() reads one exact texel (no filtering): the HDR buffer and the screen are the same size.
    float3 color = sceneColor.Load(int3(int2(position.xy), 0)).rgb;

    // The back buffer is 8-bit and cannot hold values above 1.0: they simply clip to white.
    // (That is the problem lesson 7 solves.)
    color = saturate(color);

    if (postParams.x > 0.5f) color = LinearToSrgb(color);
    return float4(color, 1.0f);
}
