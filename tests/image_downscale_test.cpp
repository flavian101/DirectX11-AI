// BoxDownscale (the Assets panel's thumbnails): sizes, averaging, never enlarging, bad input.
#include "ImageDownscale.h"
#include <cstdio>
#include <cstdlib>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL line %d: %s\n", __LINE__, #cond); ++failures; } } while (0)

int main()
{
    std::vector<unsigned char> out;
    int w = 0, h = 0;

    // A 4x2 image: left half black, right half white. Down to 2x1: one black pixel, one white.
    const unsigned char halves[4 * 2 * 4] = {
        0, 0, 0, 255,   0, 0, 0, 255,   255, 255, 255, 255,   255, 255, 255, 255,
        0, 0, 0, 255,   0, 0, 0, 255,   255, 255, 255, 255,   255, 255, 255, 255 };
    CHECK(BoxDownscale(halves, 4, 2, 2, out, w, h));
    CHECK(w == 2 && h == 1);
    CHECK(out[0] == 0 && out[4] == 255 && out[3] == 255);

    // Averaging: a checkerboard of 0 and 254 shrinks to one pixel of 127.
    const unsigned char checker[2 * 2 * 4] = { 0,0,0,255, 254,254,254,255, 254,254,254,255, 0,0,0,255 };
    CHECK(BoxDownscale(checker, 2, 2, 1, out, w, h));
    CHECK(w == 1 && h == 1 && out[0] == 127);

    // The longer side decides, and the shape is kept.
    std::vector<unsigned char> wide(400 * 100 * 4, 200);
    CHECK(BoxDownscale(wide.data(), 400, 100, 128, out, w, h));
    CHECK(w == 128 && h == 32 && out.size() == 128u * 32u * 4u && out[0] == 200);

    // Never enlarges.
    CHECK(BoxDownscale(checker, 2, 2, 64, out, w, h));
    CHECK(w == 2 && h == 2 && out.size() == 16u);

    // A very thin image keeps at least one pixel on the short side.
    std::vector<unsigned char> thin(1000 * 2 * 4, 9);
    CHECK(BoxDownscale(thin.data(), 1000, 2, 100, out, w, h));
    CHECK(w == 100 && h >= 1);

    // Bad input.
    CHECK(!BoxDownscale(nullptr, 2, 2, 64, out, w, h));
    CHECK(!BoxDownscale(checker, 0, 2, 64, out, w, h));
    CHECK(!BoxDownscale(checker, 2, 2, 0, out, w, h));

    if (failures == 0) std::printf("ImageDownscale tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
