// Turns the HDR scene buffer into the image that goes on screen.
// For now it is a straight copy; the next patches add gamma encoding and tone mapping here.
Texture2D<float4> sceneColor : register(t0);

float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
    // Load() reads one exact texel (no filtering): the HDR buffer and the screen are the same
    // size, so pixel (x,y) on screen is texel (x,y) in the buffer.
    const float3 hdr = sceneColor.Load(int3(int2(position.xy), 0)).rgb;
    return float4(hdr, 1.0f);
}
