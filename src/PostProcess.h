#pragma once
#include "Shader.h"
#include <d3d11.h>
#include <memory>

// Full-screen passes that run after the scene has been drawn. Right now: "resolve" the HDR
// scene buffer into the back buffer.
class PostProcess {
public:
    PostProcess() = default;
    ~PostProcess();
    PostProcess(const PostProcess&) = delete;
    PostProcess& operator=(const PostProcess&) = delete;

    bool Initialize(ID3D11Device* device);
    void Release();
    bool IsValid() const { return shader_ && rasterState_; }

    // Reads hdrScene, writes the displayable image into target. Leaves target bound as the
    // only render target (so the UI can draw on top) and unbinds hdrScene from the pixel stage.
    void Resolve(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* hdrScene,
                 ID3D11RenderTargetView* target, UINT width, UINT height) const;

private:
    std::shared_ptr<Shader> shader_;
    ID3D11RasterizerState* rasterState_ = nullptr;
};
