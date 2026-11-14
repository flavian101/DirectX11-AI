#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include <vector>

// Small forward lighting system: two directional lights (the sun, and a moon or a second sun that casts no shadow), up to
// LightSystem::kMaxPointLights point lights and an ambient term. Everything is packed into a single constant buffer bound
// to the pixel shader at slot b1 once per frame. The lights themselves are scene objects (LightObject.h): the scene
// copies their numbers in here every frame (Scene::ApplyLights).

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
    static constexpr size_t kMaxPointLights = 8;   // must equal MAX_POINT_LIGHTS in shaders/common.hlsli

    LightSystem() = default;
    ~LightSystem() { Release(); }
    LightSystem(const LightSystem&) = delete;
    LightSystem& operator=(const LightSystem&) = delete;

    bool Initialize(ID3D11Device* device);
    void Release();

    void SetAmbient(const DirectX::XMFLOAT3& color) { ambient_ = color; }
    DirectX::XMFLOAT3& Ambient() { return ambient_; }
    const DirectX::XMFLOAT3& Ambient() const { return ambient_; }
    // The main directional light: the one that casts shadows. Its intensity is 0 when the scene has no directional light.
    DirectionalLight& Directional() { return directional_; }
    const DirectionalLight& Directional() const { return directional_; }
    // The second directional light (a moon beside a sun). No shadows. Intensity 0 = none.
    DirectionalLight& Secondary() { return secondary_; }
    const DirectionalLight& Secondary() const { return secondary_; }

    bool AddPointLight(const PointLight& light);
    void ClearPointLights() { pointLights_.clear(); }
    std::vector<PointLight>& PointLights() { return pointLights_; }
    const std::vector<PointLight>& PointLights() const { return pointLights_; }

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
        DirectX::XMFLOAT4 dirDirection2;                         // the second directional light: xyz direction, w intensity (0 = none)
        DirectX::XMFLOAT4 dirColor2;                             // rgb color
    };

    DirectionalLight directional_;
    DirectionalLight secondary_ = { { 0.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, 0.0f };
    DirectX::XMFLOAT3 ambient_ = { 0.18f, 0.18f, 0.22f };
    std::vector<PointLight> pointLights_;
    ID3D11Buffer* cb_ = nullptr;
};
