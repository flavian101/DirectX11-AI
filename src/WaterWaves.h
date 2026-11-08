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

struct Params {
	float amplitude = 0.35f;     // height of the biggest wave, world units
	float speed = 1.0f;          // 1 = default pace
	float frequency = 0.6f;      // radians per world unit of the biggest wave
	float windDegrees = 0.0f;    // direction the waves travel toward (angle in the XZ plane)
	float spread = 0.7f;         // 0 = every wave runs the same way, 1 = fully scattered
	float choppiness = 0.5f;     // 0 = rounded sine waves, 1 = sharpest crests that never fold over
	int count = 6;               // how many of the 8 waves are used
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
		t.heightBound += w.amplitude;
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
inline void Displacement(const Table& t, float x, float z, float time, float& dx, float& dy, float& dz)
{
	dx = dy = dz = 0.0f;
	for (int i = 0; i < t.count; ++i) {
		const Wave& w = t.waves[i];
		const float phase = w.k * (w.dirX * x + w.dirZ * z) + w.omega * time + w.phase0;
		const float s = std::sin(phase), c = std::cos(phase);
		dx += w.steepness * w.amplitude * c * w.dirX;
		dz += w.steepness * w.amplitude * c * w.dirZ;
		dy += w.amplitude * s;
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

}   // namespace WaterWaves
