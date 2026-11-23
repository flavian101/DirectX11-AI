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

// How the table is made. Classic is the original fixed list of eight waves (kept so old scenes can look as they did);
// Spectrum builds the waves from an artist's sea model: a wind sea whose waves get shorter and lower together, plus a long swell.
// Sea builds them from PHYSICS (lesson 63): the wind speed and the fetch decide how tall and how long the waves are, and the speed of
// each wave follows from its length. Appended at the end: the style is saved by number.
enum class Style : int { Classic = 0, Spectrum = 1, Sea = 2 };

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

	// --- Sea style only (lesson 63): the wind decides, so amplitude, frequency and speed above are ignored ---
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

// ---- A wind sea (lesson 63) ---------------------------------------------------------------------------------------------
// The JONSWAP spectrum of this wind and fetch (SeaState.h), scaled to the significant height the wind makes, cut into `count` bands that are equally wide in LOG frequency, one wave
// per band. A wave's height is the square root of twice the energy of its band, so the table has exactly the energy of the
// spectrum: 4 sqrt(sum a^2 / 2) is the significant wave height the physics asks for. Its length follows from its frequency by
// dispersion, so it also has the speed the physics asks for. Its direction is drawn from the spread of that frequency around the wind.
inline Table BuildSea(const Params& p)
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
	int swellCount = (p.swell > 0.0f && openSea && p.flowSpeed <= 0.0f && t.count >= 2) ? std::clamp(static_cast<int>(std::lround(t.count * 0.25f)), 1, 3) : 0;
	swellCount = std::min(swellCount, t.count - 1);
	const int windCount = t.count - swellCount;

	// As in BuildSpectrum: groups only ever turn the waves down, so the heights are raised a little to keep the average what was asked.
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
		w.dirX = static_cast<float>(-std::cos(angle));   // the direction the crests come FROM (see BuildClassic)
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
	// Steepness as in BuildSpectrum: sum of Q k a <= choppiness <= 1, so no crest folds over.
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

inline Table Build(const Params& p)
{
	if (p.style == Style::Classic) return BuildClassic(p);
	if (p.style == Style::Sea && p.flowSpeed <= 0.0f) return BuildSea(p);   // a current carries its waves along: that is the Spectrum style's river branch
	return BuildSpectrum(p);
}

// ---- Waves the mesh can show (lesson 44) -----------------------------------------------------------------------------------
//
// The vertex shader can only show a wave that the grid is fine enough to follow: it keeps a wave at full height when its
// wavelength is at least six grid spacings and fades it to nothing at three (water_vs.hlsl and WaveDisplacement). The mesh has a
// fixed vertex budget (WaterMesh.h), so on a big body the cells are big: about size / 670 world units, whatever the detail
// setting. A 2000-unit sea has cells of about 3 units: waves under about 9 units are gone, and those up to about 19 are partly faded.
//
// So a big sea has to be made of bigger waves. That is also what a big sea is: waves grow with the distance they have run.
// Scaling a sea by s multiplies every wavelength AND every height by s, which keeps every wave's steepness: the same sea,
// seen from further away. ResolvableScale finds how much it takes for the mesh to show most of it.

// What the vertex shader keeps of a wave with wave number k where the grid spacing is `spacing` (1 = all of it, 0 = none).
// This IS the rule in waves.hlsli (WaveDisplacement): fade = saturate(2 - 2 k / maxK), maxK = 2 pi / (3 spacing).
inline float VertexFade(float k, float spacing)
{
	const float maxK = 6.2831853f / (3.0f * (std::max)(spacing, 1e-4f));
	return std::clamp(2.0f - 2.0f * k / maxK, 0.0f, 1.0f);
}

// The same sea made `scale` times bigger: wavelengths and heights together (so steepness is unchanged).
inline Params ScaledBy(Params p, float scale)
{
	scale = (std::max)(scale, 1e-3f);
	p.amplitude *= scale;
	p.frequency /= scale;
	return p;
}

// The share of the sea's wave HEIGHT that the mesh shows at this grid spacing: each wave's height, weighted by what the vertex
// shader keeps of it. 1 = every wave shows, 0 = a flat sea.
inline float ShownShare(const Table& table, float spacing)
{
	float total = 0.0f, shown = 0.0f;
	for (int i = 0; i < table.count; ++i) {
		total += table.waves[i].amplitude;
		shown += table.waves[i].amplitude * VertexFade(table.waves[i].k, spacing);
	}
	return total > 1e-9f ? shown / total : 1.0f;
}

constexpr float kShownShareWanted = 0.8f;   // a sea is "shown" when the mesh keeps at least this much of its wave height
constexpr float kWaveScaleMax = 8.0f;

// The smallest scale (1 or more) at which the mesh, whose finest grid spacing is `spacing`, shows at least `wanted` of the sea.
// 1 when it already does (a small body is never touched); `maxScale` if even that is not enough. ShownShare only rises as the
// waves get longer, so a bisection finds it.
inline float ResolvableScale(const Params& p, float spacing, float wanted = kShownShareWanted, float maxScale = kWaveScaleMax)
{
	if (spacing <= 0.0f) return 1.0f;
	auto shown = [&](float scale) { return ShownShare(Build(ScaledBy(p, scale)), spacing); };
	if (shown(1.0f) >= wanted) return 1.0f;
	if (shown(maxScale) < wanted) return maxScale;
	float low = 1.0f, high = maxScale;
	for (int i = 0; i < 24; ++i) {
		const float mid = 0.5f * (low + high);
		(shown(mid) >= wanted ? high : low) = mid;
	}
	return high;
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
