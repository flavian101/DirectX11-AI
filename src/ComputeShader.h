#pragma once
#include <d3d11.h>
#include <d3dcompiler.h>

// One compute shader (cs_5_0), loaded from a .hlsl file and run with Dispatch. Roadmap O2 (the GPU half): the FFT sea is made by compute passes.
//
// A compute shader has no vertices and no pixels. You give it a grid of GROUPS to run, each group is [numthreads(x, y, z)] threads declared in the
// HLSL, and every thread knows which one it is (SV_DispatchThreadID). It reads from SRVs (t registers) and writes to UAVs (u registers).
//
// Why cs_5_0 and not cs_6_0: Direct3D 11 only runs the old DXBC byte code that D3DCompile makes. Shader model 6 (DXIL) is Direct3D 12 only. DXC at
// cs_6_0 is still how the shaders are CHECKED (docs/ROADMAP.md, rule 5), because every cs_5_0 shader we write is also valid cs_6_0.
class ComputeShader {
public:
    ComputeShader() = default;
    ~ComputeShader();
    ComputeShader(const ComputeShader&) = delete;
    ComputeShader& operator=(const ComputeShader&) = delete;

    // Reads and compiles `path` (entry point `main`). `#include "x.hlsli"` resolves next to the file, like Shader::Load.
    bool Load(const wchar_t* path, ID3D11Device* device);
    bool IsValid() const { return shader_ != nullptr; }

    // Makes this the compute shader the next Dispatch runs.
    void Bind(ID3D11DeviceContext* ctx) const;
    // Runs groupsX * groupsY * groupsZ groups. Bind() first, and bind the SRVs, UAVs and constant buffers.
    void Dispatch(ID3D11DeviceContext* ctx, UINT groupsX, UINT groupsY = 1, UINT groupsZ = 1) const;

    // How many groups cover `items` things when each group handles `groupSize` of them (rounds UP, so a 100-wide image with 8-wide groups needs 13).
    static UINT GroupCount(UINT items, UINT groupSize) { return groupSize == 0 ? 0 : (items + groupSize - 1) / groupSize; }

    // A texture cannot be an input (SRV) and an output (UAV) at the same moment. If the UAV is still bound when the next pass wants to READ that
    // texture, Direct3D silently binds nothing in its place (the debug layer only warns). So every pass ends by unbinding what it used.
    static void UnbindCompute(ID3D11DeviceContext* ctx, UINT srvCount, UINT uavCount);

private:
    ID3D11ComputeShader* shader_ = nullptr;
    void Release();
};
