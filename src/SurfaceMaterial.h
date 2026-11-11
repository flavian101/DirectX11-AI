#pragma once
#include "Renderable.h"
#include "Texture.h"
#include <DirectXMath.h>
#include <d3d11.h>
#include <memory>
#include <string>

// What the pixel shader should show instead of the final lit colour. Debug aids: when a normal-mapped
// surface looks wrong, these tell you WHICH stage is wrong (see lesson 16).
enum class SurfaceDebugView { Lit = 0, VertexNormal, MappedNormal, Tangent };

// The knobs of a textured surface. Plain data: the editor edits it, SurfaceMaterial uploads it.
struct SurfaceSettings {
    DirectX::XMFLOAT4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };   // linear; multiplies the albedo map (glTF "base colour factor")
    float alphaCutoff = 0.0f;          // 0 = opaque; above 0, pixels whose alpha is below it are discarded (leaves, fences)
    bool useNormalMap = true;          // off = light the smooth surface (compare with / without)
    float normalStrength = 1.0f;       // 0 = flat, 1 = as authored, above 1 = exaggerated
    bool flipGreen = false;            // OpenGL-style normal maps have green pointing "up"; DirectX-style, "down"
    // With an ORM map the shader reads roughness (G) and metallic (B) from it and multiplies by these scales.
    float roughnessScale = 1.0f;
    float metallicScale = 1.0f;
    // With NO ORM map there is nothing to read, so these constants ARE the roughness and metallic.
    float fallbackRoughness = 0.5f;
    float fallbackMetallic = 0.0f;
    float aoStrength = 1.0f;           // 0 = ignore the occlusion map, 1 = full effect
    float emissiveIntensity = 1.0f;    // multiplies the emissive map
    SurfaceDebugView debugView = SurfaceDebugView::Lit;
};

// Must match `cbuffer SurfaceMaterial : register(b4)` in shaders/ps.hlsl, field for field (lesson 11).
struct SurfaceMaterialCB {
    DirectX::XMFLOAT4 params;   // x = normal strength, y = roughness scale (or the constant, see Upload), z = AO strength, w = emissive intensity
    DirectX::XMFLOAT4 flags;    // x = flip green, y = use normal map, z = debug view
    DirectX::XMFLOAT4 baseColor;   // linear rgba, multiplies the albedo
    DirectX::XMFLOAT4 extra;       // x = alpha cutoff, y = metallic scale
};
static_assert(sizeof(SurfaceMaterialCB) % 16 == 0, "constant buffers must be a multiple of 16 bytes");

// A set of maps for one surface: albedo (t0), normal (t5), ORM (t6), emissive (t7). Any map you do not
// load is replaced by a one-pixel NEUTRAL texture, so the shader always has something valid to sample
// and never needs an "is there a map?" branch.
class SurfaceMaterial {
public:
    explicit SurfaceMaterial(ID3D11Device* device);

    // Each returns false (and keeps the neutral default) when the file is missing or unreadable.
    bool LoadAlbedo(const std::string& path);
    bool LoadNormal(const std::string& path);
    bool LoadOrm(const std::string& path);        // R = ambient occlusion, G = roughness, B = metallic
    bool LoadEmissive(const std::string& path);
    // The same four maps, but already created (embedded images, repacked maps). Null = back to the neutral default.
    void SetAlbedo(std::unique_ptr<Texture> texture)   { albedo_ = std::move(texture); }
    void SetNormal(std::unique_ptr<Texture> texture)   { normal_ = std::move(texture); }
    void SetOrm(std::unique_ptr<Texture> texture)      { orm_ = std::move(texture); }
    void SetEmissive(std::unique_ptr<Texture> texture) { emissive_ = std::move(texture); }

    bool HasAlbedo() const { return albedo_ && albedo_->IsValid(); }
    bool HasNormalMap() const { return normal_ && normal_->IsValid(); }
    bool HasOrmMap() const { return orm_ && orm_->IsValid(); }
    bool HasEmissiveMap() const { return emissive_ && emissive_->IsValid(); }
    bool IsReady() const { return defaultAlbedo_ && defaultNormal_ && defaultOrm_ && defaultEmissive_; }

    SurfaceSettings& Settings() { return settings_; }
    const SurfaceSettings& Settings() const { return settings_; }

    // Binds the four maps to their slots (loaded one, or the neutral default).
    void Bind(ID3D11DeviceContext* ctx) const;
    // Hands the settings to the renderable's material block (b4). (Renderable skips the GPU copy if nothing changed.)
    void Upload(Renderable& renderable) const;
    // The same numbers as a plain struct, for objects that own their constant buffer instead of a Renderable (ScatterObject).
    SurfaceMaterialCB Constants() const;

private:
    static bool LoadInto(ID3D11Device* device, std::unique_ptr<Texture>& target, const std::string& path,
                         ColorSpace colorSpace);

    ID3D11Device* device_ = nullptr;   // non-owning
    SurfaceSettings settings_;
    std::unique_ptr<Texture> albedo_, normal_, orm_, emissive_;                      // supplied by the artist (may be null)
    std::unique_ptr<Texture> defaultAlbedo_, defaultNormal_, defaultOrm_, defaultEmissive_;   // neutral stand-ins
};
