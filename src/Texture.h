#pragma once
#include <d3d11.h>
#include <array>
#include <string>

class Texture {
public:
    Texture(ID3D11Device* device, const std::string& filePath);
    Texture(ID3D11Device* device, const std::array<std::string, 6>& facePaths);
    ~Texture();

    bool IsValid() const { return valid_; }
    ID3D11ShaderResourceView* GetShaderResourceView() const { return shaderResourceView_; }

private:
    ID3D11ShaderResourceView* shaderResourceView_ = nullptr;
    ID3D11Texture2D* texture_ = nullptr;
    bool valid_ = false;
};
