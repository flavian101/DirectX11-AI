#include "HeightPicture.h"
#include "libs/stb_image.h"

bool LoadHeightPicture(const std::string& path, HeightPicture& picture, std::string& error)
{
    int width = 0, height = 0, channels = 0;
    HeightPicture loaded;
    if (stbi_is_16_bit(path.c_str())) {
        stbi_us* pixels = stbi_load_16(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels) { error = "could not read the image: " + path; return false; }
        loaded.bits = 16;
        loaded.levels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
        for (size_t i = 0; i < loaded.levels.size(); ++i) loaded.levels[i] = pixels[i * 4];
        stbi_image_free(pixels);
    } else {
        stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!pixels) { error = "could not read the image: " + path; return false; }
        loaded.bits = 8;
        loaded.levels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
        for (size_t i = 0; i < loaded.levels.size(); ++i) loaded.levels[i] = pixels[i * 4];
        stbi_image_free(pixels);
    }
    loaded.width = width;
    loaded.height = height;
    if (!loaded.IsValid()) { error = "the image is smaller than 2 x 2 pixels: " + path; return false; }
    picture = std::move(loaded);
    return true;
}
