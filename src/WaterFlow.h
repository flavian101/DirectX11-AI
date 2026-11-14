#pragma once
// How a river flows and how foam lives (lesson 37). Plain C++ with no Direct3D: the SAME formulas are in
// shaders/water_ps.hlsl (keep them in step), so a test can check them on their own, and gameplay code can ask the water how
// fast it is running at a spot (Water::SampleFlow: a boat that drifts downstream).
#include <algorithm>
#include <cmath>

namespace WaterFlow {

constexpr float kBankDepth = 1.5f;        // shallower than this (world units) the bed drags the current
constexpr float kMinSpeedFactor = 0.08f;  // the water right at a bank still creeps along
constexpr float kPhaseCycle = 2.4f;       // seconds one layer of flowing ripples lives (see PhaseWeight)
constexpr int kTrailSamples = 3;          // how many earlier moments the crest-foam trail looks back to

inline float Smooth01(float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

// How fast the current runs at a spot, as a fraction of the river's flow speed (0.08 .. 1).
//   across         where the spot is across the river, -1 .. 1 (0 = the middle of the plane, +-1 = its sides)
//   depth          how deep the water is there, world units (a bank is shallow)
//   bankDrag       0 = the bed does not slow the water, 1 = at a bank it crawls
//   channelProfile 0 = the same speed across the whole plane, 1 = the sides of the plane are much slower than the middle
// Water drags on its bed and banks, so a real river is fastest in its deep middle.
inline float SpeedFactor(float across, float depth, float bankDrag, float channelProfile)
{
	bankDrag = std::clamp(bankDrag, 0.0f, 1.0f);
	channelProfile = std::clamp(channelProfile, 0.0f, 1.0f);
	const float c = std::clamp(across, -1.0f, 1.0f);
	const float middle = 1.0f - c * c;
	const float byDepth = (1.0f - 0.92f * bankDrag) + (0.92f * bankDrag) * Smooth01(depth / kBankDepth);
	const float byWidth = (1.0f - 0.8f * channelProfile) + (0.8f * channelProfile) * middle;
	return std::max(byDepth * byWidth, kMinSpeedFactor);
}

// A flowing pattern is drawn as TWO layers that live half a cycle apart. `fraction` is how far through its life a layer is
// (0 .. 1: the water carries it downstream by speed * kPhaseCycle * fraction), `weight` is how visible it is: 0 at the moment
// it is reset to its start, 1 half way. The two weights always add up to 1.
inline void PhaseWeight(float time, int layer, float& fraction, float& weight)
{
	const float x = time / kPhaseCycle + 0.5f * static_cast<float>(layer);
	fraction = x - std::floor(x);
	weight = 1.0f - std::abs(2.0f * fraction - 1.0f);
}

// Blending two unrelated layers lowers the contrast of the result by sqrt(w0^2 + w1^2) (0.71 at the worst moment, 1 when one
// layer is alone): dividing by it keeps the ripples equally strong all the time.
inline float ContrastRestore(float w0, float w1)
{
	return 1.0f / std::sqrt(std::max(w0 * w0 + w1 * w1, 1e-4f));
}

// Crest foam leaves a trail. Foam at a parcel of water is the squeeze it felt a moment ago, a little weaker for every step of
// age: sample k (1 .. kTrailSamples) looks back k / kTrailSamples of the trail time and counts this much.
inline float TrailWeight(int k)
{
	return std::exp(-1.9f * static_cast<float>(k) / static_cast<float>(kTrailSamples));
}
inline float TrailTime(float now, float trailSeconds, int k)
{
	return now - trailSeconds * static_cast<float>(k) / static_cast<float>(kTrailSamples);
}

}   // namespace WaterFlow
