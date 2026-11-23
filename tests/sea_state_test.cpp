// Unit tests for src/SeaState.h and the Sea style of src/WaterWaves.h (lesson 63). No Direct3D, no framework: a failed CHECK prints and the
// program exits 1.
//  - the numbers of a real sea: significant height and peak wavelength of a wind, how a shorter fetch shrinks both
//  - dispersion: every wave of the table travels at the speed its length asks for, in deep and in shallow water
//  - the table has the energy of the spectrum, and its waves lie around the wind
//  - the table never folds over, never exceeds its own bounds, and the same seed is the same sea
//  - what a lake is (a short fetch), and what the settings of each water body type now say
#include "SeaState.h"
#include "WaterSettings.h"
#include "WaterWaves.h"
#include "Weather.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

static int g_failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++g_failures; } } while (0)

using WaterWaves::Params;
using WaterWaves::Table;

static Params SeaParams(float wind, float fetch = 0.0f)
{
	Params p;
	p.windSpeed = wind;
	p.fetch = fetch;
	p.count = WaterWaves::kMaxWaves;
	p.swell = 0.0f;
	p.groups = 0.0f;
	return p;
}

// 4 sqrt(m0) of a table: the significant wave height its waves add up to.
static double TableHeight(const Table& t)
{
	double m0 = 0.0;
	for (int i = 0; i < t.count; ++i) m0 += 0.5 * t.waves[i].amplitude * t.waves[i].amplitude;
	return 4.0 * std::sqrt(m0);
}

static float Random01(std::uint32_t& state)
{
	state = state * 1664525u + 1013904223u;
	return static_cast<float>(state >> 8) / 16777216.0f;
}

static void TestBeaufortAndFetch()
{
	CHECK(SeaState::Beaufort(0.0) == 0 && SeaState::Beaufort(0.2) == 0 && SeaState::Beaufort(1.0) == 1);
	CHECK(SeaState::Beaufort(7.9) == 4 && SeaState::Beaufort(8.0) == 5);   // a fresh breeze (force 5) starts at 8 m/s
	CHECK(SeaState::Beaufort(14.0) == 7 && SeaState::Beaufort(25.0) == 10 && SeaState::Beaufort(40.0) == 12);

	// gF/U^2: unlimited is the cap; a tiny fetch is held at the floor; it never leaves [20, 22000].
	CHECK(SeaState::NondimFetch(10.0, 0.0) == SeaState::kMaxFetchNondim);
	CHECK(SeaState::NondimFetch(10.0, 1.0) == SeaState::kMinFetchNondim);
	CHECK(SeaState::NondimFetch(10.0, 1e9) == SeaState::kMaxFetchNondim);
	CHECK(std::fabs(SeaState::NondimFetch(10.0, 1000.0) - SeaState::kGravity * 1000.0 / 100.0) < 1e-9);

	// A calm fades the height out under half a metre per second and does not run the periods away.
	CHECK(SeaState::CalmFactor(0.0) == 0.0 && SeaState::CalmFactor(0.25) == 0.5 && SeaState::CalmFactor(0.5) == 1.0 && SeaState::CalmFactor(20.0) == 1.0);
	const SeaState::Summary dead = SeaState::Describe(0.0, 0.0);
	CHECK(dead.significantHeight == 0.0 && std::isfinite(dead.peakWavelength) && std::isfinite(dead.peakPeriod));
}

// The numbers of a real sea (Pierson-Moskowitz for the open ocean, JONSWAP for a limited fetch).
static void TestRealSeaNumbers()
{
	// Open water, 10 m/s: about 2.1 m significant height (0.21 U^2/g), a peak period of about 8 s, a peak wavelength of 80 to 100 m.
	const SeaState::Summary ten = SeaState::Describe(10.0, 0.0);
	CHECK(std::fabs(ten.significantHeight - 0.2127 * 100.0 / SeaState::kGravity) < 0.02);
	CHECK(ten.peakPeriod > 6.5 && ten.peakPeriod < 9.5);
	CHECK(ten.peakWavelength > 70.0 && ten.peakWavelength < 110.0);
	// The wave travels at sqrt(g / k) in deep water: the period and the wavelength tell the same speed.
	CHECK(std::fabs(ten.peakSpeed - ten.peakWavelength / ten.peakPeriod) < 1e-6);
	CHECK(std::fabs(ten.peakSpeed - std::sqrt(SeaState::kGravity / ten.peakWavenumber)) < 1e-6);

	// A stronger wind: taller (about with the square of the speed), longer, faster, with a longer period.
	double lastHeight = -1.0, lastLength = -1.0, lastPeriod = -1.0;
	for (double wind : { 2.0, 4.0, 6.0, 8.0, 12.0, 16.0, 24.0 }) {
		const SeaState::Summary s = SeaState::Describe(wind, 0.0);
		CHECK(s.significantHeight > lastHeight && s.peakWavelength > lastLength && s.peakPeriod > lastPeriod);
		lastHeight = s.significantHeight; lastLength = s.peakWavelength; lastPeriod = s.peakPeriod;
	}
	const SeaState::Summary five = SeaState::Describe(5.0, 0.0), twenty = SeaState::Describe(20.0, 0.0);
	CHECK(std::fabs(twenty.significantHeight / five.significantHeight - 16.0) < 0.5);   // 4x the wind, 16x the height

	// A longer fetch: taller and longer until the sea is fully developed, then no more.
	double lastH = -1.0, lastL = -1.0;
	for (double fetch : { 100.0, 500.0, 2000.0, 10000.0, 50000.0, 200000.0 }) {
		const SeaState::Summary s = SeaState::Describe(12.0, fetch);
		CHECK(s.significantHeight > lastH && s.peakWavelength > lastL);
		lastH = s.significantHeight; lastL = s.peakWavelength;
	}
	const SeaState::Summary full = SeaState::Describe(12.0, 0.0), huge = SeaState::Describe(12.0, 1e9);
	CHECK(std::fabs(full.significantHeight - huge.significantHeight) < 1e-9);

	// A lake: 200 m of water in a gale (15 m/s) makes a short chop, nothing like the ocean's 15 m/s sea.
	const SeaState::Summary lake = SeaState::Describe(15.0, 200.0), ocean = SeaState::Describe(15.0, 0.0);
	CHECK(lake.significantHeight < 0.25 && lake.peakWavelength < 8.0);
	CHECK(ocean.significantHeight > 4.0 && ocean.peakWavelength > 150.0);
}

static void TestDispersion()
{
	// Deep water: omega^2 = g k.
	for (double omega : { 0.3, 1.0, 2.5, 6.0 }) CHECK(std::fabs(SeaState::Wavenumber(omega, 0.0) - omega * omega / SeaState::kGravity) < 1e-12);
	// Finite depth satisfies omega^2 = g k tanh(k h) ...
	for (double depth : { 0.5, 2.0, 10.0, 80.0 }) {
		for (double omega : { 0.4, 1.0, 2.0, 4.0 }) {
			const double k = SeaState::Wavenumber(omega, depth);
			CHECK(std::fabs(SeaState::kGravity * k * std::tanh(k * depth) - omega * omega) < 1e-6 * std::max(1.0, omega * omega));
			CHECK(k >= omega * omega / SeaState::kGravity - 1e-9);   // ... and is never LONGER than the deep wave of the same frequency: shallow water shortens waves
		}
	}
	// A long wave in shallow water travels at sqrt(g h), whatever its length. Deep enough, the depth does not matter.
	CHECK(std::fabs(SeaState::PhaseSpeed(0.05, 2.0) - std::sqrt(SeaState::kGravity * 2.0)) < 0.03 * std::sqrt(SeaState::kGravity * 2.0));
	CHECK(std::fabs(SeaState::PhaseSpeed(2.0, 5000.0) - SeaState::PhaseSpeed(2.0, 0.0)) < 1e-9);
	// So a wave slows down on its way to the beach.
	CHECK(SeaState::PhaseSpeed(0.8, 3.0) < SeaState::PhaseSpeed(0.8, 0.0) * 0.8);

	// Every wave of a deep-water table travels at the speed its length asks for (this is what the old table did not do).
	for (float wind : { 3.0f, 8.0f, 15.0f }) {
		const Table t = WaterWaves::Build(SeaParams(wind));
		for (int i = 0; i < t.count; ++i) {
			const double w = t.waves[i].omega, k = t.waves[i].k;
			CHECK(std::fabs(w * w - SeaState::kGravity * k) < 1e-4 * w * w);
		}
	}
	// ... and in water of a given depth, with the finite-depth relation.
	Params shallow = SeaParams(8.0f);
	shallow.depth = 4.0f;
	const Table t = WaterWaves::Build(shallow);
	for (int i = 0; i < t.count; ++i) {
		const double w = t.waves[i].omega, k = t.waves[i].k;
		CHECK(std::fabs(SeaState::kGravity * k * std::tanh(k * 4.0) - w * w) < 1e-3 * w * w);
	}
}

static void TestTableEnergyAndShape()
{
	// The table has the energy of the spectrum: its waves add up to the significant height the wind makes (a few percent are in the bands
	// outside the table's range).
	for (float wind : { 4.0f, 8.0f, 12.0f, 20.0f }) {
		const Table t = WaterWaves::Build(SeaParams(wind));
		const double wanted = SeaState::Describe(wind, 0.0).significantHeight;
		CHECK(std::fabs(TableHeight(t) - wanted) < 0.08 * wanted);
	}
	// A lake of 300 m: the same, for its own, much smaller sea (the peak is high, so the table's range is shorter: a looser bound).
	for (float wind : { 6.0f, 12.0f }) {
		const Table t = WaterWaves::Build(SeaParams(wind, 300.0f));
		const double wanted = SeaState::Describe(wind, 300.0).significantHeight;
		CHECK(std::fabs(TableHeight(t) - wanted) < 0.15 * wanted);
	}
	// The height scale is a height scale: the wavelengths and speeds do not move.
	Params a = SeaParams(10.0f), b = a;
	b.heightScale = 2.0f;
	const Table ta = WaterWaves::Build(a), tb = WaterWaves::Build(b);
	CHECK(ta.count == tb.count);
	for (int i = 0; i < ta.count; ++i) {
		CHECK(ta.waves[i].k == tb.waves[i].k && ta.waves[i].omega == tb.waves[i].omega);
		CHECK(std::fabs(tb.waves[i].amplitude - 2.0f * ta.waves[i].amplitude) < 1e-5f * std::max(1.0f, ta.waves[i].amplitude));
	}
	// The tallest wave is first (the shader's swash reads wave 0), and it is near the peak of the spectrum, not at the fine end.
	const Table big = WaterWaves::Build(SeaParams(10.0f));
	const double peakK = SeaState::Describe(10.0, 0.0).peakWavenumber;
	for (int i = 1; i < big.count; ++i) CHECK(big.waves[0].amplitude >= big.waves[i].amplitude);
	CHECK(big.waves[0].k > 0.6 * peakK && big.waves[0].k < 1.8 * peakK);
	// Short waves are lower than long ones, by a lot: that is what a spectrum is.
	float longest = 1e9f, tallest = 0.0f, finest = 0.0f, finestAmplitude = 0.0f;
	for (int i = 0; i < big.count; ++i) {
		tallest = std::max(tallest, big.waves[i].amplitude);
		longest = std::min(longest, big.waves[i].k);
		if (big.waves[i].k > finest) { finest = big.waves[i].k; finestAmplitude = big.waves[i].amplitude; }
	}
	CHECK(finest > 20.0f * longest && finestAmplitude < 0.2f * tallest);

	// The count is clamped to the table; a single wave works.
	Params one = SeaParams(8.0f);
	one.count = 1;
	CHECK(WaterWaves::Build(one).count == 1);
	one.count = 99;
	CHECK(WaterWaves::Build(one).count == WaterWaves::kMaxWaves);
	CHECK(WaterWaves::kMaxWaves == 16);

	// A dead calm is a flat sea, and a valid table.
	const Table calm = WaterWaves::Build(SeaParams(0.0f));
	for (int i = 0; i < calm.count; ++i) CHECK(calm.waves[i].amplitude == 0.0f && std::isfinite(calm.waves[i].k) && std::isfinite(calm.waves[i].omega));
	CHECK(calm.heightBound == 0.0f);
}

static void TestDirections()
{
	// The energy-weighted direction of the wind sea is the wind's direction, whichever way it blows (averaged over seeds: one
	// roll of the dice may put the odd wave far off).
	for (float windDegrees : { 0.0f, 90.0f, 200.0f, 315.0f }) {
		double sumX = 0.0, sumZ = 0.0, sumWeight = 0.0;
		for (int seed = 1; seed <= 24; ++seed) {
			Params p = SeaParams(10.0f);
			p.windDegrees = windDegrees;
			p.seed = static_cast<std::uint32_t>(seed);
			const Table t = WaterWaves::Build(p);
			for (int i = 0; i < t.count; ++i) {
				const double weight = static_cast<double>(t.waves[i].amplitude) * t.waves[i].amplitude;
				sumX += weight * -t.waves[i].dirX;   // the table stores the direction the crests come FROM
				sumZ += weight * -t.waves[i].dirZ;
				sumWeight += weight;
			}
		}
		double angle = std::atan2(sumZ, sumX) * 180.0 / SeaState::kPi - windDegrees;
		while (angle > 180.0) angle -= 360.0;
		while (angle < -180.0) angle += 360.0;
		CHECK(std::fabs(angle) < 8.0);
		// ... and it IS mostly with the wind, not an even scatter: the mean direction vector is long (1 = every wave the same way).
		CHECK(std::sqrt(sumX * sumX + sumZ * sumZ) / sumWeight > 0.6);
	}
	// The biggest waves are the most aligned: the spread is narrowest at the peak, as measured.
	const double peak = 1.0;
	CHECK(SeaState::SpreadExponent(peak, peak) > SeaState::SpreadExponent(2.0 * peak, peak));
	CHECK(SeaState::SpreadExponent(peak, peak) > SeaState::SpreadExponent(0.5 * peak, peak));
	// DirectionOffset: the median is the mean direction, it grows with u, and a narrower spread (bigger s) stays closer.
	CHECK(std::fabs(SeaState::DirectionOffset(5.0, 0.5)) < 0.05);
	CHECK(SeaState::DirectionOffset(5.0, 0.2) < SeaState::DirectionOffset(5.0, 0.5) && SeaState::DirectionOffset(5.0, 0.5) < SeaState::DirectionOffset(5.0, 0.8));
	CHECK(std::fabs(SeaState::DirectionOffset(30.0, 0.9)) < std::fabs(SeaState::DirectionOffset(2.0, 0.9)));
	CHECK(std::fabs(SeaState::DirectionOffset(3.0, 0.1) + SeaState::DirectionOffset(3.0, 0.9)) < 0.1);   // symmetric

	// The artist's spread widens it (0 = nearly dead on, 1 = from all around).
	auto spreadOf = [](float spread) {
		double w = 0.0, off = 0.0;
		for (int seed = 1; seed <= 24; ++seed) {
			Params p = SeaParams(10.0f);
			p.spread = spread;
			p.windDegrees = 0.0f;
			p.seed = static_cast<std::uint32_t>(seed);
			const Table t = WaterWaves::Build(p);
			for (int i = 0; i < t.count; ++i) {
				const double weight = static_cast<double>(t.waves[i].amplitude) * t.waves[i].amplitude;
				w += weight; off += weight * std::fabs(std::atan2(-t.waves[i].dirZ, -t.waves[i].dirX));
			}
		}
		return off / w;
	};
	CHECK(spreadOf(0.0f) < spreadOf(1.0f));
}

static void TestNoFoldAndBounds()
{
	std::uint32_t state = 11;
	for (int trial = 0; trial < 300; ++trial) {
		Params p = SeaParams(Random01(state) * 30.0f, Random01(state) < 0.5f ? 0.0f : 20.0f + Random01(state) * 5000.0f);
		p.count = 1 + static_cast<int>(Random01(state) * 16.0f);
		p.choppiness = Random01(state);
		p.swell = Random01(state);
		p.swellAngle = (Random01(state) - 0.5f) * 180.0f;
		p.swellLength = 1.5f + Random01(state) * 4.5f;
		p.groups = Random01(state) * 0.9f;
		p.spread = Random01(state);
		p.heightScale = 0.25f + Random01(state) * 3.0f;
		p.shoreWaves = Random01(state);
		p.windDegrees = Random01(state) * 360.0f;
		p.seed = static_cast<std::uint32_t>(Random01(state) * 9999.0f);
		const Table t = WaterWaves::Build(p);
		CHECK(t.count >= 1 && t.count <= WaterWaves::kMaxWaves);
		float sum = 0.0f;
		for (int i = 0; i < t.count; ++i) {
			const WaterWaves::Wave& w = t.waves[i];
			CHECK(std::isfinite(w.k) && w.k > 0.0f && std::isfinite(w.omega) && w.omega > 0.0f && std::isfinite(w.amplitude) && w.amplitude >= 0.0f);
			CHECK(w.steepness >= 0.0f && w.steepness <= 1.0f);
			CHECK(std::fabs(w.dirX * w.dirX + w.dirZ * w.dirZ - 1.0f) < 1e-4f);
			sum += w.steepness * w.k * w.amplitude;
		}
		CHECK(sum <= p.choppiness + 1e-4f);   // the guarantee: no crest folds over (see BuildSpectrum)
		// The bound holds: no point of the surface rises higher than the table says.
		float highest = 0.0f;
		for (int sample = 0; sample < 60; ++sample) {
			const float x = (Random01(state) - 0.5f) * 4000.0f, z = (Random01(state) - 0.5f) * 4000.0f, time = Random01(state) * 600.0f;
			highest = std::max(highest, std::fabs(WaterWaves::HeightAt(t, x, z, time)));
		}
		CHECK(highest <= t.heightBound + 1e-3f);
	}
	// The same seed is the same sea; another seed is another.
	const Params p = SeaParams(9.0f);
	Params q = p;
	q.seed = 2;
	const Table a = WaterWaves::Build(p), b = WaterWaves::Build(p), c = WaterWaves::Build(q);
	bool same = true, different = false;
	for (int i = 0; i < a.count; ++i) {
		same = same && a.waves[i].phase0 == b.waves[i].phase0 && a.waves[i].dirX == b.waves[i].dirX && a.waves[i].amplitude == b.waves[i].amplitude;
		different = different || a.waves[i].phase0 != c.waves[i].phase0;
	}
	CHECK(same && different);
}

static void TestSwellAndGroups()
{
	// Only the open sea has a swell: a long wave below the wind sea's peak. A lake (a finite fetch) has none, whatever the setting.
	Params open = SeaParams(10.0f);
	open.swell = 0.6f;
	Params lake = open;
	lake.fetch = 400.0f;
	const double peakOmega = SeaState::Describe(10.0, 0.0).peakOmega;
	int openSwells = 0, lakeSwells = 0;
	const Table to = WaterWaves::Build(open), tl = WaterWaves::Build(lake);
	for (int i = 0; i < to.count; ++i) if (to.waves[i].omega < 0.69 * peakOmega) ++openSwells;
	const double lakePeak = SeaState::Describe(10.0, 400.0).peakOmega;
	for (int i = 0; i < tl.count; ++i) if (tl.waves[i].omega < 0.69 * lakePeak) ++lakeSwells;
	CHECK(openSwells >= 1 && lakeSwells == 0);
	// The swell is lower than the wind sea, and longer than its peak wave.
	float swellTop = 0.0f, windTop = 0.0f;
	for (int i = 0; i < to.count; ++i) {
		if (to.waves[i].omega < 0.69 * peakOmega) swellTop = std::max(swellTop, to.waves[i].amplitude);
		else windTop = std::max(windTop, to.waves[i].amplitude);
	}
	CHECK(swellTop > 0.0f && swellTop < windTop);

	// Wave groups drift at half the speed of the waves in them, and are a good many wavelengths across.
	Params grouped = SeaParams(10.0f);
	grouped.groups = 0.5f;
	const Table tg = WaterWaves::Build(grouped);
	const SeaState::Summary s = SeaState::Describe(10.0, 0.0);
	CHECK(tg.groupStrength == 0.5f && tg.groupK > 0.0f);
	CHECK(std::fabs(tg.groupSpeed / tg.groupK - 0.5 * s.peakSpeed) < 0.02 * s.peakSpeed);
	CHECK(tg.groupK < 0.3f * static_cast<float>(s.peakWavenumber));
	// Groups only turn heights down; the average height is what was asked.
	Params plain = grouped;
	plain.groups = 0.0f;
	CHECK(WaterWaves::GroupFactor(tg, 10.0f, 20.0f, 3.0f) <= 1.0f && WaterWaves::GroupFactor(tg, 10.0f, 20.0f, 3.0f) >= 0.5f);
	CHECK(TableHeight(tg) > TableHeight(WaterWaves::Build(plain)));
}

static void TestSettings()
{
	// There is one sea: the ocean's defaults use all the waves the table can hold (a spectrum needs them).
	CHECK(WaterSettings::Defaults().waveCount == WaterWaves::kMaxWaves);
	// The Scale is only a height: it reaches heightScale and nothing else of the table.
	WaterSettings sea = WaterSettings::Defaults();
	sea.waveScale = 2.0f;
	const Params p = sea.WaveParams();
	CHECK(p.heightScale == 2.0f && p.count == sea.waveCount && p.seed == static_cast<std::uint32_t>(sea.waveSeed));
	// There is no wind in the settings of the water: the scene's weather is the only wind.
	WindSettings wind;
	CHECK(wind.speed == 8.0f && WindSettings::kSpeedMax == 30.0f);
}

int main()
{
	TestBeaufortAndFetch();
	TestRealSeaNumbers();
	TestDispersion();
	TestTableEnergyAndShape();
	TestDirections();
	TestNoFoldAndBounds();
	TestSwellAndGroups();
	TestSettings();
	if (g_failures) {
		std::printf("%d check(s) failed\n", g_failures);
		return 1;
	}
	std::printf("sea state tests passed\n");
	return 0;
}
