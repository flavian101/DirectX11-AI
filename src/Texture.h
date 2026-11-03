#pragma once
#include <d3d11.h>
#include <array>
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

    bool IsValid() const { return valid_; }
    ID3D11ShaderResourceView* GetShaderResourceView() const { return shaderResourceView_; }

private:
    ID3D11ShaderResourceView* shaderResourceView_ = nullptr;
    ID3D11Texture2D* texture_ = nullptr;
    bool valid_ = false;
};
