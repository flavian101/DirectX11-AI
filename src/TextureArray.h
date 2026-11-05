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
    ~TextureArray();
    TextureArray(const TextureArray&) = delete;
    TextureArray& operator=(const TextureArray&) = delete;

    bool IsValid() const { return valid_; }
    ID3D11ShaderResourceView* GetShaderResourceView() const { return shaderResourceView_; }
    int GetLayerCount() const { return layerCount_; }
    int GetLoadedCount() const { return loadedCount_; }   // how many layers came from a real file

private:
    ID3D11Texture2D* texture_ = nullptr;
    ID3D11ShaderResourceView* shaderResourceView_ = nullptr;
    int layerCount_ = 0;
    int loadedCount_ = 0;
    bool valid_ = false;
};
