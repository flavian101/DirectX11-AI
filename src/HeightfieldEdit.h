#pragma once
// Sculpting the heightfield: brushes, "which samples did this stroke touch", and the patch that undoes a stroke.
// Pure logic like Heightfield.h (no Direct3D), so it is tested on its own: tests/heightfield_edit_test.cpp.
//
// Everything here works in the heightfield's LOCAL space and in grid indices. The terrain turns world positions into
// local ones before calling in (it knows its own position and scale), and turns the returned GridRect into "which
// chunks need new vertices".
#include "Heightfield.h"
#include <unordered_map>
#include <vector>

enum class BrushMode { Raise, Lower, Smooth, Flatten };

struct BrushSettings {
    BrushMode mode = BrushMode::Raise;
    float radius = 12.0f;        // local-space units (the terrain converts the world radius for you)
    float strength = 0.4f;       // 0..1, how fast the brush works: see the two rates below
    float falloff = 0.6f;        // 0 = full strength right to the edge, 1 = fades out from the very centre
    float targetHeight = 0.5f;   // Flatten only: the sample value (0..1) to level towards
};

// At strength 1 a Raise/Lower brush moves the ground by this fraction of the terrain's full height per second.
constexpr float kRaiseRatePerSecond = 0.25f;
// At strength 1 a Smooth/Flatten brush closes this fraction of the remaining gap per second (capped at all of it per step).
constexpr float kBlendRatePerSecond = 8.0f;

// A block of samples, inclusive on both ends. Empty when it ends before it starts.
struct GridRect {
    int x0 = 0, z0 = 0, x1 = -1, z1 = -1;
    bool IsEmpty() const { return x1 < x0 || z1 < z0; }
    void Include(int x, int z);                                  // grow to hold this sample
    GridRect Grown(int by, int maxX, int maxZ) const;            // `by` samples more on every side, kept inside 0..max
};

// How strongly the brush acts at `distance` from its centre: 1 in the middle band, then a smooth fade to 0 at `radius`.
float BrushWeight(float distance, float radius, float falloff);

// What a stroke changed: for every sample touched, its value before the stroke and after it.
struct HeightfieldPatch {
    std::vector<int> index;
    std::vector<float> before;
    std::vector<float> after;
    bool IsEmpty() const { return index.empty(); }
};

// Collects the "before" values while a stroke is in progress. Ask it to Remember() a sample BEFORE changing it; only the
// first call per sample counts, so the recorder always holds the value from before the whole stroke.
class StrokeRecorder {
public:
    void Clear() { before_.clear(); }
    bool IsEmpty() const { return before_.empty(); }
    void Remember(int index, float valueBeforeTheStroke);
    // The finished stroke. Samples that ended up where they started (raised, then lowered again) are left out.
    HeightfieldPatch Finish(const Heightfield& field) const;

private:
    std::unordered_map<int, float> before_;
};

// One step of a brush at a LOCAL position (x, z), lasting `seconds`. Returns the block of samples that changed (empty if
// none did). If a recorder is given, every sample is remembered just before it changes.
GridRect ApplyBrush(Heightfield& field, const BrushSettings& brush, float localX, float localZ, float seconds,
                    StrokeRecorder* recorder);

// Writes a stroke back: the "after" values (redo) or the "before" values (undo). Returns the block that changed.
GridRect ApplyPatch(Heightfield& field, const HeightfieldPatch& patch, bool useAfter);
