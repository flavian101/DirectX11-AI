#include "ThumbnailCache.h"
#include "ImageDownscale.h"
#include "libs/stb_image.h"
#include <filesystem>
#include <vector>

void ThumbnailCache::Release()
{
    for (auto& entry : items_) {
        if (entry.second.view) entry.second.view->Release();
    }
    items_.clear();
}

ThumbnailCache::Thumbnail ThumbnailCache::Get(const std::string& imagePath, const std::string& stamp)
{
    const std::string key = imagePath + "|" + stamp;
    const auto found = items_.find(key);
    if (found != items_.end()) return found->second;
    if (!device_ || budget_ <= 0) return {};
    --budget_;

    // A folder that is browsed for a long time, with files that change, must not grow without end.
    if (items_.size() > 600) Release();

    const Thumbnail made = Make(imagePath);
    items_[key] = made;   // stored even when empty: a file that cannot be previewed is not retried every frame
    return made;
}

ThumbnailCache::Thumbnail ThumbnailCache::Make(const std::string& imagePath)
{
    std::error_code error;
    const std::uintmax_t bytes = std::filesystem::file_size(imagePath, error);
    if (error || bytes > 32ull * 1024ull * 1024ull) return {};

    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load(imagePath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) return {};

    std::vector<unsigned char> shrunkPixels;
    int smallWidth = 0, smallHeight = 0;
    const bool shrunk = BoxDownscale(pixels, width, height, kMaxSide, shrunkPixels, smallWidth, smallHeight);
    stbi_image_free(pixels);
    if (!shrunk) return {};

    // Plain UNORM, not sRGB: ImGui draws texel values as they are, and the file's values already are display values.
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(smallWidth);
    desc.Height = static_cast<UINT>(smallHeight);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = shrunkPixels.data();
    data.SysMemPitch = static_cast<UINT>(smallWidth * 4);

    ID3D11Texture2D* texture = nullptr;
    if (FAILED(device_->CreateTexture2D(&desc, &data, &texture)) || !texture) return {};
    ID3D11ShaderResourceView* view = nullptr;
    const HRESULT hr = device_->CreateShaderResourceView(texture, nullptr, &view);
    texture->Release();
    if (FAILED(hr) || !view) return {};
    Thumbnail result;
    result.view = view;
    result.width = smallWidth;
    result.height = smallHeight;
    return result;
}
