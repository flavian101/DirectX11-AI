#pragma once
#include <d3d11.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>

// How the pixel values in a file should be read.
//   SRGB   - colour images (albedo, sky): authored in the display curve; the GPU converts every
//            sample to linear BEFORE filtering and lighting. This is the default.
//   Linear - DATA images (normal maps, roughness, masks): the numbers mean what they say.
//            Never mark these sRGB or the data gets bent.
enum class ColorSpace { SRGB, Linear };

class Texture {
public:
    Texture(ID3D11Device* device, const std::string& filePath, ColorSpace colorSpace = ColorSpace::SRGB);
    Texture(ID3D11Device* device, const std::array<std::string, 6>& facePaths, ColorSpace colorSpace = ColorSpace::SRGB);
    ~Texture();

    // A 1x1 texture of one colour: the "neutral" stand-in for a map that was not supplied
    // (flat normal, white occlusion, black emission...). Returns nullptr if creation fails.
    static std::unique_ptr<Texture> Solid(ID3D11Device* device, std::uint8_t r, std::uint8_t g,
                                          std::uint8_t b, std::uint8_t a, ColorSpace colorSpace);

    bool IsValid() const { return valid_; }
    ID3D11ShaderResourceView* GetShaderResourceView() const { return shaderResourceView_; }

private:
    Texture() = default;   // only the factories above build empty shells
    ID3D11ShaderResourceView* shaderResourceView_ = nullptr;
    ID3D11Texture2D* texture_ = nullptr;
    bool valid_ = false;
};
