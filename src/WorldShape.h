#pragma once
// SHAPING THE GROUND of a world: the controls that turn "a picture" into "good-looking terrain". Plain maths: no Direct3D.
// Tests: tests/world_shape_test.cpp.   Roadmap: docs/ROADMAP.md, "Heightmap customisation".
//
// WHY A PICTURE LOOKS FLAT. Three usual reasons, and each gets a control:
//   1. The picture uses a narrow band of grey (say 0.35 .. 0.45): the whole terrain then spans 10% of the height scale.
//      -> LEVELS (blackPoint / whitePoint) stretch the band to the full range; "Auto levels" finds the band for you.
//   2. The picture is soft or low-resolution (8-bit steps show as terraces, a small picture stretched over kilometres shows
//      as facets). -> SMOOTHING blurs the picture, BICUBIC interpolation replaces the straight-line blend between pixels,
//      and DETAIL NOISE adds fine bumps (in metres) that a picture cannot hold.
//   3. The curve is wrong: most pictures have many mid-greys, so most ground sits at mid-height and nothing rises.
//      -> CURVE (an exponent) pushes the middle down and keeps the peaks: > 1 gives flat valleys with sharp mountains,
//      < 1 gives broad highlands.
// Everything is a function of the WORLD position (like every WorldHeightSource), so tiles still meet without seams.
#include "WorldGrid.h"
#include "WorldPicture.h"
#include <memory>

struct HeightShapeSettings {
    // --- levels and curve (applied to every height, 0..1) ---
    float blackPoint = 0.0f;   // picture values at or below this become 0
    float whitePoint = 1.0f;   // picture values at or above this become 1 (must be above blackPoint)
    float curve = 1.0f;        // exponent applied after the levels: 1 = as is, > 1 flatter lowlands and sharper peaks, < 1 broader highlands
    bool invert = false;       // swap high and low (a crater becomes a hill)

    // --- picture quality (only the picture source uses these) ---
    float smoothing = 0.0f;    // blur radius in PIXELS of the picture (0 = none); removes the steps of an 8-bit image
    bool bicubic = false;      // smooth interpolation between pixels instead of straight lines; reads as less faceted when the picture is stretched
    bool prefilter = true;     // average the picture over one cell before sampling it (mip pyramid), when a cell covers more than one pixel: no stairs on thin ridges (T1)

    // --- detail noise: fine bumps added on top, strength in METRES so it does not change when the height scale does ---
    float detailStrength = 0.0f;      // metres of up and down (0 = none)
    float detailWavelength = 60.0f;   // metres: the size of the biggest detail bump
    int detailOctaves = 4;
    int detailSeed = 7;
    bool detailRidged = false;        // sharp creases (rock, mountains) instead of round bumps
    float detailValleyMask = 0.5f;    // 0 = detail everywhere, 1 = none in the lowest ground (keeps shores and riverbeds smooth)

    static constexpr float kDetailStrengthMax = 200.0f;
    static constexpr float kDetailWavelengthMin = 4.0f, kDetailWavelengthMax = 2000.0f;
    static constexpr float kSmoothingMax = 16.0f;
    static constexpr float kCurveMin = 0.25f, kCurveMax = 4.0f;
    static constexpr float kMinLevelGap = 0.01f;

    void Clamp();
    // No levels, curve, detail or inversion: the picture's own heights. (smoothing and bicubic are quality settings of the picture
    // source, not part of this test.)
    bool IsIdentity() const;
};

// A few starting points for the controls. 0 natural (the picture as it is), 1 rolling hills, 2 mountains, 3 plains with relief.
constexpr int kHeightShapePresetCount = 4;
const char* HeightShapePresetName(int preset);
HeightShapeSettings HeightShapePreset(int preset);

// Levels, invert and curve for one height (0..1 in, 0..1 out).
float ShapeHeight01(float height, const HeightShapeSettings& settings);

// Wraps any height source and shapes it. `heightScaleMetres` turns the detail strength (metres) into a 0..1 amplitude.
class ShapedHeightSource final : public WorldHeightSource {
public:
    ShapedHeightSource(std::shared_ptr<const WorldHeightSource> base, const HeightShapeSettings& settings, float heightScaleMetres);
    float Height01(double worldX, double worldZ) const override;
private:
    std::shared_ptr<const WorldHeightSource> base_;
    HeightShapeSettings settings_;
    float detailAmplitude_ = 0.0f;   // 0..1 units
    FractalHeightSource detail_;
};

// --- looking at a picture ---------------------------------------------------------------------------------------------
struct HeightImageStats {
    float minimum = 0.0f, maximum = 1.0f;
    float low = 0.0f, high = 1.0f;        // the 0.5% and 99.5% points: what the picture "really" spans, ignoring a few stray pixels
    float meanGradient = 0.0f;            // average absolute difference between neighbouring pixels (0..1 per pixel)
    float UsedRange() const { return high - low; }   // 1 = the picture uses all the grey it has
};
HeightImageStats ComputeHeightImageStats(const HeightImage& image);

// Levels that stretch the picture's real span (the `lowPercent` and `highPercent` points of its histogram) to 0..1.
// Returns false (and leaves the outputs alone) for an empty picture or one that is a single flat grey.
bool ComputeAutoLevels(const HeightImage& image, float lowPercent, float highPercent, float& blackPoint, float& whitePoint);

// A blurred copy (box blur, applied twice, which is close to a Gaussian). radius <= 0 returns an unchanged copy. Edges repeat.
HeightImage SmoothHeightImage(const HeightImage& image, int radiusPixels);

// The slope (degrees) of the steepest typical ground: a quick way to tell the user "this looks flat because the typical slope is 3 degrees".
// `gradient` is a 0..1 per-pixel difference, `heightScaleMetres` and `metresPerPixel` give it units.
float SlopeDegrees(float gradient, float heightScaleMetres, float metresPerPixel);
