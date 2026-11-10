#pragma once
#include "Texture.h"
#include <array>
#include <cstdint>
#include <d3d11.h>
#include <string>
#include <vector>

// Several images of the SAME size stored in one texture object ("slices"), read in a shader as
// Texture2DArray with a third coordinate that picks the slice. Used for terrain layers: one slot
// holds grass, rock, sand and snow instead of needing four slots per kind of map.
//
// A layer whose file is missing is filled with `fillColor` (the neutral value of that kind of map),
// so the array is always complete and the shader never has to ask "does this layer exist?".
class TextureArray {
public:
    TextureArray(ID3D11Device* device, const std::vector<std::string>& layerPaths, ColorSpace colorSpace,
                 std::array<std::uint8_t, 4> fillColor);
    // From pixels the caller already prepared: one RGBA8 slice per layer, all of width x height. `loadedCount` is only
    // reported back by GetLoadedCount(). Used by the terrain, which PACKS several image files into the channels of one
    // array (lesson 29) instead of keeping one array per kind of file.
    TextureArray(ID3D11Device* device, int width, int height, ColorSpace colorSpace,
                 const std::vector<std::vector<std::uint8_t>>& slices, int loadedCount);
    ~TextureArray();
    TextureArray(const TextureArray&) = delete;
    TextureArray& operator=(const TextureArray&) = delete;

    bool IsValid() const { return valid_; }
    ID3D11ShaderResourceView* GetShaderResourceView() const { return shaderResourceView_; }
    int GetLayerCount() const { return layerCount_; }
    int GetLoadedCount() const { return loadedCount_; }   // how many layers came from a real file

    // Helpers for callers that prepare slices themselves.
    // Reads an image file as RGBA8 (alpha 255 if the file has none). False, and the outputs untouched, if it is missing or unreadable.
    static bool LoadImageRgba8(const std::string& path, int& width, int& height, std::vector<std::uint8_t>& pixels);
    // Scales RGBA8 pixels to another size (bilinear: good enough for a one-time load).
    static std::vector<std::uint8_t> ResizeRgba8(const std::vector<std::uint8_t>& source, int sourceWidth, int sourceHeight, int width, int height);

private:
    // The shared back half of both constructors: texture + view + mip chain from finished slices.
    void CreateFromSlices(ID3D11Device* device, int width, int height, DXGI_FORMAT format,
                          const std::vector<std::vector<std::uint8_t>>& slices);

    ID3D11Texture2D* texture_ = nullptr;
    ID3D11ShaderResourceView* shaderResourceView_ = nullptr;
    int layerCount_ = 0;
    int loadedCount_ = 0;
    bool valid_ = false;
};
