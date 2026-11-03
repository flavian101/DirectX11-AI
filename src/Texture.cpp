#include "Texture.h"
#include "libs/stb_image.h"
#include "Logger.h"
#include <DirectXMath.h>
#include <algorithm>

namespace {
// Number of levels in a full mip chain: 1024 -> 512 -> ... -> 1 is 11 levels.
UINT FullMipCount(int width, int height)
{
    UINT levels = 1;
    for (int size = (std::max)(width, height); size > 1; size >>= 1) ++levels;
    return levels;
}
}

Texture::Texture(ID3D11Device* device, const std::string& filePath, ColorSpace colorSpace)
{
    const DXGI_FORMAT format = (colorSpace == ColorSpace::SRGB) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                                                                 : DXGI_FORMAT_R8G8B8A8_UNORM;
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

    // Mipmaps: the GPU can only build them for a texture that is also a render target and
    // is flagged GENERATE_MIPS, so it must be DEFAULT usage (not IMMUTABLE) and we upload
    // only level 0 ourselves, then ask the GPU to fill in the smaller levels.
    const UINT mipLevels = FullMipCount(width, height);
    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.MipLevels = mipLevels;
    textureDesc.ArraySize = 1;
    textureDesc.Format = format;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    textureDesc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    HRESULT hr = device->CreateTexture2D(&textureDesc, nullptr, &texture_);
    if (FAILED(hr)) {
        LOG_HR(hr, "Failed to create texture");
        stbi_image_free(imageData);
        return;
    }

    // Create shader resource view
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = static_cast<UINT>(-1);   // -1 = every level in the texture
    srvDesc.Texture2D.MostDetailedMip = 0;

    hr = device->CreateShaderResourceView(texture_, &srvDesc, &shaderResourceView_);
    if (FAILED(hr)) {
        LOG_HR(hr, "Failed to create shader resource view");
        texture_->Release();
        texture_ = nullptr;   // otherwise ~Texture() releases it a second time
        stbi_image_free(imageData);
        return;
    }

    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);
    context->UpdateSubresource(texture_, 0, nullptr, imageData, static_cast<UINT>(width * 4), 0);
    context->GenerateMips(shaderResourceView_);
    context->Release();

    stbi_image_free(imageData);
    valid_ = true;
}

Texture::Texture(ID3D11Device* device, const std::array<std::string, 6>& facePaths, ColorSpace colorSpace)
{
    const DXGI_FORMAT format = (colorSpace == ColorSpace::SRGB) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                                                                 : DXGI_FORMAT_R8G8B8A8_UNORM;
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

    // Same recipe as the 2D texture. The water samples this cubemap with wobbling reflection
    // vectors; without mips that minification shimmers.
    const UINT mipLevels = FullMipCount(width, height);
    D3D11_TEXTURE2D_DESC textureDesc{};
    textureDesc.Width = width;
    textureDesc.Height = height;
    textureDesc.MipLevels = mipLevels;
    textureDesc.ArraySize = 6;
    textureDesc.Format = format;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    textureDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE | D3D11_RESOURCE_MISC_GENERATE_MIPS;

    HRESULT hr = device->CreateTexture2D(&textureDesc, nullptr, &texture_);
    if (FAILED(hr)) {
        for (unsigned char* image : images) if (image) stbi_image_free(image);
        LOG_HR(hr, "Failed to create cubemap texture");
        return;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MipLevels = static_cast<UINT>(-1);
    srvDesc.TextureCube.MostDetailedMip = 0;
    hr = device->CreateShaderResourceView(texture_, &srvDesc, &shaderResourceView_);
    if (FAILED(hr)) {
        LOG_HR(hr, "Failed to create cubemap shader resource view");
        texture_->Release();
        texture_ = nullptr;
        for (unsigned char* image : images) if (image) stbi_image_free(image);
        return;
    }

    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);
    for (UINT face = 0; face < 6; ++face) {
        // Subresource index = mipSlice + arraySlice * mipLevels; level 0 of each face.
        context->UpdateSubresource(texture_, D3D11CalcSubresource(0, face, mipLevels), nullptr,
                                   images[face], static_cast<UINT>(width * 4), 0);
    }
    context->GenerateMips(shaderResourceView_);
    context->Release();
    for (unsigned char* image : images) if (image) stbi_image_free(image);
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
