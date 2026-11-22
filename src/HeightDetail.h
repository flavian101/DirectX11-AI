#pragma once
// DETAIL on top of the base ground, in METRES (roadmap H5). Plain maths: no Direct3D. Tests: tests/height_detail_test.cpp.
//
// A picture is rarely detailed enough at the scale a player stands at. Detail adds small bumps on top of it. It is a pure function of the
// world position and the settings, so any tile, at any LOD, in any order, gets the same value for the same spot.
//
// What it promises, and the old version did not: it is ADDED (never clamped, so it cannot flatten the ground), it is in metres, it never
// exceeds +/- strength, it has no feature smaller than wavelength / 2^(octaves-1), and it averages out to about zero, so adding it does
// not lift or sink the terrain. (The old "valley mask" is not carried over.)
#include <cstdint>

struct HeightDetailSettings {
    float strength = 0.0f;       // metres: the detail never goes above +strength or below -strength. 0 turns it off
    float wavelength = 64.0f;    // metres: the size of the biggest bumps
    int octaves = 4;             // how many halvings of size (each with half the height); 1 .. 8
    int seed = 1;                // another seed gives other bumps in the same places
    bool ridged = false;         // sharp crests and rounded hollows instead of rounded both ways

    static constexpr float kStrengthMax = 1000.0f;
    static constexpr float kWavelengthMin = 0.5f, kWavelengthMax = 100000.0f;
    static constexpr int kOctavesMin = 1, kOctavesMax = 8;
    bool IsIdentity() const { return !(strength > 0.0f); }
    void Clamp();
};

// The detail height at a world position, in metres: exactly 0.0 when the settings are the identity.
double DetailMetres(const HeightDetailSettings& settings, double worldX, double worldZ);
