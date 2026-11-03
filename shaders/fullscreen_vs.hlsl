// One triangle that covers the whole screen, generated from SV_VertexID: no vertex buffer,
// no input layout. Vertices 0,1,2 land at NDC (-1,1), (3,1), (-1,-3); the parts hanging off
// the screen are clipped away, leaving exactly the screen rectangle.
struct VS_OUT { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };

VS_OUT main(uint id : SV_VertexID)
{
    VS_OUT output;
    output.uv = float2((id << 1) & 2, id & 2);                              // (0,0) (2,0) (0,2)
    output.position = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}
