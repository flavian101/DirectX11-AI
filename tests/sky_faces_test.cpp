// Skybox face naming: every scheme resolves to GPU order, prefixes and case are tolerated, and near-misses are rejected.
#include "SkyFaces.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

static fs::path MakeFolder(const char* name, std::initializer_list<const char*> files)
{
    const fs::path folder = fs::temp_directory_path() / "skyfaces_test" / name;
    std::error_code error;
    fs::remove_all(folder, error);
    fs::create_directories(folder);
    for (const char* file : files) std::ofstream((folder / file).string()).put('x');
    return folder;
}

static std::string Leaf(const std::string& path) { return fs::path(path).filename().string(); }

int main()
{
    std::array<std::string, 6> faces;
    std::string scheme;

    // The original scheme keeps its meaning: back is +Z, front is -Z.
    {
        const auto f = MakeFolder("orig", { "right.png", "left.png", "top.png", "bottom.png", "back.png", "front.png" });
        CHECK(SkyFaces::Resolve(f.string(), faces, &scheme));
        CHECK(Leaf(faces[0]) == "right.png" && Leaf(faces[1]) == "left.png");
        CHECK(Leaf(faces[2]) == "top.png" && Leaf(faces[3]) == "bottom.png");
        CHECK(Leaf(faces[4]) == "back.png" && Leaf(faces[5]) == "front.png");
    }
    // Axis names, any case, any extension.
    {
        const auto f = MakeFolder("axis", { "PX.JPG", "nx.jpg", "py.Jpeg", "ny.tga", "pz.bmp", "NZ.png" });
        CHECK(SkyFaces::Resolve(f.string(), faces, &scheme));
        CHECK(scheme == "px/nx/py/ny/pz/nz");
        CHECK(Leaf(faces[0]) == "PX.JPG" && Leaf(faces[4]) == "pz.bmp" && Leaf(faces[5]) == "NZ.png");
    }
    // A prefix before the face name.
    {
        const auto f = MakeFolder("prefix", { "clouds_posx.png", "clouds_negx.png", "clouds_posy.png", "clouds_negy.png", "clouds_posz.png", "clouds_negz.png" });
        CHECK(SkyFaces::Resolve(f.string(), faces, &scheme));
        CHECK(scheme == "posx/negx/posy/negy/posz/negz");
        CHECK(Leaf(faces[3]) == "clouds_negy.png");
    }
    // Numbers: 0..5 are already in GPU order; "face_10" is not face "0".
    {
        const auto f = MakeFolder("numbers", { "0.png", "1.png", "2.png", "3.png", "4.png", "5.png" });
        CHECK(SkyFaces::Resolve(f.string(), faces, &scheme));
        CHECK(Leaf(faces[0]) == "0.png" && Leaf(faces[5]) == "5.png");
        CHECK(!SkyFaces::NameMatches("face_10.png", "0"));
        CHECK(SkyFaces::NameMatches("face_0.png", "0"));
        CHECK(SkyFaces::NameMatches("sky-2.png", "2"));
    }
    // Quake style.
    {
        const auto f = MakeFolder("quake", { "sky_rt.tga", "sky_lf.tga", "sky_up.tga", "sky_dn.tga", "sky_bk.tga", "sky_ft.tga" });
        CHECK(SkyFaces::Resolve(f.string(), faces, &scheme));
        CHECK(scheme == "rt/lf/up/dn/bk/ft");
        CHECK(Leaf(faces[4]) == "sky_bk.tga" && Leaf(faces[5]) == "sky_ft.tga");
    }
    // "up/down" instead of "top/bottom".
    {
        const auto f = MakeFolder("updown", { "right.png", "left.png", "up.png", "down.png", "back.png", "front.png" });
        CHECK(SkyFaces::Resolve(f.string(), faces, &scheme));
        CHECK(Leaf(faces[2]) == "up.png" && Leaf(faces[3]) == "down.png");
    }
    // When the exact name and a prefixed one both exist, the exact one wins.
    {
        const auto f = MakeFolder("exact", { "right.png", "old_right.png", "left.png", "top.png", "bottom.png", "back.png", "front.png" });
        CHECK(SkyFaces::Resolve(f.string(), faces, nullptr));
        CHECK(Leaf(faces[0]) == "right.png");
    }
    // Rejections: a missing face, a non-image, a word that merely ends in a face name, and a missing folder.
    {
        const auto missing = MakeFolder("missing", { "right.png", "left.png", "top.png", "bottom.png", "back.png" });
        CHECK(!SkyFaces::Resolve(missing.string(), faces, nullptr));
        const auto text = MakeFolder("text", { "right.txt", "left.txt", "top.txt", "bottom.txt", "back.txt", "front.txt" });
        CHECK(!SkyFaces::Resolve(text.string(), faces, nullptr));
        CHECK(!SkyFaces::NameMatches("bright.png", "right"));
        CHECK(!SkyFaces::Resolve((fs::temp_directory_path() / "skyfaces_test" / "nope").string(), faces, nullptr));
    }
    // Mixed schemes in one folder do not resolve (px/nx with right/left): better an error than a scrambled sky.
    {
        const auto mixed = MakeFolder("mixed", { "px.png", "left.png", "py.png", "bottom.png", "pz.png", "front.png" });
        CHECK(!SkyFaces::Resolve(mixed.string(), faces, nullptr));
    }

    std::error_code error;
    fs::remove_all(fs::temp_directory_path() / "skyfaces_test", error);
    if (failures == 0) std::printf("SkyFaces tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
