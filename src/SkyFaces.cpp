#include "SkyFaces.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace {
std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

bool IsSeparator(char ch) { return ch == '_' || ch == '-' || ch == ' ' || ch == '.'; }

// GPU order: +X, -X, +Y, -Y, +Z, -Z. In this engine "back" is +Z and "front" is -Z (the original skybox.png set),
// so every scheme that says front/back keeps that, and px/nx/pz/nz are the plain axes.
const SkyFaces::Scheme kSchemes[] = {
    { "right/left/top/bottom/back/front", { "right", "left", "top", "bottom", "back", "front" } },
    { "right/left/up/down/back/front",    { "right", "left", "up",  "down",   "back", "front" } },
    { "px/nx/py/ny/pz/nz",                { "px", "nx", "py", "ny", "pz", "nz" } },
    { "posx/negx/posy/negy/posz/negz",    { "posx", "negx", "posy", "negy", "posz", "negz" } },
    { "rt/lf/up/dn/bk/ft",                { "rt", "lf", "up", "dn", "bk", "ft" } },
    { "0/1/2/3/4/5",                      { "0", "1", "2", "3", "4", "5" } },
};
}  // namespace

const SkyFaces::Scheme* SkyFaces::Schemes(size_t& count)
{
    count = sizeof(kSchemes) / sizeof(kSchemes[0]);
    return kSchemes;
}

bool SkyFaces::IsImageFile(const std::string& fileName)
{
    const std::string extension = Lower(fs::path(fileName).extension().string());
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" || extension == ".bmp";
}

bool SkyFaces::NameMatches(const std::string& fileName, const std::string& faceName)
{
    const std::string stem = Lower(fs::path(fileName).stem().string());
    const std::string face = Lower(faceName);
    if (face.empty() || stem.size() < face.size()) return false;
    if (stem.compare(stem.size() - face.size(), face.size(), face) != 0) return false;
    if (stem.size() == face.size()) return true;
    const char before = stem[stem.size() - face.size() - 1];
    if (!IsSeparator(before)) return false;
    // "face_10" must not count as face "0": a digit name only matches when nothing numeric sits in front of the separator.
    if (std::isdigit(static_cast<unsigned char>(face[0])) && stem.size() >= face.size() + 2 &&
        std::isdigit(static_cast<unsigned char>(stem[stem.size() - face.size() - 2]))) return false;
    return true;
}

bool SkyFaces::Resolve(const std::string& folder, std::array<std::string, kFaceCount>& faces, std::string* schemeLabel)
{
    std::error_code error;
    if (!fs::is_directory(folder, error)) return false;

    std::vector<std::string> files;   // bare names of the image files in the folder
    for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, error), end; !error && it != end; it.increment(error)) {
        std::error_code entryError;
        if (!it->is_regular_file(entryError)) continue;
        const std::string name = it->path().filename().string();
        if (IsImageFile(name)) files.push_back(name);
    }
    std::sort(files.begin(), files.end());   // the same answer on every machine, whatever order the disk lists them in

    for (const Scheme& scheme : kSchemes) {
        std::array<std::string, kFaceCount> found;
        bool complete = true;
        for (size_t face = 0; face < kFaceCount && complete; ++face) {
            // Prefer the exact name ("right.png") over a prefixed one ("sky_right.png") when both exist.
            int best = -1;
            for (size_t i = 0; i < files.size(); ++i) {
                if (!NameMatches(files[i], scheme.names[face])) continue;
                const bool exact = Lower(fs::path(files[i]).stem().string()) == Lower(scheme.names[face]);
                if (best < 0 || exact) best = static_cast<int>(i);
                if (exact) break;
            }
            if (best < 0) { complete = false; break; }
            found[face] = (fs::path(folder) / files[static_cast<size_t>(best)]).generic_string();
        }
        if (!complete) continue;
        // Six different files, or the scheme matched one file twice and the "skybox" would be one picture six times.
        std::array<std::string, kFaceCount> sorted = found;
        std::sort(sorted.begin(), sorted.end());
        if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) continue;
        faces = found;
        if (schemeLabel) *schemeLabel = scheme.label;
        return true;
    }
    return false;
}

std::string SkyFaces::Hint()
{
    return "A skybox folder needs six images named like right/left/top/bottom/back/front, px/nx/py/ny/pz/nz, "
           "posx/negx/posy/negy/posz/negz, rt/lf/up/dn/bk/ft or 0 to 5 (.png, .jpg, .tga or .bmp).";
}
