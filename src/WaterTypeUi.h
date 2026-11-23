#pragma once
// Which settings each kind of water shows in the Inspector (lesson 35). A river needs a current and a width, not a swell
// and a wind direction; an ocean needs both. Showing only what matters keeps the editor short, and a table like this keeps
// that decision in ONE place that can be tested, instead of scattered through the drawing code as `if (river)`.
#include "WaterBodyType.h"

namespace WaterTypeUi {

enum class Control : int {
	WavePresets,       // Calm / Choppy / Stormy
	Speed,
	Frequency,
	Spread,            // directional spread
	Choppiness,
	WaveCount,
	WaveModel,         // sea (from the wind) / spectrum / classic
	Roughness,
	Swell,             // swell amount, angle, length
	WaveGroups,
	Seed,
	CrestFoam,
	ShoreSection,      // shore waves, shelter, swash
	Current,           // flow speed and direction: a river's reason to exist (and a Custom body's option)
	Count
};

// One bit per type: Ocean, Lake, River, Pond, Custom.
constexpr unsigned kOcean = 1u << 0, kLake = 1u << 1, kRiver = 1u << 2, kPond = 1u << 3, kCustom = 1u << 4;

inline unsigned TypesShowing(Control control)
{
	switch (control) {
	case Control::WavePresets:  return kOcean | kLake | kCustom;
	case Control::Speed:        return kOcean | kLake | kPond | kCustom;
	case Control::Frequency:    return kOcean | kLake | kCustom;
	case Control::Spread:       return kOcean | kLake | kCustom;
	case Control::Choppiness:   return kOcean | kLake | kCustom;
	case Control::WaveCount:    return kOcean | kCustom;
	case Control::WaveModel:    return kOcean | kLake | kPond | kCustom;   // the Sea style (from the wind) is the default of all four; a river is always the artist's model
	case Control::Roughness:    return kOcean | kLake | kCustom;
	case Control::Swell:        return kOcean | kCustom;                  // swell needs open water and a free direction
	case Control::WaveGroups:   return kOcean | kLake | kCustom;
	case Control::Seed:         return kOcean | kLake | kCustom;
	case Control::CrestFoam:    return kOcean | kLake | kCustom;
	case Control::ShoreSection: return kOcean | kLake | kCustom;          // rivers and ponds do not surf
	case Control::Current:      return kRiver | kCustom;                  // an ocean has swell, not a current
	default:                    return kCustom;
	}
}

inline unsigned Bit(WaterBodyType type)
{
	switch (type) {
	case WaterBodyType::Ocean:  return kOcean;
	case WaterBodyType::Lake:   return kLake;
	case WaterBodyType::River:  return kRiver;
	case WaterBodyType::Pond:   return kPond;
	case WaterBodyType::Custom: return kCustom;
	}
	return kCustom;
}

// `showAll`: the user asked to see everything for this body (a checkbox in the Inspector). Custom always shows everything.
inline bool IsShown(WaterBodyType type, Control control, bool showAll = false)
{
	return showAll || type == WaterBodyType::Custom || (TypesShowing(control) & Bit(type)) != 0;
}

} // namespace WaterTypeUi
