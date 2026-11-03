#pragma once
#include "DepthTarget.h"
#include "Shader.h"
#include <DirectXMath.h>
#include <d3d11.h>
#include <memory>

struct ShadowSettings {
    bool enabled = true;
    int resolution = 2048;     // the map is resolution x resolution texels
    int depthBias = 1500;      // constant bias, in depth-buffer units (see lesson 2)
    float slopeBias = 2.0f;    // extra bias that grows with the surface's angle to the light

    static constexpr int kResolutionMin = 512, kResolutionMax = 4096;
    static constexpr int kDepthBiasMax = 20000;
    static constexpr float kSlopeBiasMax = 8.0f;

    void Clamp();
};

// Owns everything the sun's depth pass needs: the depth texture, the depth-only shader, the
// rasterizer state with bias, and the light's view/projection matrices.
class ShadowMap {
public:
    ShadowMap() = default;
    ~ShadowMap();
    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    bool Initialize(ID3D11Device* device, const ShadowSettings& settings);
    void Release();
    bool IsValid() const { return target_.IsValid() && shader_ && rasterState_; }
    bool IsEnabled() const { return settings_.enabled && IsValid(); }

    const ShadowSettings& GetSettings() const { return settings_; }
    void SetSettings(const ShadowSettings& settings);   // rebuilds only what actually changed

    // One shadow pass = Begin(...), draw the casters with GetShader(), End(...).
    // center/radius: a sphere that contains everything that casts or receives shadows.
    void Begin(ID3D11DeviceContext* ctx, const DirectX::XMFLOAT3& lightDirection,
               const DirectX::XMFLOAT3& center, float radius);
    void End(ID3D11DeviceContext* ctx);

    const Shader& GetShader() const { return *shader_; }
    const DirectX::XMFLOAT4X4& GetView() const { return view_; }
    const DirectX::XMFLOAT4X4& GetProj() const { return proj_; }
    const DirectX::XMFLOAT3& GetLightPosition() const { return lightPosition_; }
    ID3D11ShaderResourceView* GetShaderView() const { return target_.GetShaderView(); }
    UINT GetResolution() const { return target_.GetWidth(); }

private:
    bool CreateRasterizerState();

    ID3D11Device* device_ = nullptr;
    ShadowSettings settings_;
    DepthTarget target_;
    std::shared_ptr<Shader> shader_;
    ID3D11RasterizerState* rasterState_ = nullptr;
    DirectX::XMFLOAT4X4 view_{};
    DirectX::XMFLOAT4X4 proj_{};
    DirectX::XMFLOAT3 lightPosition_{};
};
