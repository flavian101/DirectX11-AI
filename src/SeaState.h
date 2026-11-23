#pragma once
// The physics of a wind sea (lesson 63). Plain C++ with no Direct3D, so a unit test can check every number against what a real sea does.
//
// Three facts decide what the sea looks like, and none of them is "how many vertices the mesh has":
//   1. The WIND SPEED (m/s) and the FETCH (how many metres of open water the wind has blown over) set how much energy the waves
//      have and how long they are. A stronger wind and a longer fetch make a taller, longer, faster sea (JONSWAP, Hasselmann 1973).
//   2. DISPERSION: a deep-water wave of wave number k travels at omega = sqrt(g k). Long waves are fast, short ones slow. This ties
//      the speed of a wave to its length: it is not a free setting.
//   3. The energy is spread over many wavelengths and directions. The table the shader draws (WaterWaves::BuildSea) is that
//      spectrum cut into bands, one Gerstner wave per band.
//
// Units: metres, seconds, radians. Wind speed is measured 10 m above the water, like a weather report.
#include <algorithm>
#include <cmath>

namespace SeaState {

constexpr double kGravity = 9.80665;                  // m/s^2
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;
constexpr double kMinFetchNondim = 20.0;              // gF/U^2: JONSWAP's fits start around here, a shorter run is treated as this one
constexpr double kMaxFetchNondim = 22000.0;           // gF/U^2 where a sea stops growing: it is "fully developed"
constexpr double kMinWindSpeed = 0.5;                 // m/s: under this the formulas' periods run away, so the height fades out instead
constexpr double kJonswapGamma = 3.3;                 // how much sharper the peak is than a fully developed (Pierson-Moskowitz) sea

// A fade for a dead calm: 0 with no wind, 1 from kMinWindSpeed up. Multiplies every wave height.
inline double CalmFactor(double windSpeed) { return std::clamp(windSpeed / kMinWindSpeed, 0.0, 1.0); }

// The Beaufort force (0..12) of a wind speed in m/s. The editor shows it next to the slider so a wind speed means something.
inline int Beaufort(double windSpeed)
{
	static const double kUpper[12] = { 0.3, 1.6, 3.4, 5.5, 8.0, 10.8, 13.9, 17.2, 20.8, 24.5, 28.5, 32.7 };
	for (int force = 0; force < 12; ++force) {
		if (windSpeed < kUpper[force]) return force;
	}
	return 12;
}

// ---- Wind -> spectrum ---------------------------------------------------------------------------------------------------
// gF/U^2, the fetch made dimensionless. Everything about a fetch-limited sea is a function of this one number.
// `fetchMetres` <= 0 means unlimited: the open ocean, a fully developed sea.
inline double NondimFetch(double windSpeed, double fetchMetres)
{
	if (fetchMetres <= 0.0) return kMaxFetchNondim;
	const double u = std::max(windSpeed, kMinWindSpeed);
	return std::clamp(kGravity * fetchMetres / (u * u), kMinFetchNondim, kMaxFetchNondim);
}

// The angular frequency (rad/s) of the highest part of the spectrum: f_p = 3.5 (g/U) (gF/U^2)^-0.33 (Hz), from the JONSWAP data.
inline double PeakOmega(double windSpeed, double nondimFetch)
{
	return kTwoPi * 3.5 * (kGravity / std::max(windSpeed, kMinWindSpeed)) * std::pow(nondimFetch, -0.33);
}

// The significant wave height (m) the wind and fetch make: Hs = 0.0016 sqrt(gF/U^2) U^2/g for a young sea (the JONSWAP data), and no
// more than 0.2127 U^2/g once the sea is fully developed (Pierson-Moskowitz). 10 m/s over open water: about 2.1 m.
inline double SignificantHeight(double windSpeed, double nondimFetch)
{
	const double u = std::max(windSpeed, kMinWindSpeed);
	return std::min(0.0016 * std::sqrt(nondimFetch), 0.2127) * u * u / kGravity;
}

// S(omega): how much wave energy (m^2 s) lives at angular frequency omega. The JONSWAP spectrum: a ^-5 tail, a cut-off below the peak,
// and a sharpened peak (`gamma`; 1 = the smooth Pierson-Moskowitz sea). `alpha` is the overall level.
inline double JonswapDensity(double omega, double peakOmega, double alpha, double gamma)
{
	if (omega <= 1e-6 || peakOmega <= 1e-6) return 0.0;
	const double sigma = omega <= peakOmega ? 0.07 : 0.09;
	const double d = omega - peakOmega;
	const double sharpen = std::pow(gamma, std::exp(-(d * d) / (2.0 * sigma * sigma * peakOmega * peakOmega)));
	return alpha * kGravity * kGravity * std::pow(omega, -5.0) * std::exp(-1.25 * std::pow(peakOmega / omega, 4.0)) * sharpen;
}

// The spectrum of one wind over one fetch. The SHAPE (where the peak is, how sharp, how fast the tail falls) is JONSWAP's; its LEVEL is then
// scaled so that the sea has exactly the significant height SignificantHeight says: that is the number people measure.
struct Spectrum {
	double nondimFetch = kMaxFetchNondim;
	double peakOmega = 1.0;        // rad/s
	double alpha = 0.0081;         // the shape's own level, before `scale`
	double gamma = 1.0;            // peak sharpness: 3.3 for a young sea, 1 for a fully developed one
	double scale = 1.0;            // multiplies the density so that 4 sqrt(m0) is the significant height
	double Density(double omega) const { return scale * JonswapDensity(omega, peakOmega, alpha, gamma); }
};

// The energy (m^2) of S between two angular frequencies: Simpson's rule on `steps` (even) slices.
inline double BandEnergy(const Spectrum& spectrum, double lowOmega, double highOmega, int steps = 16)
{
	if (highOmega <= lowOmega) return 0.0;
	steps = std::max(2, steps + (steps % 2));
	const double h = (highOmega - lowOmega) / steps;
	double sum = spectrum.Density(lowOmega) + spectrum.Density(highOmega);
	for (int i = 1; i < steps; ++i) sum += (i % 2 ? 4.0 : 2.0) * spectrum.Density(lowOmega + h * i);
	return sum * h / 3.0;
}

inline Spectrum MakeSpectrum(double windSpeed, double fetchMetres)
{
	Spectrum s;
	s.nondimFetch = NondimFetch(windSpeed, fetchMetres);
	s.peakOmega = PeakOmega(windSpeed, s.nondimFetch);
	s.alpha = 0.076 * std::pow(s.nondimFetch, -0.22);
	// A young sea has a sharp peak (3.3); as the fetch grows the sea matures and the peak flattens toward 1. Log-linear in gF/U^2.
	const double maturity = std::log(s.nondimFetch / kMinFetchNondim) / std::log(kMaxFetchNondim / kMinFetchNondim);
	s.gamma = 1.0 + (kJonswapGamma - 1.0) * (1.0 - std::clamp(maturity, 0.0, 1.0));
	// m0 of the unscaled shape: the area under the whole spectrum, from a log-spaced sum (beyond 50 x the peak it is below one part in a million).
	double m0 = 0.0;
	constexpr int kSlices = 400;
	const double low = 0.3 * s.peakOmega, high = 50.0 * s.peakOmega;
	const double ratio = std::pow(high / low, 1.0 / kSlices);
	double a = low;
	for (int i = 0; i < kSlices; ++i, a *= ratio) m0 += BandEnergy(s, a, a * ratio, 4);
	const double target = SignificantHeight(windSpeed, s.nondimFetch) / 4.0;
	s.scale = m0 > 0.0 ? target * target / m0 : 1.0;
	return s;
}

// ---- Dispersion ---------------------------------------------------------------------------------------------------------
// The wave number k (rad/m) of a wave of angular frequency omega: omega^2 = g k tanh(k depth). `depth` <= 0 means deep water, where it
// is just k = omega^2 / g. In shallow water the same frequency needs a LARGER k (the wave is shorter and slower): that is why
// waves slow down and bunch up as they reach a beach.
inline double Wavenumber(double omega, double depth)
{
	const double deep = omega * omega / kGravity;
	if (depth <= 0.0 || omega <= 0.0) return deep;
	// Newton on f(k) = g k tanh(k h) - omega^2, from Fenton's starting guess.
	const double alpha = omega * omega * depth / kGravity;
	double k = alpha * (1.0 / std::sqrt(std::tanh(alpha))) / depth;
	for (int i = 0; i < 6; ++i) {
		const double kh = k * depth;
		const double t = std::tanh(kh);
		const double f = kGravity * k * t - omega * omega;
		const double slope = kGravity * t + kGravity * kh * (1.0 - t * t);
		if (slope < 1e-12) break;
		k -= f / slope;
	}
	return k;
}

inline double PhaseSpeed(double omega, double depth) { return omega / std::max(Wavenumber(omega, depth), 1e-9); }

// ---- Directions ---------------------------------------------------------------------------------------------------------
// How wide the waves of angular frequency omega spread around the wind: the exponent s in cos^(2s)(theta / 2). The waves at the
// peak are the most aligned; longer ones (swell-like) and shorter ones spread wider (Mitsuyasu / Hasselmann).
inline double SpreadExponent(double omega, double peakOmega)
{
	const double ratio = std::max(omega, 1e-6) / std::max(peakOmega, 1e-6);
	return 9.77 * std::pow(ratio, ratio < 1.0 ? 4.06 : -2.34);
}

// An angle (radians, -pi..pi) away from the mean direction, drawn from the density cos^(2s)(theta / 2): `u` in (0, 1) is the
// random number. Inverse of the numeric CDF, so any s works.
inline double DirectionOffset(double s, double u)
{
	constexpr int kBins = 96;
	double cdf[kBins + 1];
	cdf[0] = 0.0;
	for (int i = 0; i < kBins; ++i) {
		const double theta = -kPi + kTwoPi * (i + 0.5) / kBins;
		cdf[i + 1] = cdf[i] + std::pow(std::max(std::cos(0.5 * theta), 0.0), 2.0 * s);
	}
	const double target = std::clamp(u, 0.0, 1.0) * cdf[kBins];
	int bin = 0;
	while (bin < kBins - 1 && cdf[bin + 1] < target) ++bin;
	const double span = std::max(cdf[bin + 1] - cdf[bin], 1e-12);
	const double within = std::clamp((target - cdf[bin]) / span, 0.0, 1.0);
	return -kPi + kTwoPi * (bin + within) / kBins;
}

// ---- A summary, for the editor and the tests ----------------------------------------------------------------------------
struct Summary {
	double nondimFetch = kMaxFetchNondim;
	double peakOmega = 1.0;          // rad/s
	double peakWavenumber = 0.1;     // rad/m
	double peakWavelength = 60.0;    // m
	double peakPeriod = 6.0;         // s
	double peakSpeed = 10.0;         // m/s: how fast the biggest waves travel
	double significantHeight = 1.0;  // m: the mean height of the tallest third of the waves, 4 sqrt(m0)
};

inline Summary Describe(double windSpeed, double fetchMetres, double depth = 0.0)
{
	Summary s;
	s.nondimFetch = NondimFetch(windSpeed, fetchMetres);
	s.peakOmega = PeakOmega(windSpeed, s.nondimFetch);
	s.peakWavenumber = Wavenumber(s.peakOmega, depth);
	s.peakWavelength = kTwoPi / std::max(s.peakWavenumber, 1e-9);
	s.peakPeriod = kTwoPi / s.peakOmega;
	s.peakSpeed = s.peakOmega / std::max(s.peakWavenumber, 1e-9);
	s.significantHeight = SignificantHeight(windSpeed, s.nondimFetch) * CalmFactor(windSpeed);
	return s;
}

}   // namespace SeaState
