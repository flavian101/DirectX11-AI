#pragma once
// The per-frame decisions of culling and level of detail, as plain functions. Pure logic (no Direct3D, no DirectXMath),
// tested on its own: tests/lod_select_test.cpp.
//
// WHY SCREEN SIZE AND NOT DISTANCE (lesson 31). The terrain picks its level of detail from a distance in metres, which
// is fine for one big thing. A forest is a thousand things of different sizes: a 30 m pine and a 0.5 m rock should not
// change level at the same distance. What the eye sees is how big something looks, so that is what we measure: the
// fraction of the screen's height that the object's bounding sphere covers. Big and near -> fine detail; small or far ->
// coarse detail, or nothing at all.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

constexpr int kMaxLodLevels = 6;

// The fraction of the screen's HEIGHT that a sphere of `radius` metres covers at `distance` metres (1.0 = the whole
// screen). `projScale` is cot(fov / 2): element [1][1] of the camera's projection matrix. Multiply by the screen height
// in pixels to get the sphere's diameter in pixels.
inline float ScreenFraction(float radius, float distance, float projScale)
{
    return radius * projScale / (std::max)(distance, 0.01f);
}

// Which level of detail to draw. `thresholds` holds levelCount - 1 screen fractions, biggest first: the object uses
// level k while it covers at least thresholds[k] of the screen, and level k + 1 below that.
// `current` is the level chosen last frame (or -1 for "none yet"). With it, changing level needs a margin
// (`hysteresis`, 0.1 = 10%), so an object sitting exactly on a boundary does not flicker between two levels while the
// camera jitters. The terrain does the same with its distances (Terrain::DrawChunks).
inline int SelectLod(float screen, const float* thresholds, int levelCount, int current = -1, float hysteresis = 0.1f)
{
    if (levelCount <= 1) return 0;
    const int last = (std::min)(levelCount, kMaxLodLevels) - 1;
    if (current < 0 || current > last) {
        int lod = 0;
        while (lod < last && screen < thresholds[lod]) ++lod;   // first time: the plain thresholds
        return lod;
    }
    int lod = current;
    while (lod < last && screen < thresholds[lod] * (1.0f - hysteresis)) ++lod;   // smaller on screen: coarser, once clearly past
    while (lod > 0 && screen > thresholds[lod - 1] * (1.0f + hysteresis)) --lod;  // bigger on screen: finer, once clearly past
    return lod;
}

// "Too small to be worth drawing": below this fraction of the screen the object is skipped entirely. 0 = never skip.
inline bool BelowCullSize(float screen, float cullScreen)
{
    return cullScreen > 0.0f && screen < cullScreen;
}

// The distance beyond which an object of `radius` metres is smaller than `screenFraction` of the screen. Used to turn a
// culling size into a metre distance (an early-out for whole groups of objects, and a number to show in the editor).
inline float DistanceForScreenFraction(float radius, float screenFraction, float projScale)
{
    return screenFraction > 1e-6f ? radius * projScale / screenFraction : 1.0e30f;
}

// Model files often carry their own levels of detail as separate meshes named "Pine_LOD0", "Pine_LOD1", "tree.lod_2"...
// Returns the number after the LAST "LOD" tag in `name` (0..99), or -1 if the name has none. A tag must not be glued to
// letters before it ("Cold1" is not a LOD tag), and the digits may follow a single '_', '.', ' ' or '-'.
inline int ParseLodIndex(const std::string& name)
{
    int found = -1;
    const size_t n = name.size();
    auto lower = [&](size_t k) { return static_cast<char>(std::tolower(static_cast<unsigned char>(name[k]))); };
    for (size_t i = 0; i + 3 <= n; ++i) {
        if (lower(i) != 'l' || lower(i + 1) != 'o' || lower(i + 2) != 'd') continue;
        if (i > 0 && std::isalpha(static_cast<unsigned char>(name[i - 1]))) continue;
        size_t k = i + 3;
        if (k < n && (name[k] == '_' || name[k] == '.' || name[k] == ' ' || name[k] == '-')) ++k;
        if (k >= n || !std::isdigit(static_cast<unsigned char>(name[k]))) continue;
        int value = 0;
        for (int digits = 0; k < n && digits < 2 && std::isdigit(static_cast<unsigned char>(name[k])); ++k, ++digits) {
            value = value * 10 + (name[k] - '0');
        }
        found = value;
    }
    return found;
}

// A range of instances [start, start + count) in an instance buffer that can be drawn with ONE draw call.
struct DrawRun {
    std::uint32_t start = 0;
    std::uint32_t count = 0;
};

// Adds a range to `runs`, merging it into the previous run when the two touch. Cells are stored in the instance buffer
// in grid order, so a row of neighbouring visible cells that share a level of detail becomes one draw call, not many.
inline void AppendRun(std::vector<DrawRun>& runs, std::uint32_t start, std::uint32_t count)
{
    if (count == 0) return;
    if (!runs.empty() && runs.back().start + runs.back().count == start) runs.back().count += count;
    else runs.push_back({ start, count });
}
