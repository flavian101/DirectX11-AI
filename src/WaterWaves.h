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
#include <cstdint>

namespace WaterWaves {

constexpr int kMaxWaves = 12;   // must equal WATER_MAX_WAVES in shaders/water_material.hlsli
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

// How the table is made. Classic is the original fixed list of eight waves (kept so old scenes can look as they did);
// Spectrum builds the waves from a sea model: a wind sea whose waves get shorter and lower together, plus a long swell.
enum class Style : int { Classic = 0, Spectrum = 1 };

struct Params {
	float amplitude = 0.35f;     // height of the biggest wave, world units
	float speed = 1.0f;          // 1 = default pace
	float frequency = 0.6f;      // radians per world unit of the biggest wave
	float windDegrees = 0.0f;    // direction the waves travel toward (angle in the XZ plane)
	float spread = 0.7f;         // 0 = every wave runs the same way, 1 = fully scattered
	float choppiness = 0.5f;     // 0 = rounded sine waves, 1 = sharpest crests that never fold over
	int count = 6;               // how many of the kMaxWaves waves are used
	float shoreWaves = 0.0f;     // 0 = waves are the same everywhere; 1 = they grow, then break and die as the water gets shallow
	float flowSpeed = 0.0f;      // > 0: a RIVER. Every wave is carried along by the current at this speed (world units per second)

	// --- Spectrum style only (Classic ignores all of these) ---
	Style style = Style::Spectrum;
	float roughness = 0.5f;      // 0 = the small waves die away fast (glassy), 1 = lots of small waves (rough)
	float swell = 0.0f;          // 0 = no swell; 1 = a swell nearly as tall as the biggest wind wave
	float swellAngle = 35.0f;    // degrees: the swell's direction, measured from the wind
	float swellLength = 2.5f;    // how much longer the swell's wavelength is than the biggest wind wave's
	float groups = 0.0f;         // 0 = even waves everywhere; 1 = the sea breathes: patches of calm and rough water (wave groups)
	std::uint32_t seed = 1;      // another seed = another sea with the same character
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
	// Wave groups: a slow pattern that only ever turns the waves DOWN (a factor between 1 - groupStrength and 1), so
	// the bounds above and the no-fold-over guarantee still hold. 0 = off. See GroupFactor.
	float groupStrength = 0.0f;
	float groupK = 0.08f;       // radians per world unit of the pattern's main component
	float groupSpeed = 0.0f;    // radians per second of that component
};

// The wave-group factor at a REST position. The same two crossing slow waves in shaders/waves.hlsli: keep them in step.
// It multiplies every wave's height and sideways pull at that spot, so the sea is rough in some places and calm in others.
inline float GroupFactor(const Table& t, float x, float z, float time)
{
	if (t.groupStrength <= 0.0f) return 1.0f;
	const float p1 = t.groupK * (0.8f * x + 0.6f * z) + t.groupSpeed * time + 0.7f;
	const float p2 = 1.37f * t.groupK * (-0.6f * x + 0.8f * z) + 1.37f * t.groupSpeed * time + 2.1f;
	return 1.0f - t.groupStrength * (0.5f + 0.5f * std::sin(p1) * std::sin(p2));   // in [1 - groupStrength, 1]
}

// A small repeatable random generator: the same seed gives the same sea on every machine and every run.
struct Random {
	std::uint32_t state;
	explicit Random(std::uint32_t seed) : state(seed * 2654435761u + 0x9E3779B9u) { Next(); Next(); }
	std::uint32_t Next() { state = state * 1664525u + 1013904223u; return state; }
	float Unit() { return static_cast<float>(Next() >> 8) / 16777216.0f; }                 // [0, 1)
	float Signed() { return Unit() * 2.0f - 1.0f; }                                         // [-1, 1)
};

inline Table BuildClassic(const Params& p)
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

// The sea built from a model instead of a fixed list.
//
//  * WIND SEA: the first waves. Each is shorter than the one before (wavelength shrinks by a steady factor, each nudged
//    by a seeded amount so no two ratios repeat and the pattern never visibly tiles), and LOWER in step with a power law
//    (height ~ k^-e): that is how a real sea's energy falls with wavelength. The dominant wave points straight along the
//    wind; shorter waves scatter wider around it (`spread`).
//  * SWELL: one to three long, low waves from their own direction (`swellAngle` from the wind). They are what makes a
//    sea heave slowly under the small waves, and what a boat visibly rides.
//  * Each wave gets its own steepness: the swell is rounder than the wind waves, and the total stays under the
//    fold-over limit exactly as before.
inline Table BuildSpectrum(const Params& p)
{
	constexpr float kDegToRad = 0.01745329252f;
	constexpr float kTwoPi = 6.28318530718f;
	Table t;
	t.count = std::clamp(p.count, 1, kMaxWaves);
	Random random(p.seed);

	const bool river = p.flowSpeed > 0.0f;   // a current has one direction: no cross swell
	int swellCount = (p.swell > 0.0f && !river && t.count >= 2) ? std::clamp(static_cast<int>(std::lround(t.count * 0.25f)), 1, 3) : 0;
	swellCount = std::min(swellCount, t.count - 1);
	const int windCount = t.count - swellCount;

	// Heights are scaled up a little so that, with wave groups turning some places down, the AVERAGE height is what the
	// user asked for and the groups can only make patches calmer than that, never taller than the scaled value.
	const float groups = std::clamp(p.groups, 0.0f, 0.9f);
	const float groupBoost = 1.0f / (1.0f - 0.5f * groups);

	const float exponent = 1.7f - 0.9f * std::clamp(p.roughness, 0.0f, 1.0f);   // 1.7 (glassy) .. 0.8 (rough); 1.25 is the old table
	const float ratio = 1.38f;                                                  // each wave is this much finer than the last

	float weightedKA = 0.0f;   // sum of weight * k * a, for the steepness below
	float weight[kMaxWaves];
	int index = 0;
	for (int i = 0; i < windCount; ++i, ++index) {
		Wave& w = t.waves[index];
		const float kScale = (i == 0) ? 1.0f : std::pow(ratio, static_cast<float>(i)) * (1.0f + 0.10f * random.Signed());
		w.k = p.frequency * kScale;
		w.amplitude = p.amplitude * std::pow(kScale, -exponent) * groupBoost;
		// Direction: the dominant wave dead on the wind; each finer wave scatters wider, alternating sides, with a seeded nudge.
		const float side = (i % 2 == 0) ? 1.0f : -1.0f;
		const float reach = windCount > 1 ? static_cast<float>(i) / static_cast<float>(windCount) : 0.0f;
		const float offset = i == 0 ? 0.0f : side * std::clamp(reach + 0.15f * random.Signed(), 0.05f, 1.0f);
		const float angle = (p.windDegrees + p.spread * 95.0f * offset) * kDegToRad;
		w.dirX = -std::cos(angle);   // the direction the crests come FROM (see BuildClassic)
		w.dirZ = -std::sin(angle);
		w.phase0 = random.Unit() * kTwoPi;
		weight[index] = 1.0f;
	}
	for (int j = 0; j < swellCount; ++j, ++index) {
		Wave& w = t.waves[index];
		const float lengthen = std::max(p.swellLength, 1.2f) * (1.0f + 0.37f * static_cast<float>(j));
		w.k = p.frequency / lengthen;
		w.amplitude = p.amplitude * std::clamp(p.swell, 0.0f, 1.0f) * 0.9f / (1.0f + 0.6f * static_cast<float>(j)) * groupBoost;
		const float angle = (p.windDegrees + p.swellAngle + 10.0f * random.Signed()) * kDegToRad;
		w.dirX = -std::cos(angle);
		w.dirZ = -std::sin(angle);
		w.phase0 = random.Unit() * kTwoPi;
		weight[index] = 0.5f;   // rounder crests than the wind waves
	}

	for (int i = 0; i < t.count; ++i) {
		Wave& w = t.waves[i];
		// Deep-water dispersion (long waves are faster), or, in a river, the pattern is carried by the current.
		w.omega = river ? w.k * p.flowSpeed : p.speed * std::sqrt(w.k / kBaseFrequency);
		weightedKA += weight[i] * w.k * w.amplitude;
		t.heightBound += w.amplitude * (1.0f + kShoalGrowth * std::clamp(p.shoreWaves, 0.0f, 1.0f));
	}
	// Steepness: sum over waves of Q * k * a must stay <= choppiness (<= 1) or crests fold over. With Q_i = weight_i * c * m
	// (m = min(1, 1 / sum of weight * k * a)) the sum is exactly c * m * sum(weight * k * a) <= c, and no Q exceeds 1.
	const float c = std::clamp(p.choppiness, 0.0f, 1.0f);
	const float m = std::min(1.0f, 1.0f / std::max(weightedKA, 1e-4f));
	for (int i = 0; i < t.count; ++i) t.waves[i].steepness = weight[i] * c * m;

	// Wave groups: patches about eight base wavelengths across, drifting slowly (or with the current).
	t.groupStrength = groups;
	t.groupK = p.frequency * 0.13f;
	t.groupSpeed = t.groupK * (river ? p.flowSpeed : 0.8f * p.speed);
	return t;
}

inline Table Build(const Params& p)
{
	return p.style == Style::Classic ? BuildClassic(p) : BuildSpectrum(p);
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
	const float group = GroupFactor(t, x, z, time);   // 1 = full waves; lower in the calm patches of a sea with wave groups
	for (int i = 0; i < t.count; ++i) {
		const Wave& w = t.waves[i];
		const float a = ShoalAmplitude(w.amplitude, w.k, depth, shoreWaves) * group;
		const float aSide = std::min(ShoalAmplitude(w.amplitude, w.k, depth, shoreWaves), w.amplitude) * group;
		const float phase = w.k * (w.dirX * x + w.dirZ * z) + w.omega * time + w.phase0;
		const float s = std::sin(phase), c = std::cos(phase);
		dx += w.steepness * aSide * c * w.dirX;
		dz += w.steepness * aSide * c * w.dirZ;
		dy += a * s;
	}
}

// How many fixed-point steps find where a surface point started. Each step shrinks the error by about the choppiness, so
// 6 steps keep it under a centimetre at normal choppiness and under 5 mm at the very sharpest (4 steps allowed 2 cm).
constexpr int kInversionSteps = 6;

// Height of the surface ABOVE the water's rest level, at a WORLD position. The mesh is displaced sideways as well
// as up, so the point that ends up at (x, z) started somewhere else: a few fixed-point steps find where.
inline float HeightAt(const Table& t, float x, float z, float time)
{
	float rx = x, rz = z;
	float dx, dy, dz;
	for (int i = 0; i < kInversionSteps; ++i) {
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
	for (int i = 0; i < kInversionSteps; ++i) {
		Displacement(t, rx, rz, time, dx, dy, dz, depthAt(rx, rz), shoreWaves);
		rx = x - dx;
		rz = z - dz;
	}
	Displacement(t, rx, rz, time, dx, dy, dz, depthAt(rx, rz), shoreWaves);
	return dy;
}

}   // namespace WaterWaves
