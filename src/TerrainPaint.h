#pragma once
// Painted terrain layers (lesson 30): the artist's own layer weights, laid OVER the weights the rules bake (lesson 29).
// Pure logic (no Direct3D), like TerrainSplat.h and HeightfieldEdit.h, so it is tested on its own:
// tests/terrain_paint_test.cpp.
//
// THE IDEA. The rules answer "what is the ground made of here?" from height and slope. An artist wants to overrule them:
// "a sand path here", "rock there". Painting does not replace the rules, it adds to them. Every texel of the PaintMap
// holds four numbers, how much of each layer was painted there, and their SUM is the texel's coverage c (0 = untouched,
// 1 = fully painted). The final weights are
//         final = baked * (1 - c) + painted
// which adds up to (1 - c) + c = 1 again. Untouched ground keeps the rules; fully painted ground ignores them; erasing
// lowers c and the rules come back. The shader never knows: it reads the final splat map, as before.
#include "HeightfieldEdit.h"
#include "TerrainSplat.h"
#include <cstdint>
#include <vector>

// At strength 1 a brush closes this fraction of the remaining gap per second (the same idea as Smooth/Flatten).
constexpr float kPaintRatePerSecond = 6.0f;

struct PaintBrush {
    int layer = 0;            // 0 grass, 1 rock, 2 sand, 3 snow (the splat's channel order)
    bool erase = false;       // true: take painted weight away instead of adding it
    float radius = 12.0f;     // local-space units (the terrain converts the world radius for you)
    float strength = 0.5f;    // 0..1
    float falloff = 0.5f;     // 0 = hard edge, 1 = fades from the centre (see BrushWeight)
};

// One texel per grid CELL, like the splat map it is laid over. Four floats per texel, in the splat's channel order.
// Floats, not bytes: a gentle brush adds a tiny amount per frame, which a byte would round away.
struct PaintMap {
    int width = 0, height = 0;
    std::vector<float> weights;   // row by row, 4 per texel; each texel's four add up to at most 1

    bool IsEmpty() const { return weights.empty(); }
    void Resize(int w, int h) { width = w; height = h; weights.assign(static_cast<size_t>(w) * h * kSplatChannels, 0.0f); }
    void Clear() { weights.assign(weights.size(), 0.0f); }
    size_t TexelIndex(int x, int z) const { return static_cast<size_t>(z) * width + x; }
};

// One step of a brush at a LOCAL position, lasting `seconds`. `layout` is the heightfield's (only its size is used: the
// map covers the whole field, one texel per cell). Returns the block of TEXELS that changed. If a recorder is given,
// every float is remembered (index = texel * 4 + channel) just before it changes.
GridRect ApplyPaintBrush(PaintMap& map, const HeightfieldLayout& layout, const PaintBrush& brush, float localX, float localZ,
                         float seconds, StrokeRecorder* recorder);

// Writes a stroke back (undo = "before" values, redo = "after"). Returns the block of texels that changed.
GridRect ApplyPaintPatch(PaintMap& map, const HeightfieldPatch& patch, bool useAfter);

// Lays the painted weights over `splat` for the texels in `texels` (inclusive; clamped to the map). The splat must have
// been baked for those texels just before: the overlay reads the baked bytes and writes the final ones in place.
// Texels with no paint are left as baked.
void ApplyPaint(const PaintMap& paint, const GridRect& texels, SplatMap& splat);
void ApplyPaintAll(const PaintMap& paint, SplatMap& splat);

// Saving and loading: one byte per float, round(weight * 255), four per texel (an RGBA image).
std::vector<std::uint8_t> QuantisePaint(const PaintMap& paint);
// `rgba` holds width * height * 4 bytes. Resizes the map and fills it. If a texel's bytes add up to more than 1.0 (they can
// by rounding) it is scaled back so coverage never exceeds 1.
void PaintFromBytes(const std::uint8_t* rgba, int width, int height, PaintMap& paint);
