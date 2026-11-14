#pragma once
#include <d3d11.h>
#include <string>
#include <unordered_map>

// Small preview pictures for the Assets panel: an image file, or one face of a skybox folder, shrunk to 128 px and kept as a GPU
// texture. Making one means decoding the whole file, so at most ONE is made per frame (a folder of big textures fills in over
// a second instead of freezing the editor), files over 32 MB are skipped, and only entries that are on screen are asked for.
class ThumbnailCache {
public:
    static constexpr int kMaxSide = 128;

    ThumbnailCache() = default;
    ~ThumbnailCache() { Release(); }
    ThumbnailCache(const ThumbnailCache&) = delete;
    ThumbnailCache& operator=(const ThumbnailCache&) = delete;

    void SetDevice(ID3D11Device* device) { device_ = device; }
    void Release();                       // free every texture (before the device goes away)
    void BeginFrame() { budget_ = 1; }    // once per frame: one new thumbnail is allowed

    struct Thumbnail {
        ID3D11ShaderResourceView* view = nullptr;   // null = not ready yet, or this file cannot be previewed
        int width = 0;                              // the picture's size, to draw it with the right shape
        int height = 0;
    };
    // The picture for the image at `imagePath`: view is null while it is not ready (ask again next frame) or cannot be made.
    // `stamp` says which version of the file this is (its size and time): a changed file gets a new picture.
    Thumbnail Get(const std::string& imagePath, const std::string& stamp);

private:
    Thumbnail Make(const std::string& imagePath);

    ID3D11Device* device_ = nullptr;   // not owned
    std::unordered_map<std::string, Thumbnail> items_;
    int budget_ = 1;
};
