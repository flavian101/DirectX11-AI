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
#include "SeaState.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace WaterWaves {

constexpr int kMaxWaves = 16;   // must equal WATER_MAX_WAVES in shaders/water_material.hlsli
constexpr float kBaseFrequency = 0.6f;   // radians per world unit of the first wave; matches WaterSettings' default

// --- Waves near a shore (the same numbers are in shaders/waves.hlsli) ---------------------------------------------
// A wave that reaches shallow water first GROWS taller (the same energy is squeezed into less water), then cannot grow
// any more: it is limited to a fraction of the depth under it, and so it dies away to nothing at the water's edge.
constexpr float kShoalGrowth = 0.5f;   // a wave can become up to 50% taller in shallow water (at full strength)
constexpr float kBreakRatio = 0.4f;    // a wave's AMPLITUDE never exceeds this fraction of the depth (breaking: wave height ~ 0.8 x depth)
constexpr float kDeepWater = 1000.0f;  // "no depth information": deep enough that nothing changes

// How much a wave of wave number `k` feels the bottom at this depth: smoothly 1 in very shallow water, 0 in deep water.
// "Shallow" is relative to the wavelength: the depth is small compared with it when k * depth is below ~pi. So long waves
// feel the bottom (and the shore) from farther out than short ones. Same function in shaders/waves.hlsli.
inline float ShallowWeight(float k, float depth)
{
	const float kd = k * std::max(depth, 0.0f);
	const float t = std::clamp((kd - 0.5f) / (3.14159265f - 0.5f), 0.0f, 1.0f);
	return 1.0f - t * t * (3.0f - 2.0f * t);
}

// The amplitude a wave of nominal `amplitude` and wave number `k` has where the water is `depth` deep.
// `strength` 0 = off (the open-water amplitude, always), 1 = full effect.
inline float ShoalAmplitude(float amplitude, float k, float depth, float strength)
{
	if (strength <= 0.0f) return amplitude;
	depth = std::max(depth, 0.0f);
	const float shallow = ShallowWeight(k, depth);
	const float grown = amplitude * (1.0f + kShoalGrowth * shallow * strength);
	const float limit = kBreakRatio * depth;
	if (limit < 1e-5f) return 0.0f;
	// tanh: equal to `grown` while it is far below the limit, and never above the limit. Smooth, so no kink.
	const float limited = limit * std::tanh(grown / limit);
	return amplitude + (limited - amplitude) * std::min(strength, 1.0f);
}

// The sea is built from PHYSICS (lesson 63): the wind speed and the fetch decide how tall and how long the waves are, and the speed of
// each wave follows from its length. There is no other wave model.
struct Params {
	float windDegrees = 0.0f;    // direction the waves travel toward (angle in the XZ plane)
	float spread = 0.7f;         // 0 = every wave runs the same way, 1 = fully scattered
	float choppiness = 0.5f;     // 0 = rounded sine waves, 1 = sharpest crests that never fold over
	int count = 16;              // how many of the kMaxWaves waves are used
	float shoreWaves = 0.0f;     // 0 = waves are the same everywhere; 1 = they grow, then break and die as the water gets shallow
	float swell = 0.0f;          // 0 = no swell; 1 = a swell nearly as tall as the biggest wind wave
	float swellAngle = 35.0f;    // degrees: the swell's direction, measured from the wind
	float swellLength = 2.5f;    // how much longer the swell's wavelength is than the biggest wind wave's
	float groups = 0.0f;         // 0 = even waves everywhere; 1 = the sea breathes: patches of calm and rough water (wave groups)
	std::uint32_t seed = 1;      // another seed = another sea with the same character
	float windSpeed = 8.0f;      // m/s, 10 m above the water (the scene's weather)
	float fetch = 0.0f;          // metres of open water the wind has blown over; <= 0 = unlimited (the open ocean, no limit but the wind)
	float depth = 0.0f;          // metres of water under the waves, for the dispersion; <= 0 = deep
	float heightScale = 1.0f;    // an artist's multiplier on every wave height (1 = exactly what the physics says); wavelengths do not change
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

// ---- A wind sea (lesson 63) ---------------------------------------------------------------------------------------------
// The JONSWAP spectrum of this wind and fetch (SeaState.h), scaled to the significant height the wind makes, cut into `count` bands that are equally wide in LOG frequency, one wave
// per band. A wave's height is the square root of twice the energy of its band, so the table has exactly the energy of the
// spectrum: 4 sqrt(sum a^2 / 2) is the significant wave height the physics asks for. Its length follows from its frequency by
// dispersion, so it also has the speed the physics asks for. Its direction is drawn from the spread of that frequency around the wind.
inline Table Build(const Params& p)
{
	constexpr float kDegToRad = 0.01745329252f;
	constexpr float kTwoPi = 6.28318530718f;
	Table t;
	t.count = std::clamp(p.count, 1, kMaxWaves);
	Random random(p.seed);

	const double wind = std::clamp(static_cast<double>(p.windSpeed), 0.0, 60.0);
	const SeaState::Spectrum spectrum = SeaState::MakeSpectrum(wind, p.fetch);
	const double peak = spectrum.peakOmega;
	const double height = SeaState::CalmFactor(wind) * std::max(static_cast<double>(p.heightScale), 0.0);

	const bool openSea = p.fetch <= 0.0f;   // a swell is the memory of a storm far away: only the open sea has one, a lake does not
	int swellCount = (p.swell > 0.0f && openSea && t.count >= 2) ? std::clamp(static_cast<int>(std::lround(t.count * 0.25f)), 1, 3) : 0;
	swellCount = std::min(swellCount, t.count - 1);
	const int windCount = t.count - swellCount;

	// Groups only ever turn the waves down, so the heights are raised a little to keep the average what was asked.
	const float groups = std::clamp(p.groups, 0.0f, 0.9f);
	const double groupBoost = 1.0 / (1.0 - 0.5 * groups);

	// The bands: from a little below the peak (nothing is there) up to 7 x the peak, or 22 rad/s (a 13 cm wave) when the peak is high.
	const double low = 0.7 * peak;
	const double high = std::max(std::min(7.0 * peak, 22.0), 1.8 * low);
	const double step = std::pow(high / low, 1.0 / windCount);
	// Wider spread, in the artist's terms: 0 = every wave nearly dead on the wind, 1 = waves from all around (the physical widths are in the middle).
	const double spreadScale = 2.0 - 1.55 * std::clamp(static_cast<double>(p.spread), 0.0, 1.0);

	float weight[kMaxWaves];
	double windEnergy = 0.0;
	int index = 0;
	for (int i = 0; i < windCount; ++i, ++index) {
		const double lo = low * std::pow(step, i), hi = lo * step;
		const double energy = SeaState::BandEnergy(spectrum, lo, hi);
		windEnergy += energy;
		const double jitter = 1.0 + 0.05 * random.Signed();   // not exactly the middle of its band: a regular comb of waves looks regular
		const double omega = std::sqrt(lo * hi) * jitter;
		const double s = std::max(SeaState::SpreadExponent(omega, peak) * spreadScale, 0.8);
		const double offset = SeaState::DirectionOffset(s, 0.06 + 0.88 * random.Unit());   // (not the far tails: a wave straight against the wind is rare)
		Wave& w = t.waves[index];
		w.omega = static_cast<float>(omega);
		w.k = static_cast<float>(SeaState::Wavenumber(omega, p.depth));
		w.amplitude = static_cast<float>(std::sqrt(2.0 * energy) * height * groupBoost);
		const double angle = p.windDegrees * kDegToRad + offset;
		w.dirX = static_cast<float>(-std::cos(angle));   // the direction the crests come FROM (crests come FROM the direction the wave travels away from)
		w.dirZ = static_cast<float>(-std::sin(angle));
		w.phase0 = random.Unit() * kTwoPi;
		weight[index] = 1.0f;
	}
	if (swellCount > 0) {
		// The swell: a few long waves, as tall as `swell` x 80% of the wind sea (in height), from a direction of their own.
		const double swellEnergy = std::pow(std::clamp(static_cast<double>(p.swell), 0.0, 1.0) * 0.8, 2.0) * windEnergy;
		double shareSum = 0.0;
		for (int j = 0; j < swellCount; ++j) shareSum += 1.0 / (1.0 + 0.6 * j);
		for (int j = 0; j < swellCount; ++j, ++index) {
			const double lengthen = std::max(static_cast<double>(p.swellLength), 1.2) * (1.0 + 0.37 * j);   // x as long as the peak wave
			const double omega = peak / std::sqrt(lengthen);                                                    // (omega^2 = g k)
			const double share = (1.0 / (1.0 + 0.6 * j)) / shareSum;
			Wave& w = t.waves[index];
			w.omega = static_cast<float>(omega);
			w.k = static_cast<float>(SeaState::Wavenumber(omega, p.depth));
			w.amplitude = static_cast<float>(std::sqrt(2.0 * swellEnergy * share) * height * groupBoost);
			const float angle = (p.windDegrees + p.swellAngle + 10.0f * random.Signed()) * kDegToRad;
			w.dirX = -std::cos(angle);
			w.dirZ = -std::sin(angle);
			w.phase0 = random.Unit() * kTwoPi;
			weight[index] = 0.5f;   // rounder crests than the wind waves
		}
	}

	// The tallest wave goes first: the shader reads wave 0 for the swash at the shore.
	int biggest = 0;
	for (int i = 1; i < t.count; ++i) {
		if (t.waves[i].amplitude > t.waves[biggest].amplitude) biggest = i;
	}
	std::swap(t.waves[0], t.waves[biggest]);
	std::swap(weight[0], weight[biggest]);

	float weightedKA = 0.0f;
	for (int i = 0; i < t.count; ++i) {
		weightedKA += weight[i] * t.waves[i].k * t.waves[i].amplitude;
		t.heightBound += t.waves[i].amplitude * (1.0f + kShoalGrowth * std::clamp(p.shoreWaves, 0.0f, 1.0f));
	}
	// Steepness: sum of Q k a <= choppiness <= 1, so no crest folds over. (With Q_i = weight_i * c * m, m = min(1, 1 / sum of weight * k * a), the sum is exactly c * m * sum(weight * k * a) <= c, and no Q exceeds 1.)
	const float c = std::clamp(p.choppiness, 0.0f, 1.0f);
	const float m = std::min(1.0f, 1.0f / std::max(weightedKA, 1e-4f));
	for (int i = 0; i < t.count; ++i) t.waves[i].steepness = weight[i] * c * m;

	// Wave groups: patches about eight peak wavelengths across. A group travels at half the speed of the waves in it.
	const double peakK = SeaState::Wavenumber(peak, p.depth);
	t.groupStrength = groups;
	t.groupK = static_cast<float>(0.13 * peakK);
	t.groupSpeed = static_cast<float>(0.13 * peakK * 0.5 * SeaState::kGravity / peak);
	return t;
}

// What the water knows about the shore under a point: how deep it is, and which way the land lies (a unit vector toward
// the land times how sure we are: see WaterShore::ComputeShoreDirections). (0, 0) = no information.
struct ShoreSample {
	float depth = kDeepWater;
	float towardX = 0.0f;
	float towardZ = 0.0f;
};

// Waves cannot come FROM the land. A wave that is running away from a shore it is close to (the lee side of an island, the
// far bank of a lake) has no open water behind it to have built up in, so it is turned down instead of rising and
// breaking on a beach it is leaving. A wave running TOWARD the shore is untouched.
//   `dirX, dirZ` point to where the crests come FROM, so a wave runs away from the land when that direction points to the
//   land: away = dot(dir, toward). Only waves that feel the bottom (ShallowWeight) are affected, so far from any shore nothing changes.
// `shelter` 0 = off, 1 = a wave running straight away from a nearby shore is nearly gone. Never more than 1, never below 0.05.
inline float ShelterFactor(const Wave& w, const ShoreSample& shore, float shelter)
{
	if (shelter <= 0.0f) return 1.0f;
	const float away = std::clamp(w.dirX * shore.towardX + w.dirZ * shore.towardZ, 0.0f, 1.0f);
	return 1.0f - std::min(shelter, 0.95f) * ShallowWeight(w.k, shore.depth) * away;
}

// Where a point that rests at (x, z) is at `time`: sideways offset (dx, dz) and height dy.
// `depth` and `shoreWaves` are the shore effect: how deep the water is under the REST position, and how strongly the waves
// react to it (0 = not at all). The height follows the shoaled amplitude, but the sideways pull never exceeds what the
// open-water wave had, so a wave that grows in shallow water can never fold over.
inline void Displacement(const Table& t, float x, float z, float time, float& dx, float& dy, float& dz,
						 const ShoreSample& shore, float shoreWaves, float shelter)
{
	dx = dy = dz = 0.0f;
	const float group = GroupFactor(t, x, z, time);   // 1 = full waves; lower in the calm patches of a sea with wave groups
	for (int i = 0; i < t.count; ++i) {
		const Wave& w = t.waves[i];
		const float shoaled = ShoalAmplitude(w.amplitude, w.k, shore.depth, shoreWaves);
		const float turnedDown = ShelterFactor(w, shore, shelter) * group;
		const float a = shoaled * turnedDown;
		const float aSide = std::min(shoaled, w.amplitude) * turnedDown;
		const float phase = w.k * (w.dirX * x + w.dirZ * z) + w.omega * time + w.phase0;
		const float s = std::sin(phase), c = std::cos(phase);
		dx += w.steepness * aSide * c * w.dirX;
		dz += w.steepness * aSide * c * w.dirZ;
		dy += a * s;
	}
}

// The same with only a depth (no shore direction): the shoaling alone.
inline void Displacement(const Table& t, float x, float z, float time, float& dx, float& dy, float& dz,
						 float depth = kDeepWater, float shoreWaves = 0.0f)
{
	ShoreSample shore;
	shore.depth = depth;
	Displacement(t, x, z, time, dx, dy, dz, shore, shoreWaves, 0.0f);
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

// The same, near a shore, with the shore direction too: `shoreAt(restX, restZ)` returns a ShoreSample for a REST position
// (world space), and `shelter` is how strongly waves running away from the land are turned down.
template <class ShoreFn>
inline float HeightAtShore(const Table& t, float x, float z, float time, float shoreWaves, float shelter, ShoreFn&& shoreAt)
{
	float rx = x, rz = z;
	float dx, dy, dz;
	for (int i = 0; i < kInversionSteps; ++i) {
		Displacement(t, rx, rz, time, dx, dy, dz, shoreAt(rx, rz), shoreWaves, shelter);
		rx = x - dx;
		rz = z - dz;
	}
	Displacement(t, rx, rz, time, dx, dy, dz, shoreAt(rx, rz), shoreWaves, shelter);
	return dy;
}

// The same, with a depth only: `depthAt(restX, restZ)` says how deep the water is under a REST position (world space).
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
