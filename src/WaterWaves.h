#pragma once
// The water's wave table, built on the CPU and uploaded to the shader (shaders/waves.hlsli reads it).
// Plain C++ with no Direct3D, so the SAME numbers drive the GPU surface and gameplay code that needs the
// height of the water (floating objects, swimming, splashes). See Water::SampleHeight.
//
// The surface is a sum of GERSTNER waves. A plain sine wave only moves points up and down, which gives
// round, symmetric humps. A Gerstner wave also pulls points sideways toward the crest, so crests get
// narrow and sharp and troughs get wide and flat, like real water.
//
//     rest position (x, z)  ->  phase = k * dot(dir, (x, z)) + omega * time + phase0
//     displaced x, z        =  rest + Q * a * cos(phase) * dir
//     height                =  a * sin(phase)
#include <algorithm>
#include <cmath>

namespace WaterWaves {

constexpr int kMaxWaves = 8;
constexpr float kBaseFrequency = 0.6f;   // radians per world unit of the first wave; matches WaterSettings' default

// --- Waves near a shore (the same numbers are in shaders/waves.hlsli) ---------------------------------------------
// A wave that reaches shallow water first GROWS taller (the same energy is squeezed into less water), then cannot grow
// any more: it is limited to a fraction of the depth under it, and so it dies away to nothing at the water's edge.
constexpr float kShoalGrowth = 0.5f;   // a wave can become up to 50% taller in shallow water (at full strength)
constexpr float kBreakRatio = 0.4f;    // a wave's AMPLITUDE never exceeds this fraction of the depth (breaking: wave height ~ 0.8 x depth)
constexpr float kDeepWater = 1000.0f;  // "no depth information": deep enough that nothing changes

// The amplitude a wave of nominal `amplitude` and wave number `k` has where the water is `depth` deep.
// `strength` 0 = off (the open-water amplitude, always), 1 = full effect.
inline float ShoalAmplitude(float amplitude, float k, float depth, float strength)
{
	if (strength <= 0.0f) return amplitude;
	depth = std::max(depth, 0.0f);
	// Shallow = the depth is small compared with the wavelength: k * depth below ~pi (depth < half a wavelength).
	// Smoothly 1 in very shallow water and 0 in deep water.
	const float kd = k * depth;
	const float t = std::clamp((kd - 0.5f) / (3.14159265f - 0.5f), 0.0f, 1.0f);
	const float shallow = 1.0f - t * t * (3.0f - 2.0f * t);
	const float grown = amplitude * (1.0f + kShoalGrowth * shallow * strength);
	const float limit = kBreakRatio * depth;
	if (limit < 1e-5f) return 0.0f;
	// tanh: equal to `grown` while it is far below the limit, and never above the limit. Smooth, so no kink.
	const float limited = limit * std::tanh(grown / limit);
	return amplitude + (limited - amplitude) * std::min(strength, 1.0f);
}

struct Params {
	float amplitude = 0.35f;     // height of the biggest wave, world units
	float speed = 1.0f;          // 1 = default pace
	float frequency = 0.6f;      // radians per world unit of the biggest wave
	float windDegrees = 0.0f;    // direction the waves travel toward (angle in the XZ plane)
	float spread = 0.7f;         // 0 = every wave runs the same way, 1 = fully scattered
	float choppiness = 0.5f;     // 0 = rounded sine waves, 1 = sharpest crests that never fold over
	int count = 6;               // how many of the 8 waves are used
	float shoreWaves = 0.0f;     // 0 = waves are the same everywhere; 1 = they grow, then break and die as the water gets shallow
	float flowSpeed = 0.0f;      // > 0: a RIVER. Every wave is carried along by the current at this speed (world units per second)
};

struct Wave {
	float dirX = 1.0f, dirZ = 0.0f;   // unit direction
	float k = 0.6f;                   // radians per world unit (2*pi / wavelength)
	float amplitude = 0.0f;           // world units
	float omega = 1.0f;               // radians per second
	float steepness = 0.0f;           // Q: how far this wave pulls points toward its crest, as a fraction of its amplitude
	float phase0 = 0.0f;
};

struct Table {
	Wave waves[kMaxWaves];
	int count = 0;
	float heightBound = 0.0f;   // no point of the surface moves farther than this from rest, in any direction
};

inline Table Build(const Params& p)
{
	// Relative to the first wave. The first FOUR are the original look; waves 5..8 add finer detail and fill gaps
	// in the spectrum. Angles are offsets from the wind, scaled by `spread`.
	static const float kAngleOffset[kMaxWaves] = { 0.0f, 45.0f, 107.5f, -59.5f, 22.0f, -25.0f, 70.0f, -85.0f };
	static const float kFreqScale[kMaxWaves]   = { 1.0f, 1.7f, 2.9f, 4.3f, 1.3f, 2.2f, 3.6f, 5.6f };
	static const float kAmpScale[kMaxWaves]    = { 1.0f, 0.55f, 0.3f, 0.15f, 0.4f, 0.22f, 0.12f, 0.07f };
	static const float kPhase[kMaxWaves]       = { 0.0f, 1.3f, 2.9f, 4.1f, 5.2f, 0.7f, 3.6f, 2.2f };
	constexpr float kDegToRad = 0.01745329252f;

	Table t;
	t.count = std::clamp(p.count, 1, kMaxWaves);
	float sumKA = 0.0f;
	for (int i = 0; i < t.count; ++i) {
		Wave& w = t.waves[i];
		const float angle = (p.windDegrees + p.spread * kAngleOffset[i]) * kDegToRad;
		// `dir` points to where the crests come FROM, so with phase = k*dir.x - ... + omega*time the pattern travels
		// toward the wind. (A wave sin(k*d*x + omega*t) moves toward -d.)
		w.dirX = -std::cos(angle);
		w.dirZ = -std::sin(angle);
		w.k = p.frequency * kFreqScale[i];
		w.amplitude = p.amplitude * kAmpScale[i];
		// Deep-water dispersion: long waves travel faster than short ones (omega ~ sqrt(k)).
		// A river is different: the pattern is carried by the current, so every wave moves at the SAME speed omega / k.
		w.omega = p.flowSpeed > 0.0f ? w.k * p.flowSpeed : p.speed * std::sqrt(w.k / kBaseFrequency);
		w.phase0 = kPhase[i];
		sumKA += w.k * w.amplitude;
		// Waves can grow in shallow water: the bound (used to cull and to normalise the shader's crest height) must hold them.
		t.heightBound += w.amplitude * (1.0f + kShoalGrowth * std::clamp(p.shoreWaves, 0.0f, 1.0f));
	}
	// Crests fold over (the surface cuts through itself) once the sum of Q*k*a passes 1. Scale Q so the sum never
	// exceeds `choppiness` (<= 1), but never above 1 per wave: small waves must not be stretched sideways.
	const float q = std::clamp(p.choppiness, 0.0f, 1.0f) * std::min(1.0f, 1.0f / std::max(sumKA, 1e-4f));
	for (int i = 0; i < t.count; ++i) t.waves[i].steepness = q;
	return t;
}

// Which way a river flows in the WORLD, as an angle (0 = +X, 90 = +Z), given its flow angle in the body's OWN space
// (0 = along the body's +Z axis) and the body's yaw (rotation about Y, radians, DirectXMath's left-handed convention).
// A local direction (sin a, cos a) in (x, z) is turned by the yaw into (sin(a + yaw), cos(a + yaw)), which is the angle
// 90 - (a + yaw) in the editor's convention.
inline float RiverTravelDegrees(float flowAngleDegrees, float yawRadians)
{
	const float degrees = 90.0f - flowAngleDegrees - yawRadians * 57.29577951f;
	const float wrapped = std::fmod(degrees, 360.0f);
	return wrapped < 0.0f ? wrapped + 360.0f : wrapped;
}

// Where a point that rests at (x, z) is at `time`: sideways offset (dx, dz) and height dy.
// `depth` and `shoreWaves` are the shore effect: how deep the water is under the REST position, and how strongly the waves
// react to it (0 = not at all). The height follows the shoaled amplitude, but the sideways pull never exceeds what the
// open-water wave had, so a wave that grows in shallow water can never fold over.
inline void Displacement(const Table& t, float x, float z, float time, float& dx, float& dy, float& dz,
						 float depth = kDeepWater, float shoreWaves = 0.0f)
{
	dx = dy = dz = 0.0f;
	for (int i = 0; i < t.count; ++i) {
		const Wave& w = t.waves[i];
		const float a = ShoalAmplitude(w.amplitude, w.k, depth, shoreWaves);
		const float aSide = std::min(a, w.amplitude);
		const float phase = w.k * (w.dirX * x + w.dirZ * z) + w.omega * time + w.phase0;
		const float s = std::sin(phase), c = std::cos(phase);
		dx += w.steepness * aSide * c * w.dirX;
		dz += w.steepness * aSide * c * w.dirZ;
		dy += a * s;
	}
}

// Height of the surface ABOVE the water's rest level, at a WORLD position. The mesh is displaced sideways as well
// as up, so the point that ends up at (x, z) started somewhere else: a few fixed-point steps find where.
inline float HeightAt(const Table& t, float x, float z, float time)
{
	float rx = x, rz = z;
	float dx, dy, dz;
	for (int i = 0; i < 4; ++i) {
		Displacement(t, rx, rz, time, dx, dy, dz);
		rx = x - dx;
		rz = z - dz;
	}
	Displacement(t, rx, rz, time, dx, dy, dz);
	return dy;
}

// The same, near a shore: `depthAt(restX, restZ)` says how deep the water is under a REST position (world space).
template <class DepthFn>
inline float HeightAtShore(const Table& t, float x, float z, float time, float shoreWaves, DepthFn&& depthAt)
{
	float rx = x, rz = z;
	float dx, dy, dz;
	for (int i = 0; i < 4; ++i) {
		Displacement(t, rx, rz, time, dx, dy, dz, depthAt(rx, rz), shoreWaves);
		rx = x - dx;
		rz = z - dz;
	}
	Displacement(t, rx, rz, time, dx, dy, dz, depthAt(rx, rz), shoreWaves);
	return dy;
}

}   // namespace WaterWaves
