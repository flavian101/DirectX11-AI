#include "TextureArray.h"
#include "Logger.h"
#include "libs/stb_image.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace {
UINT FullMipCount(int width, int height)
{
    UINT levels = 1;
    for (int size = (std::max)(width, height); size > 1; size >>= 1) ++levels;
    return levels;
}

// All slices of an array must have identical dimensions, but artists' files differ. Scale `source`
// to the common size with plain bilinear filtering (good enough for a one-time load).
std::vector<std::uint8_t> ResizeBilinear(const unsigned char* source, int sourceWidth, int sourceHeight,
                                         int width, int height)
{
    std::vector<std::uint8_t> result(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        const float fy = (static_cast<float>(y) + 0.5f) * sourceHeight / height - 0.5f;
        const int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, sourceHeight - 1);
        const int y1 = (std::min)(y0 + 1, sourceHeight - 1);
        const float ty = std::clamp(fy - std::floor(fy), 0.0f, 1.0f);
        for (int x = 0; x < width; ++x) {
            const float fx = (static_cast<float>(x) + 0.5f) * sourceWidth / width - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, sourceWidth - 1);
            const int x1 = (std::min)(x0 + 1, sourceWidth - 1);
            const float tx = std::clamp(fx - std::floor(fx), 0.0f, 1.0f);
            for (int c = 0; c < 4; ++c) {
                auto at = [&](int px, int py) { return static_cast<float>(source[(static_cast<size_t>(py) * sourceWidth + px) * 4 + c]); };
                const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * tx;
                const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * tx;
                result[(static_cast<size_t>(y) * width + x) * 4 + c] = static_cast<std::uint8_t>(top + (bottom - top) * ty + 0.5f);
            }
        }
    }
    return result;
}
}

TextureArray::TextureArray(ID3D11Device* device, const std::vector<std::string>& layerPaths, ColorSpace colorSpace,
                           std::array<std::uint8_t, 4> fillColor)
{
    if (!device || layerPaths.empty()) {
        LOG_ERROR("Cannot create a texture array without a device and at least one layer");
        return;
    }
    layerCount_ = static_cast<int>(layerPaths.size());
    const DXGI_FORMAT format = (colorSpace == ColorSpace::SRGB) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                                                                 : DXGI_FORMAT_R8G8B8A8_UNORM;

    // 1. Read every file that exists. The first one that loads decides the size of the whole array.
    struct Loaded { unsigned char* pixels = nullptr; int width = 0; int height = 0; };
    std::vector<Loaded> images(layerPaths.size());
    int width = 0, height = 0;
    for (size_t i = 0; i < layerPaths.size(); ++i) {
        std::error_code error;
        if (layerPaths[i].empty() || !std::filesystem::exists(layerPaths[i], error)) {
            LOG_INFO("Layer map not found, using the neutral default: " + layerPaths[i]);
            continue;
        }
        int channels = 0;
        images[i].pixels = stbi_load(layerPaths[i].c_str(), &images[i].width, &images[i].height, &channels, STBI_rgb_alpha);
        if (!images[i].pixels) { LOG_ERROR("Failed to load layer map: " + layerPaths[i]); continue; }
        ++loadedCount_;
        if (width == 0) { width = images[i].width; height = images[i].height; }
    }
    if (width == 0) { width = 4; height = 4; }   // nothing loaded: a tiny all-default array

    // 2. Build one slice of pixels per layer: the file (resized if needed) or the neutral fill.
    std::vector<std::vector<std::uint8_t>> slices(layerPaths.size());
    for (size_t i = 0; i < layerPaths.size(); ++i) {
        if (images[i].pixels) {
            if (images[i].width == width && images[i].height == height) {
                slices[i].assign(images[i].pixels, images[i].pixels + static_cast<size_t>(width) * height * 4);
            } else {
                slices[i] = ResizeBilinear(images[i].pixels, images[i].width, images[i].height, width, height);
            }
            stbi_image_free(images[i].pixels);
        } else {
            slices[i].resize(static_cast<size_t>(width) * height * 4);
            for (size_t p = 0; p < slices[i].size(); ++p) slices[i][p] = fillColor[p % 4];
        }
    }

    CreateFromSlices(device, width, height, format, slices);
}

TextureArray::TextureArray(ID3D11Device* device, int width, int height, ColorSpace colorSpace,
                           const std::vector<std::vector<std::uint8_t>>& slices, int loadedCount)
{
    if (!device || slices.empty() || width <= 0 || height <= 0) {
        LOG_ERROR("Cannot create a texture array without a device, a size and at least one slice");
        return;
    }
    for (const std::vector<std::uint8_t>& slice : slices) {
        if (slice.size() != static_cast<size_t>(width) * height * 4) {
            LOG_ERROR("A texture array slice does not match the array's size");
            return;
        }
    }
    layerCount_ = static_cast<int>(slices.size());
    loadedCount_ = loadedCount;
    CreateFromSlices(device, width, height,
                     (colorSpace == ColorSpace::SRGB) ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM, slices);
}

bool TextureArray::LoadImageRgba8(const std::string& path, int& width, int& height, std::vector<std::uint8_t>& pixels)
{
    std::error_code error;
    if (path.empty() || !std::filesystem::exists(path, error)) return false;
    int channels = 0, w = 0, h = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!data) { LOG_ERROR("Failed to load image: " + path); return false; }
    width = w;
    height = h;
    pixels.assign(data, data + static_cast<size_t>(w) * h * 4);
    stbi_image_free(data);
    return true;
}

std::vector<std::uint8_t> TextureArray::ResizeRgba8(const std::vector<std::uint8_t>& source, int sourceWidth, int sourceHeight,
                                                    int width, int height)
{
    return ResizeBilinear(source.data(), sourceWidth, sourceHeight, width, height);
}

void TextureArray::CreateFromSlices(ID3D11Device* device, int width, int height, DXGI_FORMAT format,
                                    const std::vector<std::vector<std::uint8_t>>& slices)
{
    const UINT mipLevels = FullMipCount(width, height);
    const size_t layerCount = slices.size();
    // One texture with N slices and a full mip chain (same recipe as the cubemap: upload level 0 of
    // each slice, then let the GPU fill the smaller levels of ALL slices).
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = mipLevels;
    desc.ArraySize = static_cast<UINT>(layerCount);
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, &texture_);
    if (FAILED(hr)) { LOG_HR(hr, "Failed to create texture array"); return; }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels = static_cast<UINT>(-1);
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize = static_cast<UINT>(layerCount);
    hr = device->CreateShaderResourceView(texture_, &srvDesc, &shaderResourceView_);
    if (FAILED(hr)) {
        LOG_HR(hr, "Failed to create texture array view");
        texture_->Release();
        texture_ = nullptr;
        return;
    }

    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);
    for (size_t i = 0; i < slices.size(); ++i) {
        // Subresource index = mipSlice + arraySlice * mipLevels; level 0 of slice i.
        context->UpdateSubresource(texture_, D3D11CalcSubresource(0, static_cast<UINT>(i), mipLevels), nullptr,
                                   slices[i].data(), static_cast<UINT>(width * 4), 0);
    }
    context->GenerateMips(shaderResourceView_);
    context->Release();
    valid_ = true;
}

TextureArray::~TextureArray()
{
    if (shaderResourceView_) shaderResourceView_->Release();
    if (texture_) texture_->Release();
}
