#include "SurfaceMaterial.h"
#include "Logger.h"
#include "ShaderSlots.h"
#include <filesystem>

SurfaceMaterial::SurfaceMaterial(ID3D11Device* device) : device_(device)
{
    // The neutral value of each map: the one that changes nothing.
    defaultAlbedo_ = Texture::Solid(device, 255, 255, 255, 255, ColorSpace::SRGB);     // white: the surface colour is left alone
    defaultNormal_ = Texture::Solid(device, 128, 128, 255, 255, ColorSpace::Linear);   // (0.5, 0.5, 1.0) decodes to (0, 0, 1): straight out of the surface
    // All white: "no occlusion, and a factor of 1 for roughness and metallic". With no ORM map the settings'
    // constants (fallbackRoughness / fallbackMetallic) are what the shader multiplies this white by.
    defaultOrm_ = Texture::Solid(device, 255, 255, 255, 255, ColorSpace::Linear);
    defaultEmissive_ = Texture::Solid(device, 0, 0, 0, 255, ColorSpace::SRGB);         // gives off no light
    if (!IsReady()) LOG_ERROR("SurfaceMaterial: could not create the default textures");
}

bool SurfaceMaterial::LoadInto(ID3D11Device* device, std::unique_ptr<Texture>& target, const std::string& path,
                               ColorSpace colorSpace)
{
    // Optional maps: a missing file is normal (not every surface has every map), so say so quietly
    // instead of letting the texture loader report an error.
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        LOG_INFO("Map not found, using the neutral default: " + path);
        target.reset();
        return false;
    }
    auto texture = std::make_unique<Texture>(device, path, colorSpace);
    if (!texture->IsValid()) { target.reset(); return false; }
    target = std::move(texture);
    return true;
}

bool SurfaceMaterial::LoadAlbedo(const std::string& path)   { return LoadInto(device_, albedo_, path, ColorSpace::SRGB); }
// Normal maps and ORM maps are DATA, not colour: reading them as sRGB would bend the numbers (lesson 6).
bool SurfaceMaterial::LoadNormal(const std::string& path)   { return LoadInto(device_, normal_, path, ColorSpace::Linear); }
bool SurfaceMaterial::LoadOrm(const std::string& path)      { return LoadInto(device_, orm_, path, ColorSpace::Linear); }
bool SurfaceMaterial::LoadEmissive(const std::string& path) { return LoadInto(device_, emissive_, path, ColorSpace::SRGB); }

void SurfaceMaterial::Bind(ID3D11DeviceContext* ctx) const
{
    if (!ctx || !IsReady()) return;
    ID3D11ShaderResourceView* albedo = (HasAlbedo() ? albedo_ : defaultAlbedo_)->GetShaderResourceView();
    ID3D11ShaderResourceView* normal = (HasNormalMap() ? normal_ : defaultNormal_)->GetShaderResourceView();
    ID3D11ShaderResourceView* orm = (HasOrmMap() ? orm_ : defaultOrm_)->GetShaderResourceView();
    ID3D11ShaderResourceView* emissive = (HasEmissiveMap() ? emissive_ : defaultEmissive_)->GetShaderResourceView();
    ctx->PSSetShaderResources(Slots::kSrvAlbedo, 1, &albedo);
    ctx->PSSetShaderResources(Slots::kSrvNormal, 1, &normal);
    ctx->PSSetShaderResources(Slots::kSrvOrm, 1, &orm);
    ctx->PSSetShaderResources(Slots::kSrvEmissive, 1, &emissive);
}

void SurfaceMaterial::Upload(Renderable& renderable) const
{
    SurfaceMaterialCB cb{};
    // Shader: roughness = ORM.g * params.y, metallic = ORM.b * extra.y. A real map holds the values, so the
    // scales multiply it; the neutral default is white, so the constants take the scales' place.
    const bool hasMap = HasOrmMap();
    cb.params = DirectX::XMFLOAT4(settings_.normalStrength, hasMap ? settings_.roughnessScale : settings_.fallbackRoughness,
                                  settings_.aoStrength, settings_.emissiveIntensity);
    cb.flags = DirectX::XMFLOAT4(settings_.flipGreen ? 1.0f : 0.0f, settings_.useNormalMap ? 1.0f : 0.0f,
                                 static_cast<float>(settings_.debugView), 0.0f);
    cb.baseColor = settings_.baseColor;
    cb.extra = DirectX::XMFLOAT4(settings_.alphaCutoff, hasMap ? settings_.metallicScale : settings_.fallbackMetallic, 0.0f, 0.0f);
    renderable.SetMaterialBlock(&cb, sizeof(cb));
}
