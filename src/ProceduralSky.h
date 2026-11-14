#pragma once
#include "SkyModel.h"
#include "Shader.h"
#include <d3d11.h>
#include <chrono>
#include <memory>

// The GPU side of a procedural sky (SkyModel.h has the colours, shaders/sky_procedural.hlsli draws them).
//
// The background is computed per pixel from the sky's constants, every frame, at full resolution: the sun and moon are exact. But
// the lighting that the sky casts on the scene (IBL) and the water's mirror of the sky need a CUBE MAP, so the same sky is also
// rendered into a small one (256 x 256 per face, with mips), a few times a second, only while it is changing. The scene bakes its
// lighting from that cube whenever Update says it was redrawn.
class ProceduralSky {
public:
    static constexpr UINT kSize = 256;

    ProceduralSky() = default;
    ~ProceduralSky() { Release(); }
    ProceduralSky(const ProceduralSky&) = delete;
    ProceduralSky& operator=(const ProceduralSky&) = delete;

    bool Initialize(ID3D11Device* device);
    void Release();
    bool IsReady() const { return cubeSrv_ && paramsBuffer_ && bakeBuffer_; }

    // Call once per frame OUTSIDE any render pass (the same place the lighting is baked). Uploads `params` for the background, and
    // redraws the cube if the sky changed enough, and not more often than every `minSecondsBetweenRedraws` (the lighting bake that
    // follows each redraw is the costly part, so this is the knob for the cost of a running day). True when the cube was
    // redrawn: the caller must bake the sky's lighting again.
    bool Update(ID3D11Device* device, ID3D11DeviceContext* ctx, const SkyModel::Params& params, float minSecondsBetweenRedraws);

    ID3D11ShaderResourceView* GetCubemapView() const { return cubeSrv_; }
    ID3D11Buffer* GetParamsBuffer() const { return paramsBuffer_; }   // bound at b6 while the sky is drawn (Skybox::Render)

private:
    void Upload(ID3D11DeviceContext* ctx, const SkyModel::Params& params);
    bool Redraw(ID3D11Device* device, ID3D11DeviceContext* ctx);

    std::shared_ptr<Shader> shader_;         // kept: ShaderCache holds only weak references, and a dropped shader is recompiled on the next Get
    ID3D11Texture2D* cube_ = nullptr;
    ID3D11ShaderResourceView* cubeSrv_ = nullptr;
    ID3D11Buffer* paramsBuffer_ = nullptr;   // SkyModel::Params (cbuffer SkyParams, b6)
    ID3D11Buffer* bakeBuffer_ = nullptr;     // which face is being drawn (cbuffer BakeParams, b5)
    SkyModel::Params drawn_{};               // what the cube currently shows
    bool hasDrawn_ = false;
    bool attempted_ = false;                 // a redraw was tried (it may have failed): do not retry it every frame
    bool reportedFailure_ = false;
    std::chrono::steady_clock::time_point lastRedraw_{};
};
