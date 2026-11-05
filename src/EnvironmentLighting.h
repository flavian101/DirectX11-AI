#pragma once
#include "Shader.h"
#include <d3d11.h>
#include <memory>

// What the editor can change about image-based lighting.
enum class SkyDebugView { Sky = 0, Irradiance, Prefiltered };

struct EnvironmentSettings {
    bool enabled = true;              // off = the old flat ambient colour
    float diffuseIntensity = 1.0f;    // strength of the soft light the sky gives to every surface
    float specularIntensity = 1.0f;   // strength of the sky's reflection on glossy surfaces
    SkyDebugView skyView = SkyDebugView::Sky;   // show a baked map as the sky background, to inspect it
    float debugMip = 0.0f;            // which roughness level of the prefiltered map to show
};

// Image-based lighting (IBL): the sky, precomputed into three small textures so a pixel shader can
// ask "how much light comes from everywhere, for a surface facing this way?" with ONE texture read.
//   irradiance  (cube, 32x32)        - blurred sky for diffuse light
//   prefiltered (cube, 128x128, 5 mips) - the sky blurred by each roughness level, for reflections
//   BRDF table  (2D, 256x256)        - how reflectivity changes with angle and roughness
// Baking runs once, on the GPU, with the same full-screen-triangle trick the post-process uses.
class EnvironmentLighting {
public:
    static constexpr UINT kIrradianceSize = 32;
    static constexpr UINT kPrefilterSize = 128;
    static constexpr UINT kPrefilterMips = 5;
    static constexpr UINT kBrdfSize = 256;

    EnvironmentLighting() = default;
    ~EnvironmentLighting() { Release(); }
    EnvironmentLighting(const EnvironmentLighting&) = delete;
    EnvironmentLighting& operator=(const EnvironmentLighting&) = delete;

    // Builds all three maps from `sourceCube` (the sky). Call from the immediate context OUTSIDE any
    // render pass; it leaves the pipeline in the neutral state the renderer expects. False on failure.
    bool Bake(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* sourceCube);
    void Release();
    bool IsBaked() const { return irradianceSrv_ && prefilteredSrv_ && brdfSrv_; }

    // The sky this was baked from, to notice when the sky changes.
    ID3D11ShaderResourceView* GetSource() const { return source_; }

    // Binds the three maps (t11, t12, t13). With nothing baked, binds nulls.
    void Bind(ID3D11DeviceContext* ctx) const;

    ID3D11ShaderResourceView* GetBrdfTable() const { return brdfSrv_; }   // the editor draws it as a picture
    static constexpr float MaxPrefilterMip() { return static_cast<float>(kPrefilterMips - 1); }

private:
    struct BakeContext;
    bool CreateCube(ID3D11Device* device, UINT size, UINT mips, ID3D11Texture2D*& texture, ID3D11ShaderResourceView*& srv);

    ID3D11ShaderResourceView* source_ = nullptr;       // holds a reference
    ID3D11Texture2D* irradiance_ = nullptr;
    ID3D11ShaderResourceView* irradianceSrv_ = nullptr;
    ID3D11Texture2D* prefiltered_ = nullptr;
    ID3D11ShaderResourceView* prefilteredSrv_ = nullptr;
    ID3D11Texture2D* brdf_ = nullptr;
    ID3D11ShaderResourceView* brdfSrv_ = nullptr;
};
