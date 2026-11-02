#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include <vector>

// Small forward lighting system: one directional light plus up to
// LightSystem::kMaxPointLights point lights and an ambient term.
// Everything is packed into a single constant buffer bound to the
// pixel shader at slot b1 once per frame.

struct DirectionalLight {
    // Direction the light travels (will be normalized on upload).
    DirectX::XMFLOAT3 direction = { 0.5f, -0.8f, 0.6f };
    DirectX::XMFLOAT3 color     = { 1.0f, 1.0f, 1.0f };
    float intensity = 0.9f;
};

struct PointLight {
    DirectX::XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 color    = { 1.0f, 0.85f, 0.6f };
    float intensity = 2.0f;
    float range     = 6.0f;
};

class LightSystem {
public:
    static constexpr size_t kMaxPointLights = 4;

    LightSystem() = default;
    ~LightSystem() { Release(); }
    LightSystem(const LightSystem&) = delete;
    LightSystem& operator=(const LightSystem&) = delete;

    bool Initialize(ID3D11Device* device);
    void Release();

    void SetAmbient(const DirectX::XMFLOAT3& color) { ambient_ = color; }
    DirectionalLight& Directional() { return directional_; }
    const DirectionalLight& Directional() const { return directional_; }

    bool AddPointLight(const PointLight& light);
    void ClearPointLights() { pointLights_.clear(); }
    std::vector<PointLight>& PointLights() { return pointLights_; }

    // Uploads the light data and binds it to PS constant buffer slot b1.
    void Apply(ID3D11DeviceContext* ctx, const DirectX::XMFLOAT3& cameraPos) const;

private:
    // 16-byte aligned layout, must match cbuffer Lights in ps.hlsl.
    struct CBLights {
        DirectX::XMFLOAT4 ambientColor;                          // rgb ambient
        DirectX::XMFLOAT4 dirDirection;                          // xyz direction, w intensity
        DirectX::XMFLOAT4 dirColor;                              // rgb color
        DirectX::XMFLOAT4 pointPosRange[kMaxPointLights];        // xyz position, w range
        DirectX::XMFLOAT4 pointColorIntensity[kMaxPointLights];  // rgb color, w intensity
        DirectX::XMFLOAT4 cameraPosNumLights;                    // xyz camera pos, w point light count
    };

    DirectionalLight directional_;
    DirectX::XMFLOAT3 ambient_ = { 0.18f, 0.18f, 0.22f };
    std::vector<PointLight> pointLights_;
    ID3D11Buffer* cb_ = nullptr;
};
