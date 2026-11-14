#pragma once
#include <array>
#include <string>

// Finding the six face images of a cube-map skybox, whatever the artist called them.
//
// Skybox packs come with many naming habits, so a folder is accepted when its image files fit ANY of these schemes
// (names are case-insensitive, the extension may be png / jpg / jpeg / tga / bmp, and a prefix such as "clouds_" or
// "sky-" before the face name is fine):
//
//     right, left, top, bottom, back, front       (the original scheme of this engine)
//     right, left, up,  down,   back, front
//     px, nx, py, ny, pz, nz                      (positive / negative axis)
//     posx, negx, posy, negy, posz, negz
//     rt, lf, up, dn, bk, ft                      (the Quake / Source habit)
//     0, 1, 2, 3, 4, 5                            (already in GPU face order)
//
// The faces come back in GPU cube order: +X, -X, +Y, -Y, +Z, -Z. Pure logic (only <filesystem>), so it has a unit test.
namespace SkyFaces {

constexpr size_t kFaceCount = 6;

// One scheme the resolver recognises: its name for messages, and the six face names in GPU order.
struct Scheme {
    const char* label;
    std::array<const char*, kFaceCount> names;
};
const Scheme* Schemes(size_t& count);

// True when `fileName` (a bare file name, any case) is the face called `faceName`: the stem is exactly that name, or ends
// with it after a separator ('_', '-', ' ', '.'). "sky_right.png" is "right"; "bright.png" is not.
bool NameMatches(const std::string& fileName, const std::string& faceName);

// Looks in `folder` for six image files that fit one scheme. On success fills `faces` (full paths, GPU order) and, if
// `schemeLabel` is not null, says which scheme matched. False if no scheme is complete.
bool Resolve(const std::string& folder, std::array<std::string, kFaceCount>& faces, std::string* schemeLabel = nullptr);

// True for a file the resolver would accept as a face image (by extension).
bool IsImageFile(const std::string& fileName);

// One line that tells a person what a skybox folder needs, for error messages.
std::string Hint();

}  // namespace SkyFaces
