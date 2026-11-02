#include "Texture.h"
#include "libs/stb_image.h"
#include "Logger.h"
#include <DirectXMath.h>

Texture::Texture(ID3D11Device* device, const std::string& filePath)
{
    if (!device) {
        LOG_ERROR("Cannot create texture without a valid device");
        return;
    }

    // Load image data using stb_image
    int width, height, channels;
    unsigned char* imageData = stbi_load(filePath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!imageData) {
        LOG_ERROR("Failed to load texture: " + filePath);
        return;
    }

    // Create texture from image data
    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    textureDesc.CPUAccessFlags = 0;

    D3D11_SUBRESOURCE_DATA initData{};
    initData.pSysMem = imageData;
    initData.SysMemPitch = width * 4;

    HRESULT hr = device->CreateTexture2D(&textureDesc, &initData, &texture_);
    if (FAILED(hr)) {
        LOG_HR(hr, "Failed to create texture");
        stbi_image_free(imageData);
        return;
    }

    // Create shader resource view
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;

    hr = device->CreateShaderResourceView(texture_, &srvDesc, &shaderResourceView_);
    if (FAILED(hr)) {
        LOG_HR(hr, "Failed to create shader resource view");
        texture_->Release();
        stbi_image_free(imageData);
        return;
    }

    stbi_image_free(imageData);
    valid_ = true;
}

Texture::Texture(ID3D11Device* device, const std::array<std::string, 6>& facePaths)
{
    if (!device) {
        LOG_ERROR("Cannot create cubemap without a valid device");
        return;
    }

    std::array<unsigned char*, 6> images{};
    int width = 0;
    int height = 0;
    for (size_t face = 0; face < facePaths.size(); ++face) {
        int faceWidth = 0;
        int faceHeight = 0;
        int channels = 0;
        images[face] = stbi_load(facePaths[face].c_str(), &faceWidth, &faceHeight,
                                 &channels, STBI_rgb_alpha);
        if (!images[face] || (face > 0 && (faceWidth != width || faceHeight != height))) {
            LOG_ERROR("Failed to load skybox face: " + facePaths[face]);
            for (unsigned char* image : images) if (image) stbi_image_free(image);
            return;
        }
        width = faceWidth;
        height = faceHeight;
    }

    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 6;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    textureDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

    std::array<D3D11_SUBRESOURCE_DATA, 6> initData{};
    for (size_t face = 0; face < initData.size(); ++face) {
        initData[face].pSysMem = images[face];
        initData[face].SysMemPitch = static_cast<UINT>(width * 4);
    }

    HRESULT hr = device->CreateTexture2D(&textureDesc, initData.data(), &texture_);
    for (unsigned char* image : images) if (image) stbi_image_free(image);
    if (FAILED(hr)) {
        LOG_HR(hr, "Failed to create cubemap texture");
        return;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MipLevels = 1;
    srvDesc.TextureCube.MostDetailedMip = 0;
    hr = device->CreateShaderResourceView(texture_, &srvDesc, &shaderResourceView_);
    if (FAILED(hr)) {
        LOG_HR(hr, "Failed to create cubemap shader resource view");
        texture_->Release();
        texture_ = nullptr;
        return;
    }
    valid_ = true;
}

Texture::~Texture()
{
    if (shaderResourceView_) {
        shaderResourceView_->Release();
    }
    if (texture_) {
        texture_->Release();
    }
}
